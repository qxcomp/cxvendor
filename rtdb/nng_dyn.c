/* ============================================================
 * nng_dyn.c - dlopen 模拟链接：NNG v2.1.0 C API
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照上游 nng.h(373) / http.h(65) 的 438 个
 * 公开 NNG_DECL 函数、opaque handle、value struct、枚举与函数
 * 指针类型；args.h 无公开函数，不产生封装。导出函数名保持原生
 * 原样。链接期不需要 libnng，仅需 -ldl。
 * 加载顺序：显式路径 > NNG_DYN_LIB > LD_LIBRARY_PATH > .so >
 *           Nix > 平台默认；成功后不卸载。
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
#include <stdbool.h>
#include <stdarg.h>
#include <time.h>

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
#  if __has_include(<nng/nng.h>) && __has_include(<nng/supplemental/http/http.h>)
#    include <nng/nng.h>
#    include <nng/supplemental/http/http.h>
#    define NNG_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef NNG_DYN_HAVE_HEADER
/* ---------- 类型（nng.h / http.h 公开声明原样内嵌） ---------- */
#define NNG_MAXADDRLEN (128)

typedef struct nng_ctx_s      { uint32_t id; } nng_ctx;
typedef struct nng_dialer_s   { uint32_t id; } nng_dialer;
typedef struct nng_listener_s { uint32_t id; } nng_listener;
typedef struct nng_pipe_s     { uint32_t id; } nng_pipe;
typedef struct nng_socket_s   { uint32_t id; } nng_socket;

typedef int32_t  nng_duration;
typedef uint64_t nng_time;

typedef enum {
    NNG_OK           = 0,
    NNG_EINTR        = 1,
    NNG_ENOMEM       = 2,
    NNG_EINVAL       = 3,
    NNG_EBUSY        = 4,
    NNG_ETIMEDOUT    = 5,
    NNG_ECONNREFUSED = 6,
    NNG_ECLOSED      = 7,
    NNG_EAGAIN       = 8,
    NNG_ENOTSUP      = 9,
    NNG_EADDRINUSE   = 10,
    NNG_ESTATE       = 11,
    NNG_ENOENT       = 12,
    NNG_EPROTO       = 13,
    NNG_EUNREACHABLE = 14,
    NNG_EADDRINVAL   = 15,
    NNG_EPERM        = 16,
    NNG_EMSGSIZE     = 17,
    NNG_ECONNABORTED = 18,
    NNG_ECONNRESET   = 19,
    NNG_ECANCELED    = 20,
    NNG_ENOFILES     = 21,
    NNG_ENOSPC       = 22,
    NNG_EEXIST       = 23,
    NNG_EREADONLY    = 24,
    NNG_EWRITEONLY   = 25,
    NNG_ECRYPTO      = 26,
    NNG_EPEERAUTH    = 27,
    NNG_EBADTYPE     = 30,
    NNG_ECONNSHUT    = 31,
    NNG_ESTOPPED     = 999,
    NNG_EINTERNAL    = 1000,
    NNG_ESYSERR      = 0x10000000,
    NNG_ETRANERR     = 0x20000000
} nng_err;

typedef struct nng_msg  nng_msg;
typedef struct nng_stat nng_stat;
typedef struct nng_aio  nng_aio;
typedef struct nng_url  nng_url;
typedef struct nng_tls_config   nng_tls_config;
typedef struct nng_tls_cert_s   nng_tls_cert;

struct nng_sockaddr_inproc   { uint16_t sa_family; char   sa_name[NNG_MAXADDRLEN]; };
struct nng_sockaddr_path     { uint16_t sa_family; char   sa_path[NNG_MAXADDRLEN]; };
struct nng_sockaddr_in6      { uint16_t sa_family; uint16_t sa_port; uint32_t sa_scope; uint8_t sa_addr[16]; };
struct nng_sockaddr_in       { uint16_t sa_family; uint16_t sa_port; uint32_t sa_addr; };
struct nng_sockaddr_abstract { uint16_t sa_family; uint16_t sa_len; uint8_t sa_name[107]; };
struct nng_sockaddr_storage  { uint16_t sa_family; uint64_t sa_pad[16]; };
typedef struct nng_sockaddr_inproc   nng_sockaddr_inproc;
typedef struct nng_sockaddr_path     nng_sockaddr_path;
typedef struct nng_sockaddr_path     nng_sockaddr_ipc;
typedef struct nng_sockaddr_in       nng_sockaddr_in;
typedef struct nng_sockaddr_in6      nng_sockaddr_in6;
typedef struct nng_sockaddr_abstract nng_sockaddr_abstract;
typedef struct nng_sockaddr_storage  nng_sockaddr_storage;
typedef union nng_sockaddr {
    uint16_t              s_family;
    nng_sockaddr_ipc      s_ipc;
    nng_sockaddr_inproc   s_inproc;
    nng_sockaddr_in6      s_in6;
    nng_sockaddr_in       s_in;
    nng_sockaddr_abstract s_abstract;
    nng_sockaddr_storage  s_storage;
} nng_sockaddr;

typedef struct nng_iov { void *iov_buf; size_t iov_len; } nng_iov;

typedef enum {
    NNG_PIPE_EV_NONE,
    NNG_PIPE_EV_ADD_PRE,
    NNG_PIPE_EV_ADD_POST,
    NNG_PIPE_EV_REM_POST,
    NNG_PIPE_EV_NUM
} nng_pipe_ev;
typedef void (*nng_pipe_cb)(nng_pipe, nng_pipe_ev, void *);
typedef void (*nng_aio_cancelfn)(nng_aio *, void *, nng_err);

typedef struct nng_stream          nng_stream;
typedef struct nng_stream_dialer   nng_stream_dialer;
typedef struct nng_stream_listener nng_stream_listener;
typedef struct nng_udp             nng_udp;

typedef struct {
    int16_t num_task_threads;
    int16_t max_task_threads;
    int16_t num_expire_threads;
    int16_t max_expire_threads;
    int16_t num_poller_threads;
    int16_t max_poller_threads;
    int16_t num_resolver_threads;
    void *(*malloc_fn)(size_t);
    void *(*calloc_fn)(size_t, size_t);
    void (*free_fn)(void *, size_t);
} nng_init_params;

typedef enum nng_log_level {
    NNG_LOG_NONE   = 0,
    NNG_LOG_ERR    = 3,
    NNG_LOG_WARN   = 4,
    NNG_LOG_NOTICE = 5,
    NNG_LOG_INFO   = 6,
    NNG_LOG_DEBUG  = 7
} nng_log_level;

typedef enum nng_log_facility {
    NNG_LOG_USER   = 1,
    NNG_LOG_DAEMON = 3,
    NNG_LOG_AUTH   = 10,
    NNG_LOG_LOCAL0 = 16,
    NNG_LOG_LOCAL1 = 17,
    NNG_LOG_LOCAL2 = 18,
    NNG_LOG_LOCAL3 = 19,
    NNG_LOG_LOCAL4 = 20,
    NNG_LOG_LOCAL5 = 21,
    NNG_LOG_LOCAL6 = 22,
    NNG_LOG_LOCAL7 = 23
} nng_log_facility;
typedef void (*nng_logger)(nng_log_level, nng_log_facility, const char *, const char *);

typedef struct nng_thread nng_thread;
typedef struct nng_mtx    nng_mtx;
typedef struct nng_cv     nng_cv;

