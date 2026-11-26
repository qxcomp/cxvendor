/* ============================================================
 * avro_dyn.c - dlopen 模拟链接：Apache Avro C API（C99）
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照上游 main 的 avro.h 与 avro/ 各公开头声明
 * （用 clang AST 只读提取签名核对）：公开头共 241 个 avro_* 函数，
 * 其中 avro_schema_to_specific 未由库导出，故封装其余 240 个。
 * 排除仅内部头声明的 avro_binary_encoding / avro_codec /
 * avro_codec_decode / avro_codec_encode / avro_codec_reset /
 * avro_schema_copy_root，以及 2 个数据符号 AVRO_CURRENT_ALLOCATOR /
 * AVRO_DATUM_VALUE_CLASS；函数名保持原生原样。
 * 两个变参函数 avro_set_error / avro_prefix_error 先 vsnprintf
 * 再以 "%s" 调用真实函数，保证任意参数语义不变。
 * 类型按公开头文件原样内嵌，字段顺序即 ABI；链接期不需要
 * libavro，仅需 -ldl。
 * 加载顺序：显式路径 > AVRO_DYN_LIB > LD_LIBRARY_PATH > soname >
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
#include <stdarg.h>
#include <errno.h>

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
#  if __has_include(<avro.h>)
#    include <avro.h>
#    define AVRO_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef AVRO_DYN_HAVE_HEADER
/* ---------- 类型（avro.h 与 avro/ 各公开头原样内嵌） ---------- */
enum avro_type_t {
    AVRO_STRING, AVRO_BYTES, AVRO_INT32, AVRO_INT64, AVRO_FLOAT,
    AVRO_DOUBLE, AVRO_BOOLEAN, AVRO_NULL, AVRO_RECORD, AVRO_ENUM,
    AVRO_FIXED, AVRO_MAP, AVRO_ARRAY, AVRO_UNION, AVRO_LINK,
    AVRO_INVALID = EINVAL
};
typedef enum avro_type_t avro_type_t;

enum avro_class_t { AVRO_SCHEMA, AVRO_DATUM };
typedef enum avro_class_t avro_class_t;

struct avro_obj_t {
    avro_type_t type;
    avro_class_t class_type;
    volatile int refcount;
};

typedef struct avro_obj_t *avro_schema_t;
typedef struct avro_obj_t *avro_datum_t;
typedef struct avro_reader_t_ *avro_reader_t;
typedef struct avro_writer_t_ *avro_writer_t;
typedef struct avro_file_reader_t_ *avro_file_reader_t;
typedef struct avro_file_writer_t_ *avro_file_writer_t;
typedef struct avro_schema_error_t_ *avro_schema_error_t;

typedef void *(*avro_allocator_t)(void *user_data, void *ptr, size_t osize, size_t nsize);
typedef void (*avro_free_func_t)(void *ptr, size_t sz);

typedef struct avro_raw_array {
    size_t element_size;
    size_t element_count;
    size_t allocated_size;
    void *data;
} avro_raw_array_t;

typedef struct avro_raw_map_entry { const char *key; } avro_raw_map_entry_t;

typedef struct avro_raw_map {
    avro_raw_array_t elements;
    void *indices_by_key;
} avro_raw_map_t;

typedef struct avro_wrapped_buffer avro_wrapped_buffer_t;
struct avro_wrapped_buffer {
    const void *buf;
    size_t size;
    void *user_data;
    void (*free)(avro_wrapped_buffer_t *self);
    int (*copy)(avro_wrapped_buffer_t *dest, const avro_wrapped_buffer_t *src,
                size_t offset, size_t length);
    int (*slice)(avro_wrapped_buffer_t *self, size_t offset, size_t length);
};

typedef struct avro_raw_string { avro_wrapped_buffer_t wrapped; } avro_raw_string_t;

typedef struct avro_memoize { void *cache; } avro_memoize_t;

typedef struct avro_consumer_t avro_consumer_t;
typedef struct avro_value_iface avro_value_iface_t;
typedef struct avro_value {
    avro_value_iface_t *iface;
    void *self;
} avro_value_t;

#endif /* AVRO_DYN_HAVE_HEADER */

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static lib_handle_t avro_handle = NULL;
static int avro_init_state = 0;

static void *avro_sym(const char *name) {
    return avro_handle ? get_sym(avro_handle, name) : NULL;
}

