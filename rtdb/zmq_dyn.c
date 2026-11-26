/* ============================================================
 * zmq_dyn.c - dlopen 模拟链接：libzmq C API（4.3.5）
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照上游 include/zmq.h 的 96 个唯一导出函数。
 * ZMQ_EXPORT 共 97 条声明，其中 zmq_ppoll 有 POSIX/Windows 两个
 * 互斥声明，只封装 POSIX（const sigset_t *）一次。
 * 类型按头文件原样内嵌，字段顺序即 ABI；导出函数名保持原生原样。
 * 链接期不需要 libzmq，仅需 -ldl。
 * 加载顺序：显式路径 > ZMQ_DYN_LIB > LD_LIBRARY_PATH > soname >
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
#include <signal.h>
#include <sys/uio.h>

/* ---------- 平台适配层 ---------- */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE lib_handle_t;
  typedef unsigned __int64 zmq_fd_t;
  static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
  static void *get_sym(lib_handle_t h, const char *name) { return (void *)GetProcAddress(h, name); }
  static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *lib_error(void) { return "LoadLibrary failed"; }
  #define PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *lib_handle_t;
  typedef int zmq_fd_t;
  static lib_handle_t load_lib(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
  static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
  static const char *lib_error(void) { return dlerror(); }
  #define PATH_SEP ':'
#endif

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<zmq.h>)
#    define ZMQ_BUILD_DRAFT_API
#    include <zmq.h>
#    define ZMQ_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef ZMQ_DYN_HAVE_HEADER
/* ---------- 类型（zmq.h 原样内嵌，字段顺序即 ABI） ---------- */
typedef struct zmq_msg_t {
    unsigned char _[64] __attribute__((aligned(sizeof(void *))));
} zmq_msg_t;

typedef struct zmq_pollitem_t {
    void *socket;
    zmq_fd_t fd;
    short events;
    short revents;
} zmq_pollitem_t;

typedef struct zmq_poller_event_t {
    void *socket;
    zmq_fd_t fd;
    void *user_data;
    short events;
} zmq_poller_event_t;

typedef void (zmq_free_fn)(void *data_, void *hint_);
typedef void (zmq_timer_fn)(int timer_id, void *arg);
typedef void (zmq_thread_fn)(void *);

#define ZMQ_PAIR 0
#define ZMQ_DONTWAIT 1
#define ZMQ_SNDMORE 2
#define ZMQ_POLLIN 1
#define ZMQ_POLLOUT 2
#define ZMQ_LINGER 17
#define ZMQ_SNDTIMEO 28
#define ZMQ_RCVTIMEO 27
#define ZMQ_EVENT_ALL 0xFFFF

#endif /* ZMQ_DYN_HAVE_HEADER */

/* ---------- 动态层状态 ---------- */
static lib_handle_t zmq_handle = NULL;
static int zmq_init_state = 0;

static void *zmq_sym(const char *name) {
    return zmq_handle ? get_sym(zmq_handle, name) : NULL;
}