typedef enum nng_tls_mode { NNG_TLS_MODE_CLIENT = 0, NNG_TLS_MODE_SERVER = 1 } nng_tls_mode;
typedef enum nng_tls_auth_mode {
    NNG_TLS_AUTH_MODE_NONE     = 0,
    NNG_TLS_AUTH_MODE_OPTIONAL = 1,
    NNG_TLS_AUTH_MODE_REQUIRED = 2
} nng_tls_auth_mode;
typedef enum nng_tls_version { NNG_TLS_1_2 = 0x303, NNG_TLS_1_3 = 0x304 } nng_tls_version;
typedef enum nng_tls_group {
    NNG_TLS_GRP_SECP256R1          = 23,
    NNG_TLS_GRP_SECP384R1          = 24,
    NNG_TLS_GRP_SECP521R1          = 25,
    NNG_TLS_GRP_X25519             = 29,
    NNG_TLS_GRP_X448               = 30,
    NNG_TLS_GRP_FFDHE2048          = 256,
    NNG_TLS_GRP_FFDHE3072          = 257,
    NNG_TLS_GRP_FFDHE4096          = 258,
    NNG_TLS_GRP_FFDHE6144          = 259,
    NNG_TLS_GRP_FFDHE8192          = 260,
    NNG_TLS_GRP_SECP256R1MLKEM768  = 4587,
    NNG_TLS_GRP_X25519MLKEM768     = 4588,
    NNG_TLS_GRP_SECP384R1MLKEM1024 = 4589
} nng_tls_group;

typedef struct nng_id_map_s nng_id_map;

typedef enum {
    NNG_HTTP_STATUS_CONTINUE = 100, NNG_HTTP_STATUS_SWITCHING = 101,
    NNG_HTTP_STATUS_PROCESSING = 102, NNG_HTTP_STATUS_OK = 200,
    NNG_HTTP_STATUS_CREATED = 201, NNG_HTTP_STATUS_ACCEPTED = 202,
    NNG_HTTP_STATUS_NOT_AUTHORITATIVE = 203, NNG_HTTP_STATUS_NO_CONTENT = 204,
    NNG_HTTP_STATUS_RESET_CONTENT = 205, NNG_HTTP_STATUS_PARTIAL_CONTENT = 206,
    NNG_HTTP_STATUS_MULTI_STATUS = 207, NNG_HTTP_STATUS_ALREADY_REPORTED = 208,
    NNG_HTTP_STATUS_IM_USED = 226, NNG_HTTP_STATUS_MULTIPLE_CHOICES = 300,
    NNG_HTTP_STATUS_MOVED_PERMANENTLY = 301, NNG_HTTP_STATUS_FOUND = 302,
    NNG_HTTP_STATUS_SEE_OTHER = 303, NNG_HTTP_STATUS_NOT_MODIFIED = 304,
    NNG_HTTP_STATUS_USE_PROXY = 305, NNG_HTTP_STATUS_TEMPORARY_REDIRECT = 307,
    NNG_HTTP_STATUS_PERMANENT_REDIRECT = 308, NNG_HTTP_STATUS_BAD_REQUEST = 400,
    NNG_HTTP_STATUS_UNAUTHORIZED = 401, NNG_HTTP_STATUS_PAYMENT_REQUIRED = 402,
    NNG_HTTP_STATUS_FORBIDDEN = 403, NNG_HTTP_STATUS_NOT_FOUND = 404,
    NNG_HTTP_STATUS_METHOD_NOT_ALLOWED = 405, NNG_HTTP_STATUS_NOT_ACCEPTABLE = 406,
    NNG_HTTP_STATUS_PROXY_AUTH_REQUIRED = 407, NNG_HTTP_STATUS_REQUEST_TIMEOUT = 408,
    NNG_HTTP_STATUS_CONFLICT = 409, NNG_HTTP_STATUS_GONE = 410,
    NNG_HTTP_STATUS_LENGTH_REQUIRED = 411, NNG_HTTP_STATUS_PRECONDITION_FAILED = 412,
    NNG_HTTP_STATUS_CONTENT_TOO_LARGE = 413, NNG_HTTP_STATUS_URI_TOO_LONG = 414,
    NNG_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE = 415, NNG_HTTP_STATUS_RANGE_NOT_SATISFIABLE = 416,
    NNG_HTTP_STATUS_EXPECTATION_FAILED = 417, NNG_HTTP_STATUS_TEAPOT = 418,
    NNG_HTTP_STATUS_UNPROCESSABLE_ENTITY = 422, NNG_HTTP_STATUS_LOCKED = 423,
    NNG_HTTP_STATUS_FAILED_DEPENDENCY = 424, NNG_HTTP_STATUS_TOO_EARLY = 425,
    NNG_HTTP_STATUS_UPGRADE_REQUIRED = 426, NNG_HTTP_STATUS_PRECONDITION_REQUIRED = 428,
    NNG_HTTP_STATUS_TOO_MANY_REQUESTS = 429, NNG_HTTP_STATUS_HEADERS_TOO_LARGE = 431,
    NNG_HTTP_STATUS_UNAVAIL_LEGAL_REASONS = 451, NNG_HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
    NNG_HTTP_STATUS_NOT_IMPLEMENTED = 501, NNG_HTTP_STATUS_BAD_GATEWAY = 502,
    NNG_HTTP_STATUS_SERVICE_UNAVAILABLE = 503, NNG_HTTP_STATUS_GATEWAY_TIMEOUT = 504,
    NNG_HTTP_STATUS_HTTP_VERSION_NOT_SUPP = 505, NNG_HTTP_STATUS_VARIANT_ALSO_NEGOTIATES = 506,
    NNG_HTTP_STATUS_INSUFFICIENT_STORAGE = 507, NNG_HTTP_STATUS_LOOP_DETECTED = 508,
    NNG_HTTP_STATUS_NOT_EXTENDED = 510, NNG_HTTP_STATUS_NETWORK_AUTH_REQUIRED = 511
} nng_http_status;

typedef struct nng_http_req     nng_http_req;
typedef struct nng_http_res     nng_http_res;
typedef struct nng_http_conn    nng_http;
typedef struct nng_http_handler nng_http_handler;
typedef struct nng_http_server  nng_http_server;
typedef struct nng_http_client  nng_http_client;
typedef void (*nng_http_handler_func)(nng_http *, void *, nng_aio *);

#endif /* NNG_DYN_HAVE_HEADER */

/* ---------- 动态层状态 ---------- */
static lib_handle_t nng_handle = NULL;
static int nng_init_state = 0;

static void *nng_sym(const char *name) {
    return nng_handle ? get_sym(nng_handle, name) : NULL;
}

static int nng_do_load(const char *library_path) {
    static char bufs[64][1024];
    const char *candidates[64];
    const char *so_plain = "libnng.so.2.1.0";
    int n = 0, i;
    int res = -1;

    if (nng_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("NNG_DYN_LIB");
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
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_plain);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = so_plain;
    candidates[n++] = "libnng.so.2";
    candidates[n++] = "libnng.so";
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/%s", home, so_plain);
            candidates[n] = bufs[n]; n++;
        }
        snprintf(bufs[n], sizeof bufs[0], "/nix/var/nix/profiles/default/lib/%s", so_plain);
        candidates[n] = bufs[n]; n++;
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libnng.dylib";
    candidates[n++] = "/usr/local/lib/libnng.dylib";
