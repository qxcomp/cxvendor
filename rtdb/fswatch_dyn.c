/* ============================================================
 * fswatch_dyn.c - dlopen 模拟链接：libfswatch C API（1.22.0）
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照上游 libfswatch/src/libfswatch/c/ 下公开头文件的
 * 全部 24 个 fsw_* 公开函数（libfswatch.h / cevent.h）。
 * 类型按头文件原样内嵌，字段顺序即 ABI；导出函数名保持原生原样。
 * 数据符号 FSW_ALL_EVENT_FLAGS 不是函数，不在封装范围内。
 *
 * 上游要求首次调用其它 API 前先调用 fsw_init_library()：本封装在
 * fswatch_libok()/fswatch_dyn_init() 首次加载成功后自动调用一次。
 * 1.21 新增的 fsw_set_callback_v2/fsw_set_filter_mode/fsw_add_prune_filter
 * 作为可选符号处理：旧库缺少时返回 FSW_ERR_UNKNOWN_ERROR，不影响加载。
 *
 * 单文件；链接期不需要 libfswatch，仅需 -ldl。
 * 加载顺序：显式路径 > FSWATCH_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > Nix > 平台默认；成功后不卸载。
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
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

/* ---------- 平台适配层 ---------- */
#ifdef _WIN32
 #include <windows.h>
 typedef HMODULE fswatch_lib_handle_t;
 static fswatch_lib_handle_t fswatch_load_lib(const char *path) { return LoadLibraryA(path); }
 static void *fswatch_get_sym(fswatch_lib_handle_t h, const char *name) { return (void *)GetProcAddress(h, name); }
 static void fswatch_free_lib(fswatch_lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *fswatch_lib_error(void) { return "LoadLibrary failed"; }
 #define FSWATCH_PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *fswatch_lib_handle_t;
 static fswatch_lib_handle_t fswatch_load_lib(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
 static void *fswatch_get_sym(fswatch_lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void fswatch_free_lib(fswatch_lib_handle_t h) { if (h) dlclose(h); }
 static const char *fswatch_lib_error(void) { return dlerror(); }
 #define FSWATCH_PATH_SEP ':'
#endif

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型。
 * autotools/Debian  installs <libfswatch/c/libfswatch.h>；
 * CMake installs <libfswatch/libfswatch.h>。 */
#if defined(__has_include)
#  if __has_include(<libfswatch/c/libfswatch.h>)
#    include <libfswatch/c/libfswatch.h>
#    define FSWATCH_DYN_HAVE_HEADER 1
#  elif __has_include(<libfswatch/libfswatch.h>)
#    include <libfswatch/libfswatch.h>
#    define FSWATCH_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef FSWATCH_DYN_HAVE_HEADER
/* ---------- 类型（libfswatch_types.h / cevent.h / cmonitor.h /
 *             cfilter.h / error.h 原样内嵌） ---------- */
struct FSW_SESSION;
typedef struct FSW_SESSION *FSW_HANDLE;
typedef int FSW_STATUS;

enum fsw_event_flag
{
  NoOp = 0,
  PlatformSpecific = (1 << 0),
  Created = (1 << 1),
  Updated = (1 << 2),
  Removed = (1 << 3),
  Renamed = (1 << 4),
  OwnerModified = (1 << 5),
  AttributeModified = (1 << 6),
  MovedFrom = (1 << 7),
  MovedTo = (1 << 8),
  IsFile = (1 << 9),
  IsDir = (1 << 10),
  IsSymLink = (1 << 11),
  Link = (1 << 12),
  Overflow = (1 << 13),
  CloseWrite = (1 << 14)
};

enum fsw_process_id_kind
{
  FSW_PROCESS_ID_NONE = 0,
  FSW_PROCESS_ID_PID,
  FSW_PROCESS_ID_TID
};

typedef struct fsw_cevent
{
  char * path;
  time_t evt_time;
  enum fsw_event_flag * flags;
  unsigned int flags_num;
} fsw_cevent;

typedef struct fsw_cevent_v2
{
  char * path;
  time_t evt_time;
  enum fsw_event_flag * flags;
  unsigned int flags_num;
  unsigned long correlation_id;
  enum fsw_process_id_kind process_id_kind;
  long long process_id;
  int process_pidfd;
  bool has_process_pidfd;
} fsw_cevent_v2;

typedef void (*FSW_CEVENT_CALLBACK)(fsw_cevent const *const events,
                                    const unsigned int event_num,
                                    void *data);
typedef void (*FSW_CEVENT_CALLBACK_V2)(fsw_cevent_v2 const *const events,
                                       const unsigned int event_num,
                                       void *data);

enum fsw_monitor_type
{
  system_default_monitor_type = 0,
  fsevents_monitor_type,
  kqueue_monitor_type,
  inotify_monitor_type,
  windows_monitor_type,
  poll_monitor_type,
  fen_monitor_type,
  fanotify_monitor_type
};

enum fsw_filter_type
{
  filter_include,
  filter_exclude
};

enum fsw_filter_mode
{
  filter_mode_legacy,
  filter_mode_conjunctive
};

typedef struct fsw_cmonitor_filter
{
  char * text;
  enum fsw_filter_type type;
  bool case_sensitive;
  bool extended;
} fsw_cmonitor_filter;

typedef struct fsw_event_type_filter
{
  enum fsw_event_flag flag;
} fsw_event_type_filter;

#define FSW_OK                            0
#define FSW_ERR_UNKNOWN_ERROR             (1 << 0)
#define FSW_ERR_UNKNOWN_VALUE             (1 << 13)
#endif /* !FSWATCH_DYN_HAVE_HEADER */

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static fswatch_lib_handle_t fswatch_handle = NULL;
static int fswatch_init_state = 0;

static void *fswatch_sym(const char *name) {
    return fswatch_handle ? fswatch_get_sym(fswatch_handle, name) : NULL;
}

/* 首次加载成功后调用一次 fsw_init_library()（照 curl/mysql 范式） */
static int fswatch_call_init_library(void) {
    FSW_STATUS (*fn_init_library)(void) = NULL;
    *(void **)(&fn_init_library) = fswatch_sym("fsw_init_library");
    if (!fn_init_library) {
        fprintf(stderr, "fswatch_dyn: fsw_init_library not found\n");
        return -1;
    }
    if (fn_init_library() != FSW_OK) {
        fprintf(stderr, "fswatch_dyn: fsw_init_library failed\n");
        return -1;
    }
    return 0;
}

static int fswatch_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    const char *so_soname = "libfswatch.so.15";
    const char *so_plain = "libfswatch.so";
    static const char *required[] = {
        "fsw_init_library", "fsw_init_session", "fsw_add_path",
        "fsw_set_callback", "fsw_start_monitor", "fsw_stop_monitor",
        "fsw_is_running", "fsw_destroy_session", "fsw_last_error"
    };
    int n = 0, i, j;

    if (fswatch_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("FSWATCH_DYN_LIB");
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
                while (q && *q && n < 80) {
                    char *sep = strchr(q, FSWATCH_PATH_SEP);
                    if (sep) *sep = 0;
                    if (*q) {
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_soname);
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
    candidates[n++] = "/opt/homebrew/lib/libfswatch.dylib";
    candidates[n++] = "/usr/local/lib/libfswatch.dylib";
#elif defined(_WIN32)
    candidates[n++] = "fswatch.dll";
#else
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
#endif

    for (i = 0; i < n; i++) {
        fswatch_handle = fswatch_load_lib(candidates[i]);
        if (!fswatch_handle) continue;
        for (j = 0; j < (int)(sizeof required / sizeof required[0]); j++) {
            if (!fswatch_get_sym(fswatch_handle, required[j])) break;
        }
        if (j == (int)(sizeof required / sizeof required[0])) break;
        fswatch_free_lib(fswatch_handle);
        fswatch_handle = NULL;
    }
    if (!fswatch_handle) {
        fprintf(stderr, "fswatch_dyn: cannot load libfswatch (%s)\n", fswatch_lib_error());
        return -1;
    }
    fprintf(stderr, "fswatch_dyn: loaded %d %s\n", i, candidates[i]);
    return 0;
}

int fswatch_libok(void) {
    if (fswatch_init_state == 0) {
        if (fswatch_do_load(NULL) != 0) {
            fswatch_init_state = -1;
            return 0;
        }
        if (fswatch_call_init_library() != 0) {
            fswatch_free_lib(fswatch_handle);
            fswatch_handle = NULL;
            fswatch_init_state = -1;
            return 0;
        }
        fswatch_init_state = 1;
    }
    return (fswatch_init_state == 1 && fswatch_handle != NULL) ? 1 : 0;
}

int fswatch_dyn_init(const char *library_path) {
    if (fswatch_init_state == 1) return 0;
    if (fswatch_init_state == -1) return -1;
    if (fswatch_do_load(library_path) != 0) {
        fswatch_init_state = -1;
        return -1;
    }
    if (fswatch_call_init_library() != 0) {
        fswatch_free_lib(fswatch_handle);
        fswatch_handle = NULL;
        fswatch_init_state = -1;
        return -1;
    }
    fswatch_init_state = 1;
    return 0;
}

void fswatch_dyn_cleanup(void) {
    /* libfswatch 没有全局 cleanup API；不卸载库，只重置状态。 */
    fswatch_init_state = 0;
}

int fswatch_dyn_has_symbol(const char *name) {
    if (!fswatch_libok()) return 0;
    return fswatch_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define FSWATCH_WRAP_RET(ret, name, params, args, fallback)                          \
ret name params {                                                            \
    static ret (*fn) params = NULL;                                          \
    if (!fn) {                                                               \
        if (!fswatch_libok()) return fallback;                               \
        *(void **)(&fn) = fswatch_sym(#name);                                \
        if (!fn) {                                                           \
            fprintf(stderr, "fswatch_dyn: dlsym failed: %s\n", #name);       \
            return fallback;                                                 \
        }                                                                    \
    }                                                                        \
    return fn args;                                                          \
}

#define FSWATCH_WRAP_VOID(name, params, args)                                        \
void name params {                                                           \
    static void (*fn) params = NULL;                                         \
    if (!fn) {                                                               \
        if (!fswatch_libok()) return;                                        \
        *(void **)(&fn) = fswatch_sym(#name);                                \
        if (!fn) {                                                           \
            fprintf(stderr, "fswatch_dyn: dlsym failed: %s\n", #name);       \
            return;                                                          \
        }                                                                    \
    }                                                                        \
    fn args;                                                                 \
}

/* 可选符号：旧版本库缺少该符号时静默返回 fallback，不打印错误 */
#define FSWATCH_WRAP_OPT_RET(ret, name, params, args, fallback)                      \
ret name params {                                                            \
    static ret (*fn) params = NULL;                                          \
    static int resolved = 0;                                                 \
    if (!resolved) {                                                         \
        if (!fswatch_libok()) return fallback;                               \
        *(void **)(&fn) = fswatch_sym(#name);                                \
        resolved = 1;                                                        \
    }                                                                        \
    if (!fn) return fallback;                                                \
    return fn args;                                                          \
}

/* ============================================================
 * 导出函数封装（libfswatch.h / cevent.h 全部 24 个公开函数，
 * 顺序与头文件一致）
 * ============================================================ */
FSWATCH_WRAP_RET(FSW_STATUS, fsw_init_library, (void), (), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(FSW_HANDLE, fsw_init_session, (const enum fsw_monitor_type type), (type), NULL)
FSWATCH_WRAP_RET(FSW_STATUS, fsw_add_path, (const FSW_HANDLE handle, const char *path), (handle, path), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(FSW_STATUS, fsw_add_property, (const FSW_HANDLE handle, const char *name, const char *value), (handle, name, value), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(FSW_STATUS, fsw_set_allow_overflow, (const FSW_HANDLE handle, const bool allow_overflow), (handle, allow_overflow), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(FSW_STATUS, fsw_set_callback, (const FSW_HANDLE handle, const FSW_CEVENT_CALLBACK callback, void *data), (handle, callback, data), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_OPT_RET(FSW_STATUS, fsw_set_callback_v2, (const FSW_HANDLE handle, const FSW_CEVENT_CALLBACK_V2 callback, void *data), (handle, callback, data), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(FSW_STATUS, fsw_set_latency, (const FSW_HANDLE handle, const double latency), (handle, latency), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(FSW_STATUS, fsw_set_recursive, (const FSW_HANDLE handle, const bool recursive), (handle, recursive), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(FSW_STATUS, fsw_set_directory_only, (const FSW_HANDLE handle, const bool directory_only), (handle, directory_only), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(FSW_STATUS, fsw_set_follow_symlinks, (const FSW_HANDLE handle, const bool follow_symlinks), (handle, follow_symlinks), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(FSW_STATUS, fsw_add_event_type_filter, (const FSW_HANDLE handle, const fsw_event_type_filter event_type), (handle, event_type), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(FSW_STATUS, fsw_add_filter, (const FSW_HANDLE handle, const fsw_cmonitor_filter filter), (handle, filter), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_OPT_RET(FSW_STATUS, fsw_set_filter_mode, (const FSW_HANDLE handle, const enum fsw_filter_mode mode), (handle, mode), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_OPT_RET(FSW_STATUS, fsw_add_prune_filter, (const FSW_HANDLE handle, const fsw_cmonitor_filter filter), (handle, filter), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(FSW_STATUS, fsw_start_monitor, (const FSW_HANDLE handle), (handle), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(FSW_STATUS, fsw_stop_monitor, (const FSW_HANDLE handle), (handle), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(bool, fsw_is_running, (const FSW_HANDLE handle), (handle), false)
FSWATCH_WRAP_RET(FSW_STATUS, fsw_destroy_session, (const FSW_HANDLE handle), (handle), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(FSW_STATUS, fsw_last_error, (void), (), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(bool, fsw_is_verbose, (void), (), false)
FSWATCH_WRAP_VOID(fsw_set_verbose, (bool verbose), (verbose))
FSWATCH_WRAP_RET(FSW_STATUS, fsw_get_event_flag_by_name, (const char *name, enum fsw_event_flag *flag), (name, flag), FSW_ERR_UNKNOWN_ERROR)
FSWATCH_WRAP_RET(char *, fsw_get_event_flag_name, (const enum fsw_event_flag flag), (flag), NULL)
