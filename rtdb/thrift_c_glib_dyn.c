/* ============================================================
 * thrift_c_glib_dyn.c - dlopen 模拟链接：Apache Thrift C (glib) API
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照本机 /usr/include/thrift/c_glib/ 下全部公开头文件。
 * 覆盖头文件显式声明的 133 个函数（含 get_type / error_quark / protocol
 * read·write / transport 等）。zlib 与 multiplexed 的 6 个函数本机库未导出，
 * 仍按声明封装，运行时 dlsym 失败走零值 fallback。
 * glib 基础类型按本机 glibconfig.h / gtypes.h 原样内嵌（不 include glib.h）：
 * gboolean=int, gint64=long, GType=unsigned long, enum=4 字节。
 * 所有 ThriftXxx 对象为 opaque typedef struct（只按指针使用）。
 * 单文件；链接期不需要 libthrift_c_glib / glib，仅需 -ldl。
 * 加载顺序：显式路径 > THRIFT_C_GLIB_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > Nix > 平台默认；成功后不卸载。
 * 加载日志照搬 opendal_dyn.c 当前 diff：命中时打印候选下标与具体路径。
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

/* ---------- glib 基础类型（原样内嵌，保证 ABI 宽度一致） ---------- */
typedef int             gint;
typedef int             gboolean;
typedef signed char     gint8;
typedef short           gint16;
typedef int             gint32;
typedef long            gint64;
typedef unsigned int    guint;
typedef unsigned int    guint32;
typedef char            gchar;
typedef void           *gpointer;
typedef const void     *gconstpointer;
typedef double          gdouble;
typedef unsigned long   GType;
typedef guint32         GQuark;
typedef struct _GError GError;
typedef struct _GHashTable GHashTable;
typedef void SSL_CTX;

/* ---------- Thrift 对象类型（opaque，只按指针使用） ---------- */
typedef struct _ThriftProcessor ThriftProcessor;
typedef struct _ThriftMultiplexedProcessor ThriftMultiplexedProcessor;
typedef struct _ThriftProtocol ThriftProtocol;
typedef struct _ThriftProtocolFactory ThriftProtocolFactory;
typedef struct _ThriftTransport ThriftTransport;
typedef struct _ThriftTransportFactory ThriftTransportFactory;
typedef struct _ThriftServerTransport ThriftServerTransport;
typedef struct _ThriftServer ThriftServer;
typedef struct _ThriftStruct ThriftStruct;
typedef struct _ThriftApplicationException ThriftApplicationException;
typedef struct _ThriftSSLSocket ThriftSSLSocket;

typedef enum {
    T_STOP = 0, T_VOID = 1, T_BOOL = 2, T_BYTE = 3, T_I08 = 3,
    T_DOUBLE = 4, T_I16 = 6, T_I32 = 8, T_U64 = 9, T_I64 = 10,
    T_STRING = 11, T_UTF7 = 11, T_STRUCT = 12, T_MAP = 13,
    T_SET = 14, T_LIST = 15, T_UTF8 = 16, T_UTF16 = 17
} ThriftType;

typedef enum { T_CALL = 1, T_REPLY = 2, T_EXCEPTION = 3, T_ONEWAY = 4 } ThriftMessageType;

typedef enum {
    SSLTLS = 0, SSLv3 = 2, TLSv1_0 = 3, TLSv1_1 = 4, TLSv1_2 = 5, LATEST = TLSv1_2
} ThriftSSLSocketProtocol;

typedef gboolean (*AUTHORIZATION_MANAGER_CALLBACK)(ThriftTransport *, void *, void *, GError **);

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static lib_handle_t thrift_c_glib_handle = NULL;
static int thrift_c_glib_init_state = 0;

static void *thrift_c_glib_sym(const char *name) {
    return thrift_c_glib_handle ? get_sym(thrift_c_glib_handle, name) : NULL;
}

