/* ============================================================
 * msgpack_dyn.c - dlopen 模拟链接：msgpack-c（C API，C99）
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照上游 c_master 的 include/msgpack/ 各公开头声明：
 * object.h 14 + pack.h 1 + unpack.h 15 + version.h 4 +
 * vrefbuffer.h 6 + zone.h 8 = 48 个非 static 函数，
 * 与源码编出的 libmsgpack-c.so.2.0.0 的 48 个导出一一对应。
 * 排除 pack_template.h / unpack_template.h 中的 static inline、
 * 宏和 static 函数；函数名保持原生原样。
 * 类型按公开头文件原样内嵌，字段顺序即 ABI；链接期不需要
 * libmsgpack-c，仅需 -ldl。
 * 加载顺序：显式路径 > MSGPACK_DYN_LIB > LD_LIBRARY_PATH > soname >
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
#  if __has_include(<msgpack.h>) && __has_include(<msgpack/sysdep.h>) && __has_include(<msgpack/pack_template.h>)
#    include <msgpack.h>
#    define MSGPACK_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef MSGPACK_DYN_HAVE_HEADER
/* ---------- 类型（include/msgpack/ 各公开头原样内嵌） ---------- */
#if defined(unix) || defined(__unix) || defined(__linux__) || defined(__APPLE__) || \
    defined(__OpenBSD__) || defined(__NetBSD__) || defined(__QNX__) || \
    defined(__QNXTO__) || defined(__HAIKU__)
  #include <sys/uio.h>
  typedef struct iovec msgpack_iovec;
#else
  struct msgpack_iovec {
      void *iov_base;
      size_t iov_len;
  };
  typedef struct msgpack_iovec msgpack_iovec;
#endif

/* zone.h */
typedef struct msgpack_zone_finalizer {
    void (*func)(void *data);
    void *data;
} msgpack_zone_finalizer;

typedef struct msgpack_zone_finalizer_array {
    msgpack_zone_finalizer *tail;
    msgpack_zone_finalizer *end;
    msgpack_zone_finalizer *array;
} msgpack_zone_finalizer_array;

typedef struct msgpack_zone_chunk msgpack_zone_chunk;

typedef struct msgpack_zone_chunk_list {
    size_t free;
    char *ptr;
    msgpack_zone_chunk *head;
} msgpack_zone_chunk_list;

typedef struct msgpack_zone {
    msgpack_zone_chunk_list chunk_list;
    msgpack_zone_finalizer_array finalizer_array;
    size_t chunk_size;
} msgpack_zone;

/* object.h */
typedef enum {
    MSGPACK_OBJECT_NIL              = 0x00,
    MSGPACK_OBJECT_BOOLEAN          = 0x01,
    MSGPACK_OBJECT_POSITIVE_INTEGER = 0x02,
    MSGPACK_OBJECT_NEGATIVE_INTEGER = 0x03,
    MSGPACK_OBJECT_FLOAT32          = 0x0a,
    MSGPACK_OBJECT_FLOAT64          = 0x04,
    MSGPACK_OBJECT_FLOAT            = 0x04,
    MSGPACK_OBJECT_STR              = 0x05,
    MSGPACK_OBJECT_ARRAY            = 0x06,
    MSGPACK_OBJECT_MAP              = 0x07,
    MSGPACK_OBJECT_BIN              = 0x08,
    MSGPACK_OBJECT_EXT              = 0x09
} msgpack_object_type;

struct msgpack_object;
struct msgpack_object_kv;

typedef struct {
    uint32_t size;
    struct msgpack_object *ptr;
} msgpack_object_array;

typedef struct {
    uint32_t size;
    struct msgpack_object_kv *ptr;
} msgpack_object_map;

typedef struct {
    uint32_t size;
    const char *ptr;
} msgpack_object_str;

typedef struct {
    uint32_t size;
    const char *ptr;
} msgpack_object_bin;

typedef struct {
    int8_t type;
    uint32_t size;
    const char *ptr;
} msgpack_object_ext;

typedef union {
    bool boolean;
    uint64_t u64;
    int64_t i64;
    double f64;
    msgpack_object_array array;
    msgpack_object_map map;
    msgpack_object_str str;
    msgpack_object_bin bin;
    msgpack_object_ext ext;
} msgpack_object_union;