static int zmq_do_load(const char *library_path) {
    static char bufs[64][1024];
    const char *candidates[64];
    const char *so_soname = "libzmq.so.5";
    const char *so_ver = "libzmq.so.5.2.5";
    const char *so_plain = "libzmq.so";
    int n = 0, i;
    int res = -1;

    if (zmq_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("ZMQ_DYN_LIB");
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
    candidates[n++] = "/opt/homebrew/lib/libzmq.dylib";
    candidates[n++] = "/usr/local/lib/libzmq.dylib";
#elif defined(_WIN32)
    candidates[n++] = "libzmq.dll";
    candidates[n++] = "zmq.dll";
#else
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/x86_64-linux-gnu/%s", so_soname);
    candidates[n] = bufs[n]; n++;
#endif

    for (i = 0; i < n; i++) {
        zmq_handle = load_lib(candidates[i]);
        if (zmq_handle) { res = i; break; }
    }
    if (!zmq_handle || !zmq_sym("zmq_ctx_new")) {
        fprintf(stderr, "zmq_dyn: cannot load zmq (%s)\n", lib_error());
        if (zmq_handle) { free_lib(zmq_handle); zmq_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "zmq_dyn: loaded %d %s\n", res, candidates[res]);
    return 0;
}

int zmq_libok(void) {
    if (zmq_init_state == 0)
        zmq_init_state = (zmq_do_load(NULL) == 0) ? 1 : -1;
    return (zmq_init_state == 1 && zmq_handle != NULL) ? 1 : 0;
}

int zmq_dyn_init(const char *library_path) {
    if (zmq_do_load(library_path) == 0) {
        zmq_init_state = 1;
        return 0;
    }
    if (zmq_init_state == 0) zmq_init_state = -1;
    return -1;
}

void zmq_dyn_cleanup(void) {
    if (zmq_handle) { free_lib(zmq_handle); zmq_handle = NULL; }
    zmq_init_state = 0;
}

int zmq_dyn_has_symbol(const char *name) {
    if (!zmq_libok()) return 0;
    return zmq_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                            \
ret name params {                                                              \
    static ret (*fn) params = NULL;                                            \
    if (!fn) {                                                                 \
        if (!zmq_libok()) return fallback;                                     \
        *(void **)(&fn) = zmq_sym(#name);                                      \
        if (!fn) {                                                             \
            fprintf(stderr, "zmq_dyn: dlsym failed: %s\n", #name);             \
            return fallback;                                                   \
        }                                                                      \
    }                                                                          \
    return fn args;                                                            \
}
#define WRAP_VOID(name, params, args)                                          \
void name params {                                                             \
    static void (*fn) params = NULL;                                           \
    if (!fn) {                                                                 \
        if (!zmq_libok()) return;                                              \
        *(void **)(&fn) = zmq_sym(#name);                                      \
        if (!fn) {                                                             \
            fprintf(stderr, "zmq_dyn: dlsym failed: %s\n", #name);             \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn args;                                                                   \
}

/* ---------- 96 个唯一 ZMQ_EXPORT 公开函数（函数名原样） ---------- */
WRAP_RET(int, zmq_errno, (void), (), -1)
WRAP_RET(const char *, zmq_strerror, (int errnum_), (errnum_), NULL)
WRAP_VOID(zmq_version, (int *major_, int *minor_, int *patch_), (major_, minor_, patch_))
WRAP_RET(void *, zmq_ctx_new, (void), (), NULL)
WRAP_RET(int, zmq_ctx_term, (void *context_), (context_), -1)
WRAP_RET(int, zmq_ctx_shutdown, (void *context_), (context_), -1)
WRAP_RET(int, zmq_ctx_set, (void *context_, int option_, int optval_), (context_, option_, optval_), -1)
WRAP_RET(int, zmq_ctx_get, (void *context_, int option_), (context_, option_), -1)
WRAP_RET(void *, zmq_init, (int io_threads_), (io_threads_), NULL)
WRAP_RET(int, zmq_term, (void *context_), (context_), -1)
WRAP_RET(int, zmq_ctx_destroy, (void *context_), (context_), -1)
WRAP_RET(int, zmq_msg_init, (zmq_msg_t *msg_), (msg_), -1)
WRAP_RET(int, zmq_msg_init_size, (zmq_msg_t *msg_, size_t size_), (msg_, size_), -1)
WRAP_RET(int, zmq_msg_init_data, (zmq_msg_t *msg_, void *data_, size_t size_, zmq_free_fn *ffn_, void *hint_), (msg_, data_, size_, ffn_, hint_), -1)
WRAP_RET(int, zmq_msg_send, (zmq_msg_t *msg_, void *s_, int flags_), (msg_, s_, flags_), -1)
WRAP_RET(int, zmq_msg_recv, (zmq_msg_t *msg_, void *s_, int flags_), (msg_, s_, flags_), -1)
WRAP_RET(int, zmq_msg_close, (zmq_msg_t *msg_), (msg_), -1)
WRAP_RET(int, zmq_msg_move, (zmq_msg_t *dest_, zmq_msg_t *src_), (dest_, src_), -1)
WRAP_RET(int, zmq_msg_copy, (zmq_msg_t *dest_, zmq_msg_t *src_), (dest_, src_), -1)
WRAP_RET(void *, zmq_msg_data, (zmq_msg_t *msg_), (msg_), NULL)
WRAP_RET(size_t, zmq_msg_size, (const zmq_msg_t *msg_), (msg_), 0)
WRAP_RET(int, zmq_msg_more, (const zmq_msg_t *msg_), (msg_), -1)
WRAP_RET(int, zmq_msg_get, (const zmq_msg_t *msg_, int property_), (msg_, property_), -1)
WRAP_RET(int, zmq_msg_set, (zmq_msg_t *msg_, int property_, int optval_), (msg_, property_, optval_), -1)
WRAP_RET(const char *, zmq_msg_gets, (const zmq_msg_t *msg_, const char *property_), (msg_, property_), NULL)
WRAP_RET(void *, zmq_socket, (void *context_, int type_), (context_, type_), NULL)
WRAP_RET(int, zmq_close, (void *s_), (s_), -1)
WRAP_RET(int, zmq_setsockopt, (void *s_, int option_, const void *optval_, size_t optvallen_), (s_, option_, optval_, optvallen_), -1)
WRAP_RET(int, zmq_getsockopt, (void *s_, int option_, void *optval_, size_t *optvallen_), (s_, option_, optval_, optvallen_), -1)
WRAP_RET(int, zmq_bind, (void *s_, const char *addr_), (s_, addr_), -1)
WRAP_RET(int, zmq_connect, (void *s_, const char *addr_), (s_, addr_), -1)
WRAP_RET(int, zmq_unbind, (void *s_, const char *addr_), (s_, addr_), -1)
WRAP_RET(int, zmq_disconnect, (void *s_, const char *addr_), (s_, addr_), -1)
WRAP_RET(int, zmq_send, (void *s_, const void *buf_, size_t len_, int flags_), (s_, buf_, len_, flags_), -1)
WRAP_RET(int, zmq_send_const, (void *s_, const void *buf_, size_t len_, int flags_), (s_, buf_, len_, flags_), -1)
WRAP_RET(int, zmq_recv, (void *s_, void *buf_, size_t len_, int flags_), (s_, buf_, len_, flags_), -1)
WRAP_RET(int, zmq_socket_monitor, (void *s_, const char *addr_, int events_), (s_, addr_, events_), -1)
WRAP_RET(int, zmq_poll, (zmq_pollitem_t *items_, int nitems_, long timeout_), (items_, nitems_, timeout_), -1)
WRAP_RET(int, zmq_proxy, (void *frontend_, void *backend_, void *capture_), (frontend_, backend_, capture_), -1)
WRAP_RET(int, zmq_proxy_steerable, (void *frontend_, void *backend_, void *capture_, void *control_), (frontend_, backend_, capture_, control_), -1)
WRAP_RET(int, zmq_has, (const char *capability_), (capability_), -1)
WRAP_RET(int, zmq_device, (int type_, void *frontend_, void *backend_), (type_, frontend_, backend_), -1)
WRAP_RET(int, zmq_sendmsg, (void *s_, zmq_msg_t *msg_, int flags_), (s_, msg_, flags_), -1)
WRAP_RET(int, zmq_recvmsg, (void *s_, zmq_msg_t *msg_, int flags_), (s_, msg_, flags_), -1)
WRAP_RET(int, zmq_sendiov, (void *s_, struct iovec *iov_, size_t count_, int flags_), (s_, iov_, count_, flags_), -1)
WRAP_RET(int, zmq_recviov, (void *s_, struct iovec *iov_, size_t *count_, int flags_), (s_, iov_, count_, flags_), -1)
WRAP_RET(char *, zmq_z85_encode, (char *dest_, const uint8_t *data_, size_t size_), (dest_, data_, size_), NULL)
WRAP_RET(uint8_t *, zmq_z85_decode, (uint8_t *dest_, const char *string_), (dest_, string_), NULL)
WRAP_RET(int, zmq_curve_keypair, (char *z85_public_key_, char *z85_secret_key_), (z85_public_key_, z85_secret_key_), -1)
WRAP_RET(int, zmq_curve_public, (char *z85_public_key_, const char *z85_secret_key_), (z85_public_key_, z85_secret_key_), -1)
WRAP_RET(void *, zmq_atomic_counter_new, (void), (), NULL)
WRAP_VOID(zmq_atomic_counter_set, (void *counter_, int value_), (counter_, value_))
WRAP_RET(int, zmq_atomic_counter_inc, (void *counter_), (counter_), -1)
WRAP_RET(int, zmq_atomic_counter_dec, (void *counter_), (counter_), -1)
WRAP_RET(int, zmq_atomic_counter_value, (void *counter_), (counter_), -1)
WRAP_VOID(zmq_atomic_counter_destroy, (void **counter_p_), (counter_p_))
WRAP_RET(void *, zmq_timers_new, (void), (), NULL)
WRAP_RET(int, zmq_timers_destroy, (void **timers_p), (timers_p), -1)
WRAP_RET(int, zmq_timers_add, (void *timers, size_t interval, zmq_timer_fn handler, void *arg), (timers, interval, handler, arg), -1)
WRAP_RET(int, zmq_timers_cancel, (void *timers, int timer_id), (timers, timer_id), -1)
WRAP_RET(int, zmq_timers_set_interval, (void *timers, int timer_id, size_t interval), (timers, timer_id, interval), -1)
WRAP_RET(int, zmq_timers_reset, (void *timers, int timer_id), (timers, timer_id), -1)
WRAP_RET(long, zmq_timers_timeout, (void *timers), (timers), -1)
WRAP_RET(int, zmq_timers_execute, (void *timers), (timers), -1)
WRAP_RET(void *, zmq_stopwatch_start, (void), (), NULL)
WRAP_RET(unsigned long, zmq_stopwatch_intermediate, (void *watch_), (watch_), 0)
WRAP_RET(unsigned long, zmq_stopwatch_stop, (void *watch_), (watch_), 0)
WRAP_VOID(zmq_sleep, (int seconds_), (seconds_))
WRAP_RET(void *, zmq_threadstart, (zmq_thread_fn *func_, void *arg_), (func_, arg_), NULL)
WRAP_VOID(zmq_threadclose, (void *thread_), (thread_))
WRAP_RET(int, zmq_ctx_set_ext, (void *context_, int option_, const void *optval_, size_t optvallen_), (context_, option_, optval_, optvallen_), -1)
WRAP_RET(int, zmq_ctx_get_ext, (void *context_, int option_, void *optval_, size_t *optvallen_), (context_, option_, optval_, optvallen_), -1)
WRAP_RET(int, zmq_join, (void *s, const char *group), (s, group), -1)
WRAP_RET(int, zmq_leave, (void *s, const char *group), (s, group), -1)
WRAP_RET(uint32_t, zmq_connect_peer, (void *s_, const char *addr_), (s_, addr_), 0)
WRAP_RET(int, zmq_msg_set_routing_id, (zmq_msg_t *msg, uint32_t routing_id), (msg, routing_id), -1)
WRAP_RET(uint32_t, zmq_msg_routing_id, (zmq_msg_t *msg), (msg), 0)
WRAP_RET(int, zmq_msg_set_group, (zmq_msg_t *msg, const char *group), (msg, group), -1)
WRAP_RET(const char *, zmq_msg_group, (zmq_msg_t *msg), (msg), NULL)
WRAP_RET(int, zmq_msg_init_buffer, (zmq_msg_t *msg_, const void *buf_, size_t size_), (msg_, buf_, size_), -1)
WRAP_RET(void *, zmq_poller_new, (void), (), NULL)
WRAP_RET(int, zmq_poller_destroy, (void **poller_p), (poller_p), -1)
WRAP_RET(int, zmq_poller_size, (void *poller), (poller), -1)
WRAP_RET(int, zmq_poller_add, (void *poller, void *socket, void *user_data, short events), (poller, socket, user_data, events), -1)
WRAP_RET(int, zmq_poller_modify, (void *poller, void *socket, short events), (poller, socket, events), -1)
WRAP_RET(int, zmq_poller_remove, (void *poller, void *socket), (poller, socket), -1)
WRAP_RET(int, zmq_poller_wait, (void *poller, zmq_poller_event_t *event, long timeout), (poller, event, timeout), -1)
WRAP_RET(int, zmq_poller_wait_all, (void *poller, zmq_poller_event_t *events, int n_events, long timeout), (poller, events, n_events, timeout), -1)
WRAP_RET(int, zmq_poller_fd, (void *poller, zmq_fd_t *fd), (poller, fd), -1)
WRAP_RET(int, zmq_poller_add_fd, (void *poller, zmq_fd_t fd, void *user_data, short events), (poller, fd, user_data, events), -1)
WRAP_RET(int, zmq_poller_modify_fd, (void *poller, zmq_fd_t fd, short events), (poller, fd, events), -1)
WRAP_RET(int, zmq_poller_remove_fd, (void *poller, zmq_fd_t fd), (poller, fd), -1)
WRAP_RET(int, zmq_socket_get_peer_state, (void *socket, const void *routing_id, size_t routing_id_size), (socket, routing_id, routing_id_size), -1)
WRAP_RET(int, zmq_socket_monitor_versioned, (void *s_, const char *addr_, uint64_t events_, int event_version_, int type_), (s_, addr_, events_, event_version_, type_), -1)
WRAP_RET(int, zmq_socket_monitor_pipes_stats, (void *s), (s), -1)
WRAP_RET(int, zmq_ppoll, (zmq_pollitem_t *items_, int nitems_, long timeout_, const sigset_t *sigmask_), (items_, nitems_, timeout_, sigmask_), -1)
