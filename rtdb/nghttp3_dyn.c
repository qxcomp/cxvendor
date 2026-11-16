/* ============================================================
* nghttp3_dyn.c - type safe dynamic lib load for libnghttp3
* ============================================================
* 设计要点：
*   1. 单文件、无 .h、无 wrapper 层；链接期不需要 -lnghttp3。
*   2. 封装函数名与原版 libnghttp3 导出符号一致（按 nm -D 核对）。
*   3. 每个函数首次调用时 dlsym 缓存；失败打印 stderr 并返回安全默认值。
*   4. 公开结构体/枚举/回调 typedef 按 1.18.0 头文件核对。
*   5. 加载顺序：显式路径 > NGHTTP3_DYN_LIB > LD_LIBRARY_PATH >
*      默认 soname > .so > Nix > 平台默认。
*   6. 加载成功后不卸载（dlclose 不安全）；失败后 cleanup 复位。
*   7. 纯 C 库：RTLD_LAZY | RTLD_LOCAL。
*   8. 头文件仅用于开发期核对，本文件不 include 任何库头文件。
*
* 使用：
*   nghttp3_dyn_init(NULL);
*   ... 原版 API 直接调用 ...
*   nghttp3_dyn_cleanup();    // 仅复位加载状态，不卸载库
* ============================================================ */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>

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
* 类型定义（按 nghttp3.h 核对的公开 ABI）
* ============================================================ */
typedef ptrdiff_t nghttp3_ssize;
typedef uint64_t nghttp3_tstamp;
typedef uint64_t nghttp3_duration;

typedef struct nghttp3_conn nghttp3_conn;
typedef struct nghttp3_rcbuf nghttp3_rcbuf;
typedef struct nghttp3_qpack_encoder nghttp3_qpack_encoder;
typedef struct nghttp3_qpack_decoder nghttp3_qpack_decoder;
typedef struct nghttp3_qpack_stream_context nghttp3_qpack_stream_context;
typedef struct nghttp3_proto_settings nghttp3_proto_settings;

typedef enum nghttp3_qpack_indexing_strat {
   NGHTTP3_QPACK_INDEXING_STRAT_NONE,
   NGHTTP3_QPACK_INDEXING_STRAT_EAGER
} nghttp3_qpack_indexing_strat;

typedef struct {
   int age;
   int version_num;
   const char *version_str;
} nghttp3_info;

typedef struct {
   uint8_t *base;
   size_t len;
} nghttp3_vec;

typedef struct {
   const uint8_t *name;
   const uint8_t *value;
   size_t namelen;
   size_t valuelen;
   uint8_t flags;
} nghttp3_nv;

typedef struct {
   uint8_t *begin;
   uint8_t *end;
   uint8_t *pos;
   uint8_t *last;
} nghttp3_buf;

typedef void *(*nghttp3_malloc_t)(size_t size, void *user_data);
typedef void (*nghttp3_free_t)(void *ptr, void *user_data);
typedef void *(*nghttp3_calloc_t)(size_t nmemb, size_t size, void *user_data);
typedef void *(*nghttp3_realloc_t)(void *ptr, void *user_data, size_t size);

typedef struct {
   void *user_data;
   nghttp3_malloc_t malloc;
   nghttp3_free_t free;
   nghttp3_calloc_t calloc;
   nghttp3_realloc_t realloc;
} nghttp3_mem;

typedef struct nghttp3_settings {
   uint64_t max_field_section_size;
   size_t qpack_max_dtable_capacity;
   size_t qpack_encoder_max_dtable_capacity;
   size_t qpack_blocked_streams;
   uint8_t enable_connect_protocol;
   uint8_t h3_datagram;
   const nghttp3_vec *origin_list;
   uint64_t glitch_ratelim_burst;
   uint64_t glitch_ratelim_rate;
   nghttp3_qpack_indexing_strat qpack_indexing_strat;
} nghttp3_settings;

typedef struct __attribute__((aligned(8))) {
   uint32_t urgency;
   uint8_t inc;
} nghttp3_pri;

typedef struct {
   nghttp3_rcbuf *name;
   nghttp3_rcbuf *value;
   int32_t token;
   uint8_t flags;
} nghttp3_qpack_nv;