static int avro_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    const char *so_soname = "libavro.so.24";
    const char *so_ver = "libavro.so.24.0.1";
    const char *so_prev = "libavro.so.23";
    const char *so_old = "libavro.so.22";
    int n = 0, i;
    int res = -1;

    if (avro_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("AVRO_DYN_LIB");
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
                while (q && *q && n < 76) {
                    char *sep = strchr(q, PATH_SEP);
                    if (sep) *sep = 0;
                    if (*q && n + 5 < 96) {
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_soname);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_ver);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_prev);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, "libavro.so");
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
    candidates[n++] = so_prev;
    candidates[n++] = "libavro.so";
    candidates[n++] = so_old;
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/%s", home, so_soname);
            candidates[n] = bufs[n]; n++;
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/%s", home, so_prev);
            candidates[n] = bufs[n]; n++;
        }
        snprintf(bufs[n], sizeof bufs[0], "/nix/var/nix/profiles/default/lib/%s", so_soname);
        candidates[n] = bufs[n]; n++;
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libavro.dylib";
    candidates[n++] = "/usr/local/lib/libavro.dylib";
#elif defined(_WIN32)
    candidates[n++] = "avro.dll";
    candidates[n++] = "libavro.dll";
#else
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/x86_64-linux-gnu/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_prev);
    candidates[n] = bufs[n]; n++;
#endif

    for (i = 0; i < n; i++) {
        avro_handle = load_lib(candidates[i]);
        // printf("try %d %s\n", i, candidates[i]);
        if (avro_handle) { res = i; break; }
    }
    if (!avro_handle || !avro_sym("avro_strerror")) {
        fprintf(stderr, "avro_dyn: cannot load avro (%s)\n", lib_error());
        if (avro_handle) { free_lib(avro_handle); avro_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "avro_dyn: loaded %d %s\n", res, candidates[res]);
    return 0;
}

int avro_libok(void) {
    if (avro_init_state == 0)
        avro_init_state = (avro_do_load(NULL) == 0) ? 1 : -1;
    return (avro_init_state == 1 && avro_handle != NULL) ? 1 : 0;
}

int avro_dyn_init(const char *library_path) {
    if (avro_do_load(library_path) == 0) {
        avro_init_state = 1;
        return 0;
    }
    if (avro_init_state == 0) avro_init_state = -1;
    return -1;
}

void avro_dyn_cleanup(void) {
    if (avro_handle) { free_lib(avro_handle); avro_handle = NULL; }
    avro_init_state = 0;
}

