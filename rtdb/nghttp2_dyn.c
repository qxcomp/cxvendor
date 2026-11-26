/* ============================================================
* nghttp2_dyn.c - type safe dynamic lib load for libnghttp2
* ============================================================
* 设计要点：
*   1. 单文件、无 .h、无 wrapper 层；链接期不需要 -lnghttp2。
*   2. 封装函数名与原版 libnghttp2 导出符号一致（按 nm -D 核对）。
*   3. 每个函数首次调用时 dlsym 缓存；失败打印 stderr 并返回安全默认值。
*   4. 公开结构体/枚举/回调 typedef 按 1.70.0 头文件核对。
*   5. 加载顺序：显式路径 > NGHTTP2_DYN_LIB > LD_LIBRARY_PATH >
*      默认 soname > .so > Nix > 平台默认。
*   6. 加载成功后不卸载（dlclose 不安全）；失败后 cleanup 复位。
*   7. 纯 C 库：RTLD_LAZY | RTLD_LOCAL。
*   8. 头文件仅用于开发期核对，本文件不 include 任何库头文件。
*
* 使用：
*   nghttp2_dyn_init(NULL);
*   ... 原版 API 直接调用 ...
*   nghttp2_dyn_cleanup();    // 仅复位加载状态，不卸载库
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

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<nghttp2/nghttp2.h>)
#    include <nghttp2/nghttp2.h>
#    define NGHTTP2_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef NGHTTP2_DYN_HAVE_HEADER
/* ============================================================
* 类型定义（按 nghttp2.h 核对的公开 ABI）
* ============================================================ */
typedef int64_t nghttp2_ssize;

typedef struct nghttp2_session nghttp2_session;
typedef struct nghttp2_stream nghttp2_stream;
typedef struct nghttp2_rcbuf nghttp2_rcbuf;
typedef struct nghttp2_frame nghttp2_frame;
typedef struct nghttp2_hd_deflater nghttp2_hd_deflater;
typedef struct nghttp2_hd_inflater nghttp2_hd_inflater;
typedef struct nghttp2_session_callbacks nghttp2_session_callbacks;
typedef struct nghttp2_option nghttp2_option;

typedef struct {
   int age;
   int version_num;
   const char *version_str;
   const char *proto_str;
} nghttp2_info;

typedef struct {
   uint8_t *base;
   size_t len;
} nghttp2_vec;

typedef struct {
   uint8_t *name;
   uint8_t *value;
   size_t namelen;
   size_t valuelen;
   uint8_t flags;
} nghttp2_nv;

typedef struct {
   size_t length;
   int32_t stream_id;
   uint8_t type;
   uint8_t flags;
   uint8_t reserved;
} nghttp2_frame_hd;

typedef union {
   int fd;
   void *ptr;
} nghttp2_data_source;

typedef struct {
   int32_t stream_id;
   int32_t weight;
   uint8_t exclusive;
} nghttp2_priority_spec;

typedef struct {
   uint32_t urgency;
   int inc;
} nghttp2_extpri;

typedef struct {
   uint8_t *origin;
   size_t origin_len;
} nghttp2_origin_entry;

typedef struct {
   int32_t settings_id;
   uint32_t value;
} nghttp2_settings_entry;

typedef void *(*nghttp2_malloc_t)(size_t size, void *mem_user_data);
typedef void (*nghttp2_free_t)(void *ptr, void *mem_user_data);
typedef void *(*nghttp2_calloc_t)(size_t nmemb, size_t size, void *mem_user_data);
typedef void *(*nghttp2_realloc_t)(void *ptr, void *mem_user_data, size_t size);

typedef struct {
   void *mem_user_data;
   nghttp2_malloc_t malloc;
   nghttp2_free_t free;
   nghttp2_calloc_t calloc;
   nghttp2_realloc_t realloc;
} nghttp2_mem;


typedef enum {
   NGHTTP2_SETTINGS_HEADER_TABLE_SIZE = 0x01,
   NGHTTP2_SETTINGS_ENABLE_PUSH = 0x02,
   NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS = 0x03,
   NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE = 0x04,
   NGHTTP2_SETTINGS_MAX_FRAME_SIZE = 0x05,
   NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE = 0x06,
   NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL = 0x08,
   NGHTTP2_SETTINGS_NO_RFC7540_PRIORITIES = 0x09
} nghttp2_settings_id;

typedef enum {
   NGHTTP2_STREAM_STATE_IDLE = 1,
   NGHTTP2_STREAM_STATE_OPEN,
   NGHTTP2_STREAM_STATE_RESERVED_LOCAL,
   NGHTTP2_STREAM_STATE_RESERVED_REMOTE,
   NGHTTP2_STREAM_STATE_HALF_CLOSED_LOCAL,
   NGHTTP2_STREAM_STATE_HALF_CLOSED_REMOTE,
   NGHTTP2_STREAM_STATE_CLOSED
} nghttp2_stream_proto_state;

typedef enum {
   NGHTTP2_NV_FLAG_NONE = 0,
   NGHTTP2_NV_FLAG_NO_INDEX = 0x01,
   NGHTTP2_NV_FLAG_NO_COPY_NAME = 0x02,
   NGHTTP2_NV_FLAG_NO_COPY_VALUE = 0x04
} nghttp2_nv_flag;

typedef enum {
   NGHTTP2_HD_INFLATE_NONE = 0,
   NGHTTP2_HD_INFLATE_FINAL = 0x01,
   NGHTTP2_HD_INFLATE_EMIT = 0x02
} nghttp2_hd_inflate_flag;

