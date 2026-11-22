/* ============================================================
 * libevent_dyn.c - type safe dynamic lib load for libevent 2.1
 * ============================================================
 * 设计要点：
 *   1. 单文件、无 .h、无 wrapper 层；链接期不需要目标库。
 *   覆盖 libevent-2.1.so.7 全部 513 个导出符号：
 *     event_* 100  evbuffer_* 52  bufferevent_* 84  evdns_* 56
 *     evhttp_* 113 evrpc_* 25     evtag_* 24       evutil_* 51
 *     evthread_* 8
 *   签名与公开结构按官方 release-2.1.13-stable 头文件核对
 *   （11 个公开头文件 diff = 0）；内部符号签名取自同版本源码。
 *   双句柄：合并版优先，缺失时回退 core + extra。
 *   2. 每个函数首次调用时 dlsym 缓存；失败打印 stderr 并返回安全值。
 *   3. 加载顺序：显式路径 > LIBEVENT_DYN_LIB > LD_LIBRARY_PATH >
 *      soname > .so > Nix > 平台默认；成功后不卸载。
 *   注意：event_* / evbuffer_* / bufferevent_* 等内部下划线符号
 *   （nm 可见）也一并封装；可变参数函数按官方实现等价展开。
 *   使用：
 *     libevent_dyn_init(NULL);
 *     ... 原版 API 直接调用 ...
 *     libevent_dyn_cleanup();   // 仅复位加载状态，不卸载库
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
 typedef HMODULE lib_handle_t;
 static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
 static void *get_sym(lib_handle_t h, const char *name) {
     return (void *)GetProcAddress(h, name);
 }
 static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *lib_error(void) { return "LoadLibrary failed"; }
 #define PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *lib_handle_t;
 static lib_handle_t load_lib(const char *path) {
     return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
 }
 static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
 static const char *lib_error(void) { return dlerror(); }
 #define PATH_SEP ':'
#endif

/* ============================================================
 * 类型定义（官方头文件机械提取；未列出的 struct 为不透明句柄）
 * ============================================================ */
struct addrinfo;
struct bufferevent_ops;
struct bufferevent_private;
struct cmsghdr;
struct evdns_server_port;
struct event_callback;
struct evutil_addrinfo;
struct evutil_weakrand_state;
struct gaicb;
struct hostent;
struct in6_addr;
struct in_addr;
struct iovec;
struct itimerspec;
struct itimerval;
struct mmsghdr;
struct msghdr;
struct netent;
struct obstack;
struct protoent;
struct rpcent;
struct servent;
struct sigevent;
struct sockaddr;
struct sockaddr_in;
struct sockaddr_in6;
struct sockaddr_storage;
struct timespec;
struct timeval;
struct timex;
struct timezone;
struct tm;

struct evutil_monotonic_timer;
struct event_base;
struct event;
struct event_config;
enum event_method_feature { EV_FEATURE_ET = 0x01, EV_FEATURE_O1 = 0x02, EV_FEATURE_FDS = 0x04, EV_FEATURE_EARLY_CLOSE = 0x08 };
enum event_base_config_flag { EVENT_BASE_FLAG_NOLOCK = 0x01, EVENT_BASE_FLAG_IGNORE_ENV = 0x02, EVENT_BASE_FLAG_STARTUP_IOCP = 0x04, EVENT_BASE_FLAG_NO_CACHE_TIME = 0x08, EVENT_BASE_FLAG_EPOLL_USE_CHANGELIST = 0x10, EVENT_BASE_FLAG_PRECISE_TIMER = 0x20 };
typedef void (*event_log_cb)(int severity, const char *msg);
typedef void (*event_fatal_cb)(int err);
typedef void (*event_callback_fn)(int, short, void *);
typedef void (*event_finalize_callback_fn)(struct event *, void *);
typedef int (*event_base_foreach_event_cb)(const struct event_base *, const struct event *, void *);
struct bufferevent;
struct event_base;
struct evbuffer;
typedef void (*bufferevent_data_cb)(struct bufferevent *bev, void *ctx);
typedef void (*bufferevent_event_cb)(struct bufferevent *bev, short what, void *ctx);
enum bufferevent_options { BEV_OPT_CLOSE_ON_FREE = (1<<0), BEV_OPT_THREADSAFE = (1<<1), BEV_OPT_DEFER_CALLBACKS = (1<<2), BEV_OPT_UNLOCK_CALLBACKS = (1<<3) };
struct evdns_base;
enum bufferevent_flush_mode { BEV_NORMAL = 0, BEV_FLUSH = 1, BEV_FINISHED = 2 };
enum bufferevent_trigger_options { BEV_TRIG_IGNORE_WATERMARKS = (1<<16), BEV_TRIG_DEFER_CALLBACKS = BEV_OPT_DEFER_CALLBACKS };
enum bufferevent_filter_result { BEV_OK = 0, BEV_NEED_MORE = 1, BEV_ERROR = 2 };
typedef enum bufferevent_filter_result (*bufferevent_filter_cb)( struct evbuffer *src, struct evbuffer *dst, ssize_t dst_limit, enum bufferevent_flush_mode mode, void *ctx);
struct ev_token_bucket_cfg;
struct bufferevent_rate_limit_group;
struct evbuffer;
struct evbuffer_ptr { ssize_t pos; struct { void *chain; size_t pos_in_chain; } internal_; };
enum evbuffer_eol_style { EVBUFFER_EOL_ANY, EVBUFFER_EOL_CRLF, EVBUFFER_EOL_CRLF_STRICT, EVBUFFER_EOL_LF, EVBUFFER_EOL_NUL };
typedef void (*evbuffer_ref_cleanup_cb)(const void *data, size_t datalen, void *extra);
struct evbuffer_file_segment;
typedef void (*evbuffer_file_segment_cleanup_cb)( struct evbuffer_file_segment const* seg, int flags, void* arg);
enum evbuffer_ptr_how { EVBUFFER_PTR_SET, EVBUFFER_PTR_ADD };
struct evbuffer_cb_info { size_t orig_size; size_t n_added; size_t n_deleted; };
typedef void (*evbuffer_cb_func)(struct evbuffer *buffer, const struct evbuffer_cb_info *info, void *arg);
struct evbuffer_cb_entry;
struct event_base;
typedef void (*evbuffer_cb)(struct evbuffer *buffer, size_t old_len, size_t new_len, void *arg);
typedef void (*evdns_callback_type) (int result, char type, int count, int ttl, void *addresses, void *arg);
struct evdns_base;
struct event_base;
struct evdns_request;
typedef void (*evdns_debug_log_fn_type)(int is_warning, const char *msg);
struct evdns_server_request;
struct evdns_server_question;
typedef void (*evdns_request_callback_fn_type)(struct evdns_server_request *, void *);
typedef void (*evdns_getaddrinfo_cb)(int result, struct addrinfo *res, void *arg);
struct evdns_base;
struct evdns_getaddrinfo_request;
struct evdns_base;
struct evbuffer;
struct event_base;
struct bufferevent;
struct evhttp_connection;
struct evhttp;
struct evhttp_request;
struct evkeyvalq;
struct evhttp_bound_socket;
struct evconnlistener;
struct evdns_base;
typedef void evhttp_bound_socket_foreach_fn(struct evhttp_bound_socket *, void *);
enum evhttp_cmd_type { EVHTTP_REQ_GET = 1 << 0, EVHTTP_REQ_POST = 1 << 1, EVHTTP_REQ_HEAD = 1 << 2, EVHTTP_REQ_PUT = 1 << 3, EVHTTP_REQ_DELETE = 1 << 4, EVHTTP_REQ_OPTIONS = 1 << 5, EVHTTP_REQ_TRACE = 1 << 6, EVHTTP_REQ_CONNECT = 1 << 7, EVHTTP_REQ_PATCH = 1 << 8 };
enum evhttp_request_kind { EVHTTP_REQUEST, EVHTTP_RESPONSE };
enum evhttp_request_error { EVREQ_HTTP_TIMEOUT, EVREQ_HTTP_EOF, EVREQ_HTTP_INVALID_HEADER, EVREQ_HTTP_BUFFER_ERROR, EVREQ_HTTP_REQUEST_CANCEL, EVREQ_HTTP_DATA_TOO_LONG };
struct evhttp_uri;
struct evbuffer;
struct event_base;
struct evrpc_req_generic;
struct evrpc_request_wrapper;
struct evrpc;
struct evhttp_request;
struct evrpc_status;
struct evrpc_hook_meta;
struct evrpc_pool;
struct evrpc_base;
struct evhttp;
struct evhttp_connection;
struct evrpc_status;
enum EVRPC_HOOK_TYPE { EVRPC_INPUT, EVRPC_OUTPUT };
enum EVRPC_HOOK_RESULT { EVRPC_TERMINATE = -1, EVRPC_CONTINUE = 0, EVRPC_PAUSE = 1 };
struct evbuffer;
struct evthread_lock_callbacks { int lock_api_version; unsigned supported_locktypes; void *(*alloc)(unsigned locktype); void (*free)(void *lock, unsigned locktype); int (*lock)(unsigned mode, void *lock); int (*unlock)(unsigned mode, void *lock); };
struct evthread_condition_callbacks { int condition_api_version; void *(*alloc_condition)(unsigned condtype); void (*free_condition)(void *cond); int (*signal_condition)(void *cond, int broadcast); int (*wait_condition)(void *cond, void *lock, const struct timeval *timeout); };
struct event_base;
struct evconnlistener;
typedef void (*evconnlistener_cb)(struct evconnlistener *, int, struct sockaddr *, int socklen, void *);
typedef void (*evconnlistener_errorcb)(struct evconnlistener *, void *);

/* event2/util.h 用 #define 定义这些整型别名；此处以 typedef 等价落地 */
typedef unsigned char ev_uint8_t;
typedef signed char ev_int8_t;
typedef unsigned short ev_uint16_t;
typedef signed short ev_int16_t;
typedef unsigned int ev_uint32_t;
typedef signed int ev_int32_t;
typedef unsigned long long ev_uint64_t;
typedef signed long long ev_int64_t;
typedef uintptr_t ev_uintptr_t;
typedef intptr_t ev_intptr_t;
typedef ssize_t ev_ssize_t;
typedef socklen_t ev_socklen_t;

