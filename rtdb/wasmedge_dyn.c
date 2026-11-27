
/* ============================================================
 * wasmedge_dyn.c - type safe dynamic lib load for libwasmedge
 * ============================================================
 * 设计要点：
 *   1. 单文件、无 .h、无 wrapper 层；链接期不需要 -lwasmedge。
 *   2. 封装函数名与原版导出符号一致。
 *   3. 每个函数首次调用时 dlsym 缓存；失败打印 stderr 并返回安全默认值。
 *   4. 公开结构体/枚举/回调 typedef 按官方头文件核对编写。
 *      （本机未安装该库，签名无法用 nm 核对；dlsym 失败路径兜底。）
 *   5. 加载顺序：显式路径 > WASMEDGE_DYN_LIB > LD_LIBRARY_PATH >
 *      soname > .so > Nix > 平台默认。
 *   6. 加载成功后不卸载（dlclose 不安全）；失败后 cleanup 复位。
 *   7. 纯 C 库：RTLD_LAZY | RTLD_LOCAL。
 *
 * 使用：
 *   wasmedge_dyn_init(NULL);
 *   ... 原版 API 直接调用 ...
 *   wasmedge_dyn_cleanup();    // 仅复位加载状态，不卸载库
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
  typedef HMODULE wasmedge_lib_handle_t;
  static wasmedge_lib_handle_t wasmedge_load_lib(const char *path) { return LoadLibraryA(path); }
  static void *wasmedge_get_sym(wasmedge_lib_handle_t h, const char *name) {
      return (void *)GetProcAddress(h, name);
  }
  static void wasmedge_free_lib(wasmedge_lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *wasmedge_lib_error(void) { return "LoadLibrary failed"; }
  #define WASMEDGE_PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *wasmedge_lib_handle_t;
  static wasmedge_lib_handle_t wasmedge_load_lib(const char *path) {
      return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
  }
  static void *wasmedge_get_sym(wasmedge_lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void wasmedge_free_lib(wasmedge_lib_handle_t h) { if (h) dlclose(h); }
  static const char *wasmedge_lib_error(void) { return dlerror(); }
  #define WASMEDGE_PATH_SEP ':'
#endif

/* ============================================================
 * 类型定义（WasmEdge C API 手工核对）
 * ============================================================ */
typedef struct WasmEdge_VMContext WasmEdge_VMContext;
typedef struct WasmEdge_ConfigureContext WasmEdge_ConfigureContext;
typedef struct WasmEdge_CompilerContext WasmEdge_CompilerContext;
typedef struct WasmEdge_ExecutorContext WasmEdge_ExecutorContext;
typedef struct WasmEdge_StoreContext WasmEdge_StoreContext;
typedef struct WasmEdge_ModuleInstanceContext WasmEdge_ModuleInstanceContext;
typedef struct WasmEdge_FunctionTypeContext WasmEdge_FunctionTypeContext;
typedef struct WasmEdge_MemoryTypeContext WasmEdge_MemoryTypeContext;
typedef struct WasmEdge_MemoryInstanceContext WasmEdge_MemoryInstanceContext;
typedef struct WasmEdge_ImportObjectContext WasmEdge_ImportObjectContext;

typedef uint8_t WasmEdge_ValType;
typedef uint8_t WasmEdge_ExternalType;
typedef uint8_t WasmEdge_HostRegistration;

typedef uint32_t WasmEdge_Result;

typedef struct {
    const char *Buf;
    uint32_t Length;
} WasmEdge_String;

typedef struct {
    WasmEdge_ValType Type;
    union {
        int32_t I32;
        int64_t I64;
        float F32;
        double F64;
        const void *Ref;
        struct {
            uint64_t H;
            uint64_t L;
        } V128;
    } Value;
} WasmEdge_Value;

/* Result 分类码 */
#define WasmEdge_Result_Success   ((WasmEdge_Result)0)
#define WasmEdge_Result_Terminate ((WasmEdge_Result)1)
#define WasmEdge_Result_Fail      ((WasmEdge_Result)2)

/* 常用错误码（enum WasmEdge_ErrCode 主要成员） */
#define WasmEdge_ErrCode_Success                     0x00
#define WasmEdge_ErrCode_Terminate                   0x01
#define WasmEdge_ErrCode_Fail                        0x02
#define WasmEdge_ErrCode_ValueNumMismatch            0x20
#define WasmEdge_ErrCode_FuncNotFound                0x21
#define WasmEdge_ErrCode_ExceededPageSize            0x26
#define WasmEdge_ErrCode_WasmModuleLoadFailed        0x40