static int thrift_c_glib_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    const char *so_soname = "libthrift_c_glib.so.0";
    const char *so_plain = "libthrift_c_glib.so";
    int n = 0, i;
    int res = -1;

    if (thrift_c_glib_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("THRIFT_C_GLIB_DYN_LIB");
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
    candidates[n++] = "/opt/homebrew/lib/libthrift_c_glib.dylib";
    candidates[n++] = "/usr/local/lib/libthrift_c_glib.dylib";
#elif defined(_WIN32)
    candidates[n++] = "libthrift_c_glib.dll";
#else
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
#endif

    for (i = 0; i < n; i++) {
        thrift_c_glib_handle = load_lib(candidates[i]);
        // printf("try %d %s\n", i, candidates[i]);
        if (thrift_c_glib_handle) { res = i; break; }
    }
    if (!thrift_c_glib_handle || !thrift_c_glib_sym("thrift_transport_get_type")) {
        fprintf(stderr, "thrift_c_glib_dyn: cannot load thrift_c_glib (%s)\n", lib_error());
        if (thrift_c_glib_handle) { free_lib(thrift_c_glib_handle); thrift_c_glib_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "thrift_c_glib_dyn: loaded thrift_c_glib %d %s\n", res, candidates[res]);
    return 0;
}

int thrift_c_glib_dyn_libok(void) {
    if (thrift_c_glib_init_state == 0)
        thrift_c_glib_init_state = (thrift_c_glib_do_load(NULL) == 0) ? 1 : -1;
    return (thrift_c_glib_init_state == 1 && thrift_c_glib_handle != NULL) ? 1 : 0;
}

int thrift_c_glib_dyn_init(const char *library_path) {
    if (thrift_c_glib_do_load(library_path) == 0) {
        thrift_c_glib_init_state = 1;
        return 0;
    }
    if (thrift_c_glib_init_state == 0) thrift_c_glib_init_state = -1;
    return -1;
}

void thrift_c_glib_dyn_cleanup(void) {
    if (thrift_c_glib_handle) { free_lib(thrift_c_glib_handle); thrift_c_glib_handle = NULL; }
    thrift_c_glib_init_state = 0;
}

int thrift_c_glib_dyn_has_symbol(const char *name) {
    if (!thrift_c_glib_dyn_libok()) return 0;
    return thrift_c_glib_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                            \
ret name params {                                                              \
    static ret (*fn) params = NULL;                                            \
    if (!fn) {                                                                 \
        if (!thrift_c_glib_dyn_libok()) return fallback;                       \
        *(void **)(&fn) = thrift_c_glib_sym(#name);                            \
        if (!fn) {                                                             \
            fprintf(stderr, "thrift_c_glib_dyn: dlsym failed: %s\n", #name);   \
            return fallback;                                                   \
        }                                                                      \
    }                                                                          \
    return fn args;                                                            \
}
#define WRAP_VOID(name, params, args)                                          \
void name params {                                                             \
    static void (*fn) params = NULL;                                           \
    if (!fn) {                                                                 \
        if (!thrift_c_glib_dyn_libok()) return;                                \
        *(void **)(&fn) = thrift_c_glib_sym(#name);                            \
        if (!fn) {                                                             \
            fprintf(stderr, "thrift_c_glib_dyn: dlsym failed: %s\n", #name);   \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn args;                                                                   \
}

/* ============================================================
 * 导出函数封装（thrift/c_glib 头文件声明的 133 个函数）
 * ============================================================ */
/* --- GType get_type (31) --- */
WRAP_RET(GType, thrift_application_exception_get_type, (void), (), 0)
WRAP_RET(GType, thrift_binary_protocol_factory_get_type, (void), (), 0)
WRAP_RET(GType, thrift_binary_protocol_get_type, (void), (), 0)
WRAP_RET(GType, thrift_buffered_transport_factory_get_type, (void), (), 0)
WRAP_RET(GType, thrift_buffered_transport_get_type, (void), (), 0)
WRAP_RET(GType, thrift_compact_protocol_factory_get_type, (void), (), 0)
WRAP_RET(GType, thrift_compact_protocol_get_type, (void), (), 0)
WRAP_RET(GType, thrift_configuration_get_type, (void), (), 0)
WRAP_RET(GType, thrift_dispatch_processor_get_type, (void), (), 0)
WRAP_RET(GType, thrift_fd_transport_get_type, (void), (), 0)
WRAP_RET(GType, thrift_framed_transport_factory_get_type, (void), (), 0)
WRAP_RET(GType, thrift_framed_transport_get_type, (void), (), 0)
WRAP_RET(GType, thrift_memory_buffer_get_type, (void), (), 0)
WRAP_RET(GType, thrift_multiplexed_processor_get_type, (void), (), 0)
WRAP_RET(GType, thrift_multiplexed_protocol_get_type, (void), (), 0)
WRAP_RET(GType, thrift_processor_get_type, (void), (), 0)
WRAP_RET(GType, thrift_protocol_decorator_get_type, (void), (), 0)
WRAP_RET(GType, thrift_protocol_factory_get_type, (void), (), 0)
WRAP_RET(GType, thrift_protocol_get_type, (void), (), 0)
WRAP_RET(GType, thrift_server_get_type, (void), (), 0)
WRAP_RET(GType, thrift_server_socket_get_type, (void), (), 0)
WRAP_RET(GType, thrift_server_transport_get_type, (void), (), 0)
WRAP_RET(GType, thrift_simple_server_get_type, (void), (), 0)
WRAP_RET(GType, thrift_socket_get_type, (void), (), 0)
WRAP_RET(GType, thrift_ssl_socket_get_type, (void), (), 0)
WRAP_RET(GType, thrift_stored_message_protocol_get_type, (void), (), 0)
WRAP_RET(GType, thrift_struct_get_type, (void), (), 0)
WRAP_RET(GType, thrift_transport_factory_get_type, (void), (), 0)
WRAP_RET(GType, thrift_transport_get_type, (void), (), 0)
WRAP_RET(GType, thrift_zlib_transport_factory_get_type, (void), (), 0)
WRAP_RET(GType, thrift_zlib_transport_get_type, (void), (), 0)

/* --- GQuark error_quark (5) --- */
WRAP_RET(GQuark, thrift_application_exception_error_quark, (void), (), 0)
WRAP_RET(GQuark, thrift_multiplexed_processor_error_quark, (void), (), 0)
WRAP_RET(GQuark, thrift_protocol_error_quark, (void), (), 0)
WRAP_RET(GQuark, thrift_server_socket_error_quark, (void), (), 0)
WRAP_RET(GQuark, thrift_transport_error_quark, (void), (), 0)

/* --- hash / equal (6) --- */
WRAP_RET(guint, thrift_boolean_hash, (gconstpointer v), (v), 0)
WRAP_RET(guint, thrift_int8_hash, (gconstpointer v), (v), 0)
WRAP_RET(guint, thrift_int16_hash, (gconstpointer v), (v), 0)
WRAP_RET(gboolean, thrift_boolean_equal, (gconstpointer a, gconstpointer b), (a, b), 0)
WRAP_RET(gboolean, thrift_int8_equal, (gconstpointer a, gconstpointer b), (a, b), 0)
WRAP_RET(gboolean, thrift_int16_equal, (gconstpointer a, gconstpointer b), (a, b), 0)

/* --- 工具函数 (3) --- */
WRAP_VOID(thrift_hash_table_get_keys, (gpointer key, gpointer value, gpointer user_data), (key, value, user_data))
WRAP_VOID(thrift_safe_hash_table_destroy, (GHashTable *hash_table), (hash_table))
WRAP_VOID(thrift_string_free, (gpointer str), (str))

/* --- processor (4) --- */
WRAP_RET(gboolean, thrift_dispatch_processor_process,
         (ThriftProcessor *processor, ThriftProtocol *in, ThriftProtocol *out, GError **error),
         (processor, in, out, error), 0)
WRAP_RET(gboolean, thrift_processor_process,
         (ThriftProcessor *processor, ThriftProtocol *in, ThriftProtocol *out, GError **error),
         (processor, in, out, error), 0)
WRAP_RET(gboolean, thrift_multiplexed_processor_process,
         (ThriftMultiplexedProcessor *processor, ThriftProtocol *in, ThriftProtocol *out, GError **error),
         (processor, in, out, error), 0)
WRAP_RET(gboolean, thrift_multiplexed_processor_register_processor,
         (ThriftProcessor *processor, const gchar *name, ThriftProcessor *mp, GError **error),
         (processor, name, mp, error), 0)

/* --- protocol factory / decorator (4) --- */
WRAP_RET(ThriftProtocol *, thrift_protocol_factory_get_protocol,
         (ThriftProtocolFactory *factory, ThriftTransport *transport), (factory, transport), NULL)
WRAP_RET(gint32, thrift_protocol_decorator_write_message_begin,
         (ThriftProtocol *protocol, const gchar *name, const ThriftMessageType message_type,
          const gint32 seqid, GError **error), (protocol, name, message_type, seqid, error), 0)
WRAP_RET(gint, thrift_binary_protocol_get_min_serialized_size,
         (ThriftProtocol *protocol, ThriftType type, GError **error), (protocol, type, error), 0)
WRAP_RET(gint, thrift_protocol_get_min_serialized_size,
         (ThriftProtocol *protocol, ThriftType type, GError **error), (protocol, type, error), 0)

/* --- protocol write (21) --- */
WRAP_RET(gint32, thrift_protocol_write_message_begin,
         (ThriftProtocol *protocol, const gchar *name, const ThriftMessageType message_type,
          const gint32 seqid, GError **error), (protocol, name, message_type, seqid, error), 0)
WRAP_RET(gint32, thrift_protocol_write_message_end, (ThriftProtocol *protocol, GError **error), (protocol, error), 0)
WRAP_RET(gint32, thrift_protocol_write_struct_begin,
         (ThriftProtocol *protocol, const gchar *name, GError **error), (protocol, name, error), 0)
WRAP_RET(gint32, thrift_protocol_write_struct_end, (ThriftProtocol *protocol, GError **error), (protocol, error), 0)
WRAP_RET(gint32, thrift_protocol_write_field_begin,
         (ThriftProtocol *protocol, const gchar *name, const ThriftType field_type,
          const gint16 field_id, GError **error), (protocol, name, field_type, field_id, error), 0)
WRAP_RET(gint32, thrift_protocol_write_field_end, (ThriftProtocol *protocol, GError **error), (protocol, error), 0)
WRAP_RET(gint32, thrift_protocol_write_field_stop, (ThriftProtocol *protocol, GError **error), (protocol, error), 0)
WRAP_RET(gint32, thrift_protocol_write_map_begin,
         (ThriftProtocol *protocol, const ThriftType key_type, const ThriftType value_type,
          const guint32 size, GError **error), (protocol, key_type, value_type, size, error), 0)
WRAP_RET(gint32, thrift_protocol_write_map_end, (ThriftProtocol *protocol, GError **error), (protocol, error), 0)
WRAP_RET(gint32, thrift_protocol_write_list_begin,
         (ThriftProtocol *protocol, const ThriftType element_type, const guint32 size, GError **error),
         (protocol, element_type, size, error), 0)
WRAP_RET(gint32, thrift_protocol_write_list_end, (ThriftProtocol *protocol, GError **error), (protocol, error), 0)
WRAP_RET(gint32, thrift_protocol_write_set_begin,
         (ThriftProtocol *protocol, const ThriftType element_type, const guint32 size, GError **error),
         (protocol, element_type, size, error), 0)
WRAP_RET(gint32, thrift_protocol_write_set_end, (ThriftProtocol *protocol, GError **error), (protocol, error), 0)
WRAP_RET(gint32, thrift_protocol_write_bool,
         (ThriftProtocol *protocol, const gboolean value, GError **error), (protocol, value, error), 0)
WRAP_RET(gint32, thrift_protocol_write_byte,
         (ThriftProtocol *protocol, const gint8 value, GError **error), (protocol, value, error), 0)
WRAP_RET(gint32, thrift_protocol_write_i16,
         (ThriftProtocol *protocol, const gint16 value, GError **error), (protocol, value, error), 0)
WRAP_RET(gint32, thrift_protocol_write_i32,
         (ThriftProtocol *protocol, const gint32 value, GError **error), (protocol, value, error), 0)
WRAP_RET(gint32, thrift_protocol_write_i64,
         (ThriftProtocol *protocol, const gint64 value, GError **error), (protocol, value, error), 0)
WRAP_RET(gint32, thrift_protocol_write_double,
         (ThriftProtocol *protocol, const gdouble value, GError **error), (protocol, value, error), 0)
WRAP_RET(gint32, thrift_protocol_write_string,
         (ThriftProtocol *protocol, const gchar *str, GError **error), (protocol, str, error), 0)
WRAP_RET(gint32, thrift_protocol_write_binary,
         (ThriftProtocol *protocol, const gpointer buf, const guint32 len, GError **error),
         (protocol, buf, len, error), 0)

/* --- protocol read (21) --- */
WRAP_RET(gint32, thrift_protocol_read_message_begin,
         (ThriftProtocol *protocol, gchar **name, ThriftMessageType *message_type, gint32 *seqid, GError **error),
         (protocol, name, message_type, seqid, error), 0)
WRAP_RET(gint32, thrift_protocol_read_message_end, (ThriftProtocol *protocol, GError **error), (protocol, error), 0)
WRAP_RET(gint32, thrift_protocol_read_struct_begin,
         (ThriftProtocol *protocol, gchar **name, GError **error), (protocol, name, error), 0)
WRAP_RET(gint32, thrift_protocol_read_struct_end, (ThriftProtocol *protocol, GError **error), (protocol, error), 0)
WRAP_RET(gint32, thrift_protocol_read_field_begin,
         (ThriftProtocol *protocol, gchar **name, ThriftType *field_type, gint16 *field_id, GError **error),
         (protocol, name, field_type, field_id, error), 0)
WRAP_RET(gint32, thrift_protocol_read_field_end, (ThriftProtocol *protocol, GError **error), (protocol, error), 0)
WRAP_RET(gint32, thrift_protocol_read_map_begin,
         (ThriftProtocol *protocol, ThriftType *key_type, ThriftType *value_type, guint32 *size, GError **error),
         (protocol, key_type, value_type, size, error), 0)
WRAP_RET(gint32, thrift_protocol_read_map_end, (ThriftProtocol *protocol, GError **error), (protocol, error), 0)
WRAP_RET(gint32, thrift_protocol_read_list_begin,
         (ThriftProtocol *protocol, ThriftType *element_type, guint32 *size, GError **error),
         (protocol, element_type, size, error), 0)
WRAP_RET(gint32, thrift_protocol_read_list_end, (ThriftProtocol *protocol, GError **error), (protocol, error), 0)
WRAP_RET(gint32, thrift_protocol_read_set_begin,
         (ThriftProtocol *protocol, ThriftType *element_type, guint32 *size, GError **error),
         (protocol, element_type, size, error), 0)
WRAP_RET(gint32, thrift_protocol_read_set_end, (ThriftProtocol *protocol, GError **error), (protocol, error), 0)
WRAP_RET(gint32, thrift_protocol_read_bool,
         (ThriftProtocol *protocol, gboolean *value, GError **error), (protocol, value, error), 0)
WRAP_RET(gint32, thrift_protocol_read_byte,
         (ThriftProtocol *protocol, gint8 *value, GError **error), (protocol, value, error), 0)
WRAP_RET(gint32, thrift_protocol_read_i16,
         (ThriftProtocol *protocol, gint16 *value, GError **error), (protocol, value, error), 0)
WRAP_RET(gint32, thrift_protocol_read_i32,
         (ThriftProtocol *protocol, gint32 *value, GError **error), (protocol, value, error), 0)
WRAP_RET(gint32, thrift_protocol_read_i64,
         (ThriftProtocol *protocol, gint64 *value, GError **error), (protocol, value, error), 0)
WRAP_RET(gint32, thrift_protocol_read_double,
         (ThriftProtocol *protocol, gdouble *value, GError **error), (protocol, value, error), 0)
WRAP_RET(gint32, thrift_protocol_read_string,
         (ThriftProtocol *protocol, gchar **str, GError **error), (protocol, str, error), 0)
WRAP_RET(gint32, thrift_protocol_read_binary,
         (ThriftProtocol *protocol, gpointer *buf, guint32 *len, GError **error), (protocol, buf, len, error), 0)
WRAP_RET(gint32, thrift_protocol_skip,
         (ThriftProtocol *protocol, ThriftType type, GError **error), (protocol, type, error), 0)

/* --- struct read/write (2) --- */
WRAP_RET(gint32, thrift_struct_read,
         (ThriftStruct *object, ThriftProtocol *protocol, GError **error), (object, protocol, error), 0)
WRAP_RET(gint32, thrift_struct_write,
         (ThriftStruct *object, ThriftProtocol *protocol, GError **error), (object, protocol, error), 0)

/* --- transport (17) --- */
WRAP_RET(gboolean, thrift_transport_is_open, (ThriftTransport *transport), (transport), 0)
WRAP_RET(gboolean, thrift_transport_open, (ThriftTransport *transport, GError **error), (transport, error), 0)
WRAP_RET(gboolean, thrift_transport_peek, (ThriftTransport *transport, GError **error), (transport, error), 0)
WRAP_RET(gboolean, thrift_transport_close, (ThriftTransport *transport, GError **error), (transport, error), 0)
WRAP_RET(gint32, thrift_transport_read,
         (ThriftTransport *transport, gpointer buf, guint32 len, GError **error), (transport, buf, len, error), 0)
WRAP_RET(gboolean, thrift_transport_read_end, (ThriftTransport *transport, GError **error), (transport, error), 0)
WRAP_RET(gboolean, thrift_transport_write,
         (ThriftTransport *transport, const gpointer buf, const guint32 len, GError **error),
         (transport, buf, len, error), 0)
WRAP_RET(gboolean, thrift_transport_write_end, (ThriftTransport *transport, GError **error), (transport, error), 0)
WRAP_RET(gboolean, thrift_transport_flush, (ThriftTransport *transport, GError **error), (transport, error), 0)
WRAP_RET(gint32, thrift_transport_read_all,
         (ThriftTransport *transport, gpointer buf, guint32 len, GError **error), (transport, buf, len, error), 0)
WRAP_RET(gboolean, thrift_socket_is_open, (ThriftTransport *transport), (transport), 0)
WRAP_RET(gboolean, thrift_socket_open, (ThriftTransport *transport, GError **error), (transport, error), 0)
WRAP_RET(gboolean, thrift_socket_close, (ThriftTransport *transport, GError **error), (transport, error), 0)
WRAP_RET(gboolean, thrift_ssl_socket_is_open, (ThriftTransport *transport), (transport), 0)
WRAP_RET(gboolean, thrift_ssl_socket_open, (ThriftTransport *transport, GError **error), (transport, error), 0)
WRAP_RET(gboolean, thrift_ssl_socket_close, (ThriftTransport *transport, GError **error), (transport, error), 0)
WRAP_RET(gboolean, thrift_ssl_socket_authorize, (ThriftTransport *transport, GError **error), (transport, error), 0)

/* --- transport factory (4) --- */
WRAP_RET(ThriftTransport *, thrift_buffered_transport_factory_get_transport,
         (ThriftTransportFactory *factory, ThriftTransport *transport), (factory, transport), NULL)
WRAP_RET(ThriftTransport *, thrift_framed_transport_factory_get_transport,
         (ThriftTransportFactory *factory, ThriftTransport *transport), (factory, transport), NULL)
WRAP_RET(ThriftTransport *, thrift_zlib_transport_factory_get_transport,
         (ThriftTransportFactory *factory, ThriftTransport *transport), (factory, transport), NULL)
WRAP_RET(ThriftTransport *, thrift_transport_factory_get_transport,
         (ThriftTransportFactory *factory, ThriftTransport *transport), (factory, transport), NULL)

/* --- server transport (3) --- */
WRAP_RET(gboolean, thrift_server_transport_listen, (ThriftServerTransport *transport, GError **error), (transport, error), 0)
WRAP_RET(ThriftTransport *, thrift_server_transport_accept, (ThriftServerTransport *transport, GError **error), (transport, error), NULL)
WRAP_RET(gboolean, thrift_server_transport_close, (ThriftServerTransport *transport, GError **error), (transport, error), 0)

/* --- server (2) --- */
WRAP_RET(gboolean, thrift_server_serve, (ThriftServer *server, GError **error), (server, error), 0)
WRAP_VOID(thrift_server_stop, (ThriftServer *server), (server))

/* --- ssl socket (10) --- */
WRAP_RET(SSL_CTX *, thrift_ssl_socket_context_initialize,
         (ThriftSSLSocketProtocol ssl_protocol, GError **error), (ssl_protocol, error), NULL)
WRAP_RET(ThriftSSLSocket *, thrift_ssl_socket_new,
         (ThriftSSLSocketProtocol ssl_protocol, GError **error), (ssl_protocol, error), NULL)
WRAP_RET(ThriftSSLSocket *, thrift_ssl_socket_new_with_host,
         (ThriftSSLSocketProtocol ssl_protocol, gchar *hostname, guint port, GError **error),
         (ssl_protocol, hostname, port, error), NULL)
WRAP_RET(gboolean, thrift_ssl_load_cert_from_file,
         (ThriftSSLSocket *ssl_socket, const char *file_name), (ssl_socket, file_name), 0)
WRAP_RET(gboolean, thrift_ssl_load_cert_from_buffer,
         (ThriftSSLSocket *ssl_socket, const char chain_certs[]), (ssl_socket, chain_certs), 0)
WRAP_VOID(thrift_ssl_socket_set_manager,
          (ThriftSSLSocket *ssl_socket, AUTHORIZATION_MANAGER_CALLBACK callback), (ssl_socket, callback))
WRAP_VOID(thrift_ssl_socket_initialize_openssl, (void), ())
WRAP_VOID(thrift_ssl_socket_finalize_openssl, (void), ())
WRAP_RET(gboolean, thrift_zlib_transport_verify_checksum, (ThriftTransport *transport, GError **error), (transport, error), 0)
WRAP_RET(gboolean, thrift_zlib_transport_finish, (ThriftTransport *transport, GError **error), (transport, error), 0)
