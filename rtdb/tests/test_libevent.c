/* libevent_dyn 测试：只链 -ldl，不链 libevent
 * 类型块由 ../libevent_dyn.c 机械复制，保证 ABI 完全一致。
 */
#include "test_common.h"
#include <stdint.h>
#include <sys/time.h>
#include <sys/socket.h>

/* ------------------------- 类型（自动复制） ------------------------- */
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

/* ------------------------- 测试用常量 ------------------------- */
#define EV_TIMEOUT      0x01
#define EVLOOP_NONBLOCK 0x02

/* ------------------------- 封装函数声明 ------------------------- */
int libevent_dyn_init(const char *library_path);
int libevent_libok(void);
void libevent_dyn_cleanup(void);

const char *event_get_version(void);
uint32_t event_get_version_number(void);
const char **event_get_supported_methods(void);
const char *event_base_get_method(const struct event_base *);
struct event_base *event_base_new(void);
void event_base_free(struct event_base *);
struct event *event_new(struct event_base *, int, short, event_callback_fn, void *);
int event_add(struct event *, const struct timeval *);
int event_base_dispatch(struct event_base *);
int event_base_loop(struct event_base *, int);
int event_base_loopbreak(struct event_base *);
void event_free(struct event *);

struct evbuffer *evbuffer_new(void);
void evbuffer_free(struct evbuffer *);
int evbuffer_add(struct evbuffer *, const void *, size_t);
size_t evbuffer_get_length(const struct evbuffer *);
int evbuffer_remove(struct evbuffer *, void *, size_t);
int evbuffer_add_printf(struct evbuffer *, const char *, ...);
struct evbuffer_ptr evbuffer_search(struct evbuffer *, const char *, size_t,
                                    const struct evbuffer_ptr *);

int evutil_socketpair(int, int, int, evutil_socket_t[]);
int evutil_closesocket(evutil_socket_t);
int evutil_make_socket_nonblocking(evutil_socket_t);

static int fired;

static void on_timeout(evutil_socket_t fd, short what, void *arg) {
    (void)fd; (void)arg;
    if (what & EV_TIMEOUT)
        fired++;
}

int main(int argc, char **argv) {
    int failures = 0;

    if (libevent_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("libevent 不可加载\n");
    failures += CHECK(libevent_libok() != 0);

    /* 1. 版本与后端 */
    {
        const char **methods = event_get_supported_methods();
        printf("libevent %s (0x%08x), methods: %s\n", event_get_version(),
               (unsigned)event_get_version_number(),
               (methods && methods[0]) ? methods[0] : "(none)");
        failures += CHECK(event_get_version() != NULL);
        failures += CHECK(event_get_version_number() >= 0x02010000u);
        failures += CHECK(methods != NULL && methods[0] != NULL);
    }

    /* 2. base + 定时 event + dispatch */
    {
        struct event_base *base = event_base_new();
        struct event *ev;
        struct timeval tv = {0, 2000};

        failures += CHECK(base != NULL);
        printf("selected method: %s\n", event_base_get_method(base));
        failures += CHECK(event_base_get_method(base) != NULL);

        ev = event_new(base, -1, 0, on_timeout, NULL);
        failures += CHECK(ev != NULL);
        failures += CHECK(event_add(ev, &tv) == 0);
        failures += CHECK(event_base_dispatch(base) == 1);
        failures += CHECK(fired == 1);
        event_free(ev);

        /* 无 pending event 时 EVLOOP_NONBLOCK 返回 1 */
        failures += CHECK(event_base_loop(base, EVLOOP_NONBLOCK) == 1);
        event_base_free(base);
    }

    /* 3. evbuffer：add / search / add_printf / remove */
    {
        struct evbuffer *buf = evbuffer_new();
        struct evbuffer_ptr pos;
        char out[16];

        failures += CHECK(buf != NULL);
        failures += CHECK(evbuffer_add(buf, "hello", 5) == 0);
        failures += CHECK(evbuffer_get_length(buf) == 5);

        pos = evbuffer_search(buf, "llo", 3, NULL);
        failures += CHECK(pos.pos == 2);

        failures += CHECK(evbuffer_add_printf(buf, "-%d", 42) > 0);
        failures += CHECK(evbuffer_get_length(buf) == 8);
        memset(out, 0, sizeof out);
        failures += CHECK(evbuffer_remove(buf, out, sizeof out) == 8);
        failures += CHECK(memcmp(out, "hello-42", 8) == 0);
        evbuffer_free(buf);
    }

    /* 4. evutil 套接字工具 */
    {
        evutil_socket_t sp[2];
        failures += CHECK(evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0);
        failures += CHECK(evutil_make_socket_nonblocking(sp[0]) == 0);
        failures += CHECK(evutil_closesocket(sp[0]) == 0);
        failures += CHECK(evutil_closesocket(sp[1]) == 0);
    }

    /* 5. 错误路径（官方实现保证 NULL 安全） */
    failures += CHECK(event_base_loopbreak(NULL) < 0);
    failures += CHECK(evutil_make_socket_nonblocking(-1) < 0);

    libevent_dyn_cleanup();
    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
