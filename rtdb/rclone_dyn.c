/* ============================================================================
 * rclone_dyn.c — librclone 动态加载封装层
 *
 * 设计约束（与 mpv_dyn.c / sqlite3_dyn.c / ffmpeg_dyn.c 等一致）：
 *   1. 链接期绝不出现 -lrclone。
 *   2. 无自定义 .h 文件，无独立 wrapper .c。
 *   3. 每个封装函数首次调用时才 dlsym 自己的符号并缓存。
 *   4. 封装函数名与原版 librclone C API 一致。
 *   5. 编译期不需要任何 librclone 头文件；RcloneRPCResult 直接在本文件定义。
 *   6. 跨平台：Unix/macOS 用 dlopen/dlsym；Windows 用 LoadLibrary/GetProcAddress。
 *   7. 搜索顺序：显式路径 > RCLONE_DYN_LIB > 系统搜索路径 > 默认库名
 *      > Nix 路径 > 平台默认路径。
 *   8. 线程安全：首次调用 rclone_libok() 或任一封装函数时完成加载。
 *      一次性加载本身未加锁，建议主线程在启动阶段先调用一次
 *      rclone_libok()/rclone_dyn_init()；加载完成后的并发调用安全。
 *
 * librclone 官方 C API（已核对 rclone master librclone/librclone.go）只有 4 个：
 *
 *   struct RcloneRPCResult {
 *       char *Output;
 *       int   Status;
 *   };
 *
 *   void RcloneInitialize(void);
 *   void RcloneFinalize(void);
 *   struct RcloneRPCResult RcloneRPC(char *method, char *input);
 *   void RcloneFreeString(char *str);
 *
 * 参数与返回值要点：
 *   - RcloneRPC 按值返回结构体（不是指针），函数指针类型必须严格匹配。
 *   - method/input 均为 UTF-8 C 字符串；input 是 JSON，如 "{}"，
 *     传空串 "" 也等价于空参数。
 *   - Status 是 HTTP 状态码，200 表示成功；非 200 时 Output 通常仍是
 *     一段错误 JSON，同样需要释放。
 *   - Output 由 librclone 内部（Go 运行时）分配，必须调用
 *     RcloneFreeString 释放，绝不能用应用侧的 free()。
 *   - RcloneInitialize 必须先于 RcloneRPC 调用；RcloneFinalize 只做
 *     GC 收尾，不负责卸载库。
 *
 * 与 mpv_dyn.c 的关键差异：
 *   - librclone 没有任何版本导出符号，因此加载时用 RcloneInitialize
 *     作为基础符号校验；也没有 rclone_dyn_api_version()。
 *   - librclone 内含 Go runtime、GC 和后台线程，官方明确禁止
 *     dlclose/FreeLibrary。所以 rclone_dyn_cleanup() 在加载成功后
 *     故意不卸载库，只允许在“加载失败”状态下复位以便重试。
 *
 * 使用顺序示例：
 *
 *   RcloneInitialize();
 *   struct RcloneRPCResult r = RcloneRPC("core/version", "{}");
 *   if (r.Status == 200) { ... }
 *   RcloneFreeString(r.Output);
 *   RcloneFinalize();
 *   rclone_dyn_cleanup();   // 不卸载库，仅允许失败重试
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ============================================================
 * 原版 C API 类型（不依赖 librclone.h）
 * ============================================================ */
struct RcloneRPCResult {
    char *Output;
    int   Status;
};