typedef struct msgpack_object {
    msgpack_object_type type;
    msgpack_object_union via;
} msgpack_object;

typedef struct msgpack_object_kv {
    msgpack_object key;
    msgpack_object val;
} msgpack_object_kv;

/* pack.h */
typedef int (*msgpack_packer_write)(void *data, const char *buf, size_t len);

typedef struct msgpack_packer {
    void *data;
    msgpack_packer_write callback;
} msgpack_packer;

/* unpack.h */
typedef struct msgpack_unpacked {
    msgpack_zone *zone;
    msgpack_object data;
} msgpack_unpacked;

typedef enum {
    MSGPACK_UNPACK_SUCCESS     =  2,
    MSGPACK_UNPACK_EXTRA_BYTES =  1,
    MSGPACK_UNPACK_CONTINUE    =  0,
    MSGPACK_UNPACK_PARSE_ERROR = -1,
    MSGPACK_UNPACK_NOMEM_ERROR = -2
} msgpack_unpack_return;

typedef struct msgpack_unpacker {
    char *buffer;
    size_t used;
    size_t free;
    size_t off;
    size_t parsed;
    msgpack_zone *z;
    size_t initial_buffer_size;
    void *ctx;
} msgpack_unpacker;

/* vrefbuffer.h */
typedef struct msgpack_vrefbuffer_chunk msgpack_vrefbuffer_chunk;

typedef struct msgpack_vrefbuffer_inner_buffer {
    size_t free;
    char *ptr;
    msgpack_vrefbuffer_chunk *head;
} msgpack_vrefbuffer_inner_buffer;

typedef struct msgpack_vrefbuffer {
    msgpack_iovec *tail;
    msgpack_iovec *end;
    msgpack_iovec *array;

    size_t chunk_size;
    size_t ref_size;

    msgpack_vrefbuffer_inner_buffer inner_buffer;
} msgpack_vrefbuffer;

#endif /* MSGPACK_DYN_HAVE_HEADER */

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static lib_handle_t msgpack_handle = NULL;
static int msgpack_init_state = 0;

static void *msgpack_sym(const char *name) {
    return msgpack_handle ? get_sym(msgpack_handle, name) : NULL;
}

static int msgpack_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    const char *so_soname = "libmsgpack-c.so.2";
    const char *so_ver = "libmsgpack-c.so.2.0.0";
    const char *so_plain = "libmsgpack-c.so";
    const char *so_legacy = "libmsgpackc.so.2";
    const char *so_old = "libmsgpack.so.2";
    int n = 0, i;
    int res = -1;

    if (msgpack_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("MSGPACK_DYN_LIB");
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
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_plain);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_legacy);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_old);
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
    candidates[n++] = so_legacy;
    candidates[n++] = "libmsgpackc.so";
    candidates[n++] = so_old;
    candidates[n++] = "libmsgpack.so";
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/%s", home, so_soname);
            candidates[n] = bufs[n]; n++;
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/%s", home, so_legacy);
            candidates[n] = bufs[n]; n++;
        }
        snprintf(bufs[n], sizeof bufs[0], "/nix/var/nix/profiles/default/lib/%s", so_soname);
        candidates[n] = bufs[n]; n++;
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libmsgpack-c.dylib";
    candidates[n++] = "/usr/local/lib/libmsgpack-c.dylib";
#elif defined(_WIN32)
    candidates[n++] = "msgpack-c.dll";
    candidates[n++] = "libmsgpack-c.dll";
#else
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/x86_64-linux-gnu/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_legacy);
    candidates[n] = bufs[n]; n++;
#endif

    for (i = 0; i < n; i++) {
        msgpack_handle = load_lib(candidates[i]);
        // printf("try %d %s\n", i, candidates[i]);
        if (msgpack_handle) { res = i; break; }
    }
    if (!msgpack_handle || !msgpack_sym("msgpack_version")) {
        fprintf(stderr, "msgpack_dyn: cannot load msgpack (%s)\n", lib_error());
        if (msgpack_handle) { free_lib(msgpack_handle); msgpack_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "msgpack_dyn: loaded msgpack %d %s\n", res, candidates[res]);
    return 0;
}