/* 以下来自 2.1.13 内部头（对应导出符号的参数类型） */
typedef int evutil_socket_t;
typedef ev_uint16_t bufferevent_suspend_flags;
typedef void (*deferred_cb_fn)(struct event_callback *, void *);
typedef struct evdns_getaddrinfo_request *(*evdns_getaddrinfo_fn)(
    struct evdns_base *, const char *, const char *,
    const struct evutil_addrinfo *, evdns_getaddrinfo_cb, void *);
typedef void (*evdns_getaddrinfo_cancel_fn)(
    struct evdns_getaddrinfo_request *);

/* http-internal.h 的内部枚举（导出的 evhttp_*_ 内部函数使用） */
enum message_read_status {
    ALL_DATA_READ = 1,
    MORE_DATA_EXPECTED = 0,
    DATA_CORRUPTED = -1,
    REQUEST_CANCELED = -2,
    DATA_TOO_LONG = -3
};
enum evhttp_transfer_encoding_header_status {
    TE_INVALID,
    TE_ENDS_IN_CHUNKED,
    TE_NO_CHUNKED
};


/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static lib_handle_t libevent_handle = NULL;
static lib_handle_t libevent_extra_handle = NULL;
static void *libevent_sym(const char *name) {
    void *sym = libevent_handle ? get_sym(libevent_handle, name) : NULL;
    if (!sym && libevent_extra_handle) sym = get_sym(libevent_extra_handle, name);
    return sym;
}
static int libevent_init_state = 0;

static int libevent_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    int n = 0, i;
    if (libevent_handle) return 0;
    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("LIBEVENT_DYN_LIB");
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
                    char *sep = strchr(q, PATH_SEP);
                    if (sep) *sep = 0;
                    if (*q) {
                        snprintf(bufs[n], sizeof bufs[0], "%s/libevent-2.1.so.7", q);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/libevent.so", q);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = "libevent-2.1.so.7";
    candidates[n++] = "libevent.so";
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libevent-2.1.so.7", home);
            candidates[n] = bufs[n]; n++;
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libevent.so", home);
            candidates[n] = bufs[n]; n++;
        }
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libevent-2.1.so.7";
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libevent.so";
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libevent-2.1.so.7";
    candidates[n++] = "/usr/local/lib/libevent-2.1.so.7";
    candidates[n++] = "/opt/homebrew/lib/libevent.so";
    candidates[n++] = "/usr/local/lib/libevent.so";
#elif defined(_WIN32)
    candidates[n++] = "libevent-2.1.dll";
#else
    candidates[n++] = "/usr/local/lib/libevent-2.1.so.7";
    candidates[n++] = "/usr/lib/libevent-2.1.so.7";
    candidates[n++] = "/usr/local/lib/libevent.so";
    candidates[n++] = "/usr/lib/libevent.so";
#endif
    for (i = 0; i < n; i++) {
        libevent_handle = load_lib(candidates[i]);
        if (libevent_handle) break;
    }
    if (!libevent_handle) {
        /* 拆分版：core + extra 两个句柄 */
        static const char *core_c[] = { "libevent_core-2.1.so.7", "libevent_core.so",
            "/usr/local/lib/libevent_core-2.1.so.7", "/usr/lib/libevent_core-2.1.so.7" };
        static const char *extra_c[] = { "libevent_extra-2.1.so.7", "libevent_extra.so",
            "/usr/local/lib/libevent_extra-2.1.so.7", "/usr/lib/libevent_extra-2.1.so.7" };
        for (i = 0; i < 4 && !libevent_handle; i++) libevent_handle = load_lib(core_c[i]);
        for (i = 0; i < 4 && !libevent_extra_handle; i++) libevent_extra_handle = load_lib(extra_c[i]);
    }
    if (libevent_handle && !libevent_extra_handle &&
        !get_sym(libevent_handle, "evhttp_new")) {
        /* 显式路径/LD_LIBRARY_PATH 指向拆分版 core 时补载 extra */
        static const char *extra_c[] = { "libevent_extra-2.1.so.7", "libevent_extra.so",
            "/usr/local/lib/libevent_extra-2.1.so.7", "/usr/lib/libevent_extra-2.1.so.7" };
        for (i = 0; i < 4 && !libevent_extra_handle; i++)
            libevent_extra_handle = load_lib(extra_c[i]);
    }
    if (!libevent_handle || !libevent_sym("event_base_new")) {
        fprintf(stderr, "libevent_dyn: cannot load libevent (%s)\n", lib_error());
        if (libevent_handle) { free_lib(libevent_handle); libevent_handle = NULL; }
        if (libevent_extra_handle) { free_lib(libevent_extra_handle); libevent_extra_handle = NULL; }
        return -1;
    }
    if (!libevent_sym("evhttp_new"))
        fprintf(stderr, "libevent_dyn: warning: evhttp_new not exported\n");
    fprintf(stderr, "libevent_dyn: loaded libevent (2.1 headers)\n");
    return 0;
}

int libevent_dyn_libok(void) {
    if (libevent_init_state == 0)
        libevent_init_state = (libevent_do_load(NULL) == 0) ? 1 : -1;
    return (libevent_init_state == 1 && libevent_handle != NULL) ? 1 : 0;
}

int libevent_dyn_init(const char *library_path) {
    if (libevent_do_load(library_path) == 0) {
        libevent_init_state = 1;
        return 0;
    }
    if (libevent_init_state == 0) libevent_init_state = -1;
    return -1;
}

void libevent_dyn_cleanup(void) {
    /* 仅复位加载状态，不卸载已成功加载的库（dlclose 不安全） */
    if (!libevent_handle) libevent_init_state = 0;
    else if (libevent_init_state == -1) libevent_init_state = 0;
}