/* ---- 回调 typedef ---- */
    typedef void (*nghttp3_free)(void *ptr, void *user_data);
    typedef void (*nghttp3_debug_vprintf_callback)(const char *format, va_list args);
    typedef int (*nghttp3_acked_stream_data)(nghttp3_conn *conn, int64_t stream_id, uint64_t datalen, void *conn_user_data, void *stream_user_data);
    typedef int (*nghttp3_stream_close)(nghttp3_conn *conn, int64_t stream_id, uint64_t app_error_code, void *conn_user_data, void *stream_user_data);
    typedef int (*nghttp3_recv_data)(nghttp3_conn *conn, int64_t stream_id, const uint8_t *data, size_t datalen, void *conn_user_data, void *stream_user_data);
    typedef int (*nghttp3_deferred_consume)(nghttp3_conn *conn, int64_t stream_id, size_t consumed, void *conn_user_data, void *stream_user_data);
    typedef int (*nghttp3_begin_headers)(nghttp3_conn *conn, int64_t stream_id, void *conn_user_data, void *stream_user_data);
    typedef int (*nghttp3_recv_header)(nghttp3_conn *conn, int64_t stream_id, int32_t token, nghttp3_rcbuf *name, nghttp3_rcbuf *value, uint8_t flags, void *conn_user_data, void *stream_user_data);
    typedef int (*nghttp3_end_headers)(nghttp3_conn *conn, int64_t stream_id, int fin, void *conn_user_data, void *stream_user_data);
    typedef int (*nghttp3_end_stream)(nghttp3_conn *conn, int64_t stream_id, void *conn_user_data, void *stream_user_data);
    typedef int (*nghttp3_stop_sending)(nghttp3_conn *conn, int64_t stream_id, uint64_t app_error_code, void *conn_user_data, void *stream_user_data);
    typedef int (*nghttp3_reset_stream)(nghttp3_conn *conn, int64_t stream_id, uint64_t app_error_code, void *conn_user_data, void *stream_user_data);
    typedef int (*nghttp3_shutdown)(nghttp3_conn *conn, int64_t id, void *conn_user_data);
    typedef int (*nghttp3_recv_settings)(nghttp3_conn *conn, const nghttp3_settings *settings, void *conn_user_data);
    typedef int (*nghttp3_recv_origin)(nghttp3_conn *conn, const uint8_t *origin, size_t originlen, void *conn_user_data);
    typedef int (*nghttp3_end_origin)(nghttp3_conn *conn, void *conn_user_data);
    typedef void (*nghttp3_rand)(uint8_t *dest, size_t destlen);
    typedef int (*nghttp3_recv_settings2)(nghttp3_conn *conn, const nghttp3_proto_settings *settings, void *conn_user_data);
    typedef int (*nghttp3_stream_close2)(nghttp3_conn *conn, uint32_t flags, int64_t stream_id, uint64_t rx_app_error_code, uint64_t tx_app_error_code, void *conn_user_data, void *stream_user_data);
    typedef nghttp3_ssize (*nghttp3_read_data_callback)(nghttp3_conn *conn, int64_t stream_id, nghttp3_vec *vec, size_t veccnt, uint32_t *pflags, void *conn_user_data, void *stream_user_data);

/* ---- callbacks 结构体（公开布局，20 成员） ---- */
typedef struct nghttp3_callbacks {
   nghttp3_acked_stream_data acked_stream_data;
   nghttp3_stream_close stream_close;
   nghttp3_recv_data recv_data;
   nghttp3_deferred_consume deferred_consume;
   nghttp3_begin_headers begin_headers;
   nghttp3_recv_header recv_header;
   nghttp3_end_headers end_headers;
   nghttp3_begin_headers begin_trailers;
   nghttp3_recv_header recv_trailer;
   nghttp3_end_headers end_trailers;
   nghttp3_stop_sending stop_sending;
   nghttp3_end_stream end_stream;
   nghttp3_reset_stream reset_stream;
   nghttp3_shutdown shutdown;
   nghttp3_recv_settings recv_settings;
   nghttp3_recv_origin recv_origin;
   nghttp3_end_origin end_origin;
   nghttp3_rand rand;
   nghttp3_recv_settings2 recv_settings2;
   nghttp3_stream_close2 stream_close2;
} nghttp3_callbacks;

