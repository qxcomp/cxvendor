/* ============================================================
 * protobuf-c_dyn.c - dlopen 模拟链接：protobuf-c C API
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照本机 /usr/include/protobuf-c/protobuf-c.h。
 * 覆盖该头文件声明的全部 18 个公开函数（全部为真实导出）。
 * 类型按头文件原样内嵌（均为 opaque typedef struct / 函数指针 /
 * protobuf_c_boolean=int），字段顺序即 ABI；无 .h、无 wrapper。
 *
 * 单文件；链接期不需要 libprotobuf-c，仅需 -ldl。
 * 加载顺序：显式路径 > PROTOBUF_C_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > Nix > 平台默认；成功后不卸载。
 * 加载日志照搬 opendal_dyn.c 当前 diff：命中时打印候选下标与具体路径。
 *
 * 用法：
 *   protobuf_c_dyn_init(NULL);
 *   printf("%s\n", protobuf_c_version());
 *   protobuf_c_dyn_cleanup();
 * ============================================================ */

/* 双 ABI / 函数指针转换场景下抑制 GCC 警告 */
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
 typedef HMODULE protobuf_c_lib_handle_t;
 static protobuf_c_lib_handle_t protobuf_c_load_lib(const char *path) { return LoadLibraryA(path); }
 static void *protobuf_c_get_sym(protobuf_c_lib_handle_t h, const char *name) { return (void *)GetProcAddress(h, name); }
 static void protobuf_c_free_lib(protobuf_c_lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *protobuf_c_lib_error(void) { return "LoadLibrary failed"; }
 #define PROTOBUF_C_PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *protobuf_c_lib_handle_t;
 static protobuf_c_lib_handle_t protobuf_c_load_lib(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
 static void *protobuf_c_get_sym(protobuf_c_lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void protobuf_c_free_lib(protobuf_c_lib_handle_t h) { if (h) dlclose(h); }
 static const char *protobuf_c_lib_error(void) { return dlerror(); }
 #define PROTOBUF_C_PATH_SEP ':'
#endif

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<google/protobuf-c/protobuf-c.h>)
#    include <google/protobuf-c/protobuf-c.h>
#    define PROTOBUF_C_DYN_HAVE_HEADER 1
#  endif
#endif

/* ---------- 类型（protobuf-c.h 原样内嵌，字段顺序即 ABI） ---------- */
#ifndef PROTOBUF_C_DYN_HAVE_HEADER
typedef struct ProtobufCAllocator ProtobufCAllocator;
typedef struct ProtobufCBuffer ProtobufCBuffer;
typedef struct ProtobufCBufferSimple ProtobufCBufferSimple;
typedef struct ProtobufCEnumDescriptor ProtobufCEnumDescriptor;
typedef struct ProtobufCEnumValue ProtobufCEnumValue;
typedef struct ProtobufCEnumValueIndex ProtobufCEnumValueIndex;
typedef struct ProtobufCFieldDescriptor ProtobufCFieldDescriptor;
typedef struct ProtobufCMessage ProtobufCMessage;
typedef struct ProtobufCMessageDescriptor ProtobufCMessageDescriptor;
typedef struct ProtobufCMessageUnknownField ProtobufCMessageUnknownField;
typedef struct ProtobufCMethodDescriptor ProtobufCMethodDescriptor;
typedef struct ProtobufCService ProtobufCService;
typedef struct ProtobufCServiceDescriptor ProtobufCServiceDescriptor;

typedef int protobuf_c_boolean;
typedef void (*ProtobufCClosure)(const ProtobufCMessage *, void *closure_data);
typedef void (*ProtobufCServiceDestroy)(ProtobufCService *);
#endif /* PROTOBUF_C_DYN_HAVE_HEADER */

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static protobuf_c_lib_handle_t protobuf_c_handle = NULL;
static int protobuf_c_init_state = 0;

static void *protobuf_c_sym(const char *name) {
    return protobuf_c_handle ? protobuf_c_get_sym(protobuf_c_handle, name) : NULL;
}

static int protobuf_c_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    const char *so_soname = "libprotobuf-c.so.1";
    const char *so_plain = "libprotobuf-c.so";
    int n = 0, i;
    int res = -1;

    if (protobuf_c_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("PROTOBUF_C_DYN_LIB");
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
                    char *sep = strchr(q, PROTOBUF_C_PATH_SEP);
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
    candidates[n++] = "/opt/homebrew/lib/libprotobuf-c.dylib";
    candidates[n++] = "/usr/local/lib/libprotobuf-c.dylib";
#elif defined(_WIN32)
    candidates[n++] = "libprotobuf-c.dll";
#else
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
#endif

    for (i = 0; i < n; i++) {
        protobuf_c_handle = protobuf_c_load_lib(candidates[i]);
        // printf("try %d %s\n", i, candidates[i]);
        if (protobuf_c_handle) { res = i; break; }
    }
    if (!protobuf_c_handle || !protobuf_c_sym("protobuf_c_version")) {
        fprintf(stderr, "protobuf-c_dyn: cannot load protobuf-c (%s)\n", protobuf_c_lib_error());
        if (protobuf_c_handle) { protobuf_c_free_lib(protobuf_c_handle); protobuf_c_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "protobuf-c_dyn: loaded %d %s\n", res, candidates[res]);
    return 0;
}

int protobuf_c_libok(void) {
    if (protobuf_c_init_state == 0)
        protobuf_c_init_state = (protobuf_c_do_load(NULL) == 0) ? 1 : -1;
    return (protobuf_c_init_state == 1 && protobuf_c_handle != NULL) ? 1 : 0;
}

int protobuf_c_dyn_init(const char *library_path) {
    if (protobuf_c_do_load(library_path) == 0) {
        protobuf_c_init_state = 1;
        return 0;
    }
    if (protobuf_c_init_state == 0) protobuf_c_init_state = -1;
    return -1;
}

void protobuf_c_dyn_cleanup(void) {
    if (protobuf_c_handle) { protobuf_c_free_lib(protobuf_c_handle); protobuf_c_handle = NULL; }
    protobuf_c_init_state = 0;
}

int protobuf_c_dyn_has_symbol(const char *name) {
    if (!protobuf_c_libok()) return 0;
    return protobuf_c_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define PROTOBUF_C_WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                          \
    static ret (*fn) params = NULL;                                        \
    if (!fn) {                                                             \
        if (!protobuf_c_libok()) return fallback;                          \
        *(void **)(&fn) = protobuf_c_sym(#name);                           \
        if (!fn) {                                                         \
            fprintf(stderr, "protobuf-c_dyn: dlsym failed: %s\n", #name);  \
            return fallback;                                               \
        }                                                                  \
    }                                                                      \
    return fn args;                                                        \
}

#define PROTOBUF_C_WRAP_VOID(name, params, args)                                      \
void name params {                                                         \
    static void (*fn) params = NULL;                                       \
    if (!fn) {                                                             \
        if (!protobuf_c_libok()) return;                                   \
        *(void **)(&fn) = protobuf_c_sym(#name);                           \
        if (!fn) {                                                         \
            fprintf(stderr, "protobuf-c_dyn: dlsym failed: %s\n", #name);  \
            return;                                                        \
        }                                                                  \
    }                                                                      \
    fn args;                                                               \
}

/* ============================================================
 * 导出函数封装（protobuf-c.h 全部 18 个，顺序与头文件一致）
 * ============================================================ */
PROTOBUF_C_WRAP_RET(const char *, protobuf_c_version, (void), (), NULL)
PROTOBUF_C_WRAP_RET(uint32_t, protobuf_c_version_number, (void), (), 0)

PROTOBUF_C_WRAP_RET(const ProtobufCEnumValue *, protobuf_c_enum_descriptor_get_value_by_name,
         (const ProtobufCEnumDescriptor *desc, const char *name), (desc, name), NULL)
PROTOBUF_C_WRAP_RET(const ProtobufCEnumValue *, protobuf_c_enum_descriptor_get_value,
         (const ProtobufCEnumDescriptor *desc, int value), (desc, value), NULL)

PROTOBUF_C_WRAP_RET(const ProtobufCFieldDescriptor *, protobuf_c_message_descriptor_get_field_by_name,
         (const ProtobufCMessageDescriptor *desc, const char *name), (desc, name), NULL)
PROTOBUF_C_WRAP_RET(const ProtobufCFieldDescriptor *, protobuf_c_message_descriptor_get_field,
         (const ProtobufCMessageDescriptor *desc, unsigned value), (desc, value), NULL)

PROTOBUF_C_WRAP_RET(size_t, protobuf_c_message_get_packed_size,
         (const ProtobufCMessage *message), (message), 0)
PROTOBUF_C_WRAP_RET(size_t, protobuf_c_message_pack,
         (const ProtobufCMessage *message, uint8_t *out), (message, out), 0)
PROTOBUF_C_WRAP_RET(size_t, protobuf_c_message_pack_to_buffer,
         (const ProtobufCMessage *message, ProtobufCBuffer *buffer), (message, buffer), 0)
PROTOBUF_C_WRAP_RET(ProtobufCMessage *, protobuf_c_message_unpack,
         (const ProtobufCMessageDescriptor *descriptor, ProtobufCAllocator *allocator,
          size_t len, const uint8_t *data), (descriptor, allocator, len, data), NULL)
PROTOBUF_C_WRAP_VOID(protobuf_c_message_free_unpacked,
          (ProtobufCMessage *message, ProtobufCAllocator *allocator), (message, allocator))
PROTOBUF_C_WRAP_RET(protobuf_c_boolean, protobuf_c_message_check,
         (const ProtobufCMessage *message), (message), 0)
PROTOBUF_C_WRAP_VOID(protobuf_c_message_init,
          (const ProtobufCMessageDescriptor *descriptor, void *message), (descriptor, message))

PROTOBUF_C_WRAP_VOID(protobuf_c_service_destroy, (ProtobufCService *service), (service))
PROTOBUF_C_WRAP_RET(const ProtobufCMethodDescriptor *, protobuf_c_service_descriptor_get_method_by_name,
         (const ProtobufCServiceDescriptor *desc, const char *name), (desc, name), NULL)
PROTOBUF_C_WRAP_VOID(protobuf_c_buffer_simple_append,
          (ProtobufCBuffer *buffer, size_t len, const unsigned char *data), (buffer, len, data))
PROTOBUF_C_WRAP_VOID(protobuf_c_service_generated_init,
          (ProtobufCService *service, const ProtobufCServiceDescriptor *descriptor,
           ProtobufCServiceDestroy destroy), (service, descriptor, destroy))
PROTOBUF_C_WRAP_VOID(protobuf_c_service_invoke_internal,
          (ProtobufCService *service, unsigned method_index,
           const ProtobufCMessage *input, ProtobufCClosure closure, void *closure_data),
          (service, method_index, input, closure, closure_data))
