/* ============================================================
 * libcbor_dyn.c - dlopen 模拟链接：PJK/libcbor 0.14 (C99)
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照本机 /usr/include/cbor/ 下公开头文件的 181 个函数声明
 * （arrays / bytestrings / callbacks / common / encoding / floats_ctrls /
 *  ints / maps / serialization / streaming / strings / tags）。
 * 排除 _cbor_* 内部符号；库中另有 29 个导出（cbor_builder_*_callback、
 * cbor_copy、cbor_copy_definite、cbor_describe、cbor_load、
 * cbor_empty_callbacks）未在公开头文件声明，不在封装范围内。
 * cbor_stream_decode 按值返回 struct cbor_decoder_result，fallback 为零值结构体。
 * 类型按头文件原样内嵌，字段顺序即 ABI；链接期不需要 libcbor，仅需 -ldl。
 * 加载顺序：显式路径 > LIBCBOR_DYN_LIB > LD_LIBRARY_PATH > soname >
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
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

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
#  if __has_include(<cbor.h>)
#    include <cbor.h>
#    define LIBCBOR_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef LIBCBOR_DYN_HAVE_HEADER
/* ---------- 类型（cbor/data.h、callbacks.h、common.h 原样内嵌） ---------- */
typedef const unsigned char *cbor_data;
typedef unsigned char *cbor_mutable_data;

typedef enum cbor_type {
  CBOR_TYPE_UINT,
  CBOR_TYPE_NEGINT,
  CBOR_TYPE_BYTESTRING,
  CBOR_TYPE_STRING,
  CBOR_TYPE_ARRAY,
  CBOR_TYPE_MAP,
  CBOR_TYPE_TAG,
  CBOR_TYPE_FLOAT_CTRL
} cbor_type;

typedef enum {
  CBOR_ERR_NONE,
  CBOR_ERR_NOTENOUGHDATA,
  CBOR_ERR_NODATA,
  CBOR_ERR_MALFORMATED,
  CBOR_ERR_MEMERROR,
  CBOR_ERR_SYNTAXERROR
} cbor_error_code;

typedef enum { CBOR_INT_8, CBOR_INT_16, CBOR_INT_32, CBOR_INT_64 } cbor_int_width;
typedef enum { CBOR_FLOAT_0, CBOR_FLOAT_16, CBOR_FLOAT_32, CBOR_FLOAT_64 } cbor_float_width;
typedef enum { _CBOR_METADATA_DEFINITE, _CBOR_METADATA_INDEFINITE } _cbor_dst_metadata;

struct _cbor_int_metadata { cbor_int_width width; };
struct _cbor_bytestring_metadata { size_t length; _cbor_dst_metadata type; };
struct _cbor_string_metadata { size_t length; size_t codepoint_count; _cbor_dst_metadata type; };
struct _cbor_array_metadata { size_t allocated; size_t end_ptr; _cbor_dst_metadata type; };
struct _cbor_map_metadata { size_t allocated; size_t end_ptr; _cbor_dst_metadata type; };
struct cbor_item_t;
struct _cbor_tag_metadata { struct cbor_item_t *tagged_item; uint64_t value; };
struct _cbor_float_ctrl_metadata { cbor_float_width width; uint8_t ctrl; };

union cbor_item_metadata {
  struct _cbor_int_metadata int_metadata;
  struct _cbor_bytestring_metadata bytestring_metadata;
  struct _cbor_string_metadata string_metadata;
  struct _cbor_array_metadata array_metadata;
  struct _cbor_map_metadata map_metadata;
  struct _cbor_tag_metadata tag_metadata;
  struct _cbor_float_ctrl_metadata float_ctrl_metadata;
};

typedef struct cbor_item_t {
  union cbor_item_metadata metadata;
  size_t refcount;
  cbor_type type;
  unsigned char *data;
} cbor_item_t;