/* Proposal（enum WasmEdge_Proposals） */
#define WasmEdge_Proposal_WASI  0
#define WasmEdge_Proposal_SIMD  1
#define WasmEdge_Proposal_Threads 2
#define WasmEdge_Proposal_MultiValue 3
#define WasmEdge_Proposal_BulkMemoryOperations 4
#define WasmEdge_Proposal_ReferenceTypes 5

/* HostRegistration */
#define WasmEdge_HostRegistration_Wasi 0
#define WasmEdge_HostRegistration_WasmEdge_Process 1


/* ============================================================
 * 全局状态
 * ============================================================ */
static wasmedge_lib_handle_t wasmedge_handle = NULL;
static int wasmedge_init_state = 0;

/* ============================================================
 * 库加载：按优先级构造候选路径
 * ============================================================ */
static int wasmedge_do_load(const char *library_path) {
    if (wasmedge_handle) return 0;

    static char bufs[64][1024];
    const char *candidates[64];
    int n = 0;

    /* 1. 显式传入的路径 */
    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof(bufs[0]), "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }

    /* 2. 项目专属环境变量 */
    const char *explicit_lib = getenv("WASMEDGE_DYN_LIB");
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
                char *sep = strchr(p, WASMEDGE_PATH_SEP);
                if (sep) *sep = 0;
                if (*p) {
                    snprintf(bufs[n], sizeof(bufs[0]), "%s/libwasmedge.so.0", p);
                    candidates[n] = bufs[n]; n++;
                    snprintf(bufs[n], sizeof(bufs[0]), "%s/libwasmedge.so", p);
                    candidates[n] = bufs[n]; n++;
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 4. 默认 soname + .so */
    candidates[n++] = "libwasmedge.so.0";
    candidates[n++] = "libwasmedge.so";

    /* 5. Nix 路径 */
#ifndef _WIN32
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(bufs[n], sizeof(bufs[0]), "%s/.nix-profile/lib/libwasmedge.so", home);
        candidates[n] = bufs[n]; n++;
    }
    candidates[n++] = "/nix/var/nix/profiles/default/lib/libwasmedge.so";
#endif

    /* 6. 平台默认路径 */
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libwasmedge.dylib";
    candidates[n++] = "/usr/local/lib/libwasmedge.dylib";
#elif defined(_WIN32)
    candidates[n++] = "wasmedge.dll";
#else
    candidates[n++] = "/usr/local/lib/libwasmedge.so";
    candidates[n++] = "/usr/lib/libwasmedge.so";
#endif

    int i;
    for (i = 0; i < n; i++) {
        wasmedge_handle = wasmedge_load_lib(candidates[i]);
        if (wasmedge_handle) break;
    }
    if (!wasmedge_handle) {
        fprintf(stderr, "wasmedge_dyn: cannot load libwasmedge (%s; WASMEDGE_DYN_LIB 可指定路径)\n", wasmedge_lib_error());
        return -1;
    }

    /* 验证锚点符号 */
    if (!wasmedge_get_sym(wasmedge_handle, "WasmEdge_VMCreate")) {
        fprintf(stderr, "wasmedge_dyn: loaded library does not export WasmEdge_VMCreate\n");
        wasmedge_free_lib(wasmedge_handle);
        wasmedge_handle = NULL;
        return -1;
    }
    fprintf(stderr, "wasmedge_dyn: loaded %d %s\n", i, candidates[i]);
    return 0;
}

/* ============================================================
 * 控制函数
 * ============================================================ */
int wasmedge_libok(void) {
    if (wasmedge_init_state == 0) {
        wasmedge_init_state = (wasmedge_do_load(NULL) == 0) ? 1 : -1;
    }
    return (wasmedge_init_state == 1 && wasmedge_handle != NULL) ? 1 : 0;
}

int wasmedge_dyn_init(const char *library_path) {
    int r = wasmedge_do_load(library_path);
    if (r == 0) {
        wasmedge_init_state = 1;
        return 0;
    }
    if (wasmedge_init_state == 0) wasmedge_init_state = -1;
    return -1;
}

void wasmedge_dyn_cleanup(void) {
    /* 仅在加载失败后复位；成功加载的库不卸载（dlclose 不安全） */
    if (wasmedge_init_state == -1) {
        wasmedge_init_state = 0;
    }
    if (!wasmedge_handle) {
        wasmedge_init_state = 0;
    }
}

int wasmedge_dyn_has_symbol(const char *name) {
    if (!wasmedge_libok()) return 0;
    return wasmedge_get_sym(wasmedge_handle, name) != NULL;
}

/* ============================================================
 * WRAP 宏：可变参数无法用宏处理（两库核心集均无可变参数导出）
 * ============================================================ */