#elif defined(_WIN32)
    candidates[n++] = "nng.dll";
    candidates[n++] = "libnng.dll";
#else
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_plain);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_plain);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/x86_64-linux-gnu/%s", so_plain);
    candidates[n] = bufs[n]; n++;
#endif

    for (i = 0; i < n; i++) {
        nng_handle = load_lib(candidates[i]);
        if (nng_handle) { res = i; break; }
    }
    if (!nng_handle || !nng_sym("nng_version")) {
        fprintf(stderr, "nng_dyn: cannot load nng (%s)\n", lib_error());
        if (nng_handle) { free_lib(nng_handle); nng_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "nng_dyn: loaded %d %s\n", res, candidates[res]);
    return 0;
}

int nng_libok(void) {
    if (nng_init_state == 0)
        nng_init_state = (nng_do_load(NULL) == 0) ? 1 : -1;
    return (nng_init_state == 1 && nng_handle != NULL) ? 1 : 0;
}

int nng_dyn_init(const char *library_path) {
    if (nng_do_load(library_path) == 0) {
        nng_init_state = 1;
        return 0;
    }
    if (nng_init_state == 0) nng_init_state = -1;
    return -1;
}

void nng_dyn_cleanup(void) {
    if (nng_handle) { free_lib(nng_handle); nng_handle = NULL; }
    nng_init_state = 0;
}