int libevent_dyn_has_symbol(const char *name) {
    if (!libevent_dyn_libok()) return 0;
    return libevent_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏（首次调用 dlsym 并缓存；失败打印 stderr 返回安全值）
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!libevent_dyn_libok()) return fallback;                          \
        *(void **)(&fn) = libevent_sym(#name);                               \
        if (!fn) {                                                        \
            fprintf(stderr, "libevent_dyn: dlsym failed: %s\n", #name);     \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define WRAP_VOID(name, params, args)                                     \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!libevent_dyn_libok()) return;                                   \
        *(void **)(&fn) = libevent_sym(#name);                               \
        if (!fn) {                                                        \
            fprintf(stderr, "libevent_dyn: dlsym failed: %s\n", #name);     \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}

/* ============================================================
 * 导出函数封装（与 nm -D 导出一一对应，共 513 个）
 * ============================================================ */
WRAP_RET(int, bufferevent_add_event_, (struct event * a0, const struct timeval * a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_add_to_rate_limit_group, (struct bufferevent * a0, struct bufferevent_rate_limit_group * a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_base_set, (struct event_base * a0, struct bufferevent * a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_decref, (struct bufferevent * a0), (a0), -1)
WRAP_RET(int, bufferevent_decref_and_unlock_, (struct bufferevent * a0), (a0), -1)
WRAP_RET(int, bufferevent_decrement_read_buckets_, (struct bufferevent_private * a0, ev_ssize_t a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_decrement_read_limit, (struct bufferevent * a0, ssize_t a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_decrement_write_buckets_, (struct bufferevent_private * a0, ev_ssize_t a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_decrement_write_limit, (struct bufferevent * a0, ssize_t a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_disable, (struct bufferevent * a0, short int a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_disable_hard_, (struct bufferevent * a0, short a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_enable, (struct bufferevent * a0, short int a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_enable_locking_, (struct bufferevent * a0, void * a1), (a0, a1), -1)
WRAP_RET(struct bufferevent *, bufferevent_filter_new, (struct bufferevent * a0, bufferevent_filter_cb a1, bufferevent_filter_cb a2, int a3, void (*a4) (void *), void * a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_RET(int, bufferevent_flush, (struct bufferevent * a0, short int a1, enum bufferevent_flush_mode a2), (a0, a1, a2), -1)
WRAP_VOID(bufferevent_free, (struct bufferevent * a0), (a0))
WRAP_RET(int, bufferevent_generic_adj_existing_timeouts_, (struct bufferevent * a0), (a0), -1)
WRAP_RET(int, bufferevent_generic_adj_timeouts_, (struct bufferevent * a0), (a0), -1)
WRAP_RET(struct event_base *, bufferevent_get_base, (struct bufferevent * a0), (a0), NULL)
WRAP_RET(short int, bufferevent_get_enabled, (struct bufferevent * a0), (a0), ((short int){0}))
WRAP_RET(struct evbuffer *, bufferevent_get_input, (struct bufferevent * a0), (a0), NULL)
WRAP_RET(ssize_t, bufferevent_get_max_single_read, (struct bufferevent * a0), (a0), -1)
WRAP_RET(ssize_t, bufferevent_get_max_single_write, (struct bufferevent * a0), (a0), -1)
WRAP_RET(ssize_t, bufferevent_get_max_to_read, (struct bufferevent * a0), (a0), -1)
WRAP_RET(ssize_t, bufferevent_get_max_to_write, (struct bufferevent * a0), (a0), -1)
WRAP_RET(enum bufferevent_options, bufferevent_get_options_, (struct bufferevent * a0), (a0), ((enum bufferevent_options){0}))
WRAP_RET(struct evbuffer *, bufferevent_get_output, (struct bufferevent * a0), (a0), NULL)
WRAP_RET(int, bufferevent_get_priority, (const struct bufferevent * a0), (a0), -1)
WRAP_RET(ssize_t, bufferevent_get_read_limit, (struct bufferevent * a0), (a0), -1)
WRAP_RET(ev_ssize_t, bufferevent_get_read_max_, (struct bufferevent_private * a0), (a0), -1)
WRAP_RET(const struct ev_token_bucket_cfg *, bufferevent_get_token_bucket_cfg, (const struct bufferevent * a0), (a0), NULL)
WRAP_RET(struct bufferevent *, bufferevent_get_underlying, (struct bufferevent * a0), (a0), NULL)
WRAP_RET(ssize_t, bufferevent_get_write_limit, (struct bufferevent * a0), (a0), -1)
WRAP_RET(ev_ssize_t, bufferevent_get_write_max_, (struct bufferevent_private * a0), (a0), -1)
WRAP_VOID(bufferevent_getcb, (struct bufferevent * a0, bufferevent_data_cb * a1, bufferevent_data_cb * a2, bufferevent_event_cb * a3, void ** a4), (a0, a1, a2, a3, a4))
WRAP_RET(int, bufferevent_getfd, (struct bufferevent * a0), (a0), -1)
WRAP_RET(int, bufferevent_getwatermark, (struct bufferevent * a0, short int a1, size_t * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_VOID(bufferevent_incref, (struct bufferevent * a0), (a0))
WRAP_VOID(bufferevent_incref_and_lock_, (struct bufferevent * a0), (a0))
WRAP_RET(int, bufferevent_init_common_, (struct bufferevent_private * a0, struct event_base * a1, const struct bufferevent_ops * a2, enum bufferevent_options a3), (a0, a1, a2, a3), -1)
WRAP_VOID(bufferevent_init_generic_timeout_cbs_, (struct bufferevent * a0), (a0))
WRAP_VOID(bufferevent_lock, (struct bufferevent * a0), (a0))
WRAP_RET(struct bufferevent *, bufferevent_new, (int a0, bufferevent_data_cb a1, bufferevent_data_cb a2, bufferevent_event_cb a3, void * a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(struct bufferevent *, bufferevent_pair_get_partner, (struct bufferevent * a0), (a0), NULL)
WRAP_RET(int, bufferevent_pair_new, (struct event_base * a0, int a1, struct bufferevent ** a2), (a0, a1, a2), -1)
WRAP_RET(int, bufferevent_priority_set, (struct bufferevent * a0, int a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_rate_limit_group_decrement_read, (struct bufferevent_rate_limit_group * a0, ssize_t a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_rate_limit_group_decrement_write, (struct bufferevent_rate_limit_group * a0, ssize_t a1), (a0, a1), -1)
WRAP_VOID(bufferevent_rate_limit_group_free, (struct bufferevent_rate_limit_group * a0), (a0))
WRAP_RET(ssize_t, bufferevent_rate_limit_group_get_read_limit, (struct bufferevent_rate_limit_group * a0), (a0), -1)
WRAP_VOID(bufferevent_rate_limit_group_get_totals, (struct bufferevent_rate_limit_group * a0, uint64_t * a1, uint64_t * a2), (a0, a1, a2))
WRAP_RET(ssize_t, bufferevent_rate_limit_group_get_write_limit, (struct bufferevent_rate_limit_group * a0), (a0), -1)
WRAP_RET(struct bufferevent_rate_limit_group *, bufferevent_rate_limit_group_new, (struct event_base * a0, const struct ev_token_bucket_cfg * a1), (a0, a1), NULL)
WRAP_VOID(bufferevent_rate_limit_group_reset_totals, (struct bufferevent_rate_limit_group * a0), (a0))
WRAP_RET(int, bufferevent_rate_limit_group_set_cfg, (struct bufferevent_rate_limit_group * a0, const struct ev_token_bucket_cfg * a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_rate_limit_group_set_min_share, (struct bufferevent_rate_limit_group * a0, size_t a1), (a0, a1), -1)
WRAP_RET(size_t, bufferevent_read, (struct bufferevent * a0, void * a1, size_t a2), (a0, a1, a2), ((size_t){0}))
WRAP_RET(int, bufferevent_read_buffer, (struct bufferevent * a0, struct evbuffer * a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_remove_from_rate_limit_group, (struct bufferevent * a0), (a0), -1)
WRAP_VOID(bufferevent_run_eventcb_, (struct bufferevent * a0, short a1, int a2), (a0, a1, a2))
WRAP_VOID(bufferevent_run_readcb_, (struct bufferevent * a0, int a1), (a0, a1))
WRAP_VOID(bufferevent_run_writecb_, (struct bufferevent * a0, int a1), (a0, a1))
WRAP_RET(int, bufferevent_set_max_single_read, (struct bufferevent * a0, size_t a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_set_max_single_write, (struct bufferevent * a0, size_t a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_set_rate_limit, (struct bufferevent * a0, struct ev_token_bucket_cfg * a1), (a0, a1), -1)
WRAP_RET(int, bufferevent_set_timeouts, (struct bufferevent * a0, const struct timeval * a1, const struct timeval * a2), (a0, a1, a2), -1)
WRAP_VOID(bufferevent_setcb, (struct bufferevent * a0, bufferevent_data_cb a1, bufferevent_data_cb a2, bufferevent_event_cb a3, void * a4), (a0, a1, a2, a3, a4))
WRAP_RET(int, bufferevent_setfd, (struct bufferevent * a0, int a1), (a0, a1), -1)
WRAP_VOID(bufferevent_settimeout, (struct bufferevent * a0, int a1, int a2), (a0, a1, a2))
WRAP_VOID(bufferevent_setwatermark, (struct bufferevent * a0, short int a1, size_t a2, size_t a3), (a0, a1, a2, a3))
WRAP_RET(int, bufferevent_socket_connect, (struct bufferevent * a0, const struct sockaddr * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, bufferevent_socket_connect_hostname, (struct bufferevent * a0, struct evdns_base * a1, int a2, const char * a3, int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(const struct sockaddr *, bufferevent_socket_get_conn_address_, (struct bufferevent * a0), (a0), NULL)
WRAP_RET(int, bufferevent_socket_get_dns_error, (struct bufferevent * a0), (a0), -1)
WRAP_RET(struct bufferevent *, bufferevent_socket_new, (struct event_base * a0, int a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(int, bufferevent_socket_set_conn_address_, (struct bufferevent * a0, struct sockaddr * a1, size_t a2), (a0, a1, a2), -1)
WRAP_VOID(bufferevent_socket_set_conn_address_fd_, (struct bufferevent * a0, evutil_socket_t a1), (a0, a1))
WRAP_VOID(bufferevent_suspend_read_, (struct bufferevent * a0, bufferevent_suspend_flags a1), (a0, a1))
WRAP_VOID(bufferevent_trigger, (struct bufferevent * a0, short int a1, int a2), (a0, a1, a2))
WRAP_VOID(bufferevent_trigger_event, (struct bufferevent * a0, short int a1, int a2), (a0, a1, a2))
WRAP_VOID(bufferevent_unlock, (struct bufferevent * a0), (a0))
WRAP_VOID(bufferevent_unsuspend_read_, (struct bufferevent * a0, bufferevent_suspend_flags a1), (a0, a1))
WRAP_RET(int, bufferevent_write, (struct bufferevent * a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, bufferevent_write_buffer, (struct bufferevent * a0, struct evbuffer * a1), (a0, a1), -1)
WRAP_RET(int, evbuffer_add, (struct evbuffer * a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, evbuffer_add_buffer, (struct evbuffer * a0, struct evbuffer * a1), (a0, a1), -1)
WRAP_RET(int, evbuffer_add_buffer_reference, (struct evbuffer * a0, struct evbuffer * a1), (a0, a1), -1)
WRAP_RET(struct evbuffer_cb_entry *, evbuffer_add_cb, (struct evbuffer * a0, evbuffer_cb_func a1, void * a2), (a0, a1, a2), NULL)
WRAP_RET(int, evbuffer_add_file, (struct evbuffer * a0, int a1, int64_t a2, int64_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, evbuffer_add_file_segment, (struct evbuffer * a0, struct evbuffer_file_segment * a1, int64_t a2, int64_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(size_t, evbuffer_add_iovec, (struct evbuffer * a0, struct iovec * a1, int a2), (a0, a1, a2), ((size_t){0}))
WRAP_RET(int, evbuffer_add_reference, (struct evbuffer * a0, const void * a1, size_t a2, evbuffer_ref_cleanup_cb a3, void * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, evbuffer_add_vprintf, (struct evbuffer * a0, const char * a1, va_list a2), (a0, a1, a2), -1)
WRAP_RET(int, evbuffer_cb_clear_flags, (struct evbuffer * a0, struct evbuffer_cb_entry * a1, uint32_t a2), (a0, a1, a2), -1)
WRAP_RET(int, evbuffer_cb_set_flags, (struct evbuffer * a0, struct evbuffer_cb_entry * a1, uint32_t a2), (a0, a1, a2), -1)
WRAP_RET(int, evbuffer_clear_flags, (struct evbuffer * a0, uint64_t a1), (a0, a1), -1)
WRAP_RET(int, evbuffer_commit_space, (struct evbuffer * a0, struct iovec * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(ssize_t, evbuffer_copyout, (struct evbuffer * a0, void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(ssize_t, evbuffer_copyout_from, (struct evbuffer * a0, const struct evbuffer_ptr * a1, void * a2, size_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, evbuffer_defer_callbacks, (struct evbuffer * a0, struct event_base * a1), (a0, a1), -1)
WRAP_RET(int, evbuffer_drain, (struct evbuffer * a0, size_t a1), (a0, a1), -1)
WRAP_RET(int, evbuffer_enable_locking, (struct evbuffer * a0, void * a1), (a0, a1), -1)
WRAP_RET(int, evbuffer_expand, (struct evbuffer * a0, size_t a1), (a0, a1), -1)
WRAP_VOID(evbuffer_file_segment_add_cleanup_cb, (struct evbuffer_file_segment * a0, evbuffer_file_segment_cleanup_cb a1, void * a2), (a0, a1, a2))
WRAP_VOID(evbuffer_file_segment_free, (struct evbuffer_file_segment * a0), (a0))
WRAP_RET(struct evbuffer_file_segment *, evbuffer_file_segment_new, (int a0, int64_t a1, int64_t a2, unsigned int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(unsigned char *, evbuffer_find, (struct evbuffer * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), NULL)
WRAP_VOID(evbuffer_free, (struct evbuffer * a0), (a0))
WRAP_RET(int, evbuffer_freeze, (struct evbuffer * a0, int a1), (a0, a1), -1)
WRAP_RET(size_t, evbuffer_get_contiguous_space, (const struct evbuffer * a0), (a0), ((size_t){0}))
WRAP_RET(size_t, evbuffer_get_length, (const struct evbuffer * a0), (a0), ((size_t){0}))
WRAP_VOID(evbuffer_lock, (struct evbuffer * a0), (a0))
WRAP_RET(struct evbuffer *, evbuffer_new, (void), (), NULL)
WRAP_RET(int, evbuffer_peek, (struct evbuffer * a0, ssize_t a1, struct evbuffer_ptr * a2, struct iovec * a3, int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, evbuffer_prepend, (struct evbuffer * a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, evbuffer_prepend_buffer, (struct evbuffer * a0, struct evbuffer * a1), (a0, a1), -1)
WRAP_RET(int, evbuffer_ptr_set, (struct evbuffer * a0, struct evbuffer_ptr * a1, size_t a2, enum evbuffer_ptr_how a3), (a0, a1, a2, a3), -1)
WRAP_RET(unsigned char *, evbuffer_pullup, (struct evbuffer * a0, ssize_t a1), (a0, a1), NULL)
WRAP_RET(int, evbuffer_read, (struct evbuffer * a0, int a1, int a2), (a0, a1, a2), -1)
WRAP_RET(char *, evbuffer_readline, (struct evbuffer * a0), (a0), NULL)
WRAP_RET(char *, evbuffer_readln, (struct evbuffer * a0, size_t * a1, enum evbuffer_eol_style a2), (a0, a1, a2), NULL)
WRAP_RET(int, evbuffer_remove, (struct evbuffer * a0, void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, evbuffer_remove_buffer, (struct evbuffer * a0, struct evbuffer * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, evbuffer_remove_cb, (struct evbuffer * a0, evbuffer_cb_func a1, void * a2), (a0, a1, a2), -1)
WRAP_RET(int, evbuffer_remove_cb_entry, (struct evbuffer * a0, struct evbuffer_cb_entry * a1), (a0, a1), -1)
WRAP_RET(int, evbuffer_reserve_space, (struct evbuffer * a0, ssize_t a1, struct iovec * a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(struct evbuffer_ptr, evbuffer_search, (struct evbuffer * a0, const char * a1, size_t a2, const struct evbuffer_ptr * a3), (a0, a1, a2, a3), ((struct evbuffer_ptr){0}))
WRAP_RET(struct evbuffer_ptr, evbuffer_search_eol, (struct evbuffer * a0, struct evbuffer_ptr * a1, size_t * a2, enum evbuffer_eol_style a3), (a0, a1, a2, a3), ((struct evbuffer_ptr){0}))
WRAP_RET(struct evbuffer_ptr, evbuffer_search_range, (struct evbuffer * a0, const char * a1, size_t a2, const struct evbuffer_ptr * a3, const struct evbuffer_ptr * a4), (a0, a1, a2, a3, a4), ((struct evbuffer_ptr){0}))
WRAP_RET(int, evbuffer_set_flags, (struct evbuffer * a0, uint64_t a1), (a0, a1), -1)
WRAP_RET(int, evbuffer_setcb, (struct evbuffer * a0, evbuffer_cb a1, void * a2), (a0, a1, a2), -1)
WRAP_RET(int, evbuffer_unfreeze, (struct evbuffer * a0, int a1), (a0, a1), -1)
WRAP_VOID(evbuffer_unlock, (struct evbuffer * a0), (a0))
WRAP_RET(int, evbuffer_write, (struct evbuffer * a0, int a1), (a0, a1), -1)
WRAP_RET(int, evbuffer_write_atmost, (struct evbuffer * a0, int a1, ssize_t a2), (a0, a1, a2), -1)
WRAP_RET(struct evdns_server_port *, evdns_add_server_port, (int a0, int a1, evdns_request_callback_fn_type a2, void * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(struct evdns_server_port *, evdns_add_server_port_with_base, (struct event_base * a0, int a1, int a2, evdns_request_callback_fn_type a3, void * a4), (a0, a1, a2, a3, a4), NULL)
WRAP_VOID(evdns_base_clear_host_addresses, (struct evdns_base * a0), (a0))
WRAP_RET(int, evdns_base_clear_nameservers_and_suspend, (struct evdns_base * a0), (a0), -1)
WRAP_RET(int, evdns_base_count_nameservers, (struct evdns_base * a0), (a0), -1)
WRAP_VOID(evdns_base_free, (struct evdns_base * a0, int a1), (a0, a1))
WRAP_RET(int, evdns_base_get_nameserver_addr, (struct evdns_base * a0, int a1, struct sockaddr * a2, socklen_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, evdns_base_load_hosts, (struct evdns_base * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, evdns_base_nameserver_add, (struct evdns_base * a0, long unsigned int a1), (a0, a1), -1)
WRAP_RET(int, evdns_base_nameserver_ip_add, (struct evdns_base * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, evdns_base_nameserver_sockaddr_add, (struct evdns_base * a0, const struct sockaddr * a1, socklen_t a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(struct evdns_base *, evdns_base_new, (struct event_base * a0, int a1), (a0, a1), NULL)
WRAP_RET(int, evdns_base_resolv_conf_parse, (struct evdns_base * a0, int a1, const char *const a2), (a0, a1, a2), -1)
WRAP_RET(struct evdns_request *, evdns_base_resolve_ipv4, (struct evdns_base * a0, const char * a1, int a2, evdns_callback_type a3, void * a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(struct evdns_request *, evdns_base_resolve_ipv6, (struct evdns_base * a0, const char * a1, int a2, evdns_callback_type a3, void * a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(struct evdns_request *, evdns_base_resolve_reverse, (struct evdns_base * a0, const struct in_addr * a1, int a2, evdns_callback_type a3, void * a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(struct evdns_request *, evdns_base_resolve_reverse_ipv6, (struct evdns_base * a0, const struct in6_addr * a1, int a2, evdns_callback_type a3, void * a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(int, evdns_base_resume, (struct evdns_base * a0), (a0), -1)
WRAP_VOID(evdns_base_search_add, (struct evdns_base * a0, const char * a1), (a0, a1))
WRAP_VOID(evdns_base_search_clear, (struct evdns_base * a0), (a0))
WRAP_VOID(evdns_base_search_ndots_set, (struct evdns_base * a0, const int a1), (a0, a1))
WRAP_RET(int, evdns_base_set_option, (struct evdns_base * a0, const char * a1, const char * a2), (a0, a1, a2), -1)
WRAP_VOID(evdns_cancel_request, (struct evdns_base * a0, struct evdns_request * a1), (a0, a1))
WRAP_RET(int, evdns_clear_nameservers_and_suspend, (void), (), -1)
WRAP_VOID(evdns_close_server_port, (struct evdns_server_port * a0), (a0))
WRAP_RET(int, evdns_count_nameservers, (void), (), -1)
WRAP_RET(const char *, evdns_err_to_string, (int a0), (a0), NULL)
WRAP_RET(struct evdns_base *, evdns_get_global_base, (void), (), NULL)
WRAP_RET(struct evdns_getaddrinfo_request *, evdns_getaddrinfo, (struct evdns_base * a0, const char * a1, const char * a2, const struct addrinfo * a3, evdns_getaddrinfo_cb a4, void * a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_VOID(evdns_getaddrinfo_cancel, (struct evdns_getaddrinfo_request * a0), (a0))
WRAP_RET(int, evdns_init, (void), (), -1)
WRAP_RET(int, evdns_nameserver_add, (long unsigned int a0), (a0), -1)
WRAP_RET(int, evdns_nameserver_ip_add, (const char * a0), (a0), -1)
WRAP_RET(int, evdns_resolv_conf_parse, (int a0, const char *const a1), (a0, a1), -1)
WRAP_RET(int, evdns_resolve_ipv4, (const char * a0, int a1, evdns_callback_type a2, void * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, evdns_resolve_ipv6, (const char * a0, int a1, evdns_callback_type a2, void * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, evdns_resolve_reverse, (const struct in_addr * a0, int a1, evdns_callback_type a2, void * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, evdns_resolve_reverse_ipv6, (const struct in6_addr * a0, int a1, evdns_callback_type a2, void * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, evdns_resume, (void), (), -1)
WRAP_VOID(evdns_search_add, (const char * a0), (a0))
WRAP_VOID(evdns_search_clear, (void), ())
WRAP_VOID(evdns_search_ndots_set, (const int a0), (a0))
WRAP_RET(int, evdns_server_request_add_a_reply, (struct evdns_server_request * a0, const char * a1, int a2, const void * a3, int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, evdns_server_request_add_aaaa_reply, (struct evdns_server_request * a0, const char * a1, int a2, const void * a3, int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, evdns_server_request_add_cname_reply, (struct evdns_server_request * a0, const char * a1, const char * a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, evdns_server_request_add_ptr_reply, (struct evdns_server_request * a0, struct in_addr * a1, const char * a2, const char * a3, int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, evdns_server_request_add_reply, (struct evdns_server_request * a0, int a1, const char * a2, int a3, int a4, int a5, int a6, int a7, const char * a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
WRAP_RET(int, evdns_server_request_drop, (struct evdns_server_request * a0), (a0), -1)
WRAP_RET(int, evdns_server_request_get_requesting_addr, (struct evdns_server_request * a0, struct sockaddr * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, evdns_server_request_respond, (struct evdns_server_request * a0, int a1), (a0, a1), -1)
WRAP_VOID(evdns_server_request_set_flags, (struct evdns_server_request * a0, int a1), (a0, a1))
WRAP_VOID(evdns_set_log_fn, (evdns_debug_log_fn_type a0), (a0))
WRAP_RET(int, evdns_set_option, (const char * a0, const char * a1, int a2), (a0, a1, a2), -1)
WRAP_VOID(evdns_set_random_bytes_fn, (void (*a0) (char *, size_t)), (a0))
WRAP_VOID(evdns_set_transaction_id_fn, (uint16_t (*a0) (void)), (a0))
WRAP_VOID(evdns_shutdown, (int a0), (a0))
WRAP_VOID(event_active, (struct event * a0, int a1, short int a2), (a0, a1, a2))
WRAP_VOID(event_active_later_, (struct event * a0, int a1), (a0, a1))
WRAP_RET(int, event_add, (struct event * a0, const struct timeval * a1), (a0, a1), -1)
WRAP_RET(int, event_assign, (struct event * a0, struct event_base * a1, int a2, short int a3, event_callback_fn a4, void * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_VOID(event_base_active_by_fd, (struct event_base * a0, int a1, short int a2), (a0, a1, a2))
WRAP_VOID(event_base_active_by_signal, (struct event_base * a0, int a1), (a0, a1))
WRAP_VOID(event_base_add_virtual_, (struct event_base * a0), (a0))
WRAP_VOID(event_base_assert_ok_, (struct event_base * a0), (a0))
WRAP_RET(int, event_base_dispatch, (struct event_base * a0), (a0), -1)
WRAP_VOID(event_base_dump_events, (struct event_base * a0, FILE * a1), (a0, a1))
WRAP_RET(int, event_base_foreach_event, (struct event_base * a0, event_base_foreach_event_cb a1, void * a2), (a0, a1, a2), -1)
WRAP_VOID(event_base_free, (struct event_base * a0), (a0))
WRAP_VOID(event_base_free_nofinalize, (struct event_base * a0), (a0))
WRAP_RET(int, event_base_get_features, (const struct event_base * a0), (a0), -1)
WRAP_RET(int, event_base_get_max_events, (struct event_base * a0, unsigned int a1, int a2), (a0, a1, a2), -1)
WRAP_RET(const char *, event_base_get_method, (const struct event_base * a0), (a0), NULL)
WRAP_RET(int, event_base_get_npriorities, (struct event_base * a0), (a0), -1)
WRAP_RET(int, event_base_get_num_events, (struct event_base * a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(struct event *, event_base_get_running_event, (struct event_base * a0), (a0), NULL)
WRAP_RET(int, event_base_gettimeofday_cached, (struct event_base * a0, struct timeval * a1), (a0, a1), -1)
WRAP_RET(int, event_base_got_break, (struct event_base * a0), (a0), -1)
WRAP_RET(int, event_base_got_exit, (struct event_base * a0), (a0), -1)
WRAP_RET(const struct timeval *, event_base_init_common_timeout, (struct event_base * a0, const struct timeval * a1), (a0, a1), NULL)
WRAP_RET(int, event_base_loop, (struct event_base * a0, int a1), (a0, a1), -1)
WRAP_RET(int, event_base_loopbreak, (struct event_base * a0), (a0), -1)
WRAP_RET(int, event_base_loopcontinue, (struct event_base * a0), (a0), -1)
WRAP_RET(int, event_base_loopexit, (struct event_base * a0, const struct timeval * a1), (a0, a1), -1)
WRAP_RET(struct event_base *, event_base_new, (void), (), NULL)
WRAP_RET(struct event_base *, event_base_new_with_config, (const struct event_config * a0), (a0), NULL)
WRAP_RET(int, event_base_once, (struct event_base * a0, int a1, short int a2, event_callback_fn a3, void * a4, const struct timeval * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, event_base_priority_init, (struct event_base * a0, int a1), (a0, a1), -1)
WRAP_RET(int, event_base_set, (struct event_base * a0, struct event * a1), (a0, a1), -1)
WRAP_RET(int, event_base_start_iocp_, (struct event_base * a0, int a1), (a0, a1), -1)
WRAP_RET(int, event_base_update_cache_time, (struct event_base * a0), (a0), -1)
WRAP_RET(int, event_callback_activate_, (struct event_base * a0, struct event_callback * a1), (a0, a1), -1)
WRAP_VOID(event_callback_finalize_, (struct event_base * a0, unsigned a1, struct event_callback * a2, void (*a3)(struct event_callback *, void *)), (a0, a1, a2, a3))
WRAP_RET(int, event_config_avoid_method, (struct event_config * a0, const char * a1), (a0, a1), -1)
WRAP_VOID(event_config_free, (struct event_config * a0), (a0))
WRAP_RET(struct event_config *, event_config_new, (void), (), NULL)
WRAP_RET(int, event_config_require_features, (struct event_config * a0, int a1), (a0, a1), -1)
WRAP_RET(int, event_config_set_flag, (struct event_config * a0, int a1), (a0, a1), -1)
WRAP_RET(int, event_config_set_max_dispatch_interval, (struct event_config * a0, const struct timeval * a1, int a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, event_config_set_num_cpus_hint, (struct event_config * a0, int a1), (a0, a1), -1)
WRAP_VOID(event_debug_unassign, (struct event * a0), (a0))
WRAP_VOID(event_deferred_cb_cancel_, (struct event_base * a0, struct event_callback * a1), (a0, a1))
WRAP_VOID(event_deferred_cb_init_, (struct event_callback * a0, ev_uint8_t a1, deferred_cb_fn a2, void * a3), (a0, a1, a2, a3))
WRAP_RET(int, event_deferred_cb_schedule_, (struct event_base * a0, struct event_callback * a1), (a0, a1), -1)
WRAP_RET(int, event_del, (struct event * a0), (a0), -1)
WRAP_RET(int, event_del_block, (struct event * a0), (a0), -1)
WRAP_RET(int, event_del_noblock, (struct event * a0), (a0), -1)
WRAP_RET(int, event_dispatch, (void), (), -1)
WRAP_VOID(event_enable_debug_logging, (uint32_t a0), (a0))
WRAP_VOID(event_enable_debug_mode, (void), ())
WRAP_RET(int, event_finalize, (unsigned int a0, struct event * a1, event_finalize_callback_fn a2), (a0, a1, a2), -1)
WRAP_VOID(event_free, (struct event * a0), (a0))
WRAP_RET(int, event_free_finalize, (unsigned int a0, struct event * a1, event_finalize_callback_fn a2), (a0, a1, a2), -1)
WRAP_VOID(event_get_assignment, (const struct event * a0, struct event_base ** a1, int * a2, short int * a3, event_callback_fn * a4, void ** a5), (a0, a1, a2, a3, a4, a5))
WRAP_RET(struct event_base *, event_get_base, (const struct event * a0), (a0), NULL)
WRAP_RET(event_callback_fn, event_get_callback, (const struct event * a0), (a0), NULL)
WRAP_RET(void *, event_get_callback_arg, (const struct event * a0), (a0), NULL)
WRAP_RET(short int, event_get_events, (const struct event * a0), (a0), ((short int){0}))
WRAP_RET(int, event_get_fd, (const struct event * a0), (a0), -1)
WRAP_RET(const char *, event_get_method, (void), (), NULL)
WRAP_RET(int, event_get_priority, (const struct event * a0), (a0), -1)
WRAP_RET(size_t, event_get_struct_event_size, (void), (), ((size_t){0}))
WRAP_RET(const char **, event_get_supported_methods, (void), (), NULL)
WRAP_RET(const char *, event_get_version, (void), (), NULL)
WRAP_RET(uint32_t, event_get_version_number, (void), (), ((uint32_t){0}))
WRAP_RET(int, event_gettime_monotonic, (struct event_base * a0, struct timeval * a1), (a0, a1), -1)
WRAP_RET(struct event_base *, event_init, (void), (), NULL)
WRAP_RET(int, event_initialized, (const struct event * a0), (a0), -1)
WRAP_VOID(event_logv_, (int a0, const char * a1, const char * a2, va_list a3), (a0, a1, a2, a3))
WRAP_RET(int, event_loop, (int a0), (a0), -1)
WRAP_RET(int, event_loopbreak, (void), (), -1)
WRAP_RET(int, event_loopexit, (const struct timeval * a0), (a0), -1)
WRAP_RET(void *, event_mm_calloc_, (size_t a0, size_t a1), (a0, a1), NULL)
WRAP_VOID(event_mm_free_, (void * a0), (a0))
WRAP_RET(void *, event_mm_malloc_, (size_t a0), (a0), NULL)
WRAP_RET(void *, event_mm_realloc_, (void * a0, size_t a1), (a0, a1), NULL)
WRAP_RET(char *, event_mm_strdup_, (const char * a0), (a0), NULL)
WRAP_RET(struct event *, event_new, (struct event_base * a0, int a1, short int a2, event_callback_fn a3, void * a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(int, event_once, (int a0, short int a1, void (*a2) (int, short int, void *), void * a3, const struct timeval * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, event_pending, (const struct event * a0, short int a1, struct timeval * a2), (a0, a1, a2), -1)
WRAP_RET(int, event_priority_init, (int a0), (a0), -1)
WRAP_RET(int, event_priority_set, (struct event * a0, int a1), (a0, a1), -1)
WRAP_RET(int, event_reinit, (struct event_base * a0), (a0), -1)
WRAP_RET(int, event_remove_timer, (struct event * a0), (a0), -1)
WRAP_RET(void *, event_self_cbarg, (void), (), NULL)
WRAP_VOID(event_set, (struct event * a0, int a1, short int a2, void (*a3) (int, short int, void *), void * a4), (a0, a1, a2, a3, a4))
WRAP_VOID(event_set_fatal_callback, (event_fatal_cb a0), (a0))
WRAP_VOID(event_set_log_callback, (event_log_cb a0), (a0))
WRAP_VOID(event_set_mem_functions, (void *(*a0) (size_t), void *(*a1) (void *, size_t), void (*a2) (void *)), (a0, a1, a2))
WRAP_RET(int, evhttp_accept_socket, (struct evhttp * a0, int a1), (a0, a1), -1)
WRAP_RET(struct evhttp_bound_socket *, evhttp_accept_socket_with_handle, (struct evhttp * a0, int a1), (a0, a1), NULL)
WRAP_RET(int, evhttp_add_header, (struct evkeyvalq * a0, const char * a1, const char * a2), (a0, a1, a2), -1)
WRAP_RET(int, evhttp_add_server_alias, (struct evhttp * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, evhttp_add_virtual_host, (struct evhttp * a0, const char * a1, struct evhttp * a2), (a0, a1, a2), -1)
WRAP_RET(struct evhttp_bound_socket *, evhttp_bind_listener, (struct evhttp * a0, struct evconnlistener * a1), (a0, a1), NULL)
WRAP_RET(int, evhttp_bind_socket, (struct evhttp * a0, const char * a1, uint16_t a2), (a0, a1, a2), -1)
WRAP_RET(struct evhttp_bound_socket *, evhttp_bind_socket_with_handle, (struct evhttp * a0, const char * a1, uint16_t a2), (a0, a1, a2), NULL)
WRAP_RET(int, evhttp_bound_socket_get_fd, (struct evhttp_bound_socket * a0), (a0), -1)
WRAP_RET(struct evconnlistener *, evhttp_bound_socket_get_listener, (struct evhttp_bound_socket * a0), (a0), NULL)
WRAP_VOID(evhttp_cancel_request, (struct evhttp_request * a0), (a0))
WRAP_RET(enum evhttp_transfer_encoding_header_status, evhttp_check_transfer_encoding_, (const char * a0), (a0), ((enum evhttp_transfer_encoding_header_status){0}))
WRAP_VOID(evhttp_clear_headers, (struct evkeyvalq * a0), (a0))
WRAP_RET(struct evhttp_connection *, evhttp_connection_base_bufferevent_new, (struct event_base * a0, struct evdns_base * a1, struct bufferevent * a2, const char * a3, uint16_t a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(struct evhttp_connection *, evhttp_connection_base_new, (struct event_base * a0, struct evdns_base * a1, const char * a2, uint16_t a3), (a0, a1, a2, a3), NULL)
WRAP_VOID(evhttp_connection_fail_, (struct evhttp_connection * a0, enum evhttp_request_error a1), (a0, a1))
WRAP_VOID(evhttp_connection_free, (struct evhttp_connection * a0), (a0))
WRAP_VOID(evhttp_connection_free_on_completion, (struct evhttp_connection * a0), (a0))
WRAP_RET(const struct sockaddr *, evhttp_connection_get_addr, (struct evhttp_connection * a0), (a0), NULL)
WRAP_RET(struct event_base *, evhttp_connection_get_base, (struct evhttp_connection * a0), (a0), NULL)
WRAP_RET(struct bufferevent *, evhttp_connection_get_bufferevent, (struct evhttp_connection * a0), (a0), NULL)
WRAP_VOID(evhttp_connection_get_peer, (struct evhttp_connection * a0, char ** a1, uint16_t * a2), (a0, a1, a2))
WRAP_RET(struct evhttp *, evhttp_connection_get_server, (struct evhttp_connection * a0), (a0), NULL)
WRAP_RET(struct evhttp_connection *, evhttp_connection_new, (const char * a0, uint16_t a1), (a0, a1), NULL)
WRAP_VOID(evhttp_connection_set_base, (struct evhttp_connection * a0, struct event_base * a1), (a0, a1))
WRAP_VOID(evhttp_connection_set_closecb, (struct evhttp_connection * a0, void (*a1) (struct evhttp_connection *, void *), void * a2), (a0, a1, a2))
WRAP_VOID(evhttp_connection_set_family, (struct evhttp_connection * a0, int a1), (a0, a1))
WRAP_RET(int, evhttp_connection_set_flags, (struct evhttp_connection * a0, int a1), (a0, a1), -1)
WRAP_VOID(evhttp_connection_set_initial_retry_tv, (struct evhttp_connection * a0, const struct timeval * a1), (a0, a1))
WRAP_VOID(evhttp_connection_set_local_address, (struct evhttp_connection * a0, const char * a1), (a0, a1))
WRAP_VOID(evhttp_connection_set_local_port, (struct evhttp_connection * a0, uint16_t a1), (a0, a1))
WRAP_VOID(evhttp_connection_set_max_body_size, (struct evhttp_connection * a0, ssize_t a1), (a0, a1))
WRAP_VOID(evhttp_connection_set_max_headers_size, (struct evhttp_connection * a0, ssize_t a1), (a0, a1))
WRAP_VOID(evhttp_connection_set_retries, (struct evhttp_connection * a0, int a1), (a0, a1))
WRAP_VOID(evhttp_connection_set_timeout, (struct evhttp_connection * a0, int a1), (a0, a1))
WRAP_VOID(evhttp_connection_set_timeout_tv, (struct evhttp_connection * a0, const struct timeval * a1), (a0, a1))
WRAP_RET(char *, evhttp_decode_uri, (const char * a0), (a0), NULL)
WRAP_RET(int, evhttp_decode_uri_internal, (const char * a0, size_t a1, char * a2, int a3), (a0, a1, a2, a3), -1)
WRAP_VOID(evhttp_del_accept_socket, (struct evhttp * a0, struct evhttp_bound_socket * a1), (a0, a1))
WRAP_RET(int, evhttp_del_cb, (struct evhttp * a0, const char * a1), (a0, a1), -1)
WRAP_RET(char *, evhttp_encode_uri, (const char * a0), (a0), NULL)
WRAP_RET(const char *, evhttp_find_header, (const struct evkeyvalq * a0, const char * a1), (a0, a1), NULL)
WRAP_VOID(evhttp_foreach_bound_socket, (struct evhttp * a0, evhttp_bound_socket_foreach_fn (*a1), void * a2), (a0, a1, a2))
WRAP_VOID(evhttp_free, (struct evhttp * a0), (a0))
WRAP_RET(char *, evhttp_htmlescape, (const char * a0), (a0), NULL)
WRAP_RET(int, evhttp_make_request, (struct evhttp_connection * a0, struct evhttp_request * a1, enum evhttp_cmd_type a2, const char * a3), (a0, a1, a2, a3), -1)
WRAP_RET(struct evhttp *, evhttp_new, (struct event_base * a0), (a0), NULL)
WRAP_RET(enum message_read_status, evhttp_parse_firstline_, (struct evhttp_request * a0, struct evbuffer * a1), (a0, a1), ((enum message_read_status){0}))
WRAP_RET(enum message_read_status, evhttp_parse_headers_, (struct evhttp_request * a0, struct evbuffer * a1), (a0, a1), ((enum message_read_status){0}))
WRAP_RET(int, evhttp_parse_query, (const char * a0, struct evkeyvalq * a1), (a0, a1), -1)
WRAP_RET(int, evhttp_parse_query_str, (const char * a0, struct evkeyvalq * a1), (a0, a1), -1)
WRAP_RET(int, evhttp_remove_header, (struct evkeyvalq * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, evhttp_remove_server_alias, (struct evhttp * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, evhttp_remove_virtual_host, (struct evhttp * a0, struct evhttp * a1), (a0, a1), -1)
WRAP_VOID(evhttp_request_free, (struct evhttp_request * a0), (a0))
WRAP_RET(enum evhttp_cmd_type, evhttp_request_get_command, (const struct evhttp_request * a0), (a0), ((enum evhttp_cmd_type){0}))
WRAP_RET(struct evhttp_connection *, evhttp_request_get_connection, (struct evhttp_request * a0), (a0), NULL)
WRAP_RET(const struct evhttp_uri *, evhttp_request_get_evhttp_uri, (const struct evhttp_request * a0), (a0), NULL)
WRAP_RET(const char *, evhttp_request_get_host, (struct evhttp_request * a0), (a0), NULL)
WRAP_RET(struct evbuffer *, evhttp_request_get_input_buffer, (struct evhttp_request * a0), (a0), NULL)
WRAP_RET(struct evkeyvalq *, evhttp_request_get_input_headers, (struct evhttp_request * a0), (a0), NULL)
WRAP_RET(struct evbuffer *, evhttp_request_get_output_buffer, (struct evhttp_request * a0), (a0), NULL)
WRAP_RET(struct evkeyvalq *, evhttp_request_get_output_headers, (struct evhttp_request * a0), (a0), NULL)
WRAP_RET(int, evhttp_request_get_response_code, (const struct evhttp_request * a0), (a0), -1)
WRAP_RET(const char *, evhttp_request_get_response_code_line, (const struct evhttp_request * a0), (a0), NULL)
WRAP_RET(const char *, evhttp_request_get_uri, (const struct evhttp_request * a0), (a0), NULL)
WRAP_RET(int, evhttp_request_is_owned, (struct evhttp_request * a0), (a0), -1)
WRAP_RET(struct evhttp_request *, evhttp_request_new, (void (*a0) (struct evhttp_request *, void *), void * a1), (a0, a1), NULL)
WRAP_VOID(evhttp_request_own, (struct evhttp_request * a0), (a0))
WRAP_VOID(evhttp_request_set_chunked_cb, (struct evhttp_request * a0, void (*a1) (struct evhttp_request *, void *)), (a0, a1))
WRAP_VOID(evhttp_request_set_error_cb, (struct evhttp_request * a0, void (*a1) (enum evhttp_request_error, void *)), (a0, a1))
WRAP_VOID(evhttp_request_set_header_cb, (struct evhttp_request * a0, int (*a1) (struct evhttp_request *, void *)), (a0, a1))
WRAP_VOID(evhttp_request_set_on_complete_cb, (struct evhttp_request * a0, void (*a1) (struct evhttp_request *, void *), void * a2), (a0, a1, a2))
WRAP_VOID(evhttp_send_error, (struct evhttp_request * a0, int a1, const char * a2), (a0, a1, a2))
WRAP_VOID(evhttp_send_reply, (struct evhttp_request * a0, int a1, const char * a2, struct evbuffer * a3), (a0, a1, a2, a3))
WRAP_VOID(evhttp_send_reply_chunk, (struct evhttp_request * a0, struct evbuffer * a1), (a0, a1))
WRAP_VOID(evhttp_send_reply_chunk_with_cb, (struct evhttp_request * a0, struct evbuffer * a1, void (*a2) (struct evhttp_connection *, void *), void * a3), (a0, a1, a2, a3))
WRAP_VOID(evhttp_send_reply_end, (struct evhttp_request * a0), (a0))
WRAP_VOID(evhttp_send_reply_start, (struct evhttp_request * a0, int a1, const char * a2), (a0, a1, a2))
WRAP_VOID(evhttp_set_allowed_methods, (struct evhttp * a0, uint16_t a1), (a0, a1))
WRAP_VOID(evhttp_set_bevcb, (struct evhttp * a0, struct bufferevent *(*a1) (struct event_base *, void *), void * a2), (a0, a1, a2))
WRAP_RET(int, evhttp_set_cb, (struct evhttp * a0, const char * a1, void (*a2) (struct evhttp_request *, void *), void * a3), (a0, a1, a2, a3), -1)
WRAP_VOID(evhttp_set_default_content_type, (struct evhttp * a0, const char * a1), (a0, a1))
WRAP_RET(int, evhttp_set_flags, (struct evhttp * a0, int a1), (a0, a1), -1)
WRAP_VOID(evhttp_set_gencb, (struct evhttp * a0, void (*a1) (struct evhttp_request *, void *), void * a2), (a0, a1, a2))
WRAP_VOID(evhttp_set_max_body_size, (struct evhttp * a0, ssize_t a1), (a0, a1))
WRAP_VOID(evhttp_set_max_headers_size, (struct evhttp * a0, ssize_t a1), (a0, a1))
WRAP_VOID(evhttp_set_timeout, (struct evhttp * a0, int a1), (a0, a1))
WRAP_VOID(evhttp_set_timeout_tv, (struct evhttp * a0, const struct timeval * a1), (a0, a1))
WRAP_RET(struct evhttp *, evhttp_start, (const char * a0, uint16_t a1), (a0, a1), NULL)
WRAP_RET(int, evhttp_str_is_chunked_, (const char * a0, const char * a1), (a0, a1), -1)
WRAP_VOID(evhttp_uri_free, (struct evhttp_uri * a0), (a0))
WRAP_RET(const char *, evhttp_uri_get_fragment, (const struct evhttp_uri * a0), (a0), NULL)
WRAP_RET(const char *, evhttp_uri_get_host, (const struct evhttp_uri * a0), (a0), NULL)
WRAP_RET(const char *, evhttp_uri_get_path, (const struct evhttp_uri * a0), (a0), NULL)
WRAP_RET(int, evhttp_uri_get_port, (const struct evhttp_uri * a0), (a0), -1)
WRAP_RET(const char *, evhttp_uri_get_query, (const struct evhttp_uri * a0), (a0), NULL)
WRAP_RET(const char *, evhttp_uri_get_scheme, (const struct evhttp_uri * a0), (a0), NULL)
WRAP_RET(const char *, evhttp_uri_get_userinfo, (const struct evhttp_uri * a0), (a0), NULL)
WRAP_RET(char *, evhttp_uri_join, (struct evhttp_uri * a0, char * a1, size_t a2), (a0, a1, a2), NULL)
WRAP_RET(struct evhttp_uri *, evhttp_uri_new, (void), (), NULL)
WRAP_RET(struct evhttp_uri *, evhttp_uri_parse, (const char * a0), (a0), NULL)
WRAP_RET(struct evhttp_uri *, evhttp_uri_parse_with_flags, (const char * a0, unsigned int a1), (a0, a1), NULL)
WRAP_VOID(evhttp_uri_set_flags, (struct evhttp_uri * a0, unsigned int a1), (a0, a1))
WRAP_RET(int, evhttp_uri_set_fragment, (struct evhttp_uri * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, evhttp_uri_set_host, (struct evhttp_uri * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, evhttp_uri_set_path, (struct evhttp_uri * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, evhttp_uri_set_port, (struct evhttp_uri * a0, int a1), (a0, a1), -1)
WRAP_RET(int, evhttp_uri_set_query, (struct evhttp_uri * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, evhttp_uri_set_scheme, (struct evhttp_uri * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, evhttp_uri_set_userinfo, (struct evhttp_uri * a0, const char * a1), (a0, a1), -1)
WRAP_RET(char *, evhttp_uridecode, (const char * a0, int a1, size_t * a2), (a0, a1, a2), NULL)
WRAP_RET(char *, evhttp_uriencode, (const char * a0, ssize_t a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(void *, evrpc_add_hook, (void * a0, enum EVRPC_HOOK_TYPE a1, int (*a2) (void *, struct evhttp_request *, struct evbuffer *, void *), void * a3), (a0, a1, a2, a3), NULL)
WRAP_VOID(evrpc_free, (struct evrpc_base * a0), (a0))
WRAP_RET(void *, evrpc_get_reply, (struct evrpc_req_generic * a0), (a0), NULL)
WRAP_RET(void *, evrpc_get_request, (struct evrpc_req_generic * a0), (a0), NULL)
WRAP_VOID(evrpc_hook_add_meta, (void * a0, const char * a1, const void * a2, size_t a3), (a0, a1, a2, a3))
WRAP_RET(int, evrpc_hook_find_meta, (void * a0, const char * a1, void ** a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(struct evhttp_connection *, evrpc_hook_get_connection, (void * a0), (a0), NULL)
WRAP_RET(struct evrpc_base *, evrpc_init, (struct evhttp * a0), (a0), NULL)
WRAP_RET(int, evrpc_make_request, (struct evrpc_request_wrapper * a0), (a0), -1)
WRAP_RET(struct evrpc_request_wrapper *, evrpc_make_request_ctx, (struct evrpc_pool * a0, void * a1, void * a2, const char * a3, void (*a4) (struct evbuffer *, void *), void (*a5) (void *), int (*a6) (void *, struct evbuffer *), void (*a7) (struct evrpc_status *, void *, void *, void *), void * a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), NULL)
WRAP_VOID(evrpc_pool_add_connection, (struct evrpc_pool * a0, struct evhttp_connection * a1), (a0, a1))
WRAP_VOID(evrpc_pool_free, (struct evrpc_pool * a0), (a0))
WRAP_RET(struct evrpc_pool *, evrpc_pool_new, (struct event_base * a0), (a0), NULL)
WRAP_VOID(evrpc_pool_remove_connection, (struct evrpc_pool * a0, struct evhttp_connection * a1), (a0, a1))
WRAP_VOID(evrpc_pool_set_timeout, (struct evrpc_pool * a0, int a1), (a0, a1))
WRAP_RET(int, evrpc_register_generic, (struct evrpc_base * a0, const char * a1, void (*a2) (struct evrpc_req_generic *, void *), void * a3, void *(*a4) (void *), void * a5, void (*a6) (void *), int (*a7) (void *, struct evbuffer *), void *(*a8) (void *), void * a9, void (*a10) (void *), int (*a11) (void *), void (*a12) (struct evbuffer *, void *)), (a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12), -1)
WRAP_RET(int, evrpc_register_rpc, (struct evrpc_base * a0, struct evrpc * a1, void (*a2) (struct evrpc_req_generic *, void *), void * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, evrpc_remove_hook, (void * a0, enum EVRPC_HOOK_TYPE a1, void * a2), (a0, a1, a2), -1)
WRAP_VOID(evrpc_request_done, (struct evrpc_req_generic * a0), (a0))
WRAP_RET(struct evrpc_pool *, evrpc_request_get_pool, (struct evrpc_request_wrapper * a0), (a0), NULL)
WRAP_VOID(evrpc_request_set_cb, (struct evrpc_request_wrapper * a0, void (*a1) (struct evrpc_status *, void *, void *, void *), void * a2), (a0, a1, a2))
WRAP_VOID(evrpc_request_set_pool, (struct evrpc_request_wrapper * a0, struct evrpc_pool * a1), (a0, a1))
WRAP_RET(int, evrpc_resume_request, (void * a0, void * a1, enum EVRPC_HOOK_RESULT a2), (a0, a1, a2), -1)
WRAP_RET(int, evrpc_send_request_generic, (struct evrpc_pool * a0, void * a1, void * a2, void (*a3) (struct evrpc_status *, void *, void *, void *), void * a4, const char * a5, void (*a6) (struct evbuffer *, void *), void (*a7) (void *), int (*a8) (void *, struct evbuffer *)), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
WRAP_RET(int, evrpc_unregister_rpc, (struct evrpc_base * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, evtag_consume, (struct evbuffer * a0), (a0), -1)
WRAP_RET(int, evtag_decode_int, (ev_uint32_t * a0, struct evbuffer * a1), (a0, a1), -1)
WRAP_RET(int, evtag_decode_int64, (ev_uint64_t * a0, struct evbuffer * a1), (a0, a1), -1)
WRAP_RET(int, evtag_decode_tag, (ev_uint32_t * a0, struct evbuffer * a1), (a0, a1), -1)
WRAP_VOID(evtag_encode_int, (struct evbuffer * a0, uint32_t a1), (a0, a1))
WRAP_VOID(evtag_encode_int64, (struct evbuffer * a0, uint64_t a1), (a0, a1))
WRAP_RET(int, evtag_encode_tag, (struct evbuffer * a0, ev_uint32_t a1), (a0, a1), -1)
WRAP_VOID(evtag_init, (void), ())
WRAP_VOID(evtag_marshal, (struct evbuffer * a0, uint32_t a1, const void * a2, uint32_t a3), (a0, a1, a2, a3))
WRAP_VOID(evtag_marshal_buffer, (struct evbuffer * a0, uint32_t a1, struct evbuffer * a2), (a0, a1, a2))
WRAP_VOID(evtag_marshal_int, (struct evbuffer * a0, uint32_t a1, uint32_t a2), (a0, a1, a2))
WRAP_VOID(evtag_marshal_int64, (struct evbuffer * a0, uint32_t a1, uint64_t a2), (a0, a1, a2))
WRAP_VOID(evtag_marshal_string, (struct evbuffer * a0, uint32_t a1, const char * a2), (a0, a1, a2))
WRAP_VOID(evtag_marshal_timeval, (struct evbuffer * a0, uint32_t a1, struct timeval * a2), (a0, a1, a2))
WRAP_RET(int, evtag_payload_length, (struct evbuffer * a0, uint32_t * a1), (a0, a1), -1)
WRAP_RET(int, evtag_peek, (struct evbuffer * a0, uint32_t * a1), (a0, a1), -1)
WRAP_RET(int, evtag_peek_length, (struct evbuffer * a0, uint32_t * a1), (a0, a1), -1)
WRAP_RET(int, evtag_unmarshal, (struct evbuffer * a0, uint32_t * a1, struct evbuffer * a2), (a0, a1, a2), -1)
WRAP_RET(int, evtag_unmarshal_fixed, (struct evbuffer * a0, uint32_t a1, void * a2, size_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, evtag_unmarshal_header, (struct evbuffer * a0, uint32_t * a1), (a0, a1), -1)
WRAP_RET(int, evtag_unmarshal_int, (struct evbuffer * a0, uint32_t a1, uint32_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, evtag_unmarshal_int64, (struct evbuffer * a0, uint32_t a1, uint64_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, evtag_unmarshal_string, (struct evbuffer * a0, uint32_t a1, char ** a2), (a0, a1, a2), -1)
WRAP_RET(int, evtag_unmarshal_timeval, (struct evbuffer * a0, uint32_t a1, struct timeval * a2), (a0, a1, a2), -1)
WRAP_VOID(evthread_enable_lock_debugging, (void), ())
WRAP_VOID(evthread_enable_lock_debuging, (void), ())
WRAP_RET(struct evthread_lock_callbacks *, evthread_get_lock_callbacks, (void), (), NULL)
WRAP_RET(int, evthread_is_debug_lock_held_, (void * a0), (a0), -1)
WRAP_RET(int, evthread_make_base_notifiable, (struct event_base * a0), (a0), -1)
WRAP_RET(int, evthread_set_condition_callbacks, (const struct evthread_condition_callbacks * a0), (a0), -1)
WRAP_VOID(evthread_set_id_callback, (long unsigned int (*a0) (void)), (a0))
WRAP_RET(int, evthread_set_lock_callbacks, (const struct evthread_lock_callbacks * a0), (a0), -1)
WRAP_RET(struct evutil_addrinfo *, evutil_addrinfo_append_, (struct evutil_addrinfo * a0, struct evutil_addrinfo * a1), (a0, a1), NULL)
WRAP_VOID(evutil_adjust_hints_for_addrconfig_, (struct evutil_addrinfo * a0), (a0))
WRAP_RET(int, evutil_ascii_strcasecmp, (const char * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, evutil_ascii_strncasecmp, (const char * a0, const char * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, evutil_closesocket, (int a0), (a0), -1)
WRAP_RET(int, evutil_configure_monotonic_time, (struct evutil_monotonic_timer * a0, int a1), (a0, a1), -1)
WRAP_RET(int, evutil_configure_monotonic_time_, (struct evutil_monotonic_timer * a0, int a1), (a0, a1), -1)
WRAP_RET(int, evutil_date_rfc1123, (char * a0, const size_t a1, const struct tm * a2), (a0, a1, a2), -1)
WRAP_RET(int, evutil_ersatz_socketpair_, (int a0, int a1, int a2, evutil_socket_t a3 [2]), (a0, a1, a2, a3), -1)
WRAP_RET(const char *, evutil_format_sockaddr_port_, (const struct sockaddr * a0, char * a1, size_t a2), (a0, a1, a2), NULL)
WRAP_VOID(evutil_freeaddrinfo, (struct addrinfo * a0), (a0))
WRAP_RET(const char *, evutil_gai_strerror, (int a0), (a0), NULL)
WRAP_RET(int, evutil_getaddrinfo, (const char * a0, const char * a1, const struct addrinfo * a2, struct addrinfo ** a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, evutil_getaddrinfo_common_, (const char * a0, const char * a1, struct evutil_addrinfo * a2, struct evutil_addrinfo ** a3, int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, evutil_gettime_monotonic, (struct evutil_monotonic_timer * a0, struct timeval * a1), (a0, a1), -1)
WRAP_RET(int, evutil_gettime_monotonic_, (struct evutil_monotonic_timer * a0, struct timeval * a1), (a0, a1), -1)
WRAP_RET(const char *, evutil_inet_ntop, (int a0, const void * a1, char * a2, size_t a3), (a0, a1, a2, a3), NULL)
WRAP_RET(int, evutil_inet_pton, (int a0, const char * a1, void * a2), (a0, a1, a2), -1)
WRAP_RET(int, evutil_inet_pton_scope, (int a0, const char * a1, void * a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, evutil_make_internal_pipe_, (evutil_socket_t a0 [2]), (a0), -1)
WRAP_RET(int, evutil_make_listen_socket_ipv6only, (int a0), (a0), -1)
WRAP_RET(int, evutil_make_listen_socket_reuseable, (int a0), (a0), -1)
WRAP_RET(int, evutil_make_listen_socket_reuseable_port, (int a0), (a0), -1)
WRAP_RET(int, evutil_make_socket_closeonexec, (int a0), (a0), -1)
WRAP_RET(int, evutil_make_socket_nonblocking, (int a0), (a0), -1)
WRAP_RET(int, evutil_make_tcp_listen_socket_deferred, (int a0), (a0), -1)
WRAP_VOID(evutil_monotonic_timer_free, (struct evutil_monotonic_timer * a0), (a0))
WRAP_RET(struct evutil_monotonic_timer *, evutil_monotonic_timer_new, (void), (), NULL)
WRAP_RET(struct evutil_addrinfo *, evutil_new_addrinfo_, (struct sockaddr * a0, ev_socklen_t a1, const struct evutil_addrinfo * a2), (a0, a1, a2), NULL)
WRAP_RET(int, evutil_parse_sockaddr_port, (const char * a0, struct sockaddr * a1, int * a2), (a0, a1, a2), -1)
WRAP_RET(int, evutil_read_file_, (const char * a0, char ** a1, size_t * a2, int a3), (a0, a1, a2, a3), -1)
WRAP_VOID(evutil_rtrim_lws_, (char * a0), (a0))
WRAP_VOID(evutil_secure_rng_get_bytes, (void * a0, size_t a1), (a0, a1))
WRAP_RET(int, evutil_secure_rng_init, (void), (), -1)
WRAP_RET(int, evutil_secure_rng_set_urandom_device_file, (char * a0), (a0), -1)
WRAP_VOID(evutil_set_evdns_getaddrinfo_cancel_fn_, (evdns_getaddrinfo_cancel_fn a0), (a0))
WRAP_VOID(evutil_set_evdns_getaddrinfo_fn_, (evdns_getaddrinfo_fn a0), (a0))
WRAP_RET(int, evutil_sockaddr_cmp, (const struct sockaddr * a0, const struct sockaddr * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, evutil_sockaddr_is_loopback_, (const struct sockaddr * a0), (a0), -1)
WRAP_RET(evutil_socket_t, evutil_socket_, (int a0, int a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, evutil_socket_connect_, (evutil_socket_t * a0, const struct sockaddr * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, evutil_socketpair, (int a0, int a1, int a2, int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int64_t, evutil_strtoll, (const char * a0, char ** a1, int a2), (a0, a1, a2), -1)
WRAP_VOID(evutil_usleep_, (const struct timeval * a0), (a0))
WRAP_RET(int, evutil_v4addr_is_local_, (const struct in_addr * a0), (a0), -1)
WRAP_RET(int, evutil_v6addr_is_local_, (const struct in6_addr * a0), (a0), -1)
WRAP_RET(int, evutil_vsnprintf, (char * a0, size_t a1, const char * a2, va_list a3), (a0, a1, a2, a3), -1)
WRAP_RET(ev_int32_t, evutil_weakrand_, (struct evutil_weakrand_state * a0), (a0), -1)
WRAP_RET(ev_int32_t, evutil_weakrand_range_, (struct evutil_weakrand_state * a0, ev_int32_t a1), (a0, a1), -1)
WRAP_RET(ev_uint32_t, evutil_weakrand_seed_, (struct evutil_weakrand_state * a0, ev_uint32_t a1), (a0, a1), ((ev_uint32_t){0}))

/* ============================================================
 * 可变参数导出函数（无法用函数指针转发，按官方实现等价落地）
 * ============================================================ */
int evutil_snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    int r;
    va_start(ap, format);
    r = evutil_vsnprintf(str, size, format, ap);
    va_end(ap);
    return r;
}

int evbuffer_add_printf(struct evbuffer *buf, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = evbuffer_add_vprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

/* log-internal.h: EVENT_LOG_ERR=3 WARN=2 MSG=1 DEBUG=0 */
void event_err(int eval, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    event_logv_(3, strerror(errno), fmt, ap);
    va_end(ap);
    exit(eval);
}

void event_errx(int eval, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    event_logv_(3, NULL, fmt, ap);
    va_end(ap);
    exit(eval);
}

void event_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    event_logv_(2, strerror(errno), fmt, ap);
    va_end(ap);
}

void event_warnx(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    event_logv_(2, NULL, fmt, ap);
    va_end(ap);
}

void event_msgx(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    event_logv_(1, NULL, fmt, ap);
    va_end(ap);
}

void event_debugx_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    event_logv_(0, NULL, fmt, ap);
    va_end(ap);
}

void event_sock_err(int eval, evutil_socket_t sock, const char *fmt, ...) {
    va_list ap;
    (void)sock;
    va_start(ap, fmt);
    event_logv_(3, strerror(errno), fmt, ap);
    va_end(ap);
    exit(eval);
}

void event_sock_warn(evutil_socket_t sock, const char *fmt, ...) {
    va_list ap;
    (void)sock;
    va_start(ap, fmt);
    event_logv_(2, strerror(errno), fmt, ap);
    va_end(ap);
}