struct cbor_pair { cbor_item_t *key, *value; };
struct cbor_error { size_t position; cbor_error_code code; };
struct cbor_load_result { struct cbor_error error; size_t read; };

enum cbor_decoder_status {
  CBOR_DECODER_FINISHED,
  CBOR_DECODER_NEDATA,
  CBOR_DECODER_ERROR
};
struct cbor_decoder_result {
  size_t read;
  enum cbor_decoder_status status;
  size_t required;
};

typedef void (*cbor_int8_callback)(void*, uint8_t);
typedef void (*cbor_int16_callback)(void*, uint16_t);
typedef void (*cbor_int32_callback)(void*, uint32_t);
typedef void (*cbor_int64_callback)(void*, uint64_t);
typedef void (*cbor_simple_callback)(void*);
typedef void (*cbor_string_callback)(void*, cbor_data, uint64_t);
typedef void (*cbor_collection_callback)(void*, uint64_t);
typedef void (*cbor_float_callback)(void*, float);
typedef void (*cbor_double_callback)(void*, double);
typedef void (*cbor_bool_callback)(void*, bool);

struct cbor_callbacks {
  cbor_int8_callback uint8;
  cbor_int16_callback uint16;
  cbor_int32_callback uint32;
  cbor_int64_callback uint64;
  cbor_int64_callback negint64;
  cbor_int32_callback negint32;
  cbor_int16_callback negint16;
  cbor_int8_callback negint8;
  cbor_simple_callback byte_string_start;
  cbor_string_callback byte_string;
  cbor_string_callback string;
  cbor_simple_callback string_start;
  cbor_simple_callback indef_array_start;
  cbor_collection_callback array_start;
  cbor_simple_callback indef_map_start;
  cbor_collection_callback map_start;
  cbor_int64_callback tag;
  cbor_float_callback float2;
  cbor_float_callback float4;
  cbor_double_callback float8;
  cbor_simple_callback undefined;
  cbor_simple_callback null;
  cbor_bool_callback boolean;
  cbor_simple_callback indef_break;
};

typedef void *(*_cbor_malloc_t)(size_t);
typedef void *(*_cbor_realloc_t)(void*, size_t);
typedef void (*_cbor_free_t)(void*);

#endif /* LIBCBOR_DYN_HAVE_HEADER */

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static lib_handle_t libcbor_handle = NULL;
static int libcbor_init_state = 0;

static void *libcbor_sym(const char *name) {
    return libcbor_handle ? get_sym(libcbor_handle, name) : NULL;
}

static int libcbor_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    const char *so_soname = "libcbor.so.0.14";
    const char *so_alt = "libcbor.so.0";
    const char *so_plain = "libcbor.so";
    int n = 0, i;
    int res = -1;

    if (libcbor_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("LIBCBOR_DYN_LIB");
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
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_soname);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_alt);
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
    candidates[n++] = so_alt;
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
    candidates[n++] = "/opt/homebrew/lib/libcbor.dylib";
    candidates[n++] = "/usr/local/lib/libcbor.dylib";
#elif defined(_WIN32)
    candidates[n++] = "libcbor.dll";
#else
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
#endif

    for (i = 0; i < n; i++) {
        libcbor_handle = load_lib(candidates[i]);
        // printf("try %d %s\n", i, candidates[i]);
        if (libcbor_handle) { res = i; break; }
    }
    if (!libcbor_handle || !libcbor_sym("cbor_build_uint8")) {
        fprintf(stderr, "libcbor_dyn: cannot load libcbor (%s)\n", lib_error());
        if (libcbor_handle) { free_lib(libcbor_handle); libcbor_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "libcbor_dyn: loaded %d %s\n", res, candidates[res]);
    return 0;
}

int libcbor_libok(void) {
    if (libcbor_init_state == 0)
        libcbor_init_state = (libcbor_do_load(NULL) == 0) ? 1 : -1;
    return (libcbor_init_state == 1 && libcbor_handle != NULL) ? 1 : 0;
}