int avro_dyn_has_symbol(const char *name) {
    if (!avro_libok()) return 0;
    return avro_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                            \
ret name params {                                                              \
    static ret (*fn) params = NULL;                                            \
    if (!fn) {                                                                 \
        if (!avro_libok()) return fallback;                                    \
        *(void **)(&fn) = avro_sym(#name);                                     \
        if (!fn) {                                                             \
            fprintf(stderr, "avro_dyn: dlsym failed: %s\n", #name);            \
            return fallback;                                                   \
        }                                                                      \
    }                                                                          \
    return fn args;                                                            \
}
#define WRAP_VOID(name, params, args)                                          \
void name params {                                                             \
    static void (*fn) params = NULL;                                           \
    if (!fn) {                                                                 \
        if (!avro_libok()) return;                                             \
        *(void **)(&fn) = avro_sym(#name);                                     \
        if (!fn) {                                                             \
            fprintf(stderr, "avro_dyn: dlsym failed: %s\n", #name);            \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn args;                                                                   \
}

/* ---------- 240 个公开函数（函数名原样） ---------- */
WRAP_VOID(avro_alloc_free_func, (void * p0, size_t p1), (p0, p1))
WRAP_RET(avro_datum_t, avro_array, (avro_schema_t p0), (p0), NULL)
WRAP_RET(int, avro_array_append_datum, (avro_datum_t p0, avro_datum_t p1), (p0, p1), 0)
WRAP_RET(int, avro_array_get, (const avro_datum_t p0, int64_t p1, avro_datum_t * p2), (p0, p1, p2), 0)
WRAP_RET(size_t, avro_array_size, (const avro_datum_t p0), (p0), 0)
WRAP_RET(avro_datum_t, avro_boolean, (int8_t p0), (p0), NULL)
WRAP_RET(int, avro_boolean_get, (avro_datum_t p0, int8_t * p1), (p0, p1), 0)
WRAP_RET(int, avro_boolean_set, (avro_datum_t p0, const int8_t p1), (p0, p1), 0)
WRAP_RET(avro_datum_t, avro_bytes, (const char * p0, int64_t p1), (p0, p1), NULL)
WRAP_RET(int, avro_bytes_get, (avro_datum_t p0, char ** p1, int64_t * p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_bytes_set, (avro_datum_t p0, const char * p1, const int64_t p2), (p0, p1, p2), 0)
WRAP_RET(void *, avro_calloc, (size_t p0, size_t p1), (p0, p1), NULL)
WRAP_RET(int, avro_consume_binary, (avro_reader_t p0, avro_consumer_t * p1, void * p2), (p0, p1, p2), 0)
WRAP_VOID(avro_consumer_free, (avro_consumer_t * p0), (p0))
WRAP_RET(int, avro_datum_as_value, (avro_value_t * p0, avro_datum_t p1), (p0, p1), 0)
WRAP_RET(avro_value_iface_t *, avro_datum_class, (), (), NULL)
WRAP_VOID(avro_datum_decref, (avro_datum_t p0), (p0))
WRAP_RET(int, avro_datum_equal, (avro_datum_t p0, avro_datum_t p1), (p0, p1), 0)
WRAP_RET(avro_datum_t, avro_datum_from_schema, (const avro_schema_t p0), (p0), NULL)
WRAP_RET(avro_schema_t, avro_datum_get_schema, (const avro_datum_t p0), (p0), NULL)
WRAP_RET(avro_datum_t, avro_datum_incref, (avro_datum_t p0), (p0), NULL)
WRAP_VOID(avro_datum_print, (avro_datum_t p0, FILE * p1), (p0, p1))
WRAP_RET(int, avro_datum_reset, (avro_datum_t p0), (p0), 0)
WRAP_RET(int, avro_datum_to_json, (const avro_datum_t p0, int p1, char ** p2), (p0, p1, p2), 0)
WRAP_RET(avro_datum_t, avro_double, (double p0), (p0), NULL)
WRAP_RET(int, avro_double_get, (avro_datum_t p0, double * p1), (p0, p1), 0)
WRAP_RET(int, avro_double_set, (avro_datum_t p0, const double p1), (p0, p1), 0)
WRAP_RET(avro_datum_t, avro_enum, (avro_schema_t p0, int p1), (p0, p1), NULL)
WRAP_RET(int, avro_enum_get, (const avro_datum_t p0), (p0), 0)
WRAP_RET(const char *, avro_enum_get_name, (const avro_datum_t p0), (p0), NULL)
WRAP_RET(int, avro_enum_set, (avro_datum_t p0, const int p1), (p0, p1), 0)
WRAP_RET(int, avro_enum_set_name, (avro_datum_t p0, const char * p1), (p0, p1), 0)
WRAP_RET(int, avro_file_reader, (const char * p0, avro_file_reader_t * p1), (p0, p1), 0)
WRAP_RET(int, avro_file_reader_close, (avro_file_reader_t p0), (p0), 0)
WRAP_RET(int, avro_file_reader_fp, (FILE * p0, const char * p1, int p2, avro_file_reader_t * p3), (p0, p1, p2, p3), 0)
WRAP_RET(avro_schema_t, avro_file_reader_get_writer_schema, (avro_file_reader_t p0), (p0), NULL)
WRAP_RET(int, avro_file_reader_read, (avro_file_reader_t p0, avro_schema_t p1, avro_datum_t * p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_file_reader_read_value, (avro_file_reader_t p0, avro_value_t * p1), (p0, p1), 0)
WRAP_RET(int, avro_file_writer_append, (avro_file_writer_t p0, avro_datum_t p1), (p0, p1), 0)
WRAP_RET(int, avro_file_writer_append_encoded, (avro_file_writer_t p0, const void * p1, int64_t p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_file_writer_append_value, (avro_file_writer_t p0, avro_value_t * p1), (p0, p1), 0)
WRAP_RET(int, avro_file_writer_close, (avro_file_writer_t p0), (p0), 0)
WRAP_RET(int, avro_file_writer_create, (const char * p0, avro_schema_t p1, avro_file_writer_t * p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_file_writer_create_fp, (FILE * p0, const char * p1, int p2, avro_schema_t p3, avro_file_writer_t * p4), (p0, p1, p2, p3, p4), 0)
WRAP_RET(int, avro_file_writer_create_with_codec, (const char * p0, avro_schema_t p1, avro_file_writer_t * p2, const char * p3, size_t p4), (p0, p1, p2, p3, p4), 0)
WRAP_RET(int, avro_file_writer_create_with_codec_fp, (FILE * p0, const char * p1, int p2, avro_schema_t p3, avro_file_writer_t * p4, const char * p5, size_t p6), (p0, p1, p2, p3, p4, p5, p6), 0)
WRAP_RET(int, avro_file_writer_flush, (avro_file_writer_t p0), (p0), 0)
WRAP_RET(int, avro_file_writer_open, (const char * p0, avro_file_writer_t * p1), (p0, p1), 0)
WRAP_RET(int, avro_file_writer_open_bs, (const char * p0, avro_file_writer_t * p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_file_writer_sync, (avro_file_writer_t p0), (p0), 0)
WRAP_RET(avro_datum_t, avro_fixed, (avro_schema_t p0, const char * p1, const int64_t p2), (p0, p1, p2), NULL)
WRAP_RET(int, avro_fixed_get, (avro_datum_t p0, char ** p1, int64_t * p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_fixed_set, (avro_datum_t p0, const char * p1, const int64_t p2), (p0, p1, p2), 0)
WRAP_RET(avro_datum_t, avro_float, (float p0), (p0), NULL)
WRAP_RET(int, avro_float_get, (avro_datum_t p0, float * p1), (p0, p1), 0)
WRAP_RET(int, avro_float_set, (avro_datum_t p0, const float p1), (p0, p1), 0)
WRAP_RET(avro_value_iface_t *, avro_generic_boolean_class, (), (), NULL)
WRAP_RET(int, avro_generic_boolean_new, (avro_value_t * p0, int p1), (p0, p1), 0)
WRAP_RET(avro_value_iface_t *, avro_generic_bytes_class, (), (), NULL)
WRAP_RET(int, avro_generic_bytes_new, (avro_value_t * p0, void * p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(avro_value_iface_t *, avro_generic_class_from_schema, (avro_schema_t p0), (p0), NULL)
WRAP_RET(avro_value_iface_t *, avro_generic_double_class, (), (), NULL)
WRAP_RET(int, avro_generic_double_new, (avro_value_t * p0, double p1), (p0, p1), 0)
WRAP_RET(avro_value_iface_t *, avro_generic_float_class, (), (), NULL)
WRAP_RET(int, avro_generic_float_new, (avro_value_t * p0, float p1), (p0, p1), 0)
WRAP_RET(avro_value_iface_t *, avro_generic_int_class, (), (), NULL)
WRAP_RET(int, avro_generic_int_new, (avro_value_t * p0, int32_t p1), (p0, p1), 0)
WRAP_RET(avro_value_iface_t *, avro_generic_long_class, (), (), NULL)
WRAP_RET(int, avro_generic_long_new, (avro_value_t * p0, int64_t p1), (p0, p1), 0)
WRAP_RET(avro_value_iface_t *, avro_generic_null_class, (), (), NULL)
WRAP_RET(int, avro_generic_null_new, (avro_value_t * p0), (p0), 0)
WRAP_RET(avro_value_iface_t *, avro_generic_string_class, (), (), NULL)
WRAP_RET(int, avro_generic_string_new, (avro_value_t * p0, const char * p1), (p0, p1), 0)
WRAP_RET(int, avro_generic_string_new_length, (avro_value_t * p0, const char * p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_generic_value_new, (avro_value_iface_t * p0, avro_value_t * p1), (p0, p1), 0)
WRAP_RET(avro_datum_t, avro_givebytes, (const char * p0, int64_t p1, avro_free_func_t p2), (p0, p1, p2), NULL)
WRAP_RET(int, avro_givebytes_set, (avro_datum_t p0, const char * p1, const int64_t p2, avro_free_func_t p3), (p0, p1, p2, p3), 0)
WRAP_RET(avro_datum_t, avro_givefixed, (avro_schema_t p0, const char * p1, const int64_t p2, avro_free_func_t p3), (p0, p1, p2, p3), NULL)
WRAP_RET(int, avro_givefixed_set, (avro_datum_t p0, const char * p1, const int64_t p2, avro_free_func_t p3), (p0, p1, p2, p3), 0)
WRAP_RET(avro_datum_t, avro_givestring, (const char * p0, avro_free_func_t p1), (p0, p1), NULL)
WRAP_RET(int, avro_givestring_set, (avro_datum_t p0, const char * p1, avro_free_func_t p2), (p0, p1, p2), 0)
WRAP_RET(avro_datum_t, avro_int32, (int32_t p0), (p0), NULL)
WRAP_RET(int, avro_int32_get, (avro_datum_t p0, int32_t * p1), (p0, p1), 0)
WRAP_RET(int, avro_int32_set, (avro_datum_t p0, const int32_t p1), (p0, p1), 0)
WRAP_RET(avro_datum_t, avro_int64, (int64_t p0), (p0), NULL)
WRAP_RET(int, avro_int64_get, (avro_datum_t p0, int64_t * p1), (p0, p1), 0)
WRAP_RET(int, avro_int64_set, (avro_datum_t p0, const int64_t p1), (p0, p1), 0)
WRAP_RET(avro_datum_t, avro_map, (avro_schema_t p0), (p0), NULL)
WRAP_RET(int, avro_map_get, (const avro_datum_t p0, const char * p1, avro_datum_t * p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_map_get_index, (const avro_datum_t p0, const char * p1, int * p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_map_get_key, (const avro_datum_t p0, int p1, const char ** p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_map_set, (avro_datum_t p0, const char * p1, avro_datum_t p2), (p0, p1, p2), 0)
WRAP_RET(size_t, avro_map_size, (const avro_datum_t p0), (p0), 0)
WRAP_VOID(avro_memoize_delete, (avro_memoize_t * p0, void * p1, void * p2), (p0, p1, p2))
WRAP_VOID(avro_memoize_done, (avro_memoize_t * p0), (p0))
WRAP_RET(int, avro_memoize_get, (avro_memoize_t * p0, void * p1, void * p2, void ** p3), (p0, p1, p2, p3), 0)
WRAP_VOID(avro_memoize_init, (avro_memoize_t * p0), (p0))
WRAP_VOID(avro_memoize_set, (avro_memoize_t * p0, void * p1, void * p2, void * p3), (p0, p1, p2, p3))
WRAP_RET(avro_datum_t, avro_null, (), (), NULL)
WRAP_RET(void *, avro_raw_array_append, (avro_raw_array_t * p0), (p0), NULL)
WRAP_VOID(avro_raw_array_clear, (avro_raw_array_t * p0), (p0))
WRAP_VOID(avro_raw_array_done, (avro_raw_array_t * p0), (p0))
WRAP_RET(int, avro_raw_array_ensure_size, (avro_raw_array_t * p0, size_t p1), (p0, p1), 0)
WRAP_RET(int, avro_raw_array_ensure_size0, (avro_raw_array_t * p0, size_t p1), (p0, p1), 0)
WRAP_VOID(avro_raw_array_init, (avro_raw_array_t * p0, size_t p1), (p0, p1))
WRAP_VOID(avro_raw_map_clear, (avro_raw_map_t * p0), (p0))
WRAP_VOID(avro_raw_map_done, (avro_raw_map_t * p0), (p0))
WRAP_RET(int, avro_raw_map_ensure_size, (avro_raw_map_t * p0, size_t p1), (p0, p1), 0)
WRAP_RET(void *, avro_raw_map_get, (const avro_raw_map_t * p0, const char * p1, size_t * p2), (p0, p1, p2), NULL)
WRAP_RET(int, avro_raw_map_get_or_create, (avro_raw_map_t * p0, const char * p1, void ** p2, size_t * p3), (p0, p1, p2, p3), 0)
WRAP_VOID(avro_raw_map_init, (avro_raw_map_t * p0, size_t p1), (p0, p1))
WRAP_VOID(avro_raw_string_append, (avro_raw_string_t * p0, const char * p1), (p0, p1))
WRAP_VOID(avro_raw_string_append_length, (avro_raw_string_t * p0, const void * p1, size_t p2), (p0, p1, p2))
WRAP_VOID(avro_raw_string_clear, (avro_raw_string_t * p0), (p0))
WRAP_VOID(avro_raw_string_done, (avro_raw_string_t * p0), (p0))
WRAP_RET(int, avro_raw_string_equals, (const avro_raw_string_t * p0, const avro_raw_string_t * p1), (p0, p1), 0)
WRAP_VOID(avro_raw_string_give, (avro_raw_string_t * p0, avro_wrapped_buffer_t * p1), (p0, p1))
WRAP_RET(int, avro_raw_string_grab, (const avro_raw_string_t * p0, avro_wrapped_buffer_t * p1), (p0, p1), 0)
WRAP_VOID(avro_raw_string_init, (avro_raw_string_t * p0), (p0))
WRAP_VOID(avro_raw_string_set, (avro_raw_string_t * p0, const char * p1), (p0, p1))
WRAP_VOID(avro_raw_string_set_length, (avro_raw_string_t * p0, const void * p1, size_t p2), (p0, p1, p2))
WRAP_RET(int, avro_read, (avro_reader_t p0, void * p1, int64_t p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_read_data, (avro_reader_t p0, avro_schema_t p1, avro_schema_t p2, avro_datum_t * p3), (p0, p1, p2, p3), 0)
WRAP_VOID(avro_reader_dump, (avro_reader_t p0, FILE * p1), (p0, p1))
WRAP_RET(avro_reader_t, avro_reader_file, (FILE * p0), (p0), NULL)
WRAP_RET(avro_reader_t, avro_reader_file_fp, (FILE * p0, int p1), (p0, p1), NULL)
WRAP_VOID(avro_reader_free, (avro_reader_t p0), (p0))
WRAP_RET(int, avro_reader_is_eof, (avro_reader_t p0), (p0), 0)
WRAP_RET(avro_reader_t, avro_reader_memory, (const char * p0, int64_t p1), (p0, p1), NULL)
WRAP_VOID(avro_reader_memory_set_source, (avro_reader_t p0, const char * p1, int64_t p2), (p0, p1, p2))
WRAP_VOID(avro_reader_reset, (avro_reader_t p0), (p0))
WRAP_RET(avro_datum_t, avro_record, (avro_schema_t p0), (p0), NULL)
WRAP_RET(int, avro_record_get, (const avro_datum_t p0, const char * p1, avro_datum_t * p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_record_set, (avro_datum_t p0, const char * p1, avro_datum_t p2), (p0, p1, p2), 0)
WRAP_VOID(avro_resolved_reader_clear_source, (avro_value_t * p0), (p0))
WRAP_RET(avro_value_iface_t *, avro_resolved_reader_new, (avro_schema_t p0, avro_schema_t p1), (p0, p1), NULL)
WRAP_RET(int, avro_resolved_reader_new_value, (avro_value_iface_t * p0, avro_value_t * p1), (p0, p1), 0)
WRAP_VOID(avro_resolved_reader_set_source, (avro_value_t * p0, avro_value_t * p1), (p0, p1))
WRAP_VOID(avro_resolved_writer_clear_dest, (avro_value_t * p0), (p0))
WRAP_RET(avro_value_iface_t *, avro_resolved_writer_new, (avro_schema_t p0, avro_schema_t p1), (p0, p1), NULL)
WRAP_RET(int, avro_resolved_writer_new_value, (avro_value_iface_t * p0, avro_value_t * p1), (p0, p1), 0)
WRAP_VOID(avro_resolved_writer_set_dest, (avro_value_t * p0, avro_value_t * p1), (p0, p1))
WRAP_RET(avro_consumer_t *, avro_resolver_new, (avro_schema_t p0, avro_schema_t p1), (p0, p1), NULL)
WRAP_RET(avro_schema_t, avro_schema_array, (const avro_schema_t p0), (p0), NULL)
WRAP_RET(avro_schema_t, avro_schema_array_items, (avro_schema_t p0), (p0), NULL)
WRAP_RET(avro_schema_t, avro_schema_boolean, (), (), NULL)
WRAP_RET(avro_schema_t, avro_schema_bytes, (), (), NULL)
WRAP_RET(avro_schema_t, avro_schema_copy, (avro_schema_t p0), (p0), NULL)
WRAP_RET(int, avro_schema_datum_validate, (avro_schema_t p0, avro_datum_t p1), (p0, p1), 0)
WRAP_RET(int, avro_schema_decref, (avro_schema_t p0), (p0), 0)
WRAP_RET(avro_schema_t, avro_schema_double, (), (), NULL)
WRAP_RET(avro_schema_t, avro_schema_enum, (const char * p0), (p0), NULL)
WRAP_RET(const char *, avro_schema_enum_get, (const avro_schema_t p0, int p1), (p0, p1), NULL)
WRAP_RET(int, avro_schema_enum_get_by_name, (const avro_schema_t p0, const char * p1), (p0, p1), 0)
WRAP_RET(avro_schema_t, avro_schema_enum_ns, (const char * p0, const char * p1), (p0, p1), NULL)
WRAP_RET(int, avro_schema_enum_number_of_symbols, (const avro_schema_t p0), (p0), 0)
WRAP_RET(int, avro_schema_enum_symbol_append, (const avro_schema_t p0, const char * p1), (p0, p1), 0)
WRAP_RET(int, avro_schema_equal, (avro_schema_t p0, avro_schema_t p1), (p0, p1), 0)
WRAP_RET(avro_schema_t, avro_schema_fixed, (const char * p0, const int64_t p1), (p0, p1), NULL)
WRAP_RET(avro_schema_t, avro_schema_fixed_ns, (const char * p0, const char * p1, const int64_t p2), (p0, p1, p2), NULL)
WRAP_RET(int64_t, avro_schema_fixed_size, (const avro_schema_t p0), (p0), 0)
WRAP_RET(avro_schema_t, avro_schema_float, (), (), NULL)
WRAP_RET(int, avro_schema_from_json, (const char * p0, int32_t p1, avro_schema_t * p2, avro_schema_error_t * p3), (p0, p1, p2, p3), 0)
WRAP_RET(int, avro_schema_from_json_length, (const char * p0, size_t p1, avro_schema_t * p2), (p0, p1, p2), 0)
WRAP_RET(avro_schema_t, avro_schema_get_subschema, (const avro_schema_t p0, const char * p1), (p0, p1), NULL)
WRAP_RET(avro_schema_t, avro_schema_incref, (avro_schema_t p0), (p0), NULL)
WRAP_RET(avro_schema_t, avro_schema_int, (), (), NULL)
WRAP_RET(avro_schema_t, avro_schema_link, (avro_schema_t p0), (p0), NULL)
WRAP_RET(avro_schema_t, avro_schema_link_target, (avro_schema_t p0), (p0), NULL)
WRAP_RET(avro_schema_t, avro_schema_long, (), (), NULL)
WRAP_RET(avro_schema_t, avro_schema_map, (const avro_schema_t p0), (p0), NULL)
WRAP_RET(avro_schema_t, avro_schema_map_values, (avro_schema_t p0), (p0), NULL)
WRAP_RET(int, avro_schema_match, (avro_schema_t p0, avro_schema_t p1), (p0, p1), 0)
WRAP_RET(const char *, avro_schema_name, (const avro_schema_t p0), (p0), NULL)
WRAP_RET(const char *, avro_schema_namespace, (const avro_schema_t p0), (p0), NULL)
WRAP_RET(avro_schema_t, avro_schema_null, (), (), NULL)
WRAP_RET(avro_schema_t, avro_schema_record, (const char * p0, const char * p1), (p0, p1), NULL)
WRAP_RET(int, avro_schema_record_field_append, (const avro_schema_t p0, const char * p1, const avro_schema_t p2), (p0, p1, p2), 0)
WRAP_RET(avro_schema_t, avro_schema_record_field_get, (const avro_schema_t p0, const char * p1), (p0, p1), NULL)
WRAP_RET(avro_schema_t, avro_schema_record_field_get_by_index, (const avro_schema_t p0, int p1), (p0, p1), NULL)
WRAP_RET(int, avro_schema_record_field_get_index, (const avro_schema_t p0, const char * p1), (p0, p1), 0)
WRAP_RET(const char *, avro_schema_record_field_name, (const avro_schema_t p0, int p1), (p0, p1), NULL)
WRAP_RET(size_t, avro_schema_record_size, (const avro_schema_t p0), (p0), 0)
WRAP_RET(avro_schema_t, avro_schema_string, (), (), NULL)
WRAP_RET(int, avro_schema_to_json, (const avro_schema_t p0, avro_writer_t p1), (p0, p1), 0)
WRAP_RET(const char *, avro_schema_type_name, (const avro_schema_t p0), (p0), NULL)
WRAP_RET(avro_schema_t, avro_schema_union, (), (), NULL)
WRAP_RET(int, avro_schema_union_append, (const avro_schema_t p0, const avro_schema_t p1), (p0, p1), 0)
WRAP_RET(avro_schema_t, avro_schema_union_branch, (avro_schema_t p0, int p1), (p0, p1), NULL)
WRAP_RET(avro_schema_t, avro_schema_union_branch_by_name, (avro_schema_t p0, int * p1, const char * p2), (p0, p1, p2), NULL)
WRAP_RET(size_t, avro_schema_union_size, (const avro_schema_t p0), (p0), 0)
WRAP_VOID(avro_set_allocator, (avro_allocator_t p0, void * p1), (p0, p1))
WRAP_RET(int64_t, avro_size_data, (avro_writer_t p0, avro_schema_t p1, avro_datum_t p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_skip, (avro_reader_t p0, int64_t p1), (p0, p1), 0)
WRAP_RET(int, avro_skip_data, (avro_reader_t p0, avro_schema_t p1), (p0, p1), 0)
WRAP_RET(char *, avro_str_alloc, (size_t p0), (p0), NULL)
WRAP_RET(char *, avro_strdup, (const char * p0), (p0), NULL)
WRAP_RET(const char *, avro_strerror, (), (), NULL)
WRAP_VOID(avro_str_free, (char * p0), (p0))
WRAP_RET(avro_datum_t, avro_string, (const char * p0), (p0), NULL)
WRAP_RET(int, avro_string_get, (avro_datum_t p0, char ** p1), (p0, p1), 0)
WRAP_RET(int, avro_string_set, (avro_datum_t p0, const char * p1), (p0, p1), 0)
WRAP_RET(char *, avro_strndup, (const char * p0, size_t p1), (p0, p1), NULL)
WRAP_RET(avro_datum_t, avro_union, (avro_schema_t p0, int64_t p1, const avro_datum_t p2), (p0, p1, p2), NULL)
WRAP_RET(avro_datum_t, avro_union_current_branch, (avro_datum_t p0), (p0), NULL)
WRAP_RET(int64_t, avro_union_discriminant, (const avro_datum_t p0), (p0), 0)
WRAP_RET(int, avro_union_set_discriminant, (avro_datum_t p0, int p1, avro_datum_t * p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_value_cmp, (avro_value_t * p0, avro_value_t * p1), (p0, p1), 0)
WRAP_RET(int, avro_value_cmp_fast, (avro_value_t * p0, avro_value_t * p1), (p0, p1), 0)
WRAP_RET(int, avro_value_copy, (avro_value_t * p0, const avro_value_t * p1), (p0, p1), 0)
WRAP_RET(int, avro_value_copy_fast, (avro_value_t * p0, const avro_value_t * p1), (p0, p1), 0)
WRAP_VOID(avro_value_copy_ref, (avro_value_t * p0, const avro_value_t * p1), (p0, p1))
WRAP_VOID(avro_value_decref, (avro_value_t * p0), (p0))
WRAP_RET(int, avro_value_equal, (avro_value_t * p0, avro_value_t * p1), (p0, p1), 0)
WRAP_RET(int, avro_value_equal_fast, (avro_value_t * p0, avro_value_t * p1), (p0, p1), 0)
WRAP_RET(uint32_t, avro_value_hash, (avro_value_t * p0), (p0), 0)
WRAP_VOID(avro_value_incref, (avro_value_t * p0), (p0))
WRAP_VOID(avro_value_move_ref, (avro_value_t * p0, avro_value_t * p1), (p0, p1))
WRAP_RET(int, avro_value_read, (avro_reader_t p0, avro_value_t * p1), (p0, p1), 0)
WRAP_RET(int, avro_value_sizeof, (avro_value_t * p0, size_t * p1), (p0, p1), 0)
WRAP_RET(int, avro_value_to_json, (const avro_value_t * p0, int p1, char ** p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_value_write, (avro_writer_t p0, avro_value_t * p1), (p0, p1), 0)
WRAP_RET(int, avro_wrapped_buffer_copy, (avro_wrapped_buffer_t * p0, const avro_wrapped_buffer_t * p1, size_t p2, size_t p3), (p0, p1, p2, p3), 0)
WRAP_VOID(avro_wrapped_buffer_move, (avro_wrapped_buffer_t * p0, avro_wrapped_buffer_t * p1), (p0, p1))
WRAP_RET(int, avro_wrapped_buffer_new, (avro_wrapped_buffer_t * p0, const void * p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_wrapped_buffer_new_copy, (avro_wrapped_buffer_t * p0, const void * p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_wrapped_buffer_slice, (avro_wrapped_buffer_t * p0, size_t p1, size_t p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_write, (avro_writer_t p0, void * p1, int64_t p2), (p0, p1, p2), 0)
WRAP_RET(int, avro_write_data, (avro_writer_t p0, avro_schema_t p1, avro_datum_t p2), (p0, p1, p2), 0)
WRAP_VOID(avro_writer_dump, (avro_writer_t p0, FILE * p1), (p0, p1))
WRAP_RET(avro_writer_t, avro_writer_file, (FILE * p0), (p0), NULL)
WRAP_RET(avro_writer_t, avro_writer_file_fp, (FILE * p0, int p1), (p0, p1), NULL)
WRAP_VOID(avro_writer_flush, (avro_writer_t p0), (p0))
WRAP_VOID(avro_writer_free, (avro_writer_t p0), (p0))
WRAP_RET(avro_writer_t, avro_writer_memory, (const char * p0, int64_t p1), (p0, p1), NULL)
WRAP_VOID(avro_writer_memory_set_dest, (avro_writer_t p0, const char * p1, int64_t p2), (p0, p1, p2))
WRAP_VOID(avro_writer_reset, (avro_writer_t p0), (p0))
WRAP_RET(int64_t, avro_writer_tell, (avro_writer_t p0), (p0), 0)

/* ---------- 2 个变参函数：vsnprintf 后以 "%s" 调用真实函数 ---------- */
#define WRAP_VARARGS_VOID(name)                                                \
void name(const char *fmt, ...) {                                              \
    static void (*fn)(const char *, ...) = NULL;                               \
    char buf[8192];                                                            \
    va_list ap;                                                                \
    va_start(ap, fmt);                                                         \
    vsnprintf(buf, sizeof buf, fmt ? fmt : "", ap);                            \
    va_end(ap);                                                                \
    if (!fn) {                                                                 \
        if (!avro_libok()) return;                                             \
        *(void **)(&fn) = avro_sym(#name);                                     \
        if (!fn) {                                                             \
            fprintf(stderr, "avro_dyn: dlsym failed: %s\n", #name);            \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn("%s", buf);                                                             \
}
WRAP_VARARGS_VOID(avro_set_error)
WRAP_VARARGS_VOID(avro_prefix_error)