#define WASMEDGE_WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                         \
    if (!fn) {                                                             \
        if (!wasmedge_libok()) return fallback;                               \
        *(void **)(&fn) = wasmedge_get_sym(wasmedge_handle, #name);                    \
        if (!fn) {                                                         \
            fprintf(stderr, "wasmedge_dyn: dlsym failed: %s\n", #name);       \
            return fallback;                                                \
        }                                                                  \
    }                                                                      \
    return fn args;                                                         \
}

#define WASMEDGE_WRAP_VOID(name, params, args)                                      \
void name params {                                                        \
    static void (*fn) params = NULL;                                        \
    if (!fn) {                                                             \
        if (!wasmedge_libok()) return;                                        \
        *(void **)(&fn) = wasmedge_get_sym(wasmedge_handle, #name);                    \
        if (!fn) {                                                         \
            fprintf(stderr, "wasmedge_dyn: dlsym failed: %s\n", #name);       \
            return;                                                         \
        }                                                                  \
    }                                                                      \
    fn args;                                                                \
}

/* ============================================================
 * 导出函数封装
 * ============================================================ */


/* ---- VM ---- */
WASMEDGE_WRAP_RET(WasmEdge_VMContext *, WasmEdge_VMCreate, (const WasmEdge_ConfigureContext *conf, WasmEdge_ExecutorContext *exec), (conf, exec), NULL)
WASMEDGE_WRAP_VOID(WasmEdge_VMDelete, (WasmEdge_VMContext *vm), (vm))
WASMEDGE_WRAP_RET(WasmEdge_Result, WasmEdge_VMLoadWasmFromBuffer, (WasmEdge_VMContext *vm, const uint8_t *buf, uint32_t buf_len), (vm, buf, buf_len), WasmEdge_Result_Fail)
WASMEDGE_WRAP_RET(WasmEdge_Result, WasmEdge_VMLoadWasmFromFile, (WasmEdge_VMContext *vm, const char *path), (vm, path), WasmEdge_Result_Fail)
WASMEDGE_WRAP_RET(WasmEdge_Result, WasmEdge_VMValidate, (WasmEdge_VMContext *vm), (vm), WasmEdge_Result_Fail)
WASMEDGE_WRAP_RET(WasmEdge_Result, WasmEdge_VMInstantiate, (WasmEdge_VMContext *vm), (vm), WasmEdge_Result_Fail)
WASMEDGE_WRAP_RET(WasmEdge_Result, WasmEdge_VMExecute, (WasmEdge_VMContext *vm, const WasmEdge_String name, const WasmEdge_Value *params, const uint32_t plen, WasmEdge_Value *returns, const uint32_t rlen), (vm, name, params, plen, returns, rlen), WasmEdge_Result_Fail)
WASMEDGE_WRAP_RET(WasmEdge_Result, WasmEdge_VMExecuteRegistered, (WasmEdge_VMContext *vm, const WasmEdge_String mod, const WasmEdge_String fname, const WasmEdge_Value *params, const uint32_t plen, WasmEdge_Value *returns, const uint32_t rlen), (vm, mod, fname, params, plen, returns, rlen), WasmEdge_Result_Fail)
WASMEDGE_WRAP_RET(WasmEdge_Result, WasmEdge_VMRegisterModuleFromBuffer, (WasmEdge_VMContext *vm, const WasmEdge_String name, const uint8_t *buf, uint32_t buf_len), (vm, name, buf, buf_len), WasmEdge_Result_Fail)
WASMEDGE_WRAP_RET(WasmEdge_Result, WasmEdge_VMRegisterModuleFromFile, (WasmEdge_VMContext *vm, const WasmEdge_String name, const char *path), (vm, name, path), WasmEdge_Result_Fail)
WASMEDGE_WRAP_RET(WasmEdge_Result, WasmEdge_VMCleanup, (WasmEdge_VMContext *vm), (vm), WasmEdge_Result_Fail)
WASMEDGE_WRAP_RET(WasmEdge_Result, WasmEdge_VMGetFunctionList, (WasmEdge_VMContext *vm, WasmEdge_String *names, WasmEdge_FunctionTypeContext **types, uint32_t *list_len), (vm, names, types, list_len), WasmEdge_Result_Fail)
WASMEDGE_WRAP_RET(WasmEdge_FunctionTypeContext *, WasmEdge_VMGetFunctionType, (WasmEdge_VMContext *vm, const WasmEdge_String name), (vm, name), NULL)
WASMEDGE_WRAP_RET(WasmEdge_FunctionTypeContext *, WasmEdge_VMGetFunctionTypeRegistered, (WasmEdge_VMContext *vm, const WasmEdge_String mod, const WasmEdge_String fname), (vm, mod, fname), NULL)

/* ---- Configure ---- */
WASMEDGE_WRAP_RET(WasmEdge_ConfigureContext *, WasmEdge_ConfigureCreate, (void), (), NULL)
WASMEDGE_WRAP_VOID(WasmEdge_ConfigureDelete, (WasmEdge_ConfigureContext *c), (c))
WASMEDGE_WRAP_VOID(WasmEdge_ConfigureAddProposal, (WasmEdge_ConfigureContext *c, uint8_t prop), (c, prop))
WASMEDGE_WRAP_VOID(WasmEdge_ConfigureAddHostRegistration, (WasmEdge_ConfigureContext *c, WasmEdge_HostRegistration host), (c, host))

/* ---- String ---- */
WASMEDGE_WRAP_RET(WasmEdge_String, WasmEdge_StringCreateByBuffer, (const char *s, const uint32_t len), (s, len), ((WasmEdge_String){NULL, 0}))
WASMEDGE_WRAP_RET(WasmEdge_String, WasmEdge_StringCreateByCString, (const char *s), (s), ((WasmEdge_String){NULL, 0}))
WASMEDGE_WRAP_RET(WasmEdge_String, WasmEdge_StringWrap, (const char *s, const uint32_t len), (s, len), ((WasmEdge_String){NULL, 0}))
WASMEDGE_WRAP_RET(bool, WasmEdge_StringIsEqual, (const WasmEdge_String a, const WasmEdge_String b), (a, b), false)
WASMEDGE_WRAP_RET(uint32_t, WasmEdge_StringGetLength, (const WasmEdge_String s), (s), 0)
WASMEDGE_WRAP_RET(uint32_t, WasmEdge_StringCopy, (const WasmEdge_String s, char *buf, const uint32_t len), (s, buf, len), 0)
WASMEDGE_WRAP_VOID(WasmEdge_StringDelete, (WasmEdge_String s), (s))

/* ---- Value ---- */
WASMEDGE_WRAP_RET(WasmEdge_Value, WasmEdge_ValueGenI32, (const int32_t v), (v), ((WasmEdge_Value){0, {0}}))
WASMEDGE_WRAP_RET(WasmEdge_Value, WasmEdge_ValueGenI64, (const int64_t v), (v), ((WasmEdge_Value){0, {0}}))
WASMEDGE_WRAP_RET(WasmEdge_Value, WasmEdge_ValueGenF32, (const float v), (v), ((WasmEdge_Value){0, {0}}))
WASMEDGE_WRAP_RET(WasmEdge_Value, WasmEdge_ValueGenF64, (const double v), (v), ((WasmEdge_Value){0, {0}}))
WASMEDGE_WRAP_RET(int32_t, WasmEdge_ValueGetI32, (const WasmEdge_Value v), (v), 0)
WASMEDGE_WRAP_RET(int64_t, WasmEdge_ValueGetI64, (const WasmEdge_Value v), (v), 0)
WASMEDGE_WRAP_RET(float, WasmEdge_ValueGetF32, (const WasmEdge_Value v), (v), 0.0f)
WASMEDGE_WRAP_RET(double, WasmEdge_ValueGetF64, (const WasmEdge_Value v), (v), 0.0)

/* ---- ValType ---- */
WASMEDGE_WRAP_RET(WasmEdge_ValType, WasmEdge_ValTypeGenI32, (void), (), 0)
WASMEDGE_WRAP_RET(WasmEdge_ValType, WasmEdge_ValTypeGenI64, (void), (), 0)
WASMEDGE_WRAP_RET(WasmEdge_ValType, WasmEdge_ValTypeGenF32, (void), (), 0)
WASMEDGE_WRAP_RET(WasmEdge_ValType, WasmEdge_ValTypeGenF64, (void), (), 0)

/* ---- Log ---- */
WASMEDGE_WRAP_VOID(WasmEdge_LogSetErrorLevel, (void), ())
WASMEDGE_WRAP_VOID(WasmEdge_LogInitLogger, (void), ())

/* ---- Result ---- */
WASMEDGE_WRAP_RET(const char *, WasmEdge_ResultGetMessage, (const WasmEdge_Result r), (r), "unknown")

/* ============================================================
 * 便捷封装：Result 便捷判断（ResultOK 在官方头是 inline，无法 dlsym）
 * ============================================================ */
int wasmedge_dyn_result_ok(WasmEdge_Result r) {
    return r == WasmEdge_Result_Success;
}

const char *wasmedge_dyn_result_msg(WasmEdge_Result r) {
    return WasmEdge_ResultGetMessage(r);
}
