/* ============================================================
 * libev_dyn.c - type safe dynamic lib load for libev 4.33
 * ============================================================
 * 设计要点：
 *   1. 单文件、无 .h、无 wrapper 层；链接期不需要目标库。
 *   覆盖 libev.so.4 全部 72 个 ev_* 导出符号。
 *   libev 同时导出的 24 个 event_* 兼容符号故意不封装，
 *   避免与 libevent_dyn.c 的 event_* 冲突。
 *   ev.h 中的宏 API（ev_init / ev_io_set / ev_timer_set）
 *   不是导出符号，无法 dlsym；调用方直接操作结构字段。
 *   2. 每个函数首次调用时 dlsym 缓存；失败打印 stderr 并返回安全值。
 *   3. 加载顺序：显式路径 > LIBEV_DYN_LIB > LD_LIBRARY_PATH >
 *      soname > .so > Nix > 平台默认；成功后不卸载。
 *
 *   使用：
 *     libev_dyn_init(NULL);
 *     ... 原版 API 直接调用 ...
 *     libev_dyn_cleanup();   // 仅复位加载状态，不卸载库
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <termios.h>

/* ============================================================
 * 平台适配层
 * ============================================================ */
#ifdef _WIN32
 #include <windows.h>
 typedef HMODULE libev_lib_handle_t;
 static libev_lib_handle_t libev_load_lib(const char *path) { return LoadLibraryA(path); }
 static void *libev_get_sym(libev_lib_handle_t h, const char *name) {
     return (void *)GetProcAddress(h, name);
 }
 static void libev_free_lib(libev_lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *libev_lib_error(void) { return "LoadLibrary failed"; }
 #define LIBEV_PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *libev_lib_handle_t;
 static libev_lib_handle_t libev_load_lib(const char *path) {
     return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
 }
 static void *libev_get_sym(libev_lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void libev_free_lib(libev_lib_handle_t h) { if (h) dlclose(h); }
 static const char *libev_lib_error(void) { return dlerror(); }
 #define LIBEV_PATH_SEP ':'
#endif

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<ev.h>)
#    include <ev.h>
#    define LIBEV_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef LIBEV_DYN_HAVE_HEADER
/* ============================================================
 * 类型定义（官方头文件机械提取；未列出的 struct 为不透明句柄）
 * ============================================================ */
struct sigaction;
struct sigcontext;
struct sigstack;
struct stat;
struct stat64;
struct statx;
struct timespec;
union sigval;

typedef double ev_tstamp;
struct ev_loop;
enum { EV_UNDEF = (int)0xFFFFFFFF, EV_NONE = 0x00, EV_READ = 0x01, EV_WRITE = 0x02, EV__IOFDSET = 0x80, EV_IO = EV_READ, EV_TIMER = 0x00000100, EV_TIMEOUT = EV_TIMER, EV_PERIODIC = 0x00000200, EV_SIGNAL = 0x00000400, EV_CHILD = 0x00000800, EV_STAT = 0x00001000, EV_IDLE = 0x00002000, EV_PREPARE = 0x00004000, EV_CHECK = 0x00008000, EV_EMBED = 0x00010000, EV_FORK = 0x00020000, EV_CLEANUP = 0x00040000, EV_ASYNC = 0x00080000, EV_CUSTOM = 0x01000000, EV_ERROR = (int)0x80000000 };
typedef struct ev_watcher { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_watcher *w, int revents); } ev_watcher;
typedef struct ev_watcher_list { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_watcher_list *w, int revents); struct ev_watcher_list *next; } ev_watcher_list;
typedef struct ev_watcher_time { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_watcher_time *w, int revents); ev_tstamp at; } ev_watcher_time;
typedef struct ev_io { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_io *w, int revents); struct ev_watcher_list *next; int fd; int events; } ev_io;
typedef struct ev_timer { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_timer *w, int revents); ev_tstamp at; ev_tstamp repeat; } ev_timer;
typedef struct ev_periodic { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_periodic *w, int revents); ev_tstamp at; ev_tstamp offset; ev_tstamp interval; ev_tstamp (*reschedule_cb)(struct ev_periodic *w, ev_tstamp now) ; } ev_periodic;
typedef struct ev_signal { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_signal *w, int revents); struct ev_watcher_list *next; int signum; } ev_signal;
typedef struct ev_child { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_child *w, int revents); struct ev_watcher_list *next; int flags; int pid; int rpid; int rstatus; } ev_child;
typedef struct stat ev_statdata;
typedef struct ev_stat { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_stat *w, int revents); struct ev_watcher_list *next; ev_timer timer; ev_tstamp interval; const char *path; ev_statdata prev; ev_statdata attr; int wd; } ev_stat;
typedef struct ev_idle { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_idle *w, int revents); } ev_idle;
typedef struct ev_prepare { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_prepare *w, int revents); } ev_prepare;
typedef struct ev_check { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_check *w, int revents); } ev_check;
typedef struct ev_fork { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_fork *w, int revents); } ev_fork;
typedef struct ev_cleanup { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_cleanup *w, int revents); } ev_cleanup;
typedef struct ev_embed { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_embed *w, int revents); struct ev_loop *other; ev_io io; ev_prepare prepare; ev_check check; ev_timer timer; ev_periodic periodic; ev_idle idle; ev_fork fork; ev_cleanup cleanup; } ev_embed;
typedef struct ev_async { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_async *w, int revents); sig_atomic_t volatile sent; } ev_async;
union ev_any_watcher { struct ev_watcher w; struct ev_watcher_list wl; struct ev_io io; struct ev_timer timer; struct ev_periodic periodic; struct ev_signal signal; struct ev_child child; struct ev_stat stat; struct ev_idle idle; struct ev_prepare prepare; struct ev_check check; struct ev_fork fork; struct ev_cleanup cleanup; struct ev_embed embed; struct ev_async async; };
typedef void (*ev_loop_callback)(struct ev_loop *loop);