int libcbor_dyn_init(const char *library_path) {
    if (libcbor_do_load(library_path) == 0) {
        libcbor_init_state = 1;
        return 0;
    }
    if (libcbor_init_state == 0) libcbor_init_state = -1;
    return -1;
}

void libcbor_dyn_cleanup(void) {
    if (libcbor_handle) { free_lib(libcbor_handle); libcbor_handle = NULL; }
    libcbor_init_state = 0;
}

int libcbor_dyn_has_symbol(const char *name) {
    if (!libcbor_libok()) return 0;
    return libcbor_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                            \
ret name params {                                                              \
    static ret (*fn) params = NULL;                                            \
    if (!fn) {                                                                 \
        if (!libcbor_libok()) return fallback;                                 \
        *(void **)(&fn) = libcbor_sym(#name);                                  \
        if (!fn) {                                                             \
            fprintf(stderr, "libcbor_dyn: dlsym failed: %s\n", #name);         \
            return fallback;                                                   \
        }                                                                      \
    }                                                                          \
    return fn args;                                                            \
}
#define WRAP_VOID(name, params, args)                                          \
void name params {                                                             \
    static void (*fn) params = NULL;                                           \
    if (!fn) {                                                                 \
        if (!libcbor_libok()) return;                                          \
        *(void **)(&fn) = libcbor_sym(#name);                                  \
        if (!fn) {                                                             \
            fprintf(stderr, "libcbor_dyn: dlsym failed: %s\n", #name);         \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn args;                                                                   \
}

/* ============================================================
 * 导出函数封装（公开头文件声明的 181 个函数，函数名保持原生原样）
 * ============================================================ */
WRAP_RET(size_t, cbor_array_allocated, (const cbor_item_t* item), (item), 0)
WRAP_RET(cbor_item_t*, cbor_array_get, (const cbor_item_t* item, size_t index), (item, index), NULL)
WRAP_RET(cbor_item_t**, cbor_array_handle, (const cbor_item_t* item), (item), NULL)
WRAP_RET(bool, cbor_array_is_definite, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_array_is_indefinite, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_array_push, (cbor_item_t* array, cbor_item_t* pushee), (array, pushee), 0)
WRAP_RET(bool, cbor_array_replace, (cbor_item_t* item, size_t index, cbor_item_t* value), (item, index, value), 0)
WRAP_RET(bool, cbor_array_set, (cbor_item_t* item, size_t index, cbor_item_t* value), (item, index, value), 0)
WRAP_RET(size_t, cbor_array_size, (const cbor_item_t* item), (item), 0)
WRAP_RET(cbor_item_t*, cbor_build_bool, (bool value), (value), NULL)
WRAP_RET(cbor_item_t*, cbor_build_bytestring, (cbor_data handle, size_t length), (handle, length), NULL)
WRAP_RET(cbor_item_t*, cbor_build_ctrl, (uint8_t value), (value), NULL)
WRAP_RET(cbor_item_t*, cbor_build_float2, (float value), (value), NULL)
WRAP_RET(cbor_item_t*, cbor_build_float4, (float value), (value), NULL)
WRAP_RET(cbor_item_t*, cbor_build_float8, (double value), (value), NULL)
WRAP_RET(cbor_item_t*, cbor_build_negint16, (uint16_t value), (value), NULL)
WRAP_RET(cbor_item_t*, cbor_build_negint32, (uint32_t value), (value), NULL)
WRAP_RET(cbor_item_t*, cbor_build_negint64, (uint64_t value), (value), NULL)
WRAP_RET(cbor_item_t*, cbor_build_negint8, (uint8_t value), (value), NULL)
WRAP_RET(cbor_item_t*, cbor_build_string, (const char* val), (val), NULL)
WRAP_RET(cbor_item_t*, cbor_build_stringn, (const char* val, size_t length), (val, length), NULL)
WRAP_RET(cbor_item_t*, cbor_build_tag, (uint64_t value, cbor_item_t* item), (value, item), NULL)
WRAP_RET(cbor_item_t*, cbor_build_uint16, (uint16_t value), (value), NULL)
WRAP_RET(cbor_item_t*, cbor_build_uint32, (uint32_t value), (value), NULL)
WRAP_RET(cbor_item_t*, cbor_build_uint64, (uint64_t value), (value), NULL)
WRAP_RET(cbor_item_t*, cbor_build_uint8, (uint8_t value), (value), NULL)
WRAP_RET(bool, cbor_bytestring_add_chunk, (cbor_item_t* item, cbor_item_t* chunk), (item, chunk), 0)
WRAP_RET(size_t, cbor_bytestring_chunk_count, (const cbor_item_t* item), (item), 0)
WRAP_RET(cbor_item_t**, cbor_bytestring_chunks_handle, (const cbor_item_t* item), (item), NULL)
WRAP_RET(cbor_mutable_data, cbor_bytestring_handle, (const cbor_item_t* item), (item), NULL)
WRAP_RET(bool, cbor_bytestring_is_definite, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_bytestring_is_indefinite, (const cbor_item_t* item), (item), 0)
WRAP_RET(size_t, cbor_bytestring_length, (const cbor_item_t* item), (item), 0)
WRAP_VOID(cbor_bytestring_set_handle, (cbor_item_t* item, cbor_mutable_data data, size_t length), (item, data, length))
WRAP_RET(uint8_t, cbor_ctrl_value, (const cbor_item_t* item), (item), 0)
WRAP_VOID(cbor_decref, (cbor_item_t** item), (item))
WRAP_RET(size_t, cbor_encode_array_start, (size_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_bool, (bool a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_break, (unsigned char* a0, size_t a1), (a0, a1), 0)
WRAP_RET(size_t, cbor_encode_bytestring_start, (size_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_ctrl, (uint8_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_double, (double a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_half, (float a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_indef_array_start, (unsigned char* a0, size_t a1), (a0, a1), 0)
WRAP_RET(size_t, cbor_encode_indef_bytestring_start, (unsigned char* a0, size_t a1), (a0, a1), 0)
WRAP_RET(size_t, cbor_encode_indef_map_start, (unsigned char* a0, size_t a1), (a0, a1), 0)
WRAP_RET(size_t, cbor_encode_indef_string_start, (unsigned char* a0, size_t a1), (a0, a1), 0)
WRAP_RET(size_t, cbor_encode_map_start, (size_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_negint, (uint64_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_negint16, (uint16_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_negint32, (uint32_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_negint64, (uint64_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_negint8, (uint8_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_null, (unsigned char* a0, size_t a1), (a0, a1), 0)
WRAP_RET(size_t, cbor_encode_single, (float a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_string_start, (size_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_tag, (uint64_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_uint, (uint64_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_uint16, (uint16_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_uint32, (uint32_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_uint64, (uint64_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_uint8, (uint8_t a0, unsigned char* a1, size_t a2), (a0, a1, a2), 0)
WRAP_RET(size_t, cbor_encode_undef, (unsigned char* a0, size_t a1), (a0, a1), 0)
WRAP_RET(bool, cbor_float_ctrl_is_ctrl, (const cbor_item_t* item), (item), 0)
WRAP_RET(double, cbor_float_get_float, (const cbor_item_t* item), (item), 0)
WRAP_RET(float, cbor_float_get_float2, (const cbor_item_t* item), (item), 0)
WRAP_RET(float, cbor_float_get_float4, (const cbor_item_t* item), (item), 0)
WRAP_RET(double, cbor_float_get_float8, (const cbor_item_t* item), (item), 0)
WRAP_RET(cbor_float_width, cbor_float_get_width, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_get_bool, (const cbor_item_t* item), (item), 0)
WRAP_RET(uint64_t, cbor_get_int, (const cbor_item_t* item), (item), 0)
WRAP_RET(uint16_t, cbor_get_uint16, (const cbor_item_t* item), (item), 0)
WRAP_RET(uint32_t, cbor_get_uint32, (const cbor_item_t* item), (item), 0)
WRAP_RET(uint64_t, cbor_get_uint64, (const cbor_item_t* item), (item), 0)
WRAP_RET(uint8_t, cbor_get_uint8, (const cbor_item_t* item), (item), 0)
WRAP_RET(cbor_item_t*, cbor_incref, (cbor_item_t* item), (item), NULL)
WRAP_RET(cbor_int_width, cbor_int_get_width, (const cbor_item_t* item), (item), 0)
WRAP_VOID(cbor_intermediate_decref, (cbor_item_t* item), (item))
WRAP_RET(bool, cbor_is_bool, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_is_float, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_is_int, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_is_null, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_is_undef, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_isa_array, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_isa_bytestring, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_isa_float_ctrl, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_isa_map, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_isa_negint, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_isa_string, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_isa_tag, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_isa_uint, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_map_add, (cbor_item_t* item, struct cbor_pair pair), (item, pair), 0)
WRAP_RET(size_t, cbor_map_allocated, (const cbor_item_t* item), (item), 0)
WRAP_RET(cbor_item_t*, cbor_map_get, (const cbor_item_t* map, const cbor_item_t* key, bool (*eq)(const cbor_item_t*, const cbor_item_t*)), (map, key, eq), NULL)
WRAP_RET(struct cbor_pair*, cbor_map_handle, (const cbor_item_t* item), (item), NULL)
WRAP_RET(bool, cbor_map_is_definite, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_map_is_indefinite, (const cbor_item_t* item), (item), 0)
WRAP_RET(size_t, cbor_map_size, (const cbor_item_t* item), (item), 0)
WRAP_VOID(cbor_mark_negint, (cbor_item_t* item), (item))
WRAP_VOID(cbor_mark_uint, (cbor_item_t* item), (item))
WRAP_RET(cbor_item_t*, cbor_move, (cbor_item_t* item), (item), NULL)
WRAP_RET(cbor_item_t*, cbor_new_ctrl, (void), (), NULL)
WRAP_RET(cbor_item_t*, cbor_new_definite_array, (size_t size), (size), NULL)
WRAP_RET(cbor_item_t*, cbor_new_definite_bytestring, (void), (), NULL)
WRAP_RET(cbor_item_t*, cbor_new_definite_map, (size_t size), (size), NULL)
WRAP_RET(cbor_item_t*, cbor_new_definite_string, (void), (), NULL)
WRAP_RET(cbor_item_t*, cbor_new_float2, (void), (), NULL)
WRAP_RET(cbor_item_t*, cbor_new_float4, (void), (), NULL)
WRAP_RET(cbor_item_t*, cbor_new_float8, (void), (), NULL)
WRAP_RET(cbor_item_t*, cbor_new_indefinite_array, (void), (), NULL)
WRAP_RET(cbor_item_t*, cbor_new_indefinite_bytestring, (void), (), NULL)
WRAP_RET(cbor_item_t*, cbor_new_indefinite_map, (void), (), NULL)
WRAP_RET(cbor_item_t*, cbor_new_indefinite_string, (void), (), NULL)
WRAP_RET(cbor_item_t*, cbor_new_int16, (void), (), NULL)
WRAP_RET(cbor_item_t*, cbor_new_int32, (void), (), NULL)
WRAP_RET(cbor_item_t*, cbor_new_int64, (void), (), NULL)
WRAP_RET(cbor_item_t*, cbor_new_int8, (void), (), NULL)
WRAP_RET(cbor_item_t*, cbor_new_null, (void), (), NULL)
WRAP_RET(cbor_item_t*, cbor_new_tag, (uint64_t value), (value), NULL)
WRAP_RET(cbor_item_t*, cbor_new_undef, (void), (), NULL)
WRAP_VOID(cbor_null_array_start_callback, (void* a0, uint64_t a1), (a0, a1))
WRAP_VOID(cbor_null_boolean_callback, (void* a0, bool a1), (a0, a1))
WRAP_VOID(cbor_null_byte_string_callback, (void* a0, cbor_data a1, uint64_t a2), (a0, a1, a2))
WRAP_VOID(cbor_null_byte_string_start_callback, (void* a0), (a0))
WRAP_VOID(cbor_null_float2_callback, (void* a0, float a1), (a0, a1))
WRAP_VOID(cbor_null_float4_callback, (void* a0, float a1), (a0, a1))
WRAP_VOID(cbor_null_float8_callback, (void* a0, double a1), (a0, a1))
WRAP_VOID(cbor_null_indef_array_start_callback, (void* a0), (a0))
WRAP_VOID(cbor_null_indef_break_callback, (void* a0), (a0))
WRAP_VOID(cbor_null_indef_map_start_callback, (void* a0), (a0))
WRAP_VOID(cbor_null_map_start_callback, (void* a0, uint64_t a1), (a0, a1))
WRAP_VOID(cbor_null_negint16_callback, (void* a0, uint16_t a1), (a0, a1))
WRAP_VOID(cbor_null_negint32_callback, (void* a0, uint32_t a1), (a0, a1))
WRAP_VOID(cbor_null_negint64_callback, (void* a0, uint64_t a1), (a0, a1))
WRAP_VOID(cbor_null_negint8_callback, (void* a0, uint8_t a1), (a0, a1))
WRAP_VOID(cbor_null_null_callback, (void* a0), (a0))
WRAP_VOID(cbor_null_string_callback, (void* a0, cbor_data a1, uint64_t a2), (a0, a1, a2))
WRAP_VOID(cbor_null_string_start_callback, (void* a0), (a0))
WRAP_VOID(cbor_null_tag_callback, (void* a0, uint64_t a1), (a0, a1))
WRAP_VOID(cbor_null_uint16_callback, (void* a0, uint16_t a1), (a0, a1))
WRAP_VOID(cbor_null_uint32_callback, (void* a0, uint32_t a1), (a0, a1))
WRAP_VOID(cbor_null_uint64_callback, (void* a0, uint64_t a1), (a0, a1))
WRAP_VOID(cbor_null_uint8_callback, (void* a0, uint8_t a1), (a0, a1))
WRAP_VOID(cbor_null_undefined_callback, (void* a0), (a0))
WRAP_RET(size_t, cbor_refcount, (const cbor_item_t* item), (item), 0)
WRAP_RET(size_t, cbor_serialize, (const cbor_item_t* item, cbor_mutable_data buffer, size_t buffer_size), (item, buffer, buffer_size), 0)
WRAP_RET(size_t, cbor_serialize_alloc, (const cbor_item_t* item, unsigned char** buffer, size_t* buffer_size), (item, buffer, buffer_size), 0)
WRAP_RET(size_t, cbor_serialize_array, (const cbor_item_t* item, cbor_mutable_data buffer, size_t buffer_size), (item, buffer, buffer_size), 0)
WRAP_RET(size_t, cbor_serialize_bytestring, (const cbor_item_t* item, cbor_mutable_data buffer, size_t buffer_size), (item, buffer, buffer_size), 0)
WRAP_RET(size_t, cbor_serialize_float_ctrl, (const cbor_item_t* item, cbor_mutable_data buffer, size_t buffer_size), (item, buffer, buffer_size), 0)
WRAP_RET(size_t, cbor_serialize_map, (const cbor_item_t* item, cbor_mutable_data buffer, size_t buffer_size), (item, buffer, buffer_size), 0)
WRAP_RET(size_t, cbor_serialize_negint, (const cbor_item_t* item, cbor_mutable_data buffer, size_t buffer_size), (item, buffer, buffer_size), 0)
WRAP_RET(size_t, cbor_serialize_string, (const cbor_item_t* item, cbor_mutable_data buffer, size_t buffer_size), (item, buffer, buffer_size), 0)
WRAP_RET(size_t, cbor_serialize_tag, (const cbor_item_t* item, cbor_mutable_data buffer, size_t buffer_size), (item, buffer, buffer_size), 0)
WRAP_RET(size_t, cbor_serialize_uint, (const cbor_item_t* item, cbor_mutable_data buffer, size_t buffer_size), (item, buffer, buffer_size), 0)
WRAP_RET(size_t, cbor_serialized_size, (const cbor_item_t* item), (item), 0)
WRAP_VOID(cbor_set_allocs, (_cbor_malloc_t custom_malloc, _cbor_realloc_t custom_realloc, _cbor_free_t custom_free), (custom_malloc, custom_realloc, custom_free))
WRAP_VOID(cbor_set_bool, (cbor_item_t* item, bool value), (item, value))
WRAP_VOID(cbor_set_ctrl, (cbor_item_t* item, uint8_t value), (item, value))
WRAP_VOID(cbor_set_float2, (cbor_item_t* item, float value), (item, value))
WRAP_VOID(cbor_set_float4, (cbor_item_t* item, float value), (item, value))
WRAP_VOID(cbor_set_float8, (cbor_item_t* item, double value), (item, value))
WRAP_VOID(cbor_set_uint16, (cbor_item_t* item, uint16_t value), (item, value))
WRAP_VOID(cbor_set_uint32, (cbor_item_t* item, uint32_t value), (item, value))
WRAP_VOID(cbor_set_uint64, (cbor_item_t* item, uint64_t value), (item, value))
WRAP_VOID(cbor_set_uint8, (cbor_item_t* item, uint8_t value), (item, value))
WRAP_RET(struct cbor_decoder_result, cbor_stream_decode, (cbor_data source, size_t source_size, const struct cbor_callbacks* callbacks, void* context), (source, source_size, callbacks, context), ((struct cbor_decoder_result){0}))
WRAP_RET(bool, cbor_string_add_chunk, (cbor_item_t* item, cbor_item_t* chunk), (item, chunk), 0)
WRAP_RET(size_t, cbor_string_chunk_count, (const cbor_item_t* item), (item), 0)
WRAP_RET(cbor_item_t**, cbor_string_chunks_handle, (const cbor_item_t* item), (item), NULL)
WRAP_RET(size_t, cbor_string_codepoint_count, (const cbor_item_t* item), (item), 0)
WRAP_RET(cbor_mutable_data, cbor_string_handle, (const cbor_item_t* item), (item), NULL)
WRAP_RET(bool, cbor_string_is_definite, (const cbor_item_t* item), (item), 0)
WRAP_RET(bool, cbor_string_is_indefinite, (const cbor_item_t* item), (item), 0)
WRAP_RET(size_t, cbor_string_length, (const cbor_item_t* item), (item), 0)
WRAP_VOID(cbor_string_set_handle, (cbor_item_t* item, cbor_mutable_data data, size_t length), (item, data, length))
WRAP_RET(bool, cbor_structurally_equal, (const cbor_item_t* item1, const cbor_item_t* item2), (item1, item2), 0)
WRAP_RET(cbor_item_t*, cbor_tag_item, (const cbor_item_t* tag), (tag), NULL)
WRAP_VOID(cbor_tag_set_item, (cbor_item_t* tag, cbor_item_t* tagged_item), (tag, tagged_item))
WRAP_RET(uint64_t, cbor_tag_value, (const cbor_item_t* tag), (tag), 0)
WRAP_RET(cbor_type, cbor_typeof, (const cbor_item_t* item), (item), 0)