/* ---- 回调 typedef（ABI 透传：仅函数指针，库按真实签名调用） ---- */
    typedef ssize_t (*nghttp2_data_source_read_callback)(nghttp2_session *session, int32_t stream_id, uint8_t *buf, size_t length, uint32_t *data_flags, nghttp2_data_source *source, void *user_data);
    typedef nghttp2_ssize (*nghttp2_data_source_read_callback2)(nghttp2_session *session, int32_t stream_id, uint8_t *buf, size_t length, uint32_t *data_flags, nghttp2_data_source *source, void *user_data);
    typedef ssize_t (*nghttp2_send_callback)(nghttp2_session *session, const uint8_t *data, size_t length, int flags, void *user_data);
    typedef nghttp2_ssize (*nghttp2_send_callback2)(nghttp2_session *session, const uint8_t *data, size_t length, int flags, void *user_data);
    typedef int (*nghttp2_send_data_callback)(nghttp2_session *session, nghttp2_frame *frame, const uint8_t *framehd, size_t length, nghttp2_data_source *source, void *user_data);
    typedef ssize_t (*nghttp2_recv_callback)(nghttp2_session *session, uint8_t *buf, size_t length, int flags, void *user_data);
    typedef nghttp2_ssize (*nghttp2_recv_callback2)(nghttp2_session *session, uint8_t *buf, size_t length, int flags, void *user_data);
    typedef int (*nghttp2_on_frame_recv_callback)(nghttp2_session *session, const nghttp2_frame *frame, void *user_data);
    typedef int (*nghttp2_on_invalid_frame_recv_callback)(nghttp2_session *session, const nghttp2_frame *frame, int lib_error_code, void *user_data);
    typedef int (*nghttp2_on_data_chunk_recv_callback)(nghttp2_session *session, uint8_t flags, int32_t stream_id, const uint8_t *data, size_t len, void *user_data);
    typedef int (*nghttp2_before_frame_send_callback)(nghttp2_session *session, const nghttp2_frame *frame, void *user_data);
    typedef int (*nghttp2_on_frame_send_callback)(nghttp2_session *session, const nghttp2_frame *frame, void *user_data);
    typedef int (*nghttp2_on_frame_not_send_callback)(nghttp2_session *session, const nghttp2_frame *frame, int lib_error_code, void *user_data);
    typedef int (*nghttp2_on_stream_close_callback)(nghttp2_session *session, int32_t stream_id, uint32_t error_code, void *user_data);
    typedef int (*nghttp2_on_begin_headers_callback)(nghttp2_session *session, const nghttp2_frame *frame, void *user_data);
    typedef int (*nghttp2_on_header_callback)(nghttp2_session *session, const nghttp2_frame *frame, const uint8_t *name, size_t namelen, const uint8_t *value, size_t valuelen, uint8_t flags, void *user_data);
    typedef int (*nghttp2_on_header_callback2)(nghttp2_session *session, const nghttp2_frame *frame, nghttp2_rcbuf *name, nghttp2_rcbuf *value, uint8_t flags, void *user_data);
    typedef int (*nghttp2_on_invalid_header_callback)(nghttp2_session *session, const nghttp2_frame *frame, const uint8_t *name, size_t namelen, const uint8_t *value, size_t valuelen, uint8_t flags, void *user_data);
    typedef int (*nghttp2_on_invalid_header_callback2)(nghttp2_session *session, const nghttp2_frame *frame, nghttp2_rcbuf *name, nghttp2_rcbuf *value, uint8_t flags, void *user_data);
    typedef ssize_t (*nghttp2_select_padding_callback)(nghttp2_session *session, const nghttp2_frame *frame, size_t max_payloadlen, void *user_data);
    typedef nghttp2_ssize (*nghttp2_select_padding_callback2)(nghttp2_session *session, const nghttp2_frame *frame, size_t max_payloadlen, void *user_data);
    typedef ssize_t (*nghttp2_data_source_read_length_callback)(nghttp2_session *session, uint8_t frame_type, int32_t stream_id, int32_t session_remote_window_size, int32_t stream_remote_window_size, uint32_t remote_max_frame_size, void *user_data);
    typedef nghttp2_ssize (*nghttp2_data_source_read_length_callback2)(nghttp2_session *session, uint8_t frame_type, int32_t stream_id, int32_t session_remote_window_size, int32_t stream_remote_window_size, uint32_t remote_max_frame_size, void *user_data);
    typedef int (*nghttp2_on_begin_frame_callback)(nghttp2_session *session, const nghttp2_frame_hd *hd, void *user_data);
    typedef int (*nghttp2_on_extension_chunk_recv_callback)(nghttp2_session *session, const nghttp2_frame_hd *hd, const uint8_t *data, size_t len, void *user_data);
    typedef int (*nghttp2_unpack_extension_callback)(nghttp2_session *session, void **payload, const nghttp2_frame_hd *hd, void *user_data);
    typedef ssize_t (*nghttp2_pack_extension_callback)(nghttp2_session *session, uint8_t *buf, size_t len, const nghttp2_frame *frame, void *user_data);
    typedef nghttp2_ssize (*nghttp2_pack_extension_callback2)(nghttp2_session *session, uint8_t *buf, size_t len, const nghttp2_frame *frame, void *user_data);
    typedef int (*nghttp2_error_callback)(nghttp2_session *session, const char *msg, size_t len, void *user_data);
    typedef int (*nghttp2_error_callback2)(nghttp2_session *session, int lib_error_code, const char *msg, size_t len, void *user_data);
    typedef void (*nghttp2_rand_callback)(uint8_t *dest, size_t destlen);
    typedef void (*nghttp2_free)(void *ptr, void *mem_user_data);
    typedef void (*nghttp2_debug_vprintf_callback)(const char *format, va_list args);


typedef struct {
   nghttp2_data_source source;
   ssize_t (*read_callback)(nghttp2_session *, int32_t, uint8_t *, size_t,
                            uint32_t *, nghttp2_data_source *, void *);
} nghttp2_data_provider;

typedef struct {
   nghttp2_data_source source;
   nghttp2_ssize (*read_callback)(nghttp2_session *, int32_t, uint8_t *,
                                  size_t, uint32_t *, nghttp2_data_source *,
                                  void *);
} nghttp2_data_provider2;
/* 注意：nghttp2.h 中另有 4 个内存分配器 typedef（nghttp2_malloc/free/
* calloc/realloc），其参数为 mem_user_data；上面 WRAP 清单中的函数签名
* 引用的是 nghttp2_mem 结构体字段类型，与上面 *_t 一致。 */

#endif /* NGHTTP2_DYN_HAVE_HEADER */

/* ============================================================
* 全局状态
* ============================================================ */
static lib_handle_t nghttp2_handle = NULL;
static int nghttp2_init_state = 0;