int nng_dyn_has_symbol(const char *name) {
    if (!nng_libok()) return 0;
    return nng_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                            \
ret name params {                                                              \
    static ret (*fn) params = NULL;                                            \
    if (!fn) {                                                                 \
        if (!nng_libok()) return fallback;                                     \
        *(void **)(&fn) = nng_sym(#name);                                      \
        if (!fn) {                                                             \
            fprintf(stderr, "nng_dyn: dlsym failed: %s\n", #name);             \
            return fallback;                                                   \
        }                                                                      \
    }                                                                          \
    return fn args;                                                            \
}
#define WRAP_VOID(name, params, args)                                          \
void name params {                                                             \
    static void (*fn) params = NULL;                                           \
    if (!fn) {                                                                 \
        if (!nng_libok()) return;                                              \
        *(void **)(&fn) = nng_sym(#name);                                      \
        if (!fn) {                                                             \
            fprintf(stderr, "nng_dyn: dlsym failed: %s\n", #name);             \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn args;                                                                   \
}
/* 6 个变参日志函数：vsnprintf 后以固定参数转发（照搬 avro_dyn.c） */
#define WRAP_NNG_LOG(name)                                                     \
void name(const char *msgid, const char *msg, ...) {                           \
    static void (*fn)(const char *, const char *, ...) = NULL;                 \
    char buf[8192];                                                            \
    va_list ap;                                                                \
    va_start(ap, msg);                                                         \
    vsnprintf(buf, sizeof buf, msg ? msg : "", ap);                            \
    va_end(ap);                                                                \
    if (!fn) {                                                                 \
        if (!nng_libok()) return;                                              \
        *(void **)(&fn) = nng_sym(#name);                                      \
        if (!fn) {                                                             \
            fprintf(stderr, "nng_dyn: dlsym failed: %s\n", #name);             \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn(msgid, "%s", buf);                                                       \
}
#define WRAP_NNG_LOG_AUTH(name)                                                \
void name(nng_log_level level, const char *msgid, const char *msg, ...) {      \
    static void (*fn)(nng_log_level, const char *, const char *, ...) = NULL;  \
    char buf[8192];                                                            \
    va_list ap;                                                                \
    va_start(ap, msg);                                                         \
    vsnprintf(buf, sizeof buf, msg ? msg : "", ap);                            \
    va_end(ap);                                                                \
    if (!fn) {                                                                 \
        if (!nng_libok()) return;                                              \
        *(void **)(&fn) = nng_sym(#name);                                      \
        if (!fn) {                                                             \
            fprintf(stderr, "nng_dyn: dlsym failed: %s\n", #name);             \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn(level, msgid, "%s", buf);                                               \
}

/* ---------- nng.h 公开函数（函数名原样） ---------- */
WRAP_RET(int, nng_socket_close, (nng_socket p0), (p0), 0)
WRAP_RET(int, nng_socket_id, (nng_socket p0), (p0), 0)
WRAP_RET(int, nng_socket_set_bool, (nng_socket p0, const char *p1, bool p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_socket_set_int, (nng_socket p0, const char *p1, int p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_socket_set_size, (nng_socket p0, const char *p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_socket_set_ms, (nng_socket p0, const char *p1, nng_duration p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_socket_get_bool, (nng_socket p0, const char *p1, bool *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_socket_get_int, (nng_socket p0, const char *p1, int *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_socket_get_size, (nng_socket p0, const char *p1, size_t *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_socket_get_ms, (nng_socket p0, const char *p1, nng_duration *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_socket_get_recv_poll_fd, (nng_socket p0, int *p1), (p0, p1), 0)
WRAP_RET(int, nng_socket_get_send_poll_fd, (nng_socket p0, int *p1), (p0, p1), 0)
WRAP_RET(int, nng_socket_proto_id, (nng_socket p0, uint16_t *p1), (p0, p1), 0)
WRAP_RET(int, nng_socket_peer_id, (nng_socket p0, uint16_t *p1), (p0, p1), 0)
WRAP_RET(int, nng_socket_proto_name, (nng_socket p0, const char **p1), (p0, p1), 0)
WRAP_RET(int, nng_socket_peer_name, (nng_socket p0, const char **p1), (p0, p1), 0)
WRAP_RET(int, nng_socket_raw, (nng_socket p0, bool *p1), (p0, p1), 0)
WRAP_RET(const char *, nng_str_sockaddr, (const nng_sockaddr *p0, char *p1, size_t p2), (p0, p1, p2), NULL)
WRAP_RET(uint32_t, nng_sockaddr_port, (const nng_sockaddr *p0), (p0), 0)
WRAP_RET(bool, nng_sockaddr_equal, (const nng_sockaddr *p0, const nng_sockaddr *p1), (p0, p1), false)
WRAP_RET(uint64_t, nng_sockaddr_hash, (const nng_sockaddr *p0), (p0), 0)
WRAP_RET(nng_err, nng_pipe_notify, (nng_socket p0, nng_pipe_ev p1, nng_pipe_cb p2, void *p3), (p0, p1, p2, p3), NNG_EINVAL)
WRAP_RET(int, nng_listen, (nng_socket p0, const char *p1, nng_listener *p2, int p3), (p0, p1, p2, p3), 0)
WRAP_RET(int, nng_listen_url, (nng_socket p0, const nng_url *p1, nng_listener *p2, int p3), (p0, p1, p2, p3), 0)
WRAP_RET(int, nng_dial, (nng_socket p0, const char *p1, nng_dialer *p2, int p3), (p0, p1, p2, p3), 0)
WRAP_RET(int, nng_dial_url, (nng_socket p0, const nng_url *p1, nng_dialer *p2, int p3), (p0, p1, p2, p3), 0)
WRAP_RET(int, nng_dialer_create, (nng_dialer *p0, nng_socket p1, const char *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_dialer_create_url, (nng_dialer *p0, nng_socket p1, const nng_url *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_listener_create, (nng_listener *p0, nng_socket p1, const char *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_listener_create_url, (nng_listener *p0, nng_socket p1, const nng_url *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_dialer_start, (nng_dialer p0, int p1), (p0, p1), 0)
WRAP_RET(int, nng_dialer_bind, (nng_dialer p0, const nng_sockaddr *p1), (p0, p1), 0)
WRAP_VOID(nng_dialer_start_aio, (nng_dialer p0, int p1, nng_aio *p2), (p0, p1, p2))
WRAP_RET(int, nng_listener_start, (nng_listener p0, int p1), (p0, p1), 0)
WRAP_RET(int, nng_dialer_close, (nng_dialer p0), (p0), 0)
WRAP_RET(int, nng_listener_close, (nng_listener p0), (p0), 0)
WRAP_RET(int, nng_dialer_id, (nng_dialer p0), (p0), 0)
WRAP_RET(int, nng_listener_id, (nng_listener p0), (p0), 0)
WRAP_RET(int, nng_dialer_set_bool, (nng_dialer p0, const char *p1, bool p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_dialer_set_int, (nng_dialer p0, const char *p1, int p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_dialer_set_size, (nng_dialer p0, const char *p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_dialer_set_uint64, (nng_dialer p0, const char *p1, uint64_t p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_dialer_set_string, (nng_dialer p0, const char *p1, const char *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_dialer_set_ms, (nng_dialer p0, const char *p1, nng_duration p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_dialer_set_tls, (nng_dialer p0, nng_tls_config *p1), (p0, p1), 0)
WRAP_RET(int, nng_dialer_get_bool, (nng_dialer p0, const char *p1, bool *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_dialer_get_int, (nng_dialer p0, const char *p1, int *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_dialer_get_size, (nng_dialer p0, const char *p1, size_t *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_dialer_get_uint64, (nng_dialer p0, const char *p1, uint64_t *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_dialer_get_string, (nng_dialer p0, const char *p1, const char **p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_dialer_get_ms, (nng_dialer p0, const char *p1, nng_duration *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_dialer_get_tls, (nng_dialer p0, nng_tls_config **p1), (p0, p1), 0)
WRAP_RET(int, nng_dialer_get_url, (nng_dialer p0, const nng_url **p1), (p0, p1), 0)
WRAP_RET(int, nng_listener_set_bool, (nng_listener p0, const char *p1, bool p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_listener_set_int, (nng_listener p0, const char *p1, int p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_listener_set_size, (nng_listener p0, const char *p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_listener_set_uint64, (nng_listener p0, const char *p1, uint64_t p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_listener_set_string, (nng_listener p0, const char *p1, const char *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_listener_set_ms, (nng_listener p0, const char *p1, nng_duration p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_listener_set_tls, (nng_listener p0, nng_tls_config *p1), (p0, p1), 0)
WRAP_RET(int, nng_listener_set_security_descriptor, (nng_listener p0, void *p1), (p0, p1), 0)
WRAP_RET(int, nng_listener_get_url, (nng_listener p0, const nng_url **p1), (p0, p1), 0)
WRAP_RET(int, nng_listener_get_bool, (nng_listener p0, const char *p1, bool *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_listener_get_int, (nng_listener p0, const char *p1, int *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_listener_get_size, (nng_listener p0, const char *p1, size_t *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_listener_get_uint64, (nng_listener p0, const char *p1, uint64_t *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_listener_get_string, (nng_listener p0, const char *p1, const char **p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_listener_get_ms, (nng_listener p0, const char *p1, nng_duration *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_listener_get_tls, (nng_listener p0, nng_tls_config **p1), (p0, p1), 0)
WRAP_RET(const char *, nng_strerror, (nng_err p0), (p0), NULL)

WRAP_RET(int, nng_send, (nng_socket p0, const void *p1, size_t p2, int p3), (p0, p1, p2, p3), 0)
WRAP_RET(int, nng_recv, (nng_socket p0, void *p1, size_t *p2, int p3), (p0, p1, p2, p3), 0)
WRAP_RET(int, nng_sendmsg, (nng_socket p0, nng_msg *p1, int p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_recvmsg, (nng_socket p0, nng_msg **p1, int p2), (p0, p1, p2), 0)
WRAP_VOID(nng_socket_send, (nng_socket p0, nng_aio *p1), (p0, p1))
WRAP_VOID(nng_socket_recv, (nng_socket p0, nng_aio *p1), (p0, p1))
WRAP_RET(int, nng_ctx_open, (nng_ctx *p0, nng_socket p1), (p0, p1), 0)
WRAP_RET(int, nng_ctx_close, (nng_ctx p0), (p0), 0)
WRAP_RET(int, nng_ctx_id, (nng_ctx p0), (p0), 0)
WRAP_VOID(nng_ctx_recv, (nng_ctx p0, nng_aio *p1), (p0, p1))
WRAP_RET(int, nng_ctx_recvmsg, (nng_ctx p0, nng_msg **p1, int p2), (p0, p1, p2), 0)
WRAP_VOID(nng_ctx_send, (nng_ctx p0, nng_aio *p1), (p0, p1))
WRAP_RET(int, nng_ctx_sendmsg, (nng_ctx p0, nng_msg *p1, int p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_ctx_get_bool, (nng_ctx p0, const char *p1, bool *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_ctx_get_int, (nng_ctx p0, const char *p1, int *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_ctx_get_size, (nng_ctx p0, const char *p1, size_t *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_ctx_get_ms, (nng_ctx p0, const char *p1, nng_duration *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_ctx_set, (nng_ctx p0, const char *p1, const void *p2, size_t p3), (p0, p1, p2, p3), 0)
WRAP_RET(int, nng_ctx_set_bool, (nng_ctx p0, const char *p1, bool p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_ctx_set_int, (nng_ctx p0, const char *p1, int p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_ctx_set_size, (nng_ctx p0, const char *p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_ctx_set_ms, (nng_ctx p0, const char *p1, nng_duration p2), (p0, p1, p2), 0)
WRAP_RET(void *, nng_alloc, (size_t p0), (p0), NULL)
WRAP_VOID(nng_free, (void *p0, size_t p1), (p0, p1))
WRAP_RET(char *, nng_strdup, (const char *p0), (p0), NULL)
WRAP_VOID(nng_strfree, (char *p0), (p0))
WRAP_RET(nng_err, nng_aio_alloc, (nng_aio **p0, void (*p1)(void *), void *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_VOID(nng_aio_free, (nng_aio *p0), (p0))
WRAP_VOID(nng_aio_reap, (nng_aio *p0), (p0))
WRAP_VOID(nng_aio_stop, (nng_aio *p0), (p0))
WRAP_RET(nng_err, nng_aio_result, (nng_aio *p0), (p0), NNG_EINVAL)
WRAP_RET(size_t, nng_aio_count, (nng_aio *p0), (p0), 0)
WRAP_VOID(nng_aio_cancel, (nng_aio *p0), (p0))
WRAP_VOID(nng_aio_abort, (nng_aio *p0, nng_err p1), (p0, p1))
WRAP_VOID(nng_aio_wait, (nng_aio *p0), (p0))
WRAP_RET(bool, nng_aio_busy, (nng_aio *p0), (p0), false)
WRAP_VOID(nng_aio_set_msg, (nng_aio *p0, nng_msg *p1), (p0, p1))
WRAP_RET(nng_msg *, nng_aio_get_msg, (nng_aio *p0), (p0), NULL)
WRAP_RET(nng_err, nng_aio_set_input, (nng_aio *p0, unsigned p1, void *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(void *, nng_aio_get_input, (nng_aio *p0, unsigned p1), (p0, p1), NULL)
WRAP_RET(nng_err, nng_aio_set_output, (nng_aio *p0, unsigned p1, void *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(void *, nng_aio_get_output, (nng_aio *p0, unsigned p1), (p0, p1), NULL)
WRAP_VOID(nng_aio_set_timeout, (nng_aio *p0, nng_duration p1), (p0, p1))
WRAP_VOID(nng_aio_set_expire, (nng_aio *p0, nng_time p1), (p0, p1))
WRAP_RET(nng_err, nng_aio_set_iov, (nng_aio *p0, unsigned p1, const nng_iov *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_VOID(nng_aio_reset, (nng_aio *p0), (p0))
WRAP_VOID(nng_aio_finish, (nng_aio *p0, nng_err p1), (p0, p1))
WRAP_RET(bool, nng_aio_start, (nng_aio *p0, nng_aio_cancelfn p1, void *p2), (p0, p1, p2), false)
WRAP_VOID(nng_aio_skip_callback, (nng_aio *p0, bool *p1), (p0, p1))
WRAP_VOID(nng_sleep_aio, (nng_duration p0, nng_aio *p1), (p0, p1))
WRAP_RET(int, nng_msg_alloc, (nng_msg **p0, size_t p1), (p0, p1), 0)
WRAP_VOID(nng_msg_free, (nng_msg *p0), (p0))
WRAP_RET(int, nng_msg_realloc, (nng_msg *p0, size_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_reserve, (nng_msg *p0, size_t p1), (p0, p1), 0)
WRAP_RET(size_t, nng_msg_capacity, (nng_msg *p0), (p0), 0)
WRAP_RET(void *, nng_msg_header, (nng_msg *p0), (p0), NULL)
WRAP_RET(size_t, nng_msg_header_len, (const nng_msg *p0), (p0), 0)
WRAP_RET(void *, nng_msg_body, (nng_msg *p0), (p0), NULL)
WRAP_RET(size_t, nng_msg_len, (const nng_msg *p0), (p0), 0)
WRAP_RET(int, nng_msg_append, (nng_msg *p0, const void *p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_msg_insert, (nng_msg *p0, const void *p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_msg_trim, (nng_msg *p0, size_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_chop, (nng_msg *p0, size_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_header_append, (nng_msg *p0, const void *p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_msg_header_insert, (nng_msg *p0, const void *p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_msg_header_trim, (nng_msg *p0, size_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_header_chop, (nng_msg *p0, size_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_header_append_u16, (nng_msg *p0, uint16_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_header_append_u32, (nng_msg *p0, uint32_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_header_append_u64, (nng_msg *p0, uint64_t p1), (p0, p1), 0)

WRAP_RET(int, nng_msg_header_insert_u16, (nng_msg *p0, uint16_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_header_insert_u32, (nng_msg *p0, uint32_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_header_insert_u64, (nng_msg *p0, uint64_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_header_chop_u16, (nng_msg *p0, uint16_t *p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_header_chop_u32, (nng_msg *p0, uint32_t *p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_header_chop_u64, (nng_msg *p0, uint64_t *p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_header_trim_u16, (nng_msg *p0, uint16_t *p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_header_trim_u32, (nng_msg *p0, uint32_t *p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_header_trim_u64, (nng_msg *p0, uint64_t *p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_append_u16, (nng_msg *p0, uint16_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_append_u32, (nng_msg *p0, uint32_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_append_u64, (nng_msg *p0, uint64_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_insert_u16, (nng_msg *p0, uint16_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_insert_u32, (nng_msg *p0, uint32_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_insert_u64, (nng_msg *p0, uint64_t p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_chop_u16, (nng_msg *p0, uint16_t *p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_chop_u32, (nng_msg *p0, uint32_t *p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_chop_u64, (nng_msg *p0, uint64_t *p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_trim_u16, (nng_msg *p0, uint16_t *p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_trim_u32, (nng_msg *p0, uint32_t *p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_trim_u64, (nng_msg *p0, uint64_t *p1), (p0, p1), 0)
WRAP_RET(int, nng_msg_dup, (nng_msg **p0, const nng_msg *p1), (p0, p1), 0)
WRAP_VOID(nng_msg_clear, (nng_msg *p0), (p0))
WRAP_VOID(nng_msg_header_clear, (nng_msg *p0), (p0))
WRAP_VOID(nng_msg_set_pipe, (nng_msg *p0, nng_pipe p1), (p0, p1))
WRAP_RET(nng_pipe, nng_msg_get_pipe, (const nng_msg *p0), (p0), (nng_pipe){0})
WRAP_RET(nng_err, nng_pipe_get_bool, (nng_pipe p0, const char *p1, bool *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_pipe_get_int, (nng_pipe p0, const char *p1, int *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_pipe_get_ms, (nng_pipe p0, const char *p1, nng_duration *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_pipe_get_size, (nng_pipe p0, const char *p1, size_t *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_pipe_get_string, (nng_pipe p0, const char *p1, const char **p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_pipe_get_strdup, (nng_pipe p0, const char *p1, char **p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_pipe_get_strcpy, (nng_pipe p0, const char *p1, char *p2, size_t p3), (p0, p1, p2, p3), NNG_EINVAL)
WRAP_RET(nng_err, nng_pipe_get_strlen, (nng_pipe p0, const char *p1, size_t *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_pipe_get_scheme, (nng_pipe p0, const char **p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_pipe_peer_addr, (nng_pipe p0, nng_sockaddr *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_pipe_self_addr, (nng_pipe p0, nng_sockaddr *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_pipe_peer_cert, (nng_pipe p0, nng_tls_cert **p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_pipe_close, (nng_pipe p0), (p0), NNG_EINVAL)
WRAP_RET(int, nng_pipe_id, (nng_pipe p0), (p0), 0)
WRAP_RET(nng_socket, nng_pipe_socket, (nng_pipe p0), (p0), (nng_socket){0})
WRAP_RET(nng_dialer, nng_pipe_dialer, (nng_pipe p0), (p0), (nng_dialer){0})
WRAP_RET(nng_listener, nng_pipe_listener, (nng_pipe p0), (p0), (nng_listener){0})
WRAP_RET(int, nng_stats_get, (nng_stat **p0), (p0), 0)
WRAP_VOID(nng_stats_free, (nng_stat *p0), (p0))
WRAP_VOID(nng_stats_dump, (const nng_stat *p0), (p0))
WRAP_RET(const nng_stat *, nng_stat_next, (const nng_stat *p0), (p0), NULL)
WRAP_RET(const nng_stat *, nng_stat_child, (const nng_stat *p0), (p0), NULL)
WRAP_RET(const char *, nng_stat_name, (const nng_stat *p0), (p0), NULL)
WRAP_RET(int, nng_stat_type, (const nng_stat *p0), (p0), 0)
WRAP_RET(const nng_stat *, nng_stat_find, (const nng_stat *p0, const char *p1), (p0, p1), NULL)
WRAP_RET(const nng_stat *, nng_stat_find_socket, (const nng_stat *p0, nng_socket p1), (p0, p1), NULL)
WRAP_RET(const nng_stat *, nng_stat_find_dialer, (const nng_stat *p0, nng_dialer p1), (p0, p1), NULL)
WRAP_RET(const nng_stat *, nng_stat_find_listener, (const nng_stat *p0, nng_listener p1), (p0, p1), NULL)
WRAP_RET(int, nng_stat_unit, (const nng_stat *p0), (p0), 0)
WRAP_RET(uint64_t, nng_stat_value, (const nng_stat *p0), (p0), 0)
WRAP_RET(bool, nng_stat_bool, (const nng_stat *p0), (p0), false)
WRAP_RET(const char *, nng_stat_string, (const nng_stat *p0), (p0), NULL)
WRAP_RET(const char *, nng_stat_desc, (const nng_stat *p0), (p0), NULL)
WRAP_RET(uint64_t, nng_stat_timestamp, (const nng_stat *p0), (p0), 0)
WRAP_RET(nng_err, nng_device, (nng_socket p0, nng_socket p1), (p0, p1), NNG_EINVAL)
WRAP_VOID(nng_device_aio, (nng_aio *p0, nng_socket p1, nng_socket p2), (p0, p1, p2))
WRAP_RET(nng_err, nng_url_parse, (nng_url **p0, const char *p1), (p0, p1), NNG_EINVAL)
WRAP_VOID(nng_url_free, (nng_url *p0), (p0))
WRAP_RET(nng_err, nng_url_clone, (nng_url **p0, const nng_url *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(int, nng_url_sprintf, (char *p0, size_t p1, const nng_url *p2), (p0, p1, p2), 0)
WRAP_RET(const char *, nng_url_scheme, (const nng_url *p0), (p0), NULL)
WRAP_RET(uint32_t, nng_url_port, (const nng_url *p0), (p0), 0)
WRAP_VOID(nng_url_resolve_port, (nng_url *p0, uint32_t p1), (p0, p1))
WRAP_RET(const char *, nng_url_hostname, (const nng_url *p0), (p0), NULL)

WRAP_RET(const char *, nng_url_userinfo, (const nng_url *p0), (p0), NULL)
WRAP_RET(const char *, nng_url_path, (const nng_url *p0), (p0), NULL)
WRAP_RET(const char *, nng_url_query, (const nng_url *p0), (p0), NULL)
WRAP_RET(const char *, nng_url_fragment, (const nng_url *p0), (p0), NULL)
WRAP_RET(const char *, nng_version, (void), (), NULL)
WRAP_VOID(nng_stream_free, (nng_stream *p0), (p0))
WRAP_VOID(nng_stream_close, (nng_stream *p0), (p0))
WRAP_VOID(nng_stream_stop, (nng_stream *p0), (p0))
WRAP_VOID(nng_stream_send, (nng_stream *p0, nng_aio *p1), (p0, p1))
WRAP_VOID(nng_stream_recv, (nng_stream *p0, nng_aio *p1), (p0, p1))
WRAP_RET(nng_err, nng_stream_get_bool, (nng_stream *p0, const char *p1, bool *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_get_int, (nng_stream *p0, const char *p1, int *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_get_ms, (nng_stream *p0, const char *p1, nng_duration *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_get_size, (nng_stream *p0, const char *p1, size_t *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_get_uint64, (nng_stream *p0, const char *p1, uint64_t *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_get_string, (nng_stream *p0, const char *p1, const char **p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(const nng_sockaddr *, nng_stream_peer_addr, (nng_stream *p0), (p0), NULL)
WRAP_RET(const nng_sockaddr *, nng_stream_self_addr, (nng_stream *p0), (p0), NULL)
WRAP_RET(nng_err, nng_stream_peer_cert, (nng_stream *p0, nng_tls_cert **p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_alloc, (nng_stream_dialer **p0, const char *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_alloc_url, (nng_stream_dialer **p0, const nng_url *p1), (p0, p1), NNG_EINVAL)
WRAP_VOID(nng_stream_dialer_free, (nng_stream_dialer *p0), (p0))
WRAP_VOID(nng_stream_dialer_close, (nng_stream_dialer *p0), (p0))
WRAP_VOID(nng_stream_dialer_stop, (nng_stream_dialer *p0), (p0))
WRAP_VOID(nng_stream_dialer_dial, (nng_stream_dialer *p0, nng_aio *p1), (p0, p1))
WRAP_RET(nng_err, nng_stream_dialer_bind, (nng_stream_dialer *p0, const nng_sockaddr *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_get_bool, (nng_stream_dialer *p0, const char *p1, bool *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_get_int, (nng_stream_dialer *p0, const char *p1, int *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_get_ms, (nng_stream_dialer *p0, const char *p1, nng_duration *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_get_size, (nng_stream_dialer *p0, const char *p1, size_t *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_get_uint64, (nng_stream_dialer *p0, const char *p1, uint64_t *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_get_string, (nng_stream_dialer *p0, const char *p1, const char **p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_set_bool, (nng_stream_dialer *p0, const char *p1, bool p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_set_int, (nng_stream_dialer *p0, const char *p1, int p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_set_ms, (nng_stream_dialer *p0, const char *p1, nng_duration p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_set_size, (nng_stream_dialer *p0, const char *p1, size_t p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_set_uint64, (nng_stream_dialer *p0, const char *p1, uint64_t p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_set_string, (nng_stream_dialer *p0, const char *p1, const char *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_get_tls, (nng_stream_dialer *p0, nng_tls_config **p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_dialer_set_tls, (nng_stream_dialer *p0, nng_tls_config *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_alloc, (nng_stream_listener **p0, const char *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_alloc_url, (nng_stream_listener **p0, const nng_url *p1), (p0, p1), NNG_EINVAL)
WRAP_VOID(nng_stream_listener_free, (nng_stream_listener *p0), (p0))
WRAP_VOID(nng_stream_listener_close, (nng_stream_listener *p0), (p0))
WRAP_VOID(nng_stream_listener_stop, (nng_stream_listener *p0), (p0))
WRAP_RET(nng_err, nng_stream_listener_listen, (nng_stream_listener *p0), (p0), NNG_EINVAL)
WRAP_VOID(nng_stream_listener_accept, (nng_stream_listener *p0, nng_aio *p1), (p0, p1))
WRAP_RET(nng_err, nng_stream_listener_get_bool, (nng_stream_listener *p0, const char *p1, bool *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_get_int, (nng_stream_listener *p0, const char *p1, int *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_get_ms, (nng_stream_listener *p0, const char *p1, nng_duration *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_get_size, (nng_stream_listener *p0, const char *p1, size_t *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_get_uint64, (nng_stream_listener *p0, const char *p1, uint64_t *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_get_string, (nng_stream_listener *p0, const char *p1, const char **p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_set_bool, (nng_stream_listener *p0, const char *p1, bool p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_set_int, (nng_stream_listener *p0, const char *p1, int p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_set_ms, (nng_stream_listener *p0, const char *p1, nng_duration p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_set_size, (nng_stream_listener *p0, const char *p1, size_t p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_set_uint64, (nng_stream_listener *p0, const char *p1, uint64_t p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_set_string, (nng_stream_listener *p0, const char *p1, const char *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_get_tls, (nng_stream_listener *p0, nng_tls_config **p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_set_tls, (nng_stream_listener *p0, nng_tls_config *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_stream_listener_set_security_descriptor, (nng_stream_listener *p0, void *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(int, nng_udp_open, (nng_udp **p0, nng_sockaddr *p1), (p0, p1), 0)
WRAP_VOID(nng_udp_stop, (nng_udp *p0), (p0))
WRAP_VOID(nng_udp_close, (nng_udp *p0), (p0))
WRAP_RET(int, nng_udp_sockname, (nng_udp *p0, nng_sockaddr *p1), (p0, p1), 0)
WRAP_VOID(nng_udp_send, (nng_udp *p0, nng_aio *p1), (p0, p1))
WRAP_VOID(nng_udp_recv, (nng_udp *p0, nng_aio *p1), (p0, p1))
WRAP_RET(int, nng_udp_multicast_membership, (nng_udp *p0, nng_sockaddr *p1, bool p2), (p0, p1, p2), 0)
WRAP_RET(nng_err, nng_init, (const nng_init_params *p0), (p0), NNG_EINVAL)

WRAP_VOID(nng_fini, (void), ())
WRAP_VOID(nng_null_logger, (nng_log_level p0, nng_log_facility p1, const char *p2, const char *p3), (p0, p1, p2, p3))
WRAP_VOID(nng_stderr_logger, (nng_log_level p0, nng_log_facility p1, const char *p2, const char *p3), (p0, p1, p2, p3))
WRAP_VOID(nng_system_logger, (nng_log_level p0, nng_log_facility p1, const char *p2, const char *p3), (p0, p1, p2, p3))
WRAP_VOID(nng_log_set_facility, (nng_log_facility p0), (p0))
WRAP_VOID(nng_log_set_level, (nng_log_level p0), (p0))
WRAP_RET(nng_log_level, nng_log_get_level, (void), (), 0)
WRAP_VOID(nng_log_set_logger, (nng_logger p0), (p0))
WRAP_NNG_LOG(nng_log_err)
WRAP_NNG_LOG(nng_log_warn)
WRAP_NNG_LOG(nng_log_notice)
WRAP_NNG_LOG(nng_log_info)
WRAP_NNG_LOG(nng_log_debug)
WRAP_NNG_LOG_AUTH(nng_log_auth)
WRAP_RET(nng_time, nng_clock, (void), (), 0)
WRAP_VOID(nng_msleep, (nng_duration p0), (p0))
WRAP_RET(uint32_t, nng_random, (void), (), 0)
WRAP_RET(nng_err, nng_socket_pair, (int p0[2]), (p0), NNG_EINVAL)
WRAP_RET(int, nng_thread_create, (nng_thread **p0, void (*p1)(void *), void *p2), (p0, p1, p2), 0)
WRAP_VOID(nng_thread_set_name, (nng_thread *p0, const char *p1), (p0, p1))
WRAP_VOID(nng_thread_destroy, (nng_thread *p0), (p0))
WRAP_RET(int, nng_mtx_alloc, (nng_mtx **p0), (p0), 0)
WRAP_VOID(nng_mtx_free, (nng_mtx *p0), (p0))
WRAP_VOID(nng_mtx_lock, (nng_mtx *p0), (p0))
WRAP_VOID(nng_mtx_unlock, (nng_mtx *p0), (p0))
WRAP_RET(int, nng_cv_alloc, (nng_cv **p0, nng_mtx *p1), (p0, p1), 0)
WRAP_VOID(nng_cv_free, (nng_cv *p0), (p0))
WRAP_VOID(nng_cv_wait, (nng_cv *p0), (p0))
WRAP_RET(int, nng_cv_until, (nng_cv *p0, nng_time p1), (p0, p1), 0)
WRAP_VOID(nng_cv_wake, (nng_cv *p0), (p0))
WRAP_VOID(nng_cv_wake1, (nng_cv *p0), (p0))
WRAP_RET(int, nng_tls_config_alloc, (nng_tls_config **p0, nng_tls_mode p1), (p0, p1), 0)
WRAP_VOID(nng_tls_config_hold, (nng_tls_config *p0), (p0))
WRAP_VOID(nng_tls_config_free, (nng_tls_config *p0), (p0))
WRAP_RET(int, nng_tls_config_server_name, (nng_tls_config *p0, const char *p1), (p0, p1), 0)
WRAP_RET(int, nng_tls_config_ca_chain, (nng_tls_config *p0, const char *p1, const char *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_tls_config_own_cert, (nng_tls_config *p0, const char *p1, const char *p2, const char *p3), (p0, p1, p2, p3), 0)
WRAP_RET(int, nng_tls_config_key, (nng_tls_config *p0, const uint8_t *p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_tls_config_pass, (nng_tls_config *p0, const char *p1), (p0, p1), 0)
WRAP_RET(int, nng_tls_config_auth_mode, (nng_tls_config *p0, nng_tls_auth_mode p1), (p0, p1), 0)
WRAP_RET(int, nng_tls_config_ca_file, (nng_tls_config *p0, const char *p1), (p0, p1), 0)
WRAP_RET(int, nng_tls_config_cert_key_file, (nng_tls_config *p0, const char *p1, const char *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_tls_config_psk, (nng_tls_config *p0, const char *p1, const uint8_t *p2, size_t p3), (p0, p1, p2, p3), 0)
WRAP_RET(int, nng_tls_config_version, (nng_tls_config *p0, nng_tls_version p1, nng_tls_version p2), (p0, p1, p2), 0)
WRAP_RET(nng_err, nng_tls_config_groups, (nng_tls_config *p0, const nng_tls_group *p1, size_t p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(const char *, nng_tls_engine_name, (void), (), NULL)
WRAP_RET(const char *, nng_tls_engine_description, (void), (), NULL)
WRAP_RET(bool, nng_tls_engine_fips_mode, (void), (), false)
WRAP_RET(nng_err, nng_tls_cert_parse_pem, (nng_tls_cert **p0, const char *p1, size_t p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_tls_cert_parse_der, (nng_tls_cert **p0, const uint8_t *p1, size_t p2), (p0, p1, p2), NNG_EINVAL)
WRAP_VOID(nng_tls_cert_der, (nng_tls_cert *p0, uint8_t *p1, size_t *p2), (p0, p1, p2))
WRAP_VOID(nng_tls_cert_free, (nng_tls_cert *p0), (p0))
WRAP_RET(nng_err, nng_tls_cert_subject, (nng_tls_cert *p0, char **p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_tls_cert_issuer, (nng_tls_cert *p0, char **p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_tls_cert_serial_number, (nng_tls_cert *p0, char **p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_tls_cert_subject_cn, (nng_tls_cert *p0, char **p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_tls_cert_next_alt, (nng_tls_cert *p0, char **p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_tls_cert_not_before, (nng_tls_cert *p0, struct tm *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_tls_cert_not_after, (nng_tls_cert *p0, struct tm *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(int, nng_id_map_alloc, (nng_id_map **p0, uint64_t p1, uint64_t p2, int p3), (p0, p1, p2, p3), 0)
WRAP_VOID(nng_id_map_free, (nng_id_map *p0), (p0))
WRAP_RET(void *, nng_id_get, (nng_id_map *p0, uint64_t p1), (p0, p1), NULL)
WRAP_RET(int, nng_id_set, (nng_id_map *p0, uint64_t p1, void *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_id_alloc, (nng_id_map *p0, uint64_t *p1, void *p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_id_remove, (nng_id_map *p0, uint64_t p1), (p0, p1), 0)
WRAP_RET(bool, nng_id_visit, (nng_id_map *p0, uint64_t *p1, void **p2, uint32_t *p3), (p0, p1, p2, p3), false)
WRAP_RET(int, nng_bus0_open, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_bus0_open_raw, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_pair0_open, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_pair0_open_raw, (nng_socket *p0), (p0), 0)

WRAP_RET(int, nng_pair1_open, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_pair1_open_raw, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_pair1_open_poly, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_pull0_open, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_pull0_open_raw, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_push0_open, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_push0_open_raw, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_pub0_open, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_pub0_open_raw, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_sub0_open, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_sub0_open_raw, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_sub0_socket_subscribe, (nng_socket p0, const void *p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_sub0_socket_unsubscribe, (nng_socket p0, const void *p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_sub0_ctx_subscribe, (nng_ctx p0, const void *p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_sub0_ctx_unsubscribe, (nng_ctx p0, const void *p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, nng_rep0_open, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_rep0_open_raw, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_req0_open, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_req0_open_raw, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_respondent0_open, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_respondent0_open_raw, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_surveyor0_open, (nng_socket *p0), (p0), 0)
WRAP_RET(int, nng_surveyor0_open_raw, (nng_socket *p0), (p0), 0)

/* ---------- http.h 公开函数（函数名原样） ---------- */
WRAP_VOID(nng_http_close, (nng_http *p0), (p0))
WRAP_VOID(nng_http_read, (nng_http *p0, nng_aio *p1), (p0, p1))
WRAP_VOID(nng_http_read_all, (nng_http *p0, nng_aio *p1), (p0, p1))
WRAP_VOID(nng_http_write, (nng_http *p0, nng_aio *p1), (p0, p1))
WRAP_VOID(nng_http_write_all, (nng_http *p0, nng_aio *p1), (p0, p1))
WRAP_VOID(nng_http_write_request, (nng_http *p0, nng_aio *p1), (p0, p1))
WRAP_VOID(nng_http_write_response, (nng_http *p0, nng_aio *p1), (p0, p1))
WRAP_VOID(nng_http_read_response, (nng_http *p0, nng_aio *p1), (p0, p1))
WRAP_VOID(nng_http_reset, (nng_http *p0), (p0))
WRAP_RET(nng_err, nng_http_set_uri, (nng_http *p0, const char *p1, const char *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(const char *, nng_http_get_uri, (nng_http *p0), (p0), NULL)
WRAP_RET(nng_http_status, nng_http_get_status, (nng_http *p0), (p0), 0)
WRAP_RET(const char *, nng_http_get_reason, (nng_http *p0), (p0), NULL)
WRAP_VOID(nng_http_set_status, (nng_http *p0, nng_http_status p1, const char *p2), (p0, p1, p2))
WRAP_RET(nng_err, nng_http_set_version, (nng_http *p0, const char *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(const char *, nng_http_get_version, (nng_http *p0), (p0), NULL)
WRAP_VOID(nng_http_set_method, (nng_http *p0, const char *p1), (p0, p1))
WRAP_RET(const char *, nng_http_get_method, (nng_http *p0), (p0), NULL)
WRAP_RET(nng_err, nng_http_local_address, (nng_http *p0, nng_sockaddr *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_remote_address, (nng_http *p0, nng_sockaddr *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_get_bool, (nng_http *p0, const char *p1, bool *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_get_int, (nng_http *p0, const char *p1, int *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_get_ms, (nng_http *p0, const char *p1, nng_duration *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_get_size, (nng_http *p0, const char *p1, size_t *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_get_uint64, (nng_http *p0, const char *p1, uint64_t *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_get_string, (nng_http *p0, const char *p1, const char **p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_set_header, (nng_http *p0, const char *p1, const char *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_add_header, (nng_http *p0, const char *p1, const char *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_VOID(nng_http_del_header, (nng_http *p0, const char *p1), (p0, p1))
WRAP_RET(const char *, nng_http_get_header, (nng_http *p0, const char *p1), (p0, p1), NULL)

WRAP_RET(bool, nng_http_next_header, (nng_http *p0, const char **p1, const char **p2, void **p3), (p0, p1, p2, p3), false)
WRAP_VOID(nng_http_get_body, (nng_http *p0, void **p1, size_t *p2), (p0, p1, p2))
WRAP_VOID(nng_http_set_body, (nng_http *p0, void *p1, size_t p2), (p0, p1, p2))
WRAP_RET(nng_err, nng_http_copy_body, (nng_http *p0, const void *p1, size_t p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_peer_cert, (nng_http *p0, nng_tls_cert **p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_handler_alloc, (nng_http_handler **p0, const char *p1, nng_http_handler_func p2), (p0, p1, p2), NNG_EINVAL)
WRAP_VOID(nng_http_handler_free, (nng_http_handler *p0), (p0))
WRAP_RET(nng_err, nng_http_handler_alloc_file, (nng_http_handler **p0, const char *p1, const char *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_handler_alloc_static, (nng_http_handler **p0, const char *p1, const void *p2, size_t p3, const char *p4), (p0, p1, p2, p3, p4), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_handler_alloc_redirect, (nng_http_handler **p0, const char *p1, nng_http_status p2, const char *p3), (p0, p1, p2, p3), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_handler_alloc_directory, (nng_http_handler **p0, const char *p1, const char *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_VOID(nng_http_handler_set_method, (nng_http_handler *p0, const char *p1), (p0, p1))
WRAP_VOID(nng_http_handler_set_host, (nng_http_handler *p0, const char *p1), (p0, p1))
WRAP_VOID(nng_http_handler_collect_body, (nng_http_handler *p0, bool p1, size_t p2), (p0, p1, p2))
WRAP_VOID(nng_http_handler_set_tree, (nng_http_handler *p0), (p0))
WRAP_VOID(nng_http_handler_set_data, (nng_http_handler *p0, void *p1, void (*p2)(void *)), (p0, p1, p2))
WRAP_RET(nng_err, nng_http_server_hold, (nng_http_server **p0, const nng_url *p1), (p0, p1), NNG_EINVAL)
WRAP_VOID(nng_http_server_release, (nng_http_server *p0), (p0))
WRAP_RET(nng_err, nng_http_server_start, (nng_http_server *p0), (p0), NNG_EINVAL)
WRAP_VOID(nng_http_server_stop, (nng_http_server *p0), (p0))
WRAP_RET(nng_err, nng_http_server_add_handler, (nng_http_server *p0, nng_http_handler *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_server_del_handler, (nng_http_server *p0, nng_http_handler *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_server_set_tls, (nng_http_server *p0, nng_tls_config *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_server_get_tls, (nng_http_server *p0, nng_tls_config **p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_server_get_port, (nng_http_server *p0, int *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_server_set_error_page, (nng_http_server *p0, nng_http_status p1, const char *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_server_set_error_file, (nng_http_server *p0, nng_http_status p1, const char *p2), (p0, p1, p2), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_server_error, (nng_http_server *p0, nng_http *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_hijack_stream, (nng_http *p0, nng_stream **p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_client_alloc, (nng_http_client **p0, const nng_url *p1), (p0, p1), NNG_EINVAL)
WRAP_VOID(nng_http_client_free, (nng_http_client *p0), (p0))
WRAP_RET(nng_err, nng_http_client_set_tls, (nng_http_client *p0, nng_tls_config *p1), (p0, p1), NNG_EINVAL)
WRAP_RET(nng_err, nng_http_client_get_tls, (nng_http_client *p0, nng_tls_config **p1), (p0, p1), NNG_EINVAL)
WRAP_VOID(nng_http_client_connect, (nng_http_client *p0, nng_aio *p1), (p0, p1))
WRAP_VOID(nng_http_transact, (nng_http *p0, nng_aio *p1), (p0, p1))