int msgpack_dyn_libok(void) {
    if (msgpack_init_state == 0)
        msgpack_init_state = (msgpack_do_load(NULL) == 0) ? 1 : -1;
    return (msgpack_init_state == 1 && msgpack_handle != NULL) ? 1 : 0;
}

int msgpack_dyn_init(const char *library_path) {
    if (msgpack_do_load(library_path) == 0) {
        msgpack_init_state = 1;
        return 0;
    }
    if (msgpack_init_state == 0) msgpack_init_state = -1;
    return -1;
}

void msgpack_dyn_cleanup(void) {
    if (msgpack_handle) { free_lib(msgpack_handle); msgpack_handle = NULL; }
    msgpack_init_state = 0;
}

int msgpack_dyn_has_symbol(const char *name) {
    if (!msgpack_dyn_libok()) return 0;
    return msgpack_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                            \
ret name params {                                                              \
    static ret (*fn) params = NULL;                                            \
    if (!fn) {                                                                 \
        if (!msgpack_dyn_libok()) return fallback;                             \
        *(void **)(&fn) = msgpack_sym(#name);                                  \
        if (!fn) {                                                             \
            fprintf(stderr, "msgpack_dyn: dlsym failed: %s\n", #name);         \
            return fallback;                                                   \
        }                                                                      \
    }                                                                          \
    return fn args;                                                            \
}
#define WRAP_VOID(name, params, args)                                          \
void name params {                                                             \
    static void (*fn) params = NULL;                                           \
    if (!fn) {                                                                 \
        if (!msgpack_dyn_libok()) return;                                      \
        *(void **)(&fn) = msgpack_sym(#name);                                  \
        if (!fn) {                                                             \
            fprintf(stderr, "msgpack_dyn: dlsym failed: %s\n", #name);         \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn args;                                                                   \
}

/* ---------- 48 个公开非 static 函数（函数名原样） ---------- */
WRAP_VOID(msgpack_object_init_nil, (msgpack_object *d), (d))
WRAP_VOID(msgpack_object_init_boolean, (msgpack_object *d, bool v), (d, v))
WRAP_VOID(msgpack_object_init_unsigned_integer, (msgpack_object *d, uint64_t v), (d, v))
WRAP_VOID(msgpack_object_init_signed_integer, (msgpack_object *d, int64_t v), (d, v))
WRAP_VOID(msgpack_object_init_float32, (msgpack_object *d, float v), (d, v))
WRAP_VOID(msgpack_object_init_float64, (msgpack_object *d, double v), (d, v))
WRAP_VOID(msgpack_object_init_str, (msgpack_object *d, const char *data, uint32_t size), (d, data, size))
WRAP_VOID(msgpack_object_init_bin, (msgpack_object *d, const char *data, uint32_t size), (d, data, size))
WRAP_VOID(msgpack_object_init_ext, (msgpack_object *d, int8_t type, const char *data, uint32_t size), (d, type, data, size))
WRAP_VOID(msgpack_object_init_array, (msgpack_object *d, msgpack_object *data, uint32_t size), (d, data, size))
WRAP_VOID(msgpack_object_init_map, (msgpack_object *d, msgpack_object_kv *data, uint32_t size), (d, data, size))
WRAP_VOID(msgpack_object_print, (FILE *out, msgpack_object o), (out, o))
WRAP_RET(int, msgpack_object_print_buffer, (char *buffer, size_t buffer_size, msgpack_object o), (buffer, buffer_size, o), -1)
WRAP_RET(bool, msgpack_object_equal, (const msgpack_object x, const msgpack_object y), (x, y), false)
WRAP_RET(int, msgpack_pack_object, (msgpack_packer *pk, msgpack_object d), (pk, d), -1)
WRAP_RET(msgpack_unpack_return, msgpack_unpack_next, (msgpack_unpacked *result, const char *data, size_t len, size_t *off), (result, data, len, off), MSGPACK_UNPACK_NOMEM_ERROR)
WRAP_RET(bool, msgpack_unpacker_init, (msgpack_unpacker *mpac, size_t initial_buffer_size), (mpac, initial_buffer_size), false)
WRAP_VOID(msgpack_unpacker_destroy, (msgpack_unpacker *mpac), (mpac))
WRAP_RET(msgpack_unpacker *, msgpack_unpacker_new, (size_t initial_buffer_size), (initial_buffer_size), NULL)
WRAP_VOID(msgpack_unpacker_free, (msgpack_unpacker *mpac), (mpac))
WRAP_RET(msgpack_unpack_return, msgpack_unpacker_next, (msgpack_unpacker *mpac, msgpack_unpacked *pac), (mpac, pac), MSGPACK_UNPACK_NOMEM_ERROR)
WRAP_RET(msgpack_unpack_return, msgpack_unpacker_next_with_size, (msgpack_unpacker *mpac, msgpack_unpacked *result, size_t *p_bytes), (mpac, result, p_bytes), MSGPACK_UNPACK_NOMEM_ERROR)
WRAP_RET(int, msgpack_unpacker_execute, (msgpack_unpacker *mpac), (mpac), -1)
WRAP_RET(msgpack_object, msgpack_unpacker_data, (msgpack_unpacker *mpac), (mpac), ((msgpack_object){0}))
WRAP_RET(msgpack_zone *, msgpack_unpacker_release_zone, (msgpack_unpacker *mpac), (mpac), NULL)
WRAP_VOID(msgpack_unpacker_reset_zone, (msgpack_unpacker *mpac), (mpac))
WRAP_VOID(msgpack_unpacker_reset, (msgpack_unpacker *mpac), (mpac))
WRAP_RET(bool, msgpack_unpacker_flush_zone, (msgpack_unpacker *mpac), (mpac), false)
WRAP_RET(bool, msgpack_unpacker_expand_buffer, (msgpack_unpacker *mpac, size_t size), (mpac, size), false)
WRAP_RET(msgpack_unpack_return, msgpack_unpack, (const char *data, size_t len, size_t *off, msgpack_zone *result_zone, msgpack_object *result), (data, len, off, result_zone, result), MSGPACK_UNPACK_NOMEM_ERROR)
WRAP_RET(const char *, msgpack_version, (void), (), NULL)
WRAP_RET(int, msgpack_version_major, (void), (), -1)
WRAP_RET(int, msgpack_version_minor, (void), (), -1)
WRAP_RET(int, msgpack_version_revision, (void), (), -1)
WRAP_RET(bool, msgpack_vrefbuffer_init, (msgpack_vrefbuffer *vbuf, size_t ref_size, size_t chunk_size), (vbuf, ref_size, chunk_size), false)
WRAP_VOID(msgpack_vrefbuffer_destroy, (msgpack_vrefbuffer *vbuf), (vbuf))
WRAP_RET(int, msgpack_vrefbuffer_append_copy, (msgpack_vrefbuffer *vbuf, const char *buf, size_t len), (vbuf, buf, len), -1)
WRAP_RET(int, msgpack_vrefbuffer_append_ref, (msgpack_vrefbuffer *vbuf, const char *buf, size_t len), (vbuf, buf, len), -1)
WRAP_RET(int, msgpack_vrefbuffer_migrate, (msgpack_vrefbuffer *vbuf, msgpack_vrefbuffer *to), (vbuf, to), -1)
WRAP_VOID(msgpack_vrefbuffer_clear, (msgpack_vrefbuffer *vref), (vref))
WRAP_RET(bool, msgpack_zone_init, (msgpack_zone *zone, size_t chunk_size), (zone, chunk_size), false)
WRAP_VOID(msgpack_zone_destroy, (msgpack_zone *zone), (zone))
WRAP_RET(msgpack_zone *, msgpack_zone_new, (size_t chunk_size), (chunk_size), NULL)
WRAP_VOID(msgpack_zone_free, (msgpack_zone *zone), (zone))
WRAP_RET(bool, msgpack_zone_is_empty, (msgpack_zone *zone), (zone), true)
WRAP_VOID(msgpack_zone_clear, (msgpack_zone *zone), (zone))
WRAP_RET(void *, msgpack_zone_malloc_expand, (msgpack_zone *zone, size_t size), (zone, size), NULL)
WRAP_RET(bool, msgpack_zone_push_finalizer_expand, (msgpack_zone *zone, void (*func)(void *data), void *data), (zone, func, data), false)