/* ============================================================
* 库加载：按优先级构造候选路径
* ============================================================ */
static int nghttp2_do_load(const char *library_path) {
   if (nghttp2_handle) return 0;

   static char bufs[64][1024];
   const char *candidates[64];
   int n = 0;

   /* 1. 显式传入的路径 */
   if (library_path && *library_path) {
       snprintf(bufs[n], sizeof(bufs[0]), "%s", library_path);
       candidates[n] = bufs[n]; n++;
   }

   /* 2. 项目专属环境变量 */
   const char *explicit_lib = getenv("NGHTTP2_DYN_LIB");
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
                   snprintf(bufs[n], sizeof(bufs[0]), "%s/libnghttp2.so.14", p);
                   candidates[n] = bufs[n]; n++;
               }
               p = sep ? sep + 1 : NULL;
           }
           free(copy);
       }
   }

   /* 4. 默认 soname + .so */
   candidates[n++] = "libnghttp2.so.14";
   candidates[n++] = "libnghttp2.so";

   /* 5. Nix 路径 */
#ifndef _WIN32
   const char *home = getenv("HOME");
   if (home && *home) {
       snprintf(bufs[n], sizeof(bufs[0]), "%s/.nix-profile/lib/libnghttp2.so", home);
       candidates[n] = bufs[n]; n++;
   }
   candidates[n++] = "/nix/var/nix/profiles/default/lib/libnghttp2.so";
#endif

   /* 6. 平台默认路径 */
#ifdef __APPLE__
   candidates[n++] = "/opt/homebrew/lib/libnghttp2.dylib";
   candidates[n++] = "/usr/local/lib/libnghttp2.dylib";
#elif defined(_WIN32)
   candidates[n++] = "nghttp2.dll";
#else
   candidates[n++] = "/usr/local/lib/libnghttp2.so";
   candidates[n++] = "/usr/lib/libnghttp2.so";
#endif

   int i;
   for (i = 0; i < n; i++) {
       nghttp2_handle = load_lib(candidates[i]);
       if (nghttp2_handle) break;
   }
   if (!nghttp2_handle) {
       fprintf(stderr, "nghttp2_dyn: cannot load libnghttp2 (%s)\n", lib_error());
       return -1;
   }

   /* 验证锚点符号 */
   if (!get_sym(nghttp2_handle, "nghttp2_session_mem_recv")) {
       fprintf(stderr, "nghttp2_dyn: loaded library does not export nghttp2_session_mem_recv\n");
       free_lib(nghttp2_handle);
       nghttp2_handle = NULL;
       return -1;
   }
   fprintf(stderr, "nghttp2_dyn: loaded libnghttp2 (1.70.0 headers; runtime via nghttp2_version)\n");
   return 0;
}

/* ============================================================
* 控制函数
* ============================================================ */
int nghttp2_dyn_libok(void) {
   if (nghttp2_init_state == 0) {
       nghttp2_init_state = (nghttp2_do_load(NULL) == 0) ? 1 : -1;
   }
   return (nghttp2_init_state == 1 && nghttp2_handle != NULL) ? 1 : 0;
}

int nghttp2_dyn_init(const char *library_path) {
   int r = nghttp2_do_load(library_path);
   if (r == 0) {
       nghttp2_init_state = 1;
       return 0;
   }
   if (nghttp2_init_state == 0) nghttp2_init_state = -1;
   return -1;
}

void nghttp2_dyn_cleanup(void) {
   /* 仅在加载失败后复位；成功加载的库不卸载（dlclose 不安全） */
   if (nghttp2_init_state == -1) {
       nghttp2_init_state = 0;
   }
   if (!nghttp2_handle) {
       nghttp2_init_state = 0;
   }
}

int nghttp2_dyn_has_symbol(const char *name) {
   if (!nghttp2_dyn_libok()) return 0;
   return get_sym(nghttp2_handle, name) != NULL;
}

/* ============================================================
* WRAP 宏：可变参数无法用宏处理（本项目两个库均无可变参数导出）
* ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                         \
   static ret (*fn) params = NULL;                                         \
   if (!fn) {                                                             \
       if (!nghttp2_dyn_libok()) return fallback;                           \
       *(void **)(&fn) = get_sym(nghttp2_handle, #name);                    \
       if (!fn) {                                                         \
           fprintf(stderr, "nghttp2_dyn: dlsym failed: %s\n", #name);       \
           return fallback;                                                \
       }                                                                  \
   }                                                                      \
   return fn args;                                                         \
}

#define WRAP_VOID(name, params, args)                                      \
void name params {                                                        \
   static void (*fn) params = NULL;                                        \
   if (!fn) {                                                             \
       if (!nghttp2_dyn_libok()) return;                                    \
       *(void **)(&fn) = get_sym(nghttp2_handle, #name);                    \
       if (!fn) {                                                         \
           fprintf(stderr, "nghttp2_dyn: dlsym failed: %s\n", #name);       \
           return;                                                         \
       }                                                                  \
   }                                                                      \
   fn args;                                                                \
}

/* ============================================================
* 导出函数封装（与 nm -D 导出一一对应）
* ============================================================ */