typedef struct {
   nghttp3_read_data_callback read_data;
} nghttp3_data_reader;

/* ---- versioned API 常量（头文件核对：V4/V4/V1） ---- */
#define NGHTTP3_CALLBACKS_VERSION 4
#define NGHTTP3_SETTINGS_VERSION 4
#define NGHTTP3_PRI_VERSION 1

/* ---- 常用错误码 ---- */
#define NGHTTP3_ERR_INVALID_ARGUMENT -101
#define NGHTTP3_ERR_MALFORMED_HTTP_HEADER -105
#define NGHTTP3_ERR_FATAL -900
#define NGHTTP3_ERR_NOMEM -901

/* ============================================================
* 全局状态
* ============================================================ */
static lib_handle_t nghttp3_handle = NULL;
static int nghttp3_init_state = 0;

/* ============================================================
* 库加载：按优先级构造候选路径
* ============================================================ */
static int nghttp3_do_load(const char *library_path) {
   if (nghttp3_handle) return 0;

   static char bufs[64][1024];
   const char *candidates[64];
   int n = 0;

   /* 1. 显式传入的路径 */
   if (library_path && *library_path) {
       snprintf(bufs[n], sizeof(bufs[0]), "%s", library_path);
       candidates[n] = bufs[n]; n++;
   }

   /* 2. 项目专属环境变量 */
   const char *explicit_lib = getenv("NGHTTP3_DYN_LIB");
   if (explicit_lib && *explicit_lib) {
       snprintf(bufs[n], sizeof(bufs[0]), "%s", explicit_lib);
       candidates[n] = bufs[n]; n++;
   }

   /* 3. 系统搜索路径环境变量中的目录 */
   const char *env_path = getenv(
#ifdef _WIN32
       "PATH"
#elif defined(__APPLE__)
       "DYLD_LIBRARY_PATH"
#else
       "LD_LIBRARY_PATH"
#endif
   );
   if (env_path && *env_path) {
       char *copy = strdup(env_path);
       if (copy) {
           char *p = copy;
           while (p && *p && n < 62) {
               char *sep = strchr(p, PATH_SEP);
               if (sep) *sep = 0;
               if (*p) {
                   snprintf(bufs[n], sizeof(bufs[0]), "%s/libnghttp3.so.9", p);
                   candidates[n] = bufs[n]; n++;
               }
               p = sep ? sep + 1 : NULL;
           }
           free(copy);
       }
   }

   /* 4. 默认 soname + .so */
   candidates[n++] = "libnghttp3.so.9";
   candidates[n++] = "libnghttp3.so";

   /* 5. Nix 路径 */
#ifndef _WIN32
   const char *home = getenv("HOME");
   if (home && *home) {
       snprintf(bufs[n], sizeof(bufs[0]), "%s/.nix-profile/lib/libnghttp3.so", home);
       candidates[n] = bufs[n]; n++;
   }
   candidates[n++] = "/nix/var/nix/profiles/default/lib/libnghttp3.so";
#endif

   /* 6. 平台默认路径 */
#ifdef __APPLE__
   candidates[n++] = "/opt/homebrew/lib/libnghttp3.dylib";
   candidates[n++] = "/usr/local/lib/libnghttp3.dylib";
#elif defined(_WIN32)
   candidates[n++] = "nghttp3.dll";
#else
   candidates[n++] = "/usr/local/lib/libnghttp3.so";
   candidates[n++] = "/usr/lib/libnghttp3.so";
#endif

   int i;
   for (i = 0; i < n; i++) {
       nghttp3_handle = load_lib(candidates[i]);
       if (nghttp3_handle) break;
   }
   if (!nghttp3_handle) {
       fprintf(stderr, "nghttp3_dyn: cannot load libnghttp3 (%s)\n", lib_error());
       return -1;
   }

   /* 验证锚点符号 */
   if (!get_sym(nghttp3_handle, "nghttp3_strerror")) {
       fprintf(stderr, "nghttp3_dyn: loaded library does not export nghttp3_strerror\n");
       free_lib(nghttp3_handle);
       nghttp3_handle = NULL;
       return -1;
   }
   fprintf(stderr, "nghttp3_dyn: loaded libnghttp3 (1.18.0 headers; runtime via nghttp3_version)\n");
   return 0;
}