/* ============================================================
 * 平台适配层
 * ============================================================ */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE rclone_lib_handle_t;
  static rclone_lib_handle_t rclone_load_lib(const char *path) { return LoadLibraryA(path); }
  static void *rclone_get_sym(rclone_lib_handle_t h, const char *name) {
      return (void*)GetProcAddress(h, name);
  }
  static void rclone_free_lib(rclone_lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *rclone_lib_error(void) { return "LoadLibrary failed"; }
  #define RCLONE_PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *rclone_lib_handle_t;
  static rclone_lib_handle_t rclone_load_lib(const char *path) {
      int flags = RTLD_LAZY | RTLD_LOCAL;
  #ifdef RTLD_DEEPBIND
      flags |= RTLD_DEEPBIND;
  #endif
      return dlopen(path, flags);
  }
  static void *rclone_get_sym(rclone_lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void rclone_free_lib(rclone_lib_handle_t h) { if (h) dlclose(h); }
  static const char *rclone_lib_error(void) { return dlerror(); }
  #define RCLONE_PATH_SEP ':'
#endif

/* ============================================================
 * 全局状态
 * ============================================================ */
static rclone_lib_handle_t rclone_handle_lib = NULL;
static int rclone_init_state = 0;   /* 0=未初始化, 1=已加载, -1=加载失败 */

#define RCLONE_MAX_CANDIDATES 64
#define RCLONE_PATH_BUF_LEN   1024

/* ============================================================
 * 候选路径工具
 * ============================================================ */
static int rclone_push_candidate(const char **candidates,
                          char bufs[][RCLONE_PATH_BUF_LEN],
                          int n, const char *path) {
    if (n >= RCLONE_MAX_CANDIDATES - 1) return n;
    snprintf(bufs[n], RCLONE_PATH_BUF_LEN, "%s", path);
    candidates[n] = bufs[n];
    return n + 1;
}

static int rclone_push_candidate_fmt(const char **candidates,
                              char bufs[][RCLONE_PATH_BUF_LEN],
                              int n, const char *fmt, ...) {
    va_list ap;
    if (n >= RCLONE_MAX_CANDIDATES - 1) return n;
    va_start(ap, fmt);
    vsnprintf(bufs[n], RCLONE_PATH_BUF_LEN, fmt, ap);
    va_end(ap);
    candidates[n] = bufs[n];
    return n + 1;
}

static char *rclone_dup_string(const char *s) {
    size_t len = strlen(s) + 1;
    char *p = (char *)malloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

/* ============================================================
 * 库加载
 * ============================================================ */
static int rclone_do_load(const char *library_path) {
    if (rclone_handle_lib) return 0;

    static char bufs[RCLONE_MAX_CANDIDATES][RCLONE_PATH_BUF_LEN];
    const char *candidates[RCLONE_MAX_CANDIDATES];
    int n = 0;

    /* 1. 显式传入的路径 */
    if (library_path && *library_path) {
        n = rclone_push_candidate(candidates, bufs, n, library_path);
    }

    /* 2. 项目专属环境变量 */
    const char *explicit_lib = getenv("RCLONE_DYN_LIB");
    if (explicit_lib && *explicit_lib) {
        n = rclone_push_candidate(candidates, bufs, n, explicit_lib);
    }

    /* 3. 系统搜索路径环境变量中的目录 + 库名 */
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
        char *copy = rclone_dup_string(env_path);
        if (copy) {
            char *p = copy;
            while (p && *p) {
                char *sep = strchr(p, RCLONE_PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
#ifdef _WIN32
                    n = rclone_push_candidate_fmt(candidates, bufs, n,
                                           "%s/librclone.dll", p);
#elif defined(__APPLE__)
                    n = rclone_push_candidate_fmt(candidates, bufs, n,
                                           "%s/librclone.dylib", p);
                    n = rclone_push_candidate_fmt(candidates, bufs, n,
                                           "%s/librclone.so", p);
#else
                    n = rclone_push_candidate_fmt(candidates, bufs, n,
                                           "%s/librclone.so", p);
                    n = rclone_push_candidate_fmt(candidates, bufs, n,
                                           "%s/liblibrclone.so", p);
#endif
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 4. 默认库名 */
#ifdef _WIN32
    n = rclone_push_candidate(candidates, bufs, n, "librclone.dll");
#elif defined(__APPLE__)
    n = rclone_push_candidate(candidates, bufs, n, "librclone.dylib");
    n = rclone_push_candidate(candidates, bufs, n, "librclone.so");
#else
    n = rclone_push_candidate(candidates, bufs, n, "librclone.so");
    n = rclone_push_candidate(candidates, bufs, n, "liblibrclone.so");
#endif

    /* 5. Nix 路径 */
#ifndef _WIN32
    const char *home = getenv("HOME");
    if (home && *home) {
#ifdef __APPLE__
        n = rclone_push_candidate_fmt(candidates, bufs, n,
                               "%s/.nix-profile/lib/librclone.dylib", home);
#else
        n = rclone_push_candidate_fmt(candidates, bufs, n,
                               "%s/.nix-profile/lib/librclone.so", home);
#endif
    }
#ifdef __APPLE__
    n = rclone_push_candidate(candidates, bufs, n,
                       "/nix/var/nix/profiles/default/lib/librclone.dylib");
#else
    n = rclone_push_candidate(candidates, bufs, n,
                       "/nix/var/nix/profiles/default/lib/librclone.so");
#endif
#endif

    /* 6. 平台默认路径 */
#ifdef __APPLE__
    n = rclone_push_candidate(candidates, bufs, n,
                       "/opt/local/lib/librclone.dylib");
    n = rclone_push_candidate(candidates, bufs, n,
                       "/usr/local/lib/librclone.dylib");
    n = rclone_push_candidate(candidates, bufs, n,
                       "/opt/homebrew/lib/librclone.dylib");
#elif defined(__linux__) || defined(__ANDROID__)
    n = rclone_push_candidate(candidates, bufs, n,
                       "/usr/local/lib/librclone.so");
    n = rclone_push_candidate(candidates, bufs, n,
                       "/usr/lib/x86_64-linux-gnu/librclone.so");
    n = rclone_push_candidate(candidates, bufs, n,
                       "/usr/lib/aarch64-linux-gnu/librclone.so");
    n = rclone_push_candidate(candidates, bufs, n,
                       "/usr/lib/librclone.so");
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    n = rclone_push_candidate(candidates, bufs, n,
                       "/usr/local/lib/librclone.so");
    n = rclone_push_candidate(candidates, bufs, n,
                       "/usr/lib/librclone.so");
#endif
    candidates[n] = NULL;

    for (int i = 0; i < n; i++) {
        rclone_handle_lib = rclone_load_lib(candidates[i]);
        if (!rclone_handle_lib) continue;

        /* 验证基础符号：RcloneInitialize */
        if (!rclone_get_sym(rclone_handle_lib, "RcloneInitialize")) {
            fprintf(stderr,
                    "rclone_dyn: %s does not export RcloneInitialize, skipped\n",
                    candidates[i]);
            rclone_free_lib(rclone_handle_lib);
            rclone_handle_lib = NULL;
            continue;
        }
        fprintf(stderr, "rclone_dyn: loaded %d %s\n", i, candidates[i]);
        return 0;
    }

    if (!rclone_handle_lib) {
        const char *err = rclone_lib_error();
        fprintf(stderr, "rclone_dyn: cannot load librclone (%s)\n",
                err ? err : "unknown error");
        return -1;
    }
    return 0;
}

/* ============================================================
 * 一次性初始化
 * ============================================================ */
int rclone_libok(void) {
    if (rclone_init_state == 0) {
        rclone_init_state = (rclone_do_load(NULL) == 0) ? 1 : -1;
    }
    return (rclone_init_state == 1 && rclone_handle_lib != NULL) ? 1 : 0;
}

int rclone_dyn_init(const char *library_path) {
    if (rclone_init_state == 1) return 0;
    if (rclone_init_state == -1) return -1;
    if (rclone_do_load(library_path) == 0) {
        rclone_init_state = 1;
        return 0;
    }
    rclone_init_state = -1;
    return -1;
}

/*
 * 注意：librclone 内含 Go runtime/GC/后台线程，官方明确禁止在运行时
 * 卸载（dlclose/FreeLibrary 会崩溃）。因此：
 *   - 加载成功后本函数不卸载库，句柄保留到进程退出；
 *   - 只有加载失败状态才复位为 0，允许修正路径/环境变量后重试。
 */
void rclone_dyn_cleanup(void) {
    if (rclone_init_state == -1) {
        rclone_init_state = 0;
    }
}

/* ============================================================
 * 按需解析宏
 * ============================================================ */
#define RCLONE_RESOLVE(var, name, fallback) do {                           \
    if (!(var)) {                                                   \
        if (!rclone_libok()) return fallback;                       \
        *(void **)(&(var)) = rclone_get_sym(rclone_handle_lib, name);      \
        if (!(var)) {                                               \
            fprintf(stderr, "rclone_dyn: dlsym failed: %s\n", name);\
            return fallback;                                        \
        }                                                           \
    }                                                               \
} while (0)

#define RESOLVE_VOID(var, name) do {                                \
    if (!(var)) {                                                   \
        if (!rclone_libok()) return;                                \
        *(void **)(&(var)) = rclone_get_sym(rclone_handle_lib, name);      \
        if (!(var)) {                                               \
            fprintf(stderr, "rclone_dyn: dlsym failed: %s\n", name);\
            return;                                                 \
        }                                                           \
    }                                                               \
} while (0)

/* ============================================================
 * 封装函数：与原版 librclone C API 同名
 * ============================================================ */
void RcloneInitialize(void) {
    static void (*fn)(void) = NULL;
    RESOLVE_VOID(fn, "RcloneInitialize");
    fn();
}

void RcloneFinalize(void) {
    static void (*fn)(void) = NULL;
    RESOLVE_VOID(fn, "RcloneFinalize");
    fn();
}

struct RcloneRPCResult RcloneRPC(char *method, char *input) {
    static struct RcloneRPCResult (*fn)(char *, char *) = NULL;
    struct RcloneRPCResult result;

    /* 失败时的安全默认值：Status=0 明确表示 RPC 未执行 */
    result.Output = NULL;
    result.Status = 0;

    if (!method || !input) {
        fprintf(stderr, "rclone_dyn: RcloneRPC: method/input is NULL\n");
        return result;
    }

    if (!fn) {
        if (!rclone_libok()) return result;
        *(void **)(&fn) = rclone_get_sym(rclone_handle_lib, "RcloneRPC");
        if (!fn) {
            fprintf(stderr, "rclone_dyn: dlsym failed: RcloneRPC\n");
            return result;
        }
    }
    return fn(method, input);
}

void RcloneFreeString(char *str) {
    static void (*fn)(char *) = NULL;
    if (!str) return;
    RESOLVE_VOID(fn, "RcloneFreeString");
    fn(str);
}