WRAP_RET(int, nghttp2_check_authority, (const uint8_t *value, size_t len), (value, len), -1)
WRAP_RET(int, nghttp2_check_header_name, (const uint8_t *name, size_t len), (name, len), -1)
WRAP_RET(int, nghttp2_check_header_value, (const uint8_t *value, size_t len), (value, len), -1)
WRAP_RET(int, nghttp2_check_header_value_rfc9113, (const uint8_t *value, size_t len), (value, len), -1)
WRAP_RET(int, nghttp2_check_method, (const uint8_t *value, size_t len), (value, len), -1)
WRAP_RET(int, nghttp2_check_path, (const uint8_t *value, size_t len), (value, len), -1)
WRAP_RET(int, nghttp2_extpri_parse_priority, (nghttp2_extpri *extpri, const uint8_t *value, size_t len), (extpri, value, len), -1)
WRAP_RET(size_t, nghttp2_hd_deflate_bound, (nghttp2_hd_deflater *deflater, const nghttp2_nv *nva, size_t nvlen), (deflater, nva, nvlen), 0)
WRAP_RET(int, nghttp2_hd_deflate_change_table_size, (nghttp2_hd_deflater *deflater, size_t settings_max_dynamic_table_size), (deflater, settings_max_dynamic_table_size), -1)
WRAP_VOID(nghttp2_hd_deflate_del, (nghttp2_hd_deflater *deflater), (deflater))
WRAP_RET(size_t, nghttp2_hd_deflate_get_dynamic_table_size, (nghttp2_hd_deflater *deflater), (deflater), 0)
WRAP_RET(size_t, nghttp2_hd_deflate_get_max_dynamic_table_size, (nghttp2_hd_deflater *deflater), (deflater), 0)
WRAP_RET(size_t, nghttp2_hd_deflate_get_num_table_entries, (nghttp2_hd_deflater *deflater), (deflater), 0)
WRAP_RET(const nghttp2_nv *, nghttp2_hd_deflate_get_table_entry, (nghttp2_hd_deflater *deflater, size_t idx), (deflater, idx), NULL)
WRAP_RET(ssize_t, nghttp2_hd_deflate_hd, (nghttp2_hd_deflater *deflater, uint8_t *buf, size_t buflen, const nghttp2_nv *nva, size_t nvlen), (deflater, buf, buflen, nva, nvlen), -1)
WRAP_RET(nghttp2_ssize, nghttp2_hd_deflate_hd2, (nghttp2_hd_deflater *deflater, uint8_t *buf, size_t buflen, const nghttp2_nv *nva, size_t nvlen), (deflater, buf, buflen, nva, nvlen), -1)
WRAP_RET(ssize_t, nghttp2_hd_deflate_hd_vec, (nghttp2_hd_deflater *deflater, const nghttp2_vec *vec, size_t veclen, const nghttp2_nv *nva, size_t nvlen), (deflater, vec, veclen, nva, nvlen), -1)
WRAP_RET(nghttp2_ssize, nghttp2_hd_deflate_hd_vec2, (nghttp2_hd_deflater *deflater, const nghttp2_vec *vec, size_t veclen, const nghttp2_nv *nva, size_t nvlen), (deflater, vec, veclen, nva, nvlen), -1)
WRAP_RET(int, nghttp2_hd_deflate_new, (nghttp2_hd_deflater **deflater_ptr, size_t max_deflate_dynamic_table_size), (deflater_ptr, max_deflate_dynamic_table_size), -1)
WRAP_RET(int, nghttp2_hd_deflate_new2, (nghttp2_hd_deflater **deflater_ptr, size_t max_deflate_dynamic_table_size, nghttp2_mem *mem), (deflater_ptr, max_deflate_dynamic_table_size, mem), -1)
WRAP_RET(int, nghttp2_hd_inflate_change_table_size, (nghttp2_hd_inflater *inflater, size_t settings_max_dynamic_table_size), (inflater, settings_max_dynamic_table_size), -1)
WRAP_VOID(nghttp2_hd_inflate_del, (nghttp2_hd_inflater *inflater), (inflater))
WRAP_RET(int, nghttp2_hd_inflate_end_headers, (nghttp2_hd_inflater *inflater), (inflater), -1)
WRAP_RET(size_t, nghttp2_hd_inflate_get_dynamic_table_size, (nghttp2_hd_inflater *inflater), (inflater), 0)
WRAP_RET(size_t, nghttp2_hd_inflate_get_max_dynamic_table_size, (nghttp2_hd_inflater *inflater), (inflater), 0)
WRAP_RET(size_t, nghttp2_hd_inflate_get_num_table_entries, (nghttp2_hd_inflater *inflater), (inflater), 0)
WRAP_RET(const nghttp2_nv *, nghttp2_hd_inflate_get_table_entry, (nghttp2_hd_inflater *inflater, size_t idx), (inflater, idx), NULL)
WRAP_RET(ssize_t, nghttp2_hd_inflate_hd, (nghttp2_hd_inflater *inflater, nghttp2_nv *nv_out, int *inflate_flags, uint8_t *in, size_t inlen, int in_final), (inflater, nv_out, inflate_flags, in, inlen, in_final), -1)
WRAP_RET(ssize_t, nghttp2_hd_inflate_hd2, (nghttp2_hd_inflater *inflater, nghttp2_nv *nv_out, int *inflate_flags, const uint8_t *in, size_t inlen, int in_final), (inflater, nv_out, inflate_flags, in, inlen, in_final), -1)
WRAP_RET(nghttp2_ssize, nghttp2_hd_inflate_hd3, (nghttp2_hd_inflater *inflater, nghttp2_nv *nv_out, int *inflate_flags, const uint8_t *in, size_t inlen, int in_final), (inflater, nv_out, inflate_flags, in, inlen, in_final), -1)
WRAP_RET(int, nghttp2_hd_inflate_new, (nghttp2_hd_inflater **inflater_ptr), (inflater_ptr), -1)
WRAP_RET(int, nghttp2_hd_inflate_new2, (nghttp2_hd_inflater **inflater_ptr, nghttp2_mem *mem), (inflater_ptr, mem), -1)
WRAP_RET(const char *, nghttp2_http2_strerror, (uint32_t error_code), (error_code), NULL)
WRAP_RET(int, nghttp2_is_fatal, (int lib_error_code), (lib_error_code), -1)
WRAP_RET(int, nghttp2_nv_compare_name, (const nghttp2_nv *lhs, const nghttp2_nv *rhs), (lhs, rhs), -1)
WRAP_VOID(nghttp2_option_del, (nghttp2_option *option), (option))
WRAP_RET(int, nghttp2_option_new, (nghttp2_option **option_ptr), (option_ptr), -1)
WRAP_VOID(nghttp2_option_set_builtin_recv_extension_type, (nghttp2_option *option, uint8_t type), (option, type))
WRAP_VOID(nghttp2_option_set_glitch_rate_limit, (nghttp2_option *option, uint64_t burst, uint64_t rate), (option, burst, rate))
WRAP_VOID(nghttp2_option_set_max_continuations, (nghttp2_option *option, size_t val), (option, val))
WRAP_VOID(nghttp2_option_set_max_deflate_dynamic_table_size, (nghttp2_option *option, size_t val), (option, val))
WRAP_VOID(nghttp2_option_set_max_outbound_ack, (nghttp2_option *option, size_t val), (option, val))
WRAP_VOID(nghttp2_option_set_max_reserved_remote_streams, (nghttp2_option *option, uint32_t val), (option, val))
WRAP_VOID(nghttp2_option_set_max_send_header_block_length, (nghttp2_option *option, size_t val), (option, val))
WRAP_VOID(nghttp2_option_set_max_settings, (nghttp2_option *option, size_t val), (option, val))
WRAP_VOID(nghttp2_option_set_no_auto_ping_ack, (nghttp2_option *option, int val), (option, val))
WRAP_VOID(nghttp2_option_set_no_auto_window_update, (nghttp2_option *option, int val), (option, val))
WRAP_VOID(nghttp2_option_set_no_closed_streams, (nghttp2_option *option, int val), (option, val))
WRAP_VOID(nghttp2_option_set_no_http_messaging, (nghttp2_option *option, int val), (option, val))
WRAP_VOID(nghttp2_option_set_no_recv_client_magic, (nghttp2_option *option, int val), (option, val))
WRAP_VOID(nghttp2_option_set_no_rfc9113_leading_and_trailing_ws_validation, (nghttp2_option *option, int val), (option, val))
WRAP_VOID(nghttp2_option_set_peer_max_concurrent_streams, (nghttp2_option *option, uint32_t val), (option, val))
WRAP_VOID(nghttp2_option_set_server_fallback_rfc7540_priorities, (nghttp2_option *option, int val), (option, val))
WRAP_VOID(nghttp2_option_set_stream_reset_rate_limit, (nghttp2_option *option, uint64_t burst, uint64_t rate), (option, burst, rate))
WRAP_VOID(nghttp2_option_set_user_recv_extension_type, (nghttp2_option *option, uint8_t type), (option, type))
WRAP_RET(ssize_t, nghttp2_pack_settings_payload, (uint8_t *buf, size_t buflen, const nghttp2_settings_entry *iv, size_t niv), (buf, buflen, iv, niv), -1)
WRAP_RET(nghttp2_ssize, nghttp2_pack_settings_payload2, (uint8_t *buf, size_t buflen, const nghttp2_settings_entry *iv, size_t niv), (buf, buflen, iv, niv), -1)
WRAP_RET(int, nghttp2_priority_spec_check_default, (const nghttp2_priority_spec *pri_spec), (pri_spec), -1)
WRAP_VOID(nghttp2_priority_spec_default_init, (nghttp2_priority_spec *pri_spec), (pri_spec))
WRAP_VOID(nghttp2_priority_spec_init, (nghttp2_priority_spec *pri_spec, int32_t stream_id, int32_t weight, int exclusive), (pri_spec, stream_id, weight, exclusive))
WRAP_VOID(nghttp2_rcbuf_decref, (nghttp2_rcbuf *rcbuf), (rcbuf))
WRAP_RET(nghttp2_vec, nghttp2_rcbuf_get_buf, (nghttp2_rcbuf *rcbuf), (rcbuf), ((nghttp2_vec){NULL, 0}))
WRAP_VOID(nghttp2_rcbuf_incref, (nghttp2_rcbuf *rcbuf), (rcbuf))
WRAP_RET(int, nghttp2_rcbuf_is_static, (const nghttp2_rcbuf *rcbuf), (rcbuf), -1)
WRAP_RET(int, nghttp2_select_alpn, (const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen), (out, outlen, in, inlen), -1)
WRAP_RET(int, nghttp2_select_next_protocol, (unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen), (out, outlen, in, inlen), -1)
WRAP_VOID(nghttp2_session_callbacks_del, (nghttp2_session_callbacks *callbacks), (callbacks))
WRAP_RET(int, nghttp2_session_callbacks_new, (nghttp2_session_callbacks **callbacks_ptr), (callbacks_ptr), -1)
WRAP_VOID(nghttp2_session_callbacks_set_before_frame_send_callback, (nghttp2_session_callbacks *cbs, nghttp2_before_frame_send_callback before_frame_send_callback), (cbs, before_frame_send_callback))
WRAP_VOID(nghttp2_session_callbacks_set_data_source_read_length_callback, (nghttp2_session_callbacks *cbs, nghttp2_data_source_read_length_callback data_source_read_length_callback), (cbs, data_source_read_length_callback))
WRAP_VOID(nghttp2_session_callbacks_set_data_source_read_length_callback2, (nghttp2_session_callbacks *cbs, nghttp2_data_source_read_length_callback2 data_source_read_length_callback), (cbs, data_source_read_length_callback))
WRAP_VOID(nghttp2_session_callbacks_set_error_callback, (nghttp2_session_callbacks *cbs, nghttp2_error_callback error_callback), (cbs, error_callback))
WRAP_VOID(nghttp2_session_callbacks_set_error_callback2, (nghttp2_session_callbacks *cbs, nghttp2_error_callback2 error_callback2), (cbs, error_callback2))
WRAP_VOID(nghttp2_session_callbacks_set_on_begin_frame_callback, (nghttp2_session_callbacks *cbs, nghttp2_on_begin_frame_callback on_begin_frame_callback), (cbs, on_begin_frame_callback))
WRAP_VOID(nghttp2_session_callbacks_set_on_begin_headers_callback, (nghttp2_session_callbacks *cbs, nghttp2_on_begin_headers_callback on_begin_headers_callback), (cbs, on_begin_headers_callback))
WRAP_VOID(nghttp2_session_callbacks_set_on_data_chunk_recv_callback, (nghttp2_session_callbacks *cbs, nghttp2_on_data_chunk_recv_callback on_data_chunk_recv_callback), (cbs, on_data_chunk_recv_callback))
WRAP_VOID(nghttp2_session_callbacks_set_on_extension_chunk_recv_callback, (nghttp2_session_callbacks *cbs, nghttp2_on_extension_chunk_recv_callback on_extension_chunk_recv_callback), (cbs, on_extension_chunk_recv_callback))
WRAP_VOID(nghttp2_session_callbacks_set_on_frame_not_send_callback, (nghttp2_session_callbacks *cbs, nghttp2_on_frame_not_send_callback on_frame_not_send_callback), (cbs, on_frame_not_send_callback))
WRAP_VOID(nghttp2_session_callbacks_set_on_frame_recv_callback, (nghttp2_session_callbacks *cbs, nghttp2_on_frame_recv_callback on_frame_recv_callback), (cbs, on_frame_recv_callback))
WRAP_VOID(nghttp2_session_callbacks_set_on_frame_send_callback, (nghttp2_session_callbacks *cbs, nghttp2_on_frame_send_callback on_frame_send_callback), (cbs, on_frame_send_callback))
WRAP_VOID(nghttp2_session_callbacks_set_on_header_callback, (nghttp2_session_callbacks *cbs, nghttp2_on_header_callback on_header_callback), (cbs, on_header_callback))
WRAP_VOID(nghttp2_session_callbacks_set_on_header_callback2, (nghttp2_session_callbacks *cbs, nghttp2_on_header_callback2 on_header_callback2), (cbs, on_header_callback2))
WRAP_VOID(nghttp2_session_callbacks_set_on_invalid_frame_recv_callback, (nghttp2_session_callbacks *cbs, nghttp2_on_invalid_frame_recv_callback on_invalid_frame_recv_callback), (cbs, on_invalid_frame_recv_callback))
WRAP_VOID(nghttp2_session_callbacks_set_on_invalid_header_callback, (nghttp2_session_callbacks *cbs, nghttp2_on_invalid_header_callback on_invalid_header_callback), (cbs, on_invalid_header_callback))
WRAP_VOID(nghttp2_session_callbacks_set_on_invalid_header_callback2, (nghttp2_session_callbacks *cbs, nghttp2_on_invalid_header_callback2 on_invalid_header_callback2), (cbs, on_invalid_header_callback2))
WRAP_VOID(nghttp2_session_callbacks_set_on_stream_close_callback, (nghttp2_session_callbacks *cbs, nghttp2_on_stream_close_callback on_stream_close_callback), (cbs, on_stream_close_callback))
WRAP_VOID(nghttp2_session_callbacks_set_pack_extension_callback, (nghttp2_session_callbacks *cbs, nghttp2_pack_extension_callback pack_extension_callback), (cbs, pack_extension_callback))
WRAP_VOID(nghttp2_session_callbacks_set_pack_extension_callback2, (nghttp2_session_callbacks *cbs, nghttp2_pack_extension_callback2 pack_extension_callback), (cbs, pack_extension_callback))
WRAP_VOID(nghttp2_session_callbacks_set_rand_callback, (nghttp2_session_callbacks *cbs, nghttp2_rand_callback rand_callback), (cbs, rand_callback))
WRAP_VOID(nghttp2_session_callbacks_set_recv_callback, (nghttp2_session_callbacks *cbs, nghttp2_recv_callback recv_callback), (cbs, recv_callback))
WRAP_VOID(nghttp2_session_callbacks_set_recv_callback2, (nghttp2_session_callbacks *cbs, nghttp2_recv_callback2 recv_callback), (cbs, recv_callback))
WRAP_VOID(nghttp2_session_callbacks_set_select_padding_callback, (nghttp2_session_callbacks *cbs, nghttp2_select_padding_callback select_padding_callback), (cbs, select_padding_callback))
WRAP_VOID(nghttp2_session_callbacks_set_select_padding_callback2, (nghttp2_session_callbacks *cbs, nghttp2_select_padding_callback2 select_padding_callback), (cbs, select_padding_callback))
WRAP_VOID(nghttp2_session_callbacks_set_send_callback, (nghttp2_session_callbacks *cbs, nghttp2_send_callback send_callback), (cbs, send_callback))
WRAP_VOID(nghttp2_session_callbacks_set_send_callback2, (nghttp2_session_callbacks *cbs, nghttp2_send_callback2 send_callback), (cbs, send_callback))
WRAP_VOID(nghttp2_session_callbacks_set_send_data_callback, (nghttp2_session_callbacks *cbs, nghttp2_send_data_callback send_data_callback), (cbs, send_data_callback))
WRAP_VOID(nghttp2_session_callbacks_set_unpack_extension_callback, (nghttp2_session_callbacks *cbs, nghttp2_unpack_extension_callback unpack_extension_callback), (cbs, unpack_extension_callback))
WRAP_RET(int, nghttp2_session_change_extpri_stream_priority, (nghttp2_session *session, int32_t stream_id, const nghttp2_extpri *extpri, int ignore_client_signal), (session, stream_id, extpri, ignore_client_signal), -1)
WRAP_RET(int, nghttp2_session_change_stream_priority, (nghttp2_session *session, int32_t stream_id, const nghttp2_priority_spec *pri_spec), (session, stream_id, pri_spec), -1)
WRAP_RET(int, nghttp2_session_check_request_allowed, (nghttp2_session *session), (session), -1)
WRAP_RET(int, nghttp2_session_check_server_session, (nghttp2_session *session), (session), -1)
WRAP_RET(int, nghttp2_session_client_new, (nghttp2_session **session_ptr, const nghttp2_session_callbacks *callbacks, void *user_data), (session_ptr, callbacks, user_data), -1)
WRAP_RET(int, nghttp2_session_client_new2, (nghttp2_session **session_ptr, const nghttp2_session_callbacks *callbacks, void *user_data, const nghttp2_option *option), (session_ptr, callbacks, user_data, option), -1)
WRAP_RET(int, nghttp2_session_client_new3, (nghttp2_session **session_ptr, const nghttp2_session_callbacks *callbacks, void *user_data, const nghttp2_option *option, nghttp2_mem *mem), (session_ptr, callbacks, user_data, option, mem), -1)
WRAP_RET(int, nghttp2_session_consume, (nghttp2_session *session, int32_t stream_id, size_t size), (session, stream_id, size), -1)
WRAP_RET(int, nghttp2_session_consume_connection, (nghttp2_session *session, size_t size), (session, size), -1)
WRAP_RET(int, nghttp2_session_consume_stream, (nghttp2_session *session, int32_t stream_id, size_t size), (session, stream_id, size), -1)
WRAP_RET(int, nghttp2_session_create_idle_stream, (nghttp2_session *session, int32_t stream_id, const nghttp2_priority_spec *pri_spec), (session, stream_id, pri_spec), -1)
WRAP_VOID(nghttp2_session_del, (nghttp2_session *session), (session))
WRAP_RET(nghttp2_stream *, nghttp2_session_find_stream, (nghttp2_session *session, int32_t stream_id), (session, stream_id), NULL)
WRAP_RET(int32_t, nghttp2_session_get_effective_local_window_size, (nghttp2_session *session), (session), -1)
WRAP_RET(int32_t, nghttp2_session_get_effective_recv_data_length, (nghttp2_session *session), (session), -1)
WRAP_RET(int, nghttp2_session_get_extpri_stream_priority, (nghttp2_session *session, nghttp2_extpri *extpri, int32_t stream_id), (session, extpri, stream_id), -1)
WRAP_RET(size_t, nghttp2_session_get_hd_deflate_dynamic_table_size, (nghttp2_session *session), (session), 0)
WRAP_RET(size_t, nghttp2_session_get_hd_inflate_dynamic_table_size, (nghttp2_session *session), (session), 0)
WRAP_RET(int32_t, nghttp2_session_get_last_proc_stream_id, (nghttp2_session *session), (session), -1)
WRAP_RET(uint32_t, nghttp2_session_get_local_settings, (nghttp2_session *session, nghttp2_settings_id id), (session, id), -1)
WRAP_RET(int32_t, nghttp2_session_get_local_window_size, (nghttp2_session *session), (session), -1)
WRAP_RET(uint32_t, nghttp2_session_get_next_stream_id, (nghttp2_session *session), (session), -1)
WRAP_RET(size_t, nghttp2_session_get_outbound_queue_size, (nghttp2_session *session), (session), 0)
WRAP_RET(uint32_t, nghttp2_session_get_remote_settings, (nghttp2_session *session, nghttp2_settings_id id), (session, id), -1)
WRAP_RET(int32_t, nghttp2_session_get_remote_window_size, (nghttp2_session *session), (session), -1)
WRAP_RET(nghttp2_stream *, nghttp2_session_get_root_stream, (nghttp2_session *session), (session), NULL)
WRAP_RET(int32_t, nghttp2_session_get_stream_effective_local_window_size, (nghttp2_session *session, int32_t stream_id), (session, stream_id), -1)
WRAP_RET(int32_t, nghttp2_session_get_stream_effective_recv_data_length, (nghttp2_session *session, int32_t stream_id), (session, stream_id), -1)
WRAP_RET(int, nghttp2_session_get_stream_local_close, (nghttp2_session *session, int32_t stream_id), (session, stream_id), -1)
WRAP_RET(int32_t, nghttp2_session_get_stream_local_window_size, (nghttp2_session *session, int32_t stream_id), (session, stream_id), -1)
WRAP_RET(int, nghttp2_session_get_stream_remote_close, (nghttp2_session *session, int32_t stream_id), (session, stream_id), -1)
WRAP_RET(int32_t, nghttp2_session_get_stream_remote_window_size, (nghttp2_session *session, int32_t stream_id), (session, stream_id), -1)
WRAP_RET(void *, nghttp2_session_get_stream_user_data, (nghttp2_session *session, int32_t stream_id), (session, stream_id), NULL)
WRAP_RET(ssize_t, nghttp2_session_mem_recv, (nghttp2_session *session, const uint8_t *in, size_t inlen), (session, in, inlen), -1)
WRAP_RET(nghttp2_ssize, nghttp2_session_mem_recv2, (nghttp2_session *session, const uint8_t *in, size_t inlen), (session, in, inlen), -1)
WRAP_RET(ssize_t, nghttp2_session_mem_send, (nghttp2_session *session, const uint8_t **data_ptr), (session, data_ptr), -1)
WRAP_RET(nghttp2_ssize, nghttp2_session_mem_send2, (nghttp2_session *session, const uint8_t **data_ptr), (session, data_ptr), -1)
WRAP_RET(int, nghttp2_session_recv, (nghttp2_session *session), (session), -1)
WRAP_RET(int, nghttp2_session_resume_data, (nghttp2_session *session, int32_t stream_id), (session, stream_id), -1)
WRAP_RET(int, nghttp2_session_send, (nghttp2_session *session), (session), -1)
WRAP_RET(int, nghttp2_session_server_new, (nghttp2_session **session_ptr, const nghttp2_session_callbacks *callbacks, void *user_data), (session_ptr, callbacks, user_data), -1)
WRAP_RET(int, nghttp2_session_server_new2, (nghttp2_session **session_ptr, const nghttp2_session_callbacks *callbacks, void *user_data, const nghttp2_option *option), (session_ptr, callbacks, user_data, option), -1)
WRAP_RET(int, nghttp2_session_server_new3, (nghttp2_session **session_ptr, const nghttp2_session_callbacks *callbacks, void *user_data, const nghttp2_option *option, nghttp2_mem *mem), (session_ptr, callbacks, user_data, option, mem), -1)
WRAP_RET(int, nghttp2_session_set_local_window_size, (nghttp2_session *session, uint8_t flags, int32_t stream_id, int32_t window_size), (session, flags, stream_id, window_size), -1)
WRAP_RET(int, nghttp2_session_set_next_stream_id, (nghttp2_session *session, int32_t next_stream_id), (session, next_stream_id), -1)
WRAP_RET(int, nghttp2_session_set_stream_user_data, (nghttp2_session *session, int32_t stream_id, void *stream_user_data), (session, stream_id, stream_user_data), -1)
WRAP_VOID(nghttp2_session_set_user_data, (nghttp2_session *session, void *user_data), (session, user_data))
WRAP_RET(int, nghttp2_session_terminate_session, (nghttp2_session *session, uint32_t error_code), (session, error_code), -1)
WRAP_RET(int, nghttp2_session_terminate_session2, (nghttp2_session *session, int32_t last_stream_id, uint32_t error_code), (session, last_stream_id, error_code), -1)
WRAP_RET(int, nghttp2_session_upgrade, (nghttp2_session *session, const uint8_t *settings_payload, size_t settings_payloadlen, void *stream_user_data), (session, settings_payload, settings_payloadlen, stream_user_data), -1)
WRAP_RET(int, nghttp2_session_upgrade2, (nghttp2_session *session, const uint8_t *settings_payload, size_t settings_payloadlen, int head_request, void *stream_user_data), (session, settings_payload, settings_payloadlen, head_request, stream_user_data), -1)
WRAP_RET(int, nghttp2_session_want_read, (nghttp2_session *session), (session), -1)
WRAP_RET(int, nghttp2_session_want_write, (nghttp2_session *session), (session), -1)
WRAP_VOID(nghttp2_set_debug_vprintf_callback, (nghttp2_debug_vprintf_callback debug_vprintf_callback), (debug_vprintf_callback))
WRAP_RET(nghttp2_stream *, nghttp2_stream_get_first_child, (nghttp2_stream *stream), (stream), NULL)
WRAP_RET(nghttp2_stream *, nghttp2_stream_get_next_sibling, (nghttp2_stream *stream), (stream), NULL)
WRAP_RET(nghttp2_stream *, nghttp2_stream_get_parent, (nghttp2_stream *stream), (stream), NULL)
WRAP_RET(nghttp2_stream *, nghttp2_stream_get_previous_sibling, (nghttp2_stream *stream), (stream), NULL)
WRAP_RET(nghttp2_stream_proto_state, nghttp2_stream_get_state, (nghttp2_stream *stream), (stream), 0)
WRAP_RET(int32_t, nghttp2_stream_get_stream_id, (nghttp2_stream *stream), (stream), -1)
WRAP_RET(int32_t, nghttp2_stream_get_sum_dependency_weight, (nghttp2_stream *stream), (stream), -1)
WRAP_RET(int32_t, nghttp2_stream_get_weight, (nghttp2_stream *stream), (stream), -1)
WRAP_RET(const char *, nghttp2_strerror, (int lib_error_code), (lib_error_code), NULL)
WRAP_RET(int, nghttp2_submit_altsvc, (nghttp2_session *session, uint8_t flags, int32_t stream_id, const uint8_t *origin, size_t origin_len, const uint8_t *field_value, size_t field_value_len), (session, flags, stream_id, origin, origin_len, field_value, field_value_len), -1)
WRAP_RET(int, nghttp2_submit_data, (nghttp2_session *session, uint8_t flags, int32_t stream_id, const nghttp2_data_provider *data_prd), (session, flags, stream_id, data_prd), -1)
WRAP_RET(int, nghttp2_submit_data2, (nghttp2_session *session, uint8_t flags, int32_t stream_id, const nghttp2_data_provider2 *data_prd), (session, flags, stream_id, data_prd), -1)
WRAP_RET(int, nghttp2_submit_extension, (nghttp2_session *session, uint8_t type, uint8_t flags, int32_t stream_id, void *payload), (session, type, flags, stream_id, payload), -1)
WRAP_RET(int, nghttp2_submit_goaway, (nghttp2_session *session, uint8_t flags, int32_t last_stream_id, uint32_t error_code, const uint8_t *opaque_data, size_t opaque_data_len), (session, flags, last_stream_id, error_code, opaque_data, opaque_data_len), -1)
WRAP_RET(int32_t, nghttp2_submit_headers, (nghttp2_session *session, uint8_t flags, int32_t stream_id, const nghttp2_priority_spec *pri_spec, const nghttp2_nv *nva, size_t nvlen, void *stream_user_data), (session, flags, stream_id, pri_spec, nva, nvlen, stream_user_data), -1)
WRAP_RET(int, nghttp2_submit_origin, (nghttp2_session *session, uint8_t flags, const nghttp2_origin_entry *ov, size_t nov), (session, flags, ov, nov), -1)
WRAP_RET(int, nghttp2_submit_ping, (nghttp2_session *session, uint8_t flags, const uint8_t *opaque_data), (session, flags, opaque_data), -1)
WRAP_RET(int, nghttp2_submit_priority, (nghttp2_session *session, uint8_t flags, int32_t stream_id, const nghttp2_priority_spec *pri_spec), (session, flags, stream_id, pri_spec), -1)
WRAP_RET(int, nghttp2_submit_priority_update, (nghttp2_session *session, uint8_t flags, int32_t stream_id, const uint8_t *field_value, size_t field_value_len), (session, flags, stream_id, field_value, field_value_len), -1)
WRAP_RET(int32_t, nghttp2_submit_push_promise, (nghttp2_session *session, uint8_t flags, int32_t stream_id, const nghttp2_nv *nva, size_t nvlen, void *promised_stream_user_data), (session, flags, stream_id, nva, nvlen, promised_stream_user_data), -1)
WRAP_RET(int32_t, nghttp2_submit_request, (nghttp2_session *session, const nghttp2_priority_spec *pri_spec, const nghttp2_nv *nva, size_t nvlen, const nghttp2_data_provider *data_prd, void *stream_user_data), (session, pri_spec, nva, nvlen, data_prd, stream_user_data), -1)
WRAP_RET(int32_t, nghttp2_submit_request2, (nghttp2_session *session, const nghttp2_priority_spec *pri_spec, const nghttp2_nv *nva, size_t nvlen, const nghttp2_data_provider2 *data_prd, void *stream_user_data), (session, pri_spec, nva, nvlen, data_prd, stream_user_data), -1)
WRAP_RET(int, nghttp2_submit_response, (nghttp2_session *session, int32_t stream_id, const nghttp2_nv *nva, size_t nvlen, const nghttp2_data_provider *data_prd), (session, stream_id, nva, nvlen, data_prd), -1)
WRAP_RET(int, nghttp2_submit_response2, (nghttp2_session *session, int32_t stream_id, const nghttp2_nv *nva, size_t nvlen, const nghttp2_data_provider2 *data_prd), (session, stream_id, nva, nvlen, data_prd), -1)
WRAP_RET(int, nghttp2_submit_rst_stream, (nghttp2_session *session, uint8_t flags, int32_t stream_id, uint32_t error_code), (session, flags, stream_id, error_code), -1)
WRAP_RET(int, nghttp2_submit_settings, (nghttp2_session *session, uint8_t flags, const nghttp2_settings_entry *iv, size_t niv), (session, flags, iv, niv), -1)
WRAP_RET(int, nghttp2_submit_shutdown_notice, (nghttp2_session *session), (session), -1)
WRAP_RET(int, nghttp2_submit_trailer, (nghttp2_session *session, int32_t stream_id, const nghttp2_nv *nva, size_t nvlen), (session, stream_id, nva, nvlen), -1)
WRAP_RET(int, nghttp2_submit_window_update, (nghttp2_session *session, uint8_t flags, int32_t stream_id, int32_t window_size_increment), (session, flags, stream_id, window_size_increment), -1)
WRAP_RET(nghttp2_info *, nghttp2_version, (int least_version), (least_version), NULL)