/* ============================================================
* 控制函数
* ============================================================ */
int nghttp3_dyn_libok(void) {
   if (nghttp3_init_state == 0) {
       nghttp3_init_state = (nghttp3_do_load(NULL) == 0) ? 1 : -1;
   }
   return (nghttp3_init_state == 1 && nghttp3_handle != NULL) ? 1 : 0;
}

int nghttp3_dyn_init(const char *library_path) {
   int r = nghttp3_do_load(library_path);
   if (r == 0) {
       nghttp3_init_state = 1;
       return 0;
   }
   if (nghttp3_init_state == 0) nghttp3_init_state = -1;
   return -1;
}

void nghttp3_dyn_cleanup(void) {
   /* 仅在加载失败后复位；成功加载的库不卸载（dlclose 不安全） */
   if (nghttp3_init_state == -1) {
       nghttp3_init_state = 0;
   }
   if (!nghttp3_handle) {
       nghttp3_init_state = 0;
   }
}

int nghttp3_dyn_has_symbol(const char *name) {
   if (!nghttp3_dyn_libok()) return 0;
   return get_sym(nghttp3_handle, name) != NULL;
}

/* ============================================================
* WRAP 宏：可变参数无法用宏处理（本项目两个库均无可变参数导出）
* ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                         \
   static ret (*fn) params = NULL;                                         \
   if (!fn) {                                                             \
       if (!nghttp3_dyn_libok()) return fallback;                           \
       *(void **)(&fn) = get_sym(nghttp3_handle, #name);                    \
       if (!fn) {                                                         \
           fprintf(stderr, "nghttp3_dyn: dlsym failed: %s\n", #name);       \
           return fallback;                                                \
       }                                                                  \
   }                                                                      \
   return fn args;                                                         \
}

#define WRAP_VOID(name, params, args)                                      \
void name params {                                                        \
   static void (*fn) params = NULL;                                        \
   if (!fn) {                                                             \
       if (!nghttp3_dyn_libok()) return;                                    \
       *(void **)(&fn) = get_sym(nghttp3_handle, #name);                    \
       if (!fn) {                                                         \
           fprintf(stderr, "nghttp3_dyn: dlsym failed: %s\n", #name);       \
           return;                                                         \
       }                                                                  \
   }                                                                      \
   fn args;                                                                \
}

/* ============================================================
* 导出函数封装（与 nm -D 导出一一对应）
* ============================================================ */

