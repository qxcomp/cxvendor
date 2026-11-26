/* ============================================================
 * fastipc_dyn.c - dlopen 模拟链接：FastIPC C API (fipc.h v1.0.0)
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照上游 include/fipc.h 的 30 个 FIPC_API
 * 函数、4 个 opaque handle 与 fipc_result_t；导出函数名保持原生
 * 原样。链接期不需要 libfastipc，仅需 -ldl。
 * 加载顺序：显式路径 > FASTIPC_DYN_LIB > LD_LIBRARY_PATH > soname >
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

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<fipc.h>)
#    include <fipc.h>
#    define FASTIPC_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef FASTIPC_DYN_HAVE_HEADER
/* ---------- 类型（include/fipc.h 原样内嵌） ---------- */
typedef struct fipc_listener fipc_listener_t;
typedef struct fipc_conn fipc_conn_t;
typedef struct fipc_queue fipc_queue_t;
typedef struct fipc_service fipc_service_t;

typedef enum
{
    FIPC_OK = 0,
    FIPC_TIMEOUT = 1,
    FIPC_DISCONNECTED = 2,
    FIPC_CANCELLED = 3,
    FIPC_TOO_LARGE = 4,
    FIPC_INVALID = 5,
    FIPC_NO_MEMORY = 6,
    FIPC_ADDR_IN_USE = 7,
    FIPC_OVERRUN = 8,
    FIPC_UNSUBSCRIBED = 9,
    FIPC_ABANDONED = 10
} fipc_result_t;

#define FIPC_NO_WAIT 0
#define FIPC_FOREVER (-1)

#define FIPC_SUBSCRIBER_LOSSY 1
#define FIPC_SUBSCRIBER_GATED 2
#define FIPC_PRODUCER 3
#define FIPC_CONSUMER 4
#define FIPC_CLIENT 5
#define FIPC_SERVER 6

#endif /* FASTIPC_DYN_HAVE_HEADER */

/* ---------- 动态层状态 ---------- */
static lib_handle_t fastipc_handle = NULL;
static int fastipc_init_state = 0;

static void *fastipc_sym(const char *name) {
    return fastipc_handle ? get_sym(fastipc_handle, name) : NULL;
}

