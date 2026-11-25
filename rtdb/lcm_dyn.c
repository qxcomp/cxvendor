/* ============================================================
 * lcm_dyn.c - dlopen 模拟链接：LCM 1.5.3 C API
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照上游 v1.5.3 的 lcm/lcm.h 与 lcm/eventlog.h：
 * 10 个核心 lcm_* 函数 + 6 个 lcm_eventlog_* 函数，共 16 个
 * 公开函数；默认 C namespace 为 lcm，导出名保持原生原样。
 * 链接期不需要 liblcm，仅需 -ldl。
 * 加载顺序：显式路径 > LCM_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > Nix > 平台默认；成功后不卸载。
 * 加载日志照搬 opendal_dyn.c 当前 diff。
 * ============================================================ */

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* ---------- 平台适配层 ---------- */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE lib_handle_t;
  static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
  static void *get_sym(lib_handle_t h, const char *name) { return (void *)GetProcAddress(h, name); }
  static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *lib_error(void) { return "LoadLibrary failed"; }
  #define PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *lib_handle_t;
  static lib_handle_t load_lib(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
  static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
  static const char *lib_error(void) { return dlerror(); }
  #define PATH_SEP ':'
#endif

/* ---------- 类型（lcm.h / eventlog.h 原样内嵌） ---------- */
typedef struct _lcm_t lcm_t;
typedef struct _lcm_subscription_t lcm_subscription_t;
typedef struct _lcm_recv_buf_t lcm_recv_buf_t;
struct _lcm_recv_buf_t {
    void *data;
    uint32_t data_size;
    int64_t recv_utime;
    lcm_t *lcm;
};
typedef void (*lcm_msg_handler_t)(const lcm_recv_buf_t *rbuf, const char *channel, void *user_data);

typedef struct _lcm_eventlog_t lcm_eventlog_t;
struct _lcm_eventlog_t {
    FILE *f;
    int64_t eventcount;
};

typedef struct _lcm_eventlog_event_t lcm_eventlog_event_t;
struct _lcm_eventlog_event_t {
    int64_t eventnum;
    int64_t timestamp;
    int32_t channellen;
    int32_t datalen;
    char *channel;
    void *data;
};

/* ---------- 动态层状态 ---------- */
static lib_handle_t lcm_lib_handle = NULL;
static int lcm_init_state = 0;

static void *lcm_sym(const char *name) {
    return lcm_lib_handle ? get_sym(lcm_lib_handle, name) : NULL;
}

static int lcm_do_load(const char *library_path) {
    static char bufs[64][1024];
    const char *candidates[64];
    const char *so_soname = "liblcm.so.1";
    const char *so_ver = "liblcm.so.1.5.3";
    const char *so_plain = "liblcm.so";
    int n = 0, i;
    int res = -1;

    if (lcm_lib_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("LCM_DYN_LIB");
        if (e && *e) {
            snprintf(bufs[n], sizeof bufs[0], "%s", e);
            candidates[n] = bufs[n]; n++;
        }
    }
    {
#ifdef _WIN32
        const char *env_path = getenv("PATH");
#elif defined(__APPLE__)
        const char *env_path = getenv("DYLD_LIBRARY_PATH");
#else
        const char *env_path = getenv("LD_LIBRARY_PATH");
#endif
        if (env_path && *env_path) {
            char *copy = strdup(env_path);
            if (copy) {
                char *q = copy;
                while (q && *q && n < 50) {
                    char *sep = strchr(q, PATH_SEP);
                    if (sep) *sep = 0;
                    if (*q && n + 3 < 64) {
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_soname);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_ver);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_plain);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = so_soname;
    candidates[n++] = so_ver;
    candidates[n++] = so_plain;
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/%s", home, so_soname);
            candidates[n] = bufs[n]; n++;
        }
        snprintf(bufs[n], sizeof bufs[0], "/nix/var/nix/profiles/default/lib/%s", so_soname);
        candidates[n] = bufs[n]; n++;
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/liblcm.dylib";
    candidates[n++] = "/usr/local/lib/liblcm.dylib";
#elif defined(_WIN32)
    candidates[n++] = "lcm.dll";
    candidates[n++] = "liblcm.dll";
#else
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/x86_64-linux-gnu/%s", so_soname);
    candidates[n] = bufs[n]; n++;
#endif

    for (i = 0; i < n; i++) {
        lcm_lib_handle = load_lib(candidates[i]);
        if (lcm_lib_handle) { res = i; break; }
    }
    if (!lcm_lib_handle || !lcm_sym("lcm_create")) {
        fprintf(stderr, "lcm_dyn: cannot load lcm (%s)\n", lib_error());
        if (lcm_lib_handle) { free_lib(lcm_lib_handle); lcm_lib_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "lcm_dyn: loaded lcm %d %s\n", res, candidates[res]);
    return 0;
}

int lcm_dyn_libok(void) {
    if (lcm_init_state == 0)
        lcm_init_state = (lcm_do_load(NULL) == 0) ? 1 : -1;
    return (lcm_init_state == 1 && lcm_lib_handle != NULL) ? 1 : 0;
}

int lcm_dyn_init(const char *library_path) {
    if (lcm_do_load(library_path) == 0) {
        lcm_init_state = 1;
        return 0;
    }
    if (lcm_init_state == 0) lcm_init_state = -1;
    return -1;
}

void lcm_dyn_cleanup(void) {
    if (lcm_lib_handle) { free_lib(lcm_lib_handle); lcm_lib_handle = NULL; }
    lcm_init_state = 0;
}

int lcm_dyn_has_symbol(const char *name) {
    if (!lcm_dyn_libok()) return 0;
    return lcm_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                            \
ret name params {                                                              \
    static ret (*fn) params = NULL;                                            \
    if (!fn) {                                                                 \
        if (!lcm_dyn_libok()) return fallback;                                 \
        *(void **)(&fn) = lcm_sym(#name);                                      \
        if (!fn) {                                                             \
            fprintf(stderr, "lcm_dyn: dlsym failed: %s\n", #name);             \
            return fallback;                                                   \
        }                                                                      \
    }                                                                          \
    return fn args;                                                            \
}
#define WRAP_VOID(name, params, args)                                          \
void name params {                                                             \
    static void (*fn) params = NULL;                                           \
    if (!fn) {                                                                 \
        if (!lcm_dyn_libok()) return;                                          \
        *(void **)(&fn) = lcm_sym(#name);                                      \
        if (!fn) {                                                             \
            fprintf(stderr, "lcm_dyn: dlsym failed: %s\n", #name);             \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn args;                                                                   \
}

/* ---------- 16 个公开函数（函数名原样） ---------- */
WRAP_RET(lcm_t *, lcm_create, (const char *provider), (provider), NULL)
WRAP_VOID(lcm_destroy, (lcm_t *lcm), (lcm))
WRAP_RET(int, lcm_get_fileno, (lcm_t *lcm), (lcm), -1)
WRAP_RET(lcm_subscription_t *, lcm_subscribe, (lcm_t *lcm, const char *channel, lcm_msg_handler_t handler, void *user_data), (lcm, channel, handler, user_data), NULL)
WRAP_RET(int, lcm_unsubscribe, (lcm_t *lcm, lcm_subscription_t *handler), (lcm, handler), -1)
WRAP_RET(int, lcm_publish, (lcm_t *lcm, const char *channel, const void *data, unsigned int datalen), (lcm, channel, data, datalen), -1)
WRAP_RET(int, lcm_handle, (lcm_t *lcm), (lcm), -1)
WRAP_RET(int, lcm_handle_timeout, (lcm_t *lcm, int timeout_millis), (lcm, timeout_millis), -1)
WRAP_RET(int, lcm_subscription_set_queue_capacity, (lcm_subscription_t *handler, int num_messages), (handler, num_messages), -1)
WRAP_RET(int, lcm_subscription_get_queue_size, (lcm_subscription_t *handler), (handler), -1)
WRAP_RET(lcm_eventlog_t *, lcm_eventlog_create, (const char *path, const char *mode), (path, mode), NULL)
WRAP_RET(lcm_eventlog_event_t *, lcm_eventlog_read_next_event, (lcm_eventlog_t *eventlog), (eventlog), NULL)
WRAP_VOID(lcm_eventlog_free_event, (lcm_eventlog_event_t *event), (event))
WRAP_RET(int, lcm_eventlog_seek_to_timestamp, (lcm_eventlog_t *eventlog, int64_t ts), (eventlog, ts), -1)
WRAP_RET(int, lcm_eventlog_write_event, (lcm_eventlog_t *eventlog, lcm_eventlog_event_t *event), (eventlog, event), -1)
WRAP_VOID(lcm_eventlog_destroy, (lcm_eventlog_t *eventlog), (eventlog))