WRAP_VOID(nghttp3_buf_free, (nghttp3_buf *buf, const nghttp3_mem *mem), (buf, mem))
WRAP_VOID(nghttp3_buf_init, (nghttp3_buf *buf), (buf))
WRAP_RET(size_t, nghttp3_buf_left, (const nghttp3_buf *buf), (buf), 0)
WRAP_RET(size_t, nghttp3_buf_len, (const nghttp3_buf *buf), (buf), 0)
WRAP_VOID(nghttp3_buf_reset, (nghttp3_buf *buf), (buf))
WRAP_RET(int, nghttp3_check_header_name, (const uint8_t *name, size_t len), (name, len), -1)
WRAP_RET(int, nghttp3_check_header_value, (const uint8_t *value, size_t len), (value, len), -1)
WRAP_RET(int, nghttp3_conn_add_ack_offset, (nghttp3_conn *conn, int64_t stream_id, uint64_t n), (conn, stream_id, n), -1)
WRAP_RET(int, nghttp3_conn_add_write_offset, (nghttp3_conn *conn, int64_t stream_id, size_t n), (conn, stream_id, n), -1)
WRAP_RET(int, nghttp3_conn_bind_control_stream, (nghttp3_conn *conn, int64_t stream_id), (conn, stream_id), -1)
WRAP_RET(int, nghttp3_conn_bind_qpack_streams, (nghttp3_conn *conn, int64_t qenc_stream_id, int64_t qdec_stream_id), (conn, qenc_stream_id, qdec_stream_id), -1)
WRAP_VOID(nghttp3_conn_block_stream, (nghttp3_conn *conn, int64_t stream_id), (conn, stream_id))
WRAP_RET(int, nghttp3_conn_client_new_versioned, (nghttp3_conn **pconn, int callbacks_version, const nghttp3_callbacks *callbacks, int settings_version, const nghttp3_settings *settings, const nghttp3_mem *mem, void *conn_user_data), (pconn, callbacks_version, callbacks, settings_version, settings, mem, conn_user_data), -1)
WRAP_RET(int, nghttp3_conn_close_stream, (nghttp3_conn *conn, int64_t stream_id, uint64_t app_error_code), (conn, stream_id, app_error_code), -1)
WRAP_RET(int, nghttp3_conn_close_stream2, (nghttp3_conn *conn, uint32_t flags, int64_t stream_id, uint64_t rx_app_error_code, uint64_t tx_app_error_code), (conn, flags, stream_id, rx_app_error_code, tx_app_error_code), -1)
WRAP_VOID(nghttp3_conn_del, (nghttp3_conn *conn), (conn))
WRAP_RET(uint64_t, nghttp3_conn_get_frame_payload_left, (nghttp3_conn *conn, int64_t stream_id), (conn, stream_id), -1)
WRAP_RET(uint64_t, nghttp3_conn_get_frame_payload_left2, (const nghttp3_conn *conn, int64_t stream_id), (conn, stream_id), -1)
WRAP_RET(int, nghttp3_conn_get_stream_priority2_versioned, (const nghttp3_conn *conn, int pri_version, nghttp3_pri *dest, int64_t stream_id), (conn, pri_version, dest, stream_id), -1)
WRAP_RET(int, nghttp3_conn_get_stream_priority_versioned, (nghttp3_conn *conn, int pri_version, nghttp3_pri *dest, int64_t stream_id), (conn, pri_version, dest, stream_id), -1)
WRAP_RET(void *, nghttp3_conn_get_stream_user_data, (const nghttp3_conn *conn, int64_t stream_id), (conn, stream_id), NULL)
WRAP_RET(int, nghttp3_conn_is_drained, (nghttp3_conn *conn), (conn), -1)
WRAP_RET(int, nghttp3_conn_is_drained2, (const nghttp3_conn *conn), (conn), -1)
WRAP_RET(int, nghttp3_conn_is_stream_flushed, (const nghttp3_conn *conn, int64_t stream_id), (conn, stream_id), -1)
WRAP_RET(int, nghttp3_conn_is_stream_writable, (nghttp3_conn *conn, int64_t stream_id), (conn, stream_id), -1)
WRAP_RET(int, nghttp3_conn_is_stream_writable2, (const nghttp3_conn *conn, int64_t stream_id), (conn, stream_id), -1)
WRAP_RET(nghttp3_ssize, nghttp3_conn_read_stream, (nghttp3_conn *conn, int64_t stream_id, const uint8_t *src, size_t srclen, int fin), (conn, stream_id, src, srclen, fin), -1)
WRAP_RET(nghttp3_ssize, nghttp3_conn_read_stream2, (nghttp3_conn *conn, int64_t stream_id, const uint8_t *src, size_t srclen, int fin, nghttp3_tstamp ts), (conn, stream_id, src, srclen, fin, ts), -1)
WRAP_RET(int, nghttp3_conn_resume_stream, (nghttp3_conn *conn, int64_t stream_id), (conn, stream_id), -1)
WRAP_RET(int, nghttp3_conn_server_new_versioned, (nghttp3_conn **pconn, int callbacks_version, const nghttp3_callbacks *callbacks, int settings_version, const nghttp3_settings *settings, const nghttp3_mem *mem, void *conn_user_data), (pconn, callbacks_version, callbacks, settings_version, settings, mem, conn_user_data), -1)
WRAP_RET(int, nghttp3_conn_set_client_stream_priority, (nghttp3_conn *conn, int64_t stream_id, const uint8_t *data, size_t datalen), (conn, stream_id, data, datalen), -1)
WRAP_VOID(nghttp3_conn_set_max_client_streams_bidi, (nghttp3_conn *conn, uint64_t max_streams), (conn, max_streams))
WRAP_VOID(nghttp3_conn_set_max_concurrent_streams, (nghttp3_conn *conn, size_t max_concurrent_streams), (conn, max_concurrent_streams))
WRAP_RET(int, nghttp3_conn_set_server_stream_priority_versioned, (nghttp3_conn *conn, int64_t stream_id, int pri_version, const nghttp3_pri *pri), (conn, stream_id, pri_version, pri), -1)
WRAP_RET(int, nghttp3_conn_set_stream_user_data, (nghttp3_conn *conn, int64_t stream_id, void *stream_user_data), (conn, stream_id, stream_user_data), -1)
WRAP_RET(int, nghttp3_conn_shutdown, (nghttp3_conn *conn), (conn), -1)
WRAP_RET(int, nghttp3_conn_shutdown_stream_read, (nghttp3_conn *conn, int64_t stream_id), (conn, stream_id), -1)
WRAP_VOID(nghttp3_conn_shutdown_stream_write, (nghttp3_conn *conn, int64_t stream_id), (conn, stream_id))
WRAP_RET(int, nghttp3_conn_submit_info, (nghttp3_conn *conn, int64_t stream_id, const nghttp3_nv *nva, size_t nvlen), (conn, stream_id, nva, nvlen), -1)
WRAP_RET(int, nghttp3_conn_submit_request, (nghttp3_conn *conn, int64_t stream_id, const nghttp3_nv *nva, size_t nvlen, const nghttp3_data_reader *dr, void *stream_user_data), (conn, stream_id, nva, nvlen, dr, stream_user_data), -1)
WRAP_RET(int, nghttp3_conn_submit_response, (nghttp3_conn *conn, int64_t stream_id, const nghttp3_nv *nva, size_t nvlen, const nghttp3_data_reader *dr), (conn, stream_id, nva, nvlen, dr), -1)
WRAP_RET(int, nghttp3_conn_submit_shutdown_notice, (nghttp3_conn *conn), (conn), -1)
WRAP_RET(int, nghttp3_conn_submit_trailers, (nghttp3_conn *conn, int64_t stream_id, const nghttp3_nv *nva, size_t nvlen), (conn, stream_id, nva, nvlen), -1)
WRAP_RET(int, nghttp3_conn_unblock_stream, (nghttp3_conn *conn, int64_t stream_id), (conn, stream_id), -1)
WRAP_RET(int, nghttp3_conn_update_ack_offset, (nghttp3_conn *conn, int64_t stream_id, uint64_t offset), (conn, stream_id, offset), -1)
WRAP_RET(nghttp3_ssize, nghttp3_conn_writev_stream, (nghttp3_conn *conn, int64_t *pstream_id, int *pfin, nghttp3_vec *vec, size_t veccnt), (conn, pstream_id, pfin, vec, veccnt), -1)
WRAP_RET(uint64_t, nghttp3_err_infer_quic_app_error_code, (int liberr), (liberr), -1)
WRAP_RET(int, nghttp3_err_is_fatal, (int liberr), (liberr), -1)
WRAP_RET(const uint8_t *, nghttp3_get_uvarint, (uint64_t *dest, const uint8_t *p), (dest, p), NULL)
WRAP_RET(size_t, nghttp3_get_uvarintlen, (const uint8_t *p), (p), 0)
WRAP_RET(const uint8_t *, nghttp3_get_varint, (int64_t *dest, const uint8_t *p), (dest, p), NULL)
WRAP_RET(const nghttp3_mem *, nghttp3_mem_default, (void), (), NULL)
WRAP_RET(int, nghttp3_pri_parse_priority_versioned, (int pri_version, nghttp3_pri *dest, const uint8_t *value, size_t len), (pri_version, dest, value, len), -1)
WRAP_RET(uint8_t *, nghttp3_put_uvarint, (uint8_t *p, uint64_t n), (p, n), NULL)
WRAP_RET(size_t, nghttp3_put_uvarintlen, (uint64_t n), (n), 0)
WRAP_RET(int, nghttp3_qpack_decoder_cancel_stream, (nghttp3_qpack_decoder *decoder, int64_t stream_id), (decoder, stream_id), -1)
WRAP_VOID(nghttp3_qpack_decoder_del, (nghttp3_qpack_decoder *decoder), (decoder))
WRAP_RET(size_t, nghttp3_qpack_decoder_get_decoder_streamlen, (nghttp3_qpack_decoder *decoder), (decoder), 0)
WRAP_RET(size_t, nghttp3_qpack_decoder_get_decoder_streamlen2, (const nghttp3_qpack_decoder *decoder), (decoder), 0)
WRAP_RET(uint64_t, nghttp3_qpack_decoder_get_icnt, (const nghttp3_qpack_decoder *decoder), (decoder), -1)
WRAP_RET(int, nghttp3_qpack_decoder_new, (nghttp3_qpack_decoder **pdecoder, size_t hard_max_dtable_capacity, size_t max_blocked_streams, const nghttp3_mem *mem), (pdecoder, hard_max_dtable_capacity, max_blocked_streams, mem), -1)
WRAP_RET(nghttp3_ssize, nghttp3_qpack_decoder_read_encoder, (nghttp3_qpack_decoder *decoder, const uint8_t *src, size_t srclen), (decoder, src, srclen), -1)
WRAP_RET(nghttp3_ssize, nghttp3_qpack_decoder_read_request, (nghttp3_qpack_decoder *decoder, nghttp3_qpack_stream_context *sctx, nghttp3_qpack_nv *nv, uint8_t *pflags, const uint8_t *src, size_t srclen, int fin), (decoder, sctx, nv, pflags, src, srclen, fin), -1)
WRAP_VOID(nghttp3_qpack_decoder_set_max_concurrent_streams, (nghttp3_qpack_decoder *decoder, size_t max_concurrent_streams), (decoder, max_concurrent_streams))
WRAP_RET(int, nghttp3_qpack_decoder_set_max_dtable_capacity, (nghttp3_qpack_decoder *decoder, size_t max_dtable_capacity), (decoder, max_dtable_capacity), -1)
WRAP_VOID(nghttp3_qpack_decoder_write_decoder, (nghttp3_qpack_decoder *decoder, nghttp3_buf *dbuf), (decoder, dbuf))
WRAP_VOID(nghttp3_qpack_encoder_ack_everything, (nghttp3_qpack_encoder *encoder), (encoder))
WRAP_VOID(nghttp3_qpack_encoder_del, (nghttp3_qpack_encoder *encoder), (encoder))
WRAP_RET(int, nghttp3_qpack_encoder_encode, (nghttp3_qpack_encoder *encoder, nghttp3_buf *pbuf, nghttp3_buf *rbuf, nghttp3_buf *ebuf, int64_t stream_id, const nghttp3_nv *nva, size_t nvlen), (encoder, pbuf, rbuf, ebuf, stream_id, nva, nvlen), -1)
WRAP_RET(size_t, nghttp3_qpack_encoder_get_num_blocked_streams, (nghttp3_qpack_encoder *encoder), (encoder), 0)
WRAP_RET(size_t, nghttp3_qpack_encoder_get_num_blocked_streams2, (const nghttp3_qpack_encoder *encoder), (encoder), 0)
WRAP_RET(int, nghttp3_qpack_encoder_new, (nghttp3_qpack_encoder **pencoder, size_t hard_max_dtable_capacity, const nghttp3_mem *mem), (pencoder, hard_max_dtable_capacity, mem), -1)
WRAP_RET(int, nghttp3_qpack_encoder_new2, (nghttp3_qpack_encoder **pencoder, size_t hard_max_dtable_capacity, uint64_t seed, const nghttp3_mem *mem), (pencoder, hard_max_dtable_capacity, seed, mem), -1)
WRAP_RET(nghttp3_ssize, nghttp3_qpack_encoder_read_decoder, (nghttp3_qpack_encoder *encoder, const uint8_t *src, size_t srclen), (encoder, src, srclen), -1)
WRAP_VOID(nghttp3_qpack_encoder_set_indexing_strat, (nghttp3_qpack_encoder *encoder, nghttp3_qpack_indexing_strat strat), (encoder, strat))
WRAP_VOID(nghttp3_qpack_encoder_set_max_blocked_streams, (nghttp3_qpack_encoder *encoder, size_t max_blocked_streams), (encoder, max_blocked_streams))
WRAP_VOID(nghttp3_qpack_encoder_set_max_dtable_capacity, (nghttp3_qpack_encoder *encoder, size_t max_dtable_capacity), (encoder, max_dtable_capacity))
WRAP_VOID(nghttp3_qpack_stream_context_del, (nghttp3_qpack_stream_context *sctx), (sctx))
WRAP_RET(uint64_t, nghttp3_qpack_stream_context_get_ricnt, (nghttp3_qpack_stream_context *sctx), (sctx), -1)
WRAP_RET(uint64_t, nghttp3_qpack_stream_context_get_ricnt2, (const nghttp3_qpack_stream_context *sctx), (sctx), -1)
WRAP_RET(int, nghttp3_qpack_stream_context_new, (nghttp3_qpack_stream_context **psctx, int64_t stream_id, const nghttp3_mem *mem), (psctx, stream_id, mem), -1)
WRAP_VOID(nghttp3_qpack_stream_context_reset, (nghttp3_qpack_stream_context *sctx), (sctx))
WRAP_VOID(nghttp3_rcbuf_decref, (nghttp3_rcbuf *rcbuf), (rcbuf))
WRAP_RET(nghttp3_vec, nghttp3_rcbuf_get_buf, (const nghttp3_rcbuf *rcbuf), (rcbuf), ((nghttp3_vec){NULL, 0}))
WRAP_VOID(nghttp3_rcbuf_incref, (nghttp3_rcbuf *rcbuf), (rcbuf))
WRAP_RET(int, nghttp3_rcbuf_is_static, (const nghttp3_rcbuf *rcbuf), (rcbuf), -1)
WRAP_VOID(nghttp3_set_debug_vprintf_callback, (nghttp3_debug_vprintf_callback debug_vprintf_callback), (debug_vprintf_callback))
WRAP_VOID(nghttp3_settings_default_versioned, (int settings_version, nghttp3_settings *settings), (settings_version, settings))
WRAP_RET(const char *, nghttp3_strerror, (int liberr), (liberr), NULL)
WRAP_RET(uint64_t, nghttp3_vec_len, (const nghttp3_vec *vec, size_t cnt), (vec, cnt), -1)
WRAP_RET(const nghttp3_info *, nghttp3_version, (int least_version), (least_version), NULL)