static int fastipc_do_load(const char *library_path) {
    static char bufs[64][1024];
    const char *candidates[64];
    const char *so_soname = "libfastipc.so";
    int n = 0, i;
    int res = -1;

    if (fastipc_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("FASTIPC_DYN_LIB");
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
                while (q && *q && n < 52) {
                    char *sep = strchr(q, PATH_SEP);
                    if (sep) *sep = 0;
                    if (*q && n + 1 < 64) {
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_soname);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = so_soname;
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
    candidates[n++] = "/opt/homebrew/lib/libfastipc.dylib";
    candidates[n++] = "/usr/local/lib/libfastipc.dylib";
#elif defined(_WIN32)
    candidates[n++] = "fastipc.dll";
    candidates[n++] = "libfastipc.dll";
#else
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/x86_64-linux-gnu/%s", so_soname);
    candidates[n] = bufs[n]; n++;
#endif

    for (i = 0; i < n; i++) {
        fastipc_handle = load_lib(candidates[i]);
        if (fastipc_handle) { res = i; break; }
    }
    if (!fastipc_handle || !fastipc_sym("fipc_result_str")) {
        fprintf(stderr, "fastipc_dyn: cannot load fastipc (%s)\n", lib_error());
        if (fastipc_handle) { free_lib(fastipc_handle); fastipc_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "fastipc_dyn: loaded %d %s\n", res, candidates[res]);
    return 0;
}

int fastipc_libok(void) {
    if (fastipc_init_state == 0)
        fastipc_init_state = (fastipc_do_load(NULL) == 0) ? 1 : -1;
    return (fastipc_init_state == 1 && fastipc_handle != NULL) ? 1 : 0;
}

int fastipc_dyn_init(const char *library_path) {
    if (fastipc_do_load(library_path) == 0) {
        fastipc_init_state = 1;
        return 0;
    }
    if (fastipc_init_state == 0) fastipc_init_state = -1;
    return -1;
}

void fastipc_dyn_cleanup(void) {
    if (fastipc_handle) { free_lib(fastipc_handle); fastipc_handle = NULL; }
    fastipc_init_state = 0;
}

int fastipc_dyn_has_symbol(const char *name) {
    if (!fastipc_libok()) return 0;
    return fastipc_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                            \
ret name params {                                                              \
    static ret (*fn) params = NULL;                                            \
    if (!fn) {                                                                 \
        if (!fastipc_libok()) return fallback;                                 \
        *(void **)(&fn) = fastipc_sym(#name);                                  \
        if (!fn) {                                                             \
            fprintf(stderr, "fastipc_dyn: dlsym failed: %s\n", #name);         \
            return fallback;                                                   \
        }                                                                      \
    }                                                                          \
    return fn args;                                                            \
}
#define WRAP_VOID(name, params, args)                                          \
void name params {                                                             \
    static void (*fn) params = NULL;                                           \
    if (!fn) {                                                                 \
        if (!fastipc_libok()) return;                                          \
        *(void **)(&fn) = fastipc_sym(#name);                                  \
        if (!fn) {                                                             \
            fprintf(stderr, "fastipc_dyn: dlsym failed: %s\n", #name);         \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn args;                                                                   \
}

/* ---------- 30 个 FIPC_API 公开函数（函数名原样） ---------- */
WRAP_RET(const char *, fipc_result_str, (fipc_result_t result), (result), NULL)
WRAP_RET(fipc_result_t, fipc_stream_create, (const char *name, size_t capacity, fipc_listener_t **out_listener), (name, capacity, out_listener), FIPC_INVALID)
WRAP_RET(fipc_result_t, fipc_stream_accept, (fipc_listener_t *listener, fipc_conn_t **out_conn, int timeout_ms), (listener, out_conn, timeout_ms), FIPC_INVALID)
WRAP_VOID(fipc_listener_cancel, (fipc_listener_t *listener), (listener))
WRAP_VOID(fipc_listener_close, (fipc_listener_t *listener), (listener))
WRAP_RET(fipc_result_t, fipc_stream_connect, (const char *name, fipc_conn_t **out_conn, int timeout_ms), (name, out_conn, timeout_ms), FIPC_INVALID)
WRAP_RET(size_t, fipc_max_piece, (const fipc_conn_t *conn), (conn), 0)
WRAP_VOID(fipc_cancel, (fipc_conn_t *conn), (conn))
WRAP_VOID(fipc_close, (fipc_conn_t *conn), (conn))
WRAP_RET(fipc_result_t, fipc_send, (fipc_conn_t *conn, const void *data, size_t len, int timeout_ms), (conn, data, len, timeout_ms), FIPC_INVALID)
WRAP_RET(fipc_result_t, fipc_recv, (fipc_conn_t *conn, void *buf, size_t buf_len, size_t *out_len, int timeout_ms), (conn, buf, buf_len, out_len, timeout_ms), FIPC_INVALID)
WRAP_RET(fipc_result_t, fipc_send_many, (fipc_conn_t *conn, const void *const *datas, const size_t *lens, size_t count, size_t *out_sent, int timeout_ms), (conn, datas, lens, count, out_sent, timeout_ms), FIPC_INVALID)
WRAP_RET(fipc_result_t, fipc_recv_many, (fipc_conn_t *conn, void *buf, size_t buf_len, size_t *lens, size_t max_count, size_t *out_count, int timeout_ms), (conn, buf, buf_len, lens, max_count, out_count, timeout_ms), FIPC_INVALID)
WRAP_RET(fipc_result_t, fipc_send_acquire, (fipc_conn_t *conn, size_t len, void **out_buf, int timeout_ms), (conn, len, out_buf, timeout_ms), FIPC_INVALID)
WRAP_RET(fipc_result_t, fipc_send_commit, (fipc_conn_t *conn, size_t len), (conn, len), FIPC_INVALID)
WRAP_RET(fipc_result_t, fipc_recv_acquire, (fipc_conn_t *conn, const void **out_data, size_t *out_len, int timeout_ms), (conn, out_data, out_len, timeout_ms), FIPC_INVALID)
WRAP_VOID(fipc_recv_release, (fipc_conn_t *conn), (conn))
WRAP_RET(fipc_result_t, fipc_broadcast_create, (const char *name, size_t capacity, uint32_t max_subscribers, int max_stall_ms, fipc_conn_t **out_publisher), (name, capacity, max_subscribers, max_stall_ms, out_publisher), FIPC_INVALID)
WRAP_RET(fipc_result_t, fipc_broadcast_join, (const char *name, int role, fipc_conn_t **out_conn, int timeout_ms), (name, role, out_conn, timeout_ms), FIPC_INVALID)
WRAP_RET(fipc_result_t, fipc_broadcast_wait_for_members, (fipc_conn_t *publisher, uint32_t at_least, uint32_t *out_count, int timeout_ms), (publisher, at_least, out_count, timeout_ms), FIPC_INVALID)
WRAP_RET(fipc_result_t, fipc_queue_create, (const char *name, size_t capacity, uint32_t max_producers, uint32_t max_consumers, fipc_queue_t **out_queue), (name, capacity, max_producers, max_consumers, out_queue), FIPC_INVALID)
WRAP_RET(fipc_result_t, fipc_queue_join, (const char *name, int role, fipc_conn_t **out_conn, int timeout_ms), (name, role, out_conn, timeout_ms), FIPC_INVALID)
WRAP_RET(fipc_result_t, fipc_queue_wait_for_members, (fipc_queue_t *queue, int role, uint32_t at_least, uint32_t *out_count, int timeout_ms), (queue, role, at_least, out_count, timeout_ms), FIPC_INVALID)
WRAP_VOID(fipc_queue_cancel, (fipc_queue_t *queue), (queue))
WRAP_VOID(fipc_queue_close, (fipc_queue_t *queue), (queue))
WRAP_RET(fipc_result_t, fipc_service_create, (const char *name, size_t capacity, size_t max_message, uint32_t max_clients, uint32_t max_servers, fipc_service_t **out_service), (name, capacity, max_message, max_clients, max_servers, out_service), FIPC_INVALID)
WRAP_RET(fipc_result_t, fipc_service_join, (const char *name, int role, fipc_conn_t **out_conn, int timeout_ms), (name, role, out_conn, timeout_ms), FIPC_INVALID)
WRAP_RET(fipc_result_t, fipc_service_wait_for_members, (fipc_service_t *service, int role, uint32_t at_least, uint32_t *out_count, int timeout_ms), (service, role, at_least, out_count, timeout_ms), FIPC_INVALID)
WRAP_VOID(fipc_service_cancel, (fipc_service_t *service), (service))
WRAP_VOID(fipc_service_close, (fipc_service_t *service), (service))