#endif /* LIBEV_DYN_HAVE_HEADER */

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static libev_lib_handle_t libev_handle = NULL;
static void *libev_sym(const char *name) {
    return libev_handle ? libev_get_sym(libev_handle, name) : NULL;
}
static int libev_init_state = 0;

static int libev_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    int n = 0, i;
    if (libev_handle) return 0;
    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("LIBEV_DYN_LIB");
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
                    char *sep = strchr(q, LIBEV_PATH_SEP);
                    if (sep) *sep = 0;
                    if (*q) {
                        snprintf(bufs[n], sizeof bufs[0], "%s/libev.so.4", q);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/libev.so", q);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = "libev.so.4";
    candidates[n++] = "libev.so";
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libev.so.4", home);
            candidates[n] = bufs[n]; n++;
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libev.so", home);
            candidates[n] = bufs[n]; n++;
        }
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libev.so.4";
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libev.so";
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libev.so.4";
    candidates[n++] = "/usr/local/lib/libev.so.4";
    candidates[n++] = "/opt/homebrew/lib/libev.so";
    candidates[n++] = "/usr/local/lib/libev.so";
#elif defined(_WIN32)
    candidates[n++] = "libev.dll";
#else
    candidates[n++] = "/usr/local/lib/libev.so.4";
    candidates[n++] = "/usr/lib/libev.so.4";
    candidates[n++] = "/usr/local/lib/libev.so";
    candidates[n++] = "/usr/lib/libev.so";
#endif
    for (i = 0; i < n; i++) {
        libev_handle = libev_load_lib(candidates[i]);
        if (libev_handle) break;
    }
    if (!libev_handle || !libev_sym("ev_loop_new")) {
        fprintf(stderr, "libev_dyn: cannot load libev (%s)\n", libev_lib_error());
        if (libev_handle) { libev_free_lib(libev_handle); libev_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "libev_dyn: loaded %d %s (4.33 headers)\n", i, candidates[i]);
    return 0;
}

int libev_libok(void) {
    if (libev_init_state == 0)
        libev_init_state = (libev_do_load(NULL) == 0) ? 1 : -1;
    return (libev_init_state == 1 && libev_handle != NULL) ? 1 : 0;
}

int libev_dyn_init(const char *library_path) {
    if (libev_do_load(library_path) == 0) {
        libev_init_state = 1;
        return 0;
    }
    if (libev_init_state == 0) libev_init_state = -1;
    return -1;
}

void libev_dyn_cleanup(void) {
    /* 仅复位加载状态，不卸载已成功加载的库（dlclose 不安全） */
    if (!libev_handle) libev_init_state = 0;
    else if (libev_init_state == -1) libev_init_state = 0;
}