/* ---- 便捷封装：versioned API + 头文件常量注入 ---- */
int nghttp3_conn_client_new(nghttp3_conn **pconn, const nghttp3_callbacks *cb,
                            const nghttp3_settings *settings,
                            const nghttp3_mem *mem, void *conn_user_data) {
    return nghttp3_conn_client_new_versioned(pconn, NGHTTP3_CALLBACKS_VERSION,
                                             cb, NGHTTP3_SETTINGS_VERSION,
                                             settings, mem, conn_user_data);
}

int nghttp3_conn_server_new(nghttp3_conn **pconn, const nghttp3_callbacks *cb,
                            const nghttp3_settings *settings,
                            const nghttp3_mem *mem, void *conn_user_data) {
    return nghttp3_conn_server_new_versioned(pconn, NGHTTP3_CALLBACKS_VERSION,
                                             cb, NGHTTP3_SETTINGS_VERSION,
                                             settings, mem, conn_user_data);
}

int nghttp3_pri_parse_priority(nghttp3_pri *dest, const uint8_t *value,
                               size_t len) {
    return nghttp3_pri_parse_priority_versioned(NGHTTP3_PRI_VERSION, dest,
                                                value, len);
}

void nghttp3_settings_default(nghttp3_settings *settings) {
    nghttp3_settings_default_versioned(NGHTTP3_SETTINGS_VERSION, settings);
}
