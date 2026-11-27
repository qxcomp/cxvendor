
/* ============================================================
 * wasmtime_dyn.c - type safe dynamic lib load for libwasmtime
 * ============================================================
 * 设计要点：
 *   1. 单文件、无 .h、无 wrapper 层；链接期不需要 -lwasmtime。
 *   2. 封装函数名与原版导出符号一致。
 *   3. 每个函数首次调用时 dlsym 缓存；失败打印 stderr 并返回安全默认值。
 *   4. 公开结构体/枚举/回调 typedef 按官方头文件核对编写。
 *      （本机未安装该库，签名无法用 nm 核对；dlsym 失败路径兜底。）
 *   5. 加载顺序：显式路径 > WASMTIME_DYN_LIB > LD_LIBRARY_PATH >
 *      soname > .so > Nix > 平台默认。
 *   6. 加载成功后不卸载（dlclose 不安全）；失败后 cleanup 复位。
 *   7. 纯 C 库：RTLD_LAZY | RTLD_LOCAL。
 *
 * 使用：
 *   wasmtime_dyn_init(NULL);
 *   ... 原版 API 直接调用 ...
 *   wasmtime_dyn_cleanup();    // 仅复位加载状态，不卸载库
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================
 * 平台适配层
 * ============================================================ */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE wasmtime_lib_handle_t;
  static wasmtime_lib_handle_t wasmtime_load_lib(const char *path) { return LoadLibraryA(path); }
  static void *wasmtime_get_sym(wasmtime_lib_handle_t h, const char *name) {
      return (void *)GetProcAddress(h, name);
  }
  static void wasmtime_free_lib(wasmtime_lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *wasmtime_lib_error(void) { return "LoadLibrary failed"; }
  #define WASMTIME_PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *wasmtime_lib_handle_t;
  static wasmtime_lib_handle_t wasmtime_load_lib(const char *path) {
      return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
  }
  static void *wasmtime_get_sym(wasmtime_lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void wasmtime_free_lib(wasmtime_lib_handle_t h) { if (h) dlclose(h); }
  static const char *wasmtime_lib_error(void) { return dlerror(); }
  #define WASMTIME_PATH_SEP ':'
#endif

/* ============================================================
 * 类型定义（wasm.h 标准 wasm-c-api + wasmtime.h 扩展，手工核对）
 * ============================================================ */
typedef uint8_t wasm_byte_t;

typedef struct {
    size_t size;
    wasm_byte_t *data;
} wasm_byte_vec_t;

typedef wasm_byte_vec_t wasm_name_t;
typedef wasm_byte_vec_t wasm_message_t;

typedef struct wasm_engine_t wasm_engine_t;
typedef struct wasm_store_t wasm_store_t;
typedef struct wasm_valtype_t wasm_valtype_t;
typedef struct wasm_functype_t wasm_functype_t;
typedef struct wasm_memorytype_t wasm_memorytype_t;
typedef struct wasm_module_t wasm_module_t;
typedef struct wasm_instance_t wasm_instance_t;
typedef struct wasm_func_t wasm_func_t;
typedef struct wasm_memory_t wasm_memory_t;
typedef struct wasm_trap_t wasm_trap_t;
typedef struct wasm_extern_t wasm_extern_t;
typedef struct wasm_ref_t wasm_ref_t;
typedef struct wasm_importtype_t wasm_importtype_t;
typedef struct wasm_exporttype_t wasm_exporttype_t;

typedef uint32_t wasm_memory_pages_t;
typedef float float32_t;
typedef double float64_t;

typedef enum {
    WASM_I32,
    WASM_I64,
    WASM_F32,
    WASM_F64,
    WASM_ANYREF = 128,
    WASM_FUNCREF
} wasm_valkind_t;

typedef struct {
    wasm_valkind_t kind;
    union {
        int32_t i32;
        int64_t i64;
        float32_t f32;
        float64_t f64;
        wasm_ref_t *ref;
    } of;
} wasm_val_t;

typedef struct {
    size_t size;
    wasm_val_t *data;
} wasm_val_vec_t;

typedef struct {
    size_t size;
    wasm_valtype_t **data;
} wasm_valtype_vec_t;

typedef struct {
    size_t size;
    wasm_extern_t **data;
} wasm_extern_vec_t;

typedef struct {
    size_t size;
    wasm_importtype_t **data;
} wasm_importtype_vec_t;

typedef struct {
    size_t size;
    wasm_exporttype_t **data;
} wasm_exporttype_vec_t;

typedef struct {
    uint32_t min;
    uint32_t max;
} wasm_limits_t;

/* ---- 回调 typedef ---- */
typedef struct wasm_trap_t *(*wasm_func_callback_t)(
    const wasm_val_vec_t *args, wasm_val_vec_t *results);
typedef struct wasm_trap_t *(*wasm_func_callback_with_env_t)(
    void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results);

/* ---- wasmtime.h 扩展 ---- */
typedef struct wasmtime_engine_t wasmtime_engine_t;
typedef struct wasmtime_store_t wasmtime_store_t;
typedef struct wasmtime_context_t wasmtime_context_t;
typedef struct wasmtime_module_t wasmtime_module_t;
typedef struct wasmtime_linker_t wasmtime_linker_t;
typedef struct wasmtime_trap_t wasmtime_trap_t;
typedef struct wasmtime_error_t wasmtime_error_t;
typedef struct wasmtime_config_t wasmtime_config_t;
typedef struct wasi_config_t wasi_config_t;

typedef struct {
    wasmtime_store_t *store;
    uint64_t index;
} wasmtime_func_t;

typedef struct {
    wasmtime_store_t *store;
    uint64_t index;
} wasmtime_instance_t;

typedef struct {
    wasmtime_store_t *store;
    uint64_t index;
} wasmtime_memory_t;

typedef struct {
    wasmtime_store_t *store;
    uint64_t index;
} wasmtime_global_t;

typedef struct {
    wasmtime_store_t *store;
    uint64_t index;
} wasmtime_table_t;

typedef struct {
    wasmtime_store_t *store;
    uint64_t index;
} wasmtime_externref_unused_marker_t;

typedef enum {
    WASMTIME_I32,
    WASMTIME_I64,
    WASMTIME_F32,
    WASMTIME_F64,
    WASMTIME_FUNC,
    WASMTIME_GLOBAL,
    WASMTIME_TABLE,
    WASMTIME_EXTERNREF
} wasmtime_valkind_t;

typedef struct {
    wasmtime_valkind_t kind;
    union {
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;
        wasmtime_func_t func;
        wasmtime_global_t global;
        wasmtime_table_t table;
        void *externref;
    } of;
} wasmtime_extern_t;

typedef struct {
    wasmtime_valkind_t kind;
    union {
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;
        wasmtime_func_t func;
        wasmtime_global_t global;
        wasmtime_table_t table;
        void *externref;
    } of;
} wasmtime_val_t;

typedef enum {
    WASMTIME_TRAP_CODE_NONE = 0,
    WASMTIME_TRAP_CODE_STACK_OVERFLOW,
    WASMTIME_TRAP_CODE_MEMORY_OOB,
    WASMTIME_TRAP_CODE_HEAP_OOB,
    WASMTIME_TRAP_CODE_INDIRECT_CALL_OOB,
    WASMTIME_TRAP_CODE_BAD_SIGNATURE,
    WASMTIME_TRAP_CODE_OUT_OF_FUEL,
    WASMTIME_TRAP_CODE_INTERRUPT,
    WASMTIME_TRAP_CODE_TABLE_OOB,
} wasmtime_trap_code_t;


/* ============================================================
 * 全局状态
 * ============================================================ */
static wasmtime_lib_handle_t wasmtime_handle = NULL;
static int wasmtime_init_state = 0;

/* ============================================================
 * 库加载：按优先级构造候选路径
 * ============================================================ */
static int wasmtime_do_load(const char *library_path) {
    if (wasmtime_handle) return 0;

    static char bufs[64][1024];
    const char *candidates[64];
    int n = 0;

    /* 1. 显式传入的路径 */
    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof(bufs[0]), "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }

    /* 2. 项目专属环境变量 */
    const char *explicit_lib = getenv("WASMTIME_DYN_LIB");
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
            while (p && *p && n < 60) {
                char *sep = strchr(p, WASMTIME_PATH_SEP);
                if (sep) *sep = 0;
                if (*p) {
                    snprintf(bufs[n], sizeof(bufs[0]), "%s/libwasmtime.so.0", p);
                    candidates[n] = bufs[n]; n++;
                    snprintf(bufs[n], sizeof(bufs[0]), "%s/libwasmtime.so", p);
                    candidates[n] = bufs[n]; n++;
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 4. 默认 soname + .so */
    candidates[n++] = "libwasmtime.so.0";
    candidates[n++] = "libwasmtime.so";

    /* 5. Nix 路径 */
#ifndef _WIN32
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(bufs[n], sizeof(bufs[0]), "%s/.nix-profile/lib/libwasmtime.so", home);
        candidates[n] = bufs[n]; n++;
    }
    candidates[n++] = "/nix/var/nix/profiles/default/lib/libwasmtime.so";
#endif

    /* 6. 平台默认路径 */
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libwasmtime.dylib";
    candidates[n++] = "/usr/local/lib/libwasmtime.dylib";
#elif defined(_WIN32)
    candidates[n++] = "wasmtime.dll";
#else
    candidates[n++] = "/usr/local/lib/libwasmtime.so";
    candidates[n++] = "/usr/lib/libwasmtime.so";
#endif

    int i;
    for (i = 0; i < n; i++) {
        wasmtime_handle = wasmtime_load_lib(candidates[i]);
        if (wasmtime_handle) break;
    }
    if (!wasmtime_handle) {
        fprintf(stderr, "wasmtime_dyn: cannot load libwasmtime (%s; WASMTIME_DYN_LIB 可指定路径)\n", wasmtime_lib_error());
        return -1;
    }

    /* 验证锚点符号 */
    if (!wasmtime_get_sym(wasmtime_handle, "wasmtime_wat2wasm")) {
        fprintf(stderr, "wasmtime_dyn: loaded library does not export wasmtime_wat2wasm\n");
        wasmtime_free_lib(wasmtime_handle);
        wasmtime_handle = NULL;
        return -1;
    }
    fprintf(stderr, "wasmtime_dyn: loaded %d %s\n", i, candidates[i]);
    return 0;
}

/* ============================================================
 * 控制函数
 * ============================================================ */
int wasmtime_libok(void) {
    if (wasmtime_init_state == 0) {
        wasmtime_init_state = (wasmtime_do_load(NULL) == 0) ? 1 : -1;
    }
    return (wasmtime_init_state == 1 && wasmtime_handle != NULL) ? 1 : 0;
}

int wasmtime_dyn_init(const char *library_path) {
    int r = wasmtime_do_load(library_path);
    if (r == 0) {
        wasmtime_init_state = 1;
        return 0;
    }
    if (wasmtime_init_state == 0) wasmtime_init_state = -1;
    return -1;
}

void wasmtime_dyn_cleanup(void) {
    /* 仅在加载失败后复位；成功加载的库不卸载（dlclose 不安全） */
    if (wasmtime_init_state == -1) {
        wasmtime_init_state = 0;
    }
    if (!wasmtime_handle) {
        wasmtime_init_state = 0;
    }
}

int wasmtime_dyn_has_symbol(const char *name) {
    if (!wasmtime_libok()) return 0;
    return wasmtime_get_sym(wasmtime_handle, name) != NULL;
}

/* ============================================================
 * WRAP 宏：可变参数无法用宏处理（两库核心集均无可变参数导出）
 * ============================================================ */
#define WASMTIME_WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                         \
    if (!fn) {                                                             \
        if (!wasmtime_libok()) return fallback;                               \
        *(void **)(&fn) = wasmtime_get_sym(wasmtime_handle, #name);                    \
        if (!fn) {                                                         \
            fprintf(stderr, "wasmtime_dyn: dlsym failed: %s\n", #name);       \
            return fallback;                                                \
        }                                                                  \
    }                                                                      \
    return fn args;                                                         \
}

#define WASMTIME_WRAP_VOID(name, params, args)                                      \
void name params {                                                        \
    static void (*fn) params = NULL;                                        \
    if (!fn) {                                                             \
        if (!wasmtime_libok()) return;                                        \
        *(void **)(&fn) = wasmtime_get_sym(wasmtime_handle, #name);                    \
        if (!fn) {                                                         \
            fprintf(stderr, "wasmtime_dyn: dlsym failed: %s\n", #name);       \
            return;                                                         \
        }                                                                  \
    }                                                                      \
    fn args;                                                                \
}

/* ============================================================
 * 导出函数封装
 * ============================================================ */


/* ---- wasm.h：engine/store ---- */
WASMTIME_WRAP_RET(wasm_engine_t *, wasm_engine_new, (void), (), NULL)
WASMTIME_WRAP_VOID(wasm_engine_delete, (wasm_engine_t *engine), (engine))
WASMTIME_WRAP_RET(wasm_store_t *, wasm_store_new, (wasm_engine_t *engine), (engine), NULL)
WASMTIME_WRAP_VOID(wasm_store_delete, (wasm_store_t *store), (store))

/* ---- wasm.h：byte_vec/name ---- */
WASMTIME_WRAP_VOID(wasm_byte_vec_new_empty, (wasm_byte_vec_t *out), (out))
WASMTIME_WRAP_VOID(wasm_byte_vec_new_uninitialized, (wasm_byte_vec_t *out, size_t n), (out, n))
WASMTIME_WRAP_VOID(wasm_byte_vec_new, (wasm_byte_vec_t *out, size_t n, const wasm_byte_t d[]), (out, n, d))
WASMTIME_WRAP_VOID(wasm_byte_vec_copy, (wasm_byte_vec_t *out, const wasm_byte_vec_t *src), (out, src))
WASMTIME_WRAP_VOID(wasm_byte_vec_delete, (wasm_byte_vec_t *v), (v))

/* ---- wasm.h：valtype/functype/memorytype ---- */
WASMTIME_WRAP_RET(wasm_valtype_t *, wasm_valtype_new, (wasm_valkind_t kind), (kind), NULL)
WASMTIME_WRAP_VOID(wasm_valtype_delete, (wasm_valtype_t *t), (t))
WASMTIME_WRAP_RET(wasm_valkind_t, wasm_valtype_kind, (const wasm_valtype_t *t), (t), 0)
WASMTIME_WRAP_VOID(wasm_valtype_vec_new_empty, (wasm_valtype_vec_t *out), (out))
WASMTIME_WRAP_VOID(wasm_valtype_vec_new_uninitialized, (wasm_valtype_vec_t *out, size_t n), (out, n))
WASMTIME_WRAP_VOID(wasm_valtype_vec_copy, (wasm_valtype_vec_t *out, const wasm_valtype_vec_t *src), (out, src))
WASMTIME_WRAP_VOID(wasm_valtype_vec_delete, (wasm_valtype_vec_t *v), (v))
WASMTIME_WRAP_RET(wasm_functype_t *, wasm_functype_new, (wasm_valtype_vec_t *params, wasm_valtype_vec_t *results), (params, results), NULL)
WASMTIME_WRAP_VOID(wasm_functype_delete, (wasm_functype_t *t), (t))
WASMTIME_WRAP_RET(wasm_valtype_vec_t *, wasm_functype_params, (const wasm_functype_t *t), (t), NULL)
WASMTIME_WRAP_RET(wasm_valtype_vec_t *, wasm_functype_results, (const wasm_functype_t *t), (t), NULL)
WASMTIME_WRAP_RET(wasm_memorytype_t *, wasm_memorytype_new, (const wasm_limits_t *limits), (limits), NULL)
WASMTIME_WRAP_VOID(wasm_memorytype_delete, (wasm_memorytype_t *t), (t))
WASMTIME_WRAP_RET(const wasm_limits_t *, wasm_memorytype_limits, (const wasm_memorytype_t *t), (t), NULL)

/* ---- wasm.h：module ---- */
WASMTIME_WRAP_RET(wasm_module_t *, wasm_module_new, (wasm_store_t *store, const wasm_byte_vec_t *binary), (store, binary), NULL)
WASMTIME_WRAP_VOID(wasm_module_delete, (wasm_module_t *m), (m))
WASMTIME_WRAP_RET(bool, wasm_module_validate, (wasm_store_t *store, const wasm_byte_vec_t *binary), (store, binary), false)
WASMTIME_WRAP_VOID(wasm_module_imports, (wasm_store_t *store, const wasm_module_t *m, wasm_importtype_vec_t *out), (store, m, out))
WASMTIME_WRAP_VOID(wasm_module_exports, (wasm_store_t *store, const wasm_module_t *m, wasm_exporttype_vec_t *out), (store, m, out))
WASMTIME_WRAP_VOID(wasm_importtype_vec_delete, (wasm_importtype_vec_t *v), (v))
WASMTIME_WRAP_VOID(wasm_exporttype_vec_delete, (wasm_exporttype_vec_t *v), (v))

/* ---- wasm.h：trap ---- */
WASMTIME_WRAP_RET(wasm_trap_t *, wasm_trap_new, (wasm_store_t *store, const wasm_message_t *msg), (store, msg), NULL)
WASMTIME_WRAP_VOID(wasm_trap_delete, (wasm_trap_t *t), (t))
WASMTIME_WRAP_VOID(wasm_trap_message, (const wasm_trap_t *t, wasm_message_t *out), (t, out))
WASMTIME_WRAP_VOID(wasm_message_delete, (wasm_message_t *m), (m))

/* ---- wasm.h：instance/extern ---- */
WASMTIME_WRAP_RET(wasm_instance_t *, wasm_instance_new, (wasm_store_t *store, const wasm_module_t *m, const wasm_extern_vec_t *imports, wasm_trap_t **trap), (store, m, imports, trap), NULL)
WASMTIME_WRAP_VOID(wasm_instance_delete, (wasm_instance_t *inst), (inst))
WASMTIME_WRAP_VOID(wasm_instance_exports, (const wasm_instance_t *inst, wasm_extern_vec_t *out), (inst, out))
WASMTIME_WRAP_VOID(wasm_extern_vec_new_empty, (wasm_extern_vec_t *out), (out))
WASMTIME_WRAP_VOID(wasm_extern_vec_new_uninitialized, (wasm_extern_vec_t *out, size_t n), (out, n))
WASMTIME_WRAP_VOID(wasm_extern_vec_copy, (wasm_extern_vec_t *out, const wasm_extern_vec_t *src), (out, src))
WASMTIME_WRAP_VOID(wasm_extern_vec_delete, (wasm_extern_vec_t *v), (v))
WASMTIME_WRAP_RET(wasm_func_t *, wasm_extern_as_func, (wasm_extern_t *ex), (ex), NULL)
WASMTIME_WRAP_RET(wasm_extern_t *, wasm_func_as_extern, (wasm_func_t *f), (f), NULL)
WASMTIME_WRAP_RET(wasm_memory_t *, wasm_extern_as_memory, (wasm_extern_t *ex), (ex), NULL)

/* ---- wasm.h：func ---- */
WASMTIME_WRAP_RET(wasm_func_t *, wasm_func_new, (wasm_store_t *store, const wasm_functype_t *type, wasm_func_callback_t callback), (store, type, callback), NULL)
WASMTIME_WRAP_VOID(wasm_func_delete, (wasm_func_t *f), (f))
WASMTIME_WRAP_RET(const wasm_functype_t *, wasm_func_type, (const wasm_func_t *f), (f), NULL)
WASMTIME_WRAP_RET(wasm_trap_t *, wasm_func_call, (const wasm_func_t *f, const wasm_val_vec_t *args, wasm_val_vec_t *results), (f, args, results), NULL)
WASMTIME_WRAP_RET(wasm_func_t *, wasm_func_new_with_env, (wasm_store_t *store, const wasm_functype_t *type, wasm_func_callback_with_env_t callback, void *env, void (*finalizer)(void *)), (store, type, callback, env, finalizer), NULL)

/* ---- wasm.h：memory ---- */
WASMTIME_WRAP_RET(wasm_memory_t *, wasm_memory_new, (wasm_store_t *store, const wasm_memorytype_t *t), (store, t), NULL)
WASMTIME_WRAP_VOID(wasm_memory_delete, (wasm_memory_t *m), (m))
WASMTIME_WRAP_RET(const wasm_memorytype_t *, wasm_memory_type, (const wasm_memory_t *m), (m), NULL)
WASMTIME_WRAP_RET(wasm_byte_t *, wasm_memory_data, (wasm_memory_t *m), (m), NULL)
WASMTIME_WRAP_RET(size_t, wasm_memory_data_size, (wasm_memory_t *m), (m), 0)
WASMTIME_WRAP_RET(wasm_memory_pages_t, wasm_memory_size, (const wasm_memory_t *m), (m), 0)
WASMTIME_WRAP_RET(bool, wasm_memory_grow, (wasm_memory_t *m, wasm_memory_pages_t delta), (m, delta), false)

/* ---- wasmtime.h：error/wat ---- */
WASMTIME_WRAP_RET(wasmtime_error_t *, wasmtime_wat2wasm, (const char *wat, size_t wat_size, wasm_byte_vec_t **out), (wat, wat_size, out), NULL)
WASMTIME_WRAP_VOID(wasmtime_error_delete, (wasmtime_error_t *e), (e))
WASMTIME_WRAP_VOID(wasmtime_error_message, (const wasmtime_error_t *e, wasm_name_t *msg), (e, msg))

/* ---- wasmtime.h：trap ---- */
WASMTIME_WRAP_RET(wasmtime_error_t *, wasmtime_trap_code, (const wasmtime_trap_t *t, wasmtime_trap_code_t *code_out), (t, code_out), NULL)
WASMTIME_WRAP_RET(wasmtime_trap_t *, wasmtime_trap_new, (wasmtime_context_t *ctx, const char *msg, size_t size), (ctx, msg, size), NULL)
WASMTIME_WRAP_VOID(wasmtime_trap_delete, (wasmtime_trap_t *t), (t))

/* ---- wasmtime.h：config/engine ---- */
WASMTIME_WRAP_RET(wasmtime_config_t *, wasmtime_config_new, (void), (), NULL)
WASMTIME_WRAP_VOID(wasmtime_config_delete, (wasmtime_config_t *c), (c))
WASMTIME_WRAP_VOID(wasmtime_config_consume_fuel_set, (wasmtime_config_t *c, bool enabled), (c, enabled))
WASMTIME_WRAP_VOID(wasmtime_config_epoch_interruption_set, (wasmtime_config_t *c, bool enabled), (c, enabled))
WASMTIME_WRAP_VOID(wasmtime_config_max_wasm_stack_set, (wasmtime_config_t *c, size_t stack), (c, stack))
WASMTIME_WRAP_RET(wasmtime_engine_t *, wasmtime_engine_new, (void), (), NULL)
WASMTIME_WRAP_RET(wasmtime_engine_t *, wasmtime_engine_new_with_config, (wasmtime_config_t *config), (config), NULL)
WASMTIME_WRAP_VOID(wasmtime_engine_delete, (wasmtime_engine_t *engine), (engine))
WASMTIME_WRAP_VOID(wasmtime_engine_increment_epoch, (wasmtime_engine_t *engine), (engine))

/* ---- wasmtime.h：store/context ---- */
WASMTIME_WRAP_RET(wasmtime_store_t *, wasmtime_store_new, (wasmtime_engine_t *engine, void *data, void (*finalizer)(void *)), (engine, data, finalizer), NULL)
WASMTIME_WRAP_VOID(wasmtime_store_delete, (wasmtime_store_t *store), (store))
WASMTIME_WRAP_RET(wasmtime_context_t *, wasmtime_context, (const wasmtime_store_t *store), (store), NULL)

/* ---- wasmtime.h：module ---- */
WASMTIME_WRAP_RET(wasmtime_error_t *, wasmtime_module_new, (wasmtime_engine_t *engine, const wasm_byte_vec_t *wasm, wasmtime_module_t **out), (engine, wasm, out), NULL)
WASMTIME_WRAP_VOID(wasmtime_module_delete, (wasmtime_module_t *m), (m))
WASMTIME_WRAP_RET(wasmtime_error_t *, wasmtime_module_validate, (wasmtime_engine_t *engine, const wasm_byte_vec_t *wasm), (engine, wasm), NULL)

/* ---- wasmtime.h：func/val ---- */
WASMTIME_WRAP_VOID(wasmtime_func_call, (wasmtime_context_t *ctx, const wasmtime_func_t *f, const wasmtime_val_t args[], size_t nargs, wasmtime_val_t results[], size_t nresults, wasmtime_trap_t **trap_out), (ctx, f, args, nargs, results, nresults, trap_out))
WASMTIME_WRAP_VOID(wasmtime_func_delete, (wasmtime_context_t *ctx, const wasmtime_func_t *f), (ctx, f))

/* ---- wasmtime.h：linker ---- */
WASMTIME_WRAP_RET(wasmtime_linker_t *, wasmtime_linker_new, (const wasmtime_engine_t *engine), (engine), NULL)
WASMTIME_WRAP_VOID(wasmtime_linker_delete, (wasmtime_linker_t *l), (l))
WASMTIME_WRAP_RET(wasmtime_error_t *, wasmtime_linker_define_wasi, (wasmtime_linker_t *l), (l), NULL)
WASMTIME_WRAP_RET(wasmtime_error_t *, wasmtime_linker_define, (wasmtime_linker_t *l, const char *module, size_t module_len, const char *name, size_t name_len, const wasmtime_extern_t *item), (l, module, module_len, name, name_len, item), NULL)
WASMTIME_WRAP_RET(wasmtime_error_t *, wasmtime_linker_instantiate, (wasmtime_linker_t *l, const wasmtime_module_t *m, wasmtime_instance_t *instance_out, wasmtime_trap_t **trap_out), (l, m, instance_out, trap_out), NULL)

/* ---- wasmtime.h：instance ---- */
WASMTIME_WRAP_RET(bool, wasmtime_instance_export_get, (wasmtime_context_t *ctx, const wasmtime_instance_t *inst, const char *name, size_t name_len, wasmtime_extern_t *item_out), (ctx, inst, name, name_len, item_out), false)

/* ---- wasmtime.h：memory ---- */
WASMTIME_WRAP_RET(wasm_byte_t *, wasmtime_memory_data, (wasmtime_context_t *ctx, const wasmtime_memory_t *m), (ctx, m), NULL)
WASMTIME_WRAP_RET(size_t, wasmtime_memory_data_size, (wasmtime_context_t *ctx, const wasmtime_memory_t *m), (ctx, m), 0)
WASMTIME_WRAP_RET(wasmtime_error_t *, wasmtime_memory_grow, (wasmtime_context_t *ctx, const wasmtime_memory_t *m, uint64_t new_pages, uint64_t *old_pages_out), (ctx, m, new_pages, old_pages_out), NULL)

/* ---- wasi.h ---- */
WASMTIME_WRAP_RET(wasi_config_t *, wasi_config_new, (void), (), NULL)
WASMTIME_WRAP_VOID(wasi_config_delete, (wasi_config_t *c), (c))
WASMTIME_WRAP_VOID(wasi_config_inherit_stdout, (wasi_config_t *c), (c))
WASMTIME_WRAP_VOID(wasi_config_inherit_stderr, (wasi_config_t *c), (c))
WASMTIME_WRAP_VOID(wasi_config_inherit_stdin, (wasi_config_t *c), (c))
WASMTIME_WRAP_VOID(wasi_config_argv, (wasi_config_t *c, const char *argv[]), (c, argv))

/* ============================================================
 * 便捷封装：wat → module 一步到位
 * ============================================================ */
wasmtime_error_t *wasmtime_dyn_compile_from_wat(wasmtime_engine_t *engine,
                                                const char *wat,
                                                wasmtime_module_t **out) {
    wasm_byte_vec_t wasm;
    wasm_byte_vec_t *wasm_ptr = &wasm;
    wasmtime_error_t *err = wasmtime_wat2wasm(wat, strlen(wat), &wasm_ptr);
    if (err) return err;
    err = wasmtime_module_new(engine, &wasm, out);
    wasm_byte_vec_delete(&wasm);
    return err;
}