int libev_dyn_has_symbol(const char *name) {
    if (!libev_libok()) return 0;
    return libev_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏（首次调用 dlsym 并缓存；失败打印 stderr 返回安全值）
 * ============================================================ */
#define LIBEV_WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!libev_libok()) return fallback;                              \
        *(void **)(&fn) = libev_sym(#name);                               \
        if (!fn) {                                                        \
            fprintf(stderr, "libev_dyn: dlsym failed: %s\n", #name);     \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define LIBEV_WRAP_VOID(name, params, args)                                     \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!libev_libok()) return;                                       \
        *(void **)(&fn) = libev_sym(#name);                               \
        if (!fn) {                                                        \
            fprintf(stderr, "libev_dyn: dlsym failed: %s\n", #name);     \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}

/* ============================================================
 * 导出函数封装（与 nm -D 导出一一对应，共 72 个）
 * ============================================================ */
LIBEV_WRAP_VOID(ev_async_send, (struct ev_loop * a0, ev_async * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_async_start, (struct ev_loop * a0, ev_async * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_async_stop, (struct ev_loop * a0, ev_async * a1), (a0, a1))
LIBEV_WRAP_RET(unsigned int, ev_backend, (struct ev_loop * a0), (a0), ((unsigned int){0}))
LIBEV_WRAP_VOID(ev_break, (struct ev_loop * a0, int a1), (a0, a1))
LIBEV_WRAP_VOID(ev_check_start, (struct ev_loop * a0, ev_check * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_check_stop, (struct ev_loop * a0, ev_check * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_child_start, (struct ev_loop * a0, ev_child * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_child_stop, (struct ev_loop * a0, ev_child * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_cleanup_start, (struct ev_loop * a0, ev_cleanup * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_cleanup_stop, (struct ev_loop * a0, ev_cleanup * a1), (a0, a1))
LIBEV_WRAP_RET(int, ev_clear_pending, (struct ev_loop * a0, void * a1), (a0, a1), -1)
LIBEV_WRAP_RET(struct ev_loop *, ev_default_loop, (unsigned int a0), (a0), NULL)
LIBEV_WRAP_RET(unsigned int, ev_depth, (struct ev_loop * a0), (a0), ((unsigned int){0}))
LIBEV_WRAP_VOID(ev_embed_start, (struct ev_loop * a0, ev_embed * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_embed_stop, (struct ev_loop * a0, ev_embed * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_embed_sweep, (struct ev_loop * a0, ev_embed * a1), (a0, a1))
LIBEV_WRAP_RET(unsigned int, ev_embeddable_backends, (void), (), ((unsigned int){0}))
LIBEV_WRAP_VOID(ev_feed_event, (struct ev_loop * a0, void * a1, int a2), (a0, a1, a2))
LIBEV_WRAP_VOID(ev_feed_fd_event, (struct ev_loop * a0, int a1, int a2), (a0, a1, a2))
LIBEV_WRAP_VOID(ev_feed_signal, (int a0), (a0))
LIBEV_WRAP_VOID(ev_feed_signal_event, (struct ev_loop * a0, int a1), (a0, a1))
LIBEV_WRAP_VOID(ev_fork_start, (struct ev_loop * a0, ev_fork * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_fork_stop, (struct ev_loop * a0, ev_fork * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_idle_start, (struct ev_loop * a0, ev_idle * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_idle_stop, (struct ev_loop * a0, ev_idle * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_invoke, (struct ev_loop * a0, void * a1, int a2), (a0, a1, a2))
LIBEV_WRAP_VOID(ev_invoke_pending, (struct ev_loop * a0), (a0))
LIBEV_WRAP_VOID(ev_io_start, (struct ev_loop * a0, ev_io * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_io_stop, (struct ev_loop * a0, ev_io * a1), (a0, a1))
LIBEV_WRAP_RET(unsigned int, ev_iteration, (struct ev_loop * a0), (a0), ((unsigned int){0}))
LIBEV_WRAP_VOID(ev_loop_destroy, (struct ev_loop * a0), (a0))
LIBEV_WRAP_VOID(ev_loop_fork, (struct ev_loop * a0), (a0))
LIBEV_WRAP_RET(struct ev_loop *, ev_loop_new, (unsigned int a0), (a0), NULL)
LIBEV_WRAP_RET(ev_tstamp, ev_now, (struct ev_loop * a0), (a0), ((ev_tstamp){0}))
LIBEV_WRAP_VOID(ev_now_update, (struct ev_loop * a0), (a0))
LIBEV_WRAP_VOID(ev_once, (struct ev_loop * a0, int a1, int a2, ev_tstamp a3, void (*a4) (int, void *), void * a5), (a0, a1, a2, a3, a4, a5))
LIBEV_WRAP_RET(unsigned int, ev_pending_count, (struct ev_loop * a0), (a0), ((unsigned int){0}))
LIBEV_WRAP_VOID(ev_periodic_again, (struct ev_loop * a0, ev_periodic * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_periodic_start, (struct ev_loop * a0, ev_periodic * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_periodic_stop, (struct ev_loop * a0, ev_periodic * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_prepare_start, (struct ev_loop * a0, ev_prepare * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_prepare_stop, (struct ev_loop * a0, ev_prepare * a1), (a0, a1))
LIBEV_WRAP_RET(unsigned int, ev_recommended_backends, (void), (), ((unsigned int){0}))
LIBEV_WRAP_VOID(ev_ref, (struct ev_loop * a0), (a0))
LIBEV_WRAP_VOID(ev_resume, (struct ev_loop * a0), (a0))
LIBEV_WRAP_RET(int, ev_run, (struct ev_loop * a0, int a1), (a0, a1), -1)
LIBEV_WRAP_VOID(ev_set_allocator, (void *(*a0) (void *, long int)), (a0))
LIBEV_WRAP_VOID(ev_set_invoke_pending_cb, (struct ev_loop * a0, ev_loop_callback a1), (a0, a1))
LIBEV_WRAP_VOID(ev_set_io_collect_interval, (struct ev_loop * a0, ev_tstamp a1), (a0, a1))
LIBEV_WRAP_VOID(ev_set_loop_release_cb, (struct ev_loop * a0, void (*a1) (struct ev_loop *), void (*a2) (struct ev_loop *)), (a0, a1, a2))
LIBEV_WRAP_VOID(ev_set_syserr_cb, (void (*a0) (const char *)), (a0))
LIBEV_WRAP_VOID(ev_set_timeout_collect_interval, (struct ev_loop * a0, ev_tstamp a1), (a0, a1))
LIBEV_WRAP_VOID(ev_set_userdata, (struct ev_loop * a0, void * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_signal_start, (struct ev_loop * a0, ev_signal * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_signal_stop, (struct ev_loop * a0, ev_signal * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_sleep, (ev_tstamp a0), (a0))
LIBEV_WRAP_VOID(ev_stat_start, (struct ev_loop * a0, ev_stat * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_stat_stat, (struct ev_loop * a0, ev_stat * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_stat_stop, (struct ev_loop * a0, ev_stat * a1), (a0, a1))
LIBEV_WRAP_RET(unsigned int, ev_supported_backends, (void), (), ((unsigned int){0}))
LIBEV_WRAP_VOID(ev_suspend, (struct ev_loop * a0), (a0))
LIBEV_WRAP_RET(ev_tstamp, ev_time, (void), (), ((ev_tstamp){0}))
LIBEV_WRAP_VOID(ev_timer_again, (struct ev_loop * a0, ev_timer * a1), (a0, a1))
LIBEV_WRAP_RET(ev_tstamp, ev_timer_remaining, (struct ev_loop * a0, ev_timer * a1), (a0, a1), ((ev_tstamp){0}))
LIBEV_WRAP_VOID(ev_timer_start, (struct ev_loop * a0, ev_timer * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_timer_stop, (struct ev_loop * a0, ev_timer * a1), (a0, a1))
LIBEV_WRAP_VOID(ev_unref, (struct ev_loop * a0), (a0))
LIBEV_WRAP_RET(void *, ev_userdata, (struct ev_loop * a0), (a0), NULL)
LIBEV_WRAP_VOID(ev_verify, (struct ev_loop * a0), (a0))
LIBEV_WRAP_RET(int, ev_version_major, (void), (), -1)
LIBEV_WRAP_RET(int, ev_version_minor, (void), (), -1)
