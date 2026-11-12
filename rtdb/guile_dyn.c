/* ============================================================================
 * guile_dyn.c — libguile 3.0 动态加载封装层
 *
 * 设计约束（与 mpv_dyn.c / python_dyn.c / ruby_dyn.c 一致）：
 *   1. 链接期绝不出现 -lguile。
 *   2. 无自定义 .h 文件，无独立 wrapper .c。
 *   3. 每个封装函数首次调用时才 dlsym 自己的符号并缓存。
 *   4. 封装函数名与原版 Guile C API 一致；宏 API 用 guile_dyn_* helper 补齐。
 *   5. 编译期不需要 <libguile.h>；SCM 在本文件内按机器字定义。
 *   6. 跨平台：Unix/macOS 用 dlopen/dlsym；Windows 用 LoadLibrary/GetProcAddress。
 *   7. 搜索顺序：显式路径 > GUILE_DYN_LIB > 系统搜索路径 > 默认库名
 *      > Nix > 平台默认路径。
 *   8. 线程安全：首次调用 guile_dyn_libok() 或任一封装函数时完成加载。
 *
 * 版本：
 *   - 只面向 Guile 3.0.x（libguile-3.0）。
 *   - Guile 3.0 的 C API 符号仍使用 @@GUILE_2.0 版本节点，dlsym 无需处理。
 *   - 加载时校验 scm_init_guile / scm_c_eval_string。
 *   - guile_dyn_version() 返回从库名解析的 "3.0"；
 *     guile_dyn_runtime_version() 在初始化后调用 scm_effective_version()
 *     返回真实版本（需要 Guile 已初始化）。
 *
 * 重要限制：
 *   - Guile 的常用 C API 有大量宏（scm_is_true、scm_from_int、scm_from_bool、
 *     scm_list_length 等），没有导出符号；本文件提供等价 helper。
 *   - scm_init_guile 必须在主线程调用，且栈基址正确；更稳妥的方式是
 *     scm_with_guile() / scm_boot_guile()。
 *   - SCM 值在多次分配之间保存时必须 scm_gc_protect_object()，或放在
 *     C 栈上让保守 GC 可见。
 *   - scm_c_eval_string 出错会 longjmp；需要捕获时用 scm_c_catch()。
 *   - scm_to_utf8_string() 返回的内存由 scm_malloc 分配，
 *     用 guile_dyn_free_string()（内部 free）释放。
 *   - 初始化成功后不要 dlclose/FreeLibrary；Guile 的线程和 GC 状态
 *     不适合运行时卸载。guile_dyn_cleanup() 只允许加载失败后复位重试。
 *
 * 使用顺序示例：
 *
 *   if (guile_dyn_init(NULL) != 0) { ... }
 *   scm_init_guile();              // 或 scm_with_guile()
 *   SCM v = scm_c_eval_string("(+ 1 2)");
 *   printf("%d\n", scm_to_int32(v));
 *   SCM s = scm_from_utf8_string("hello");
 *   char *p = scm_to_utf8_string(s);
 *   guile_dyn_free_string(p);
 *   guile_dyn_cleanup();           // 不卸载库
 *
 * 编译：链接期只需要 -ldl（Linux/BSD），不要链接 -lguile。
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================
 * SCM 类型与 3.0 常量
 * ============================================================ */
typedef uintptr_t SCM;

/* Guile 2.0 ABI（3.0 沿用）的 IFLAG 编码 */
#define GUILE_DYN_SCM_BOOL_F      ((SCM)0x004)
#define GUILE_DYN_SCM_ELISP_NIL   ((SCM)0x104)
#define GUILE_DYN_SCM_EOL         ((SCM)0x304)
#define GUILE_DYN_SCM_BOOL_T      ((SCM)0x404)
#define GUILE_DYN_SCM_UNSPECIFIED ((SCM)0x804)
#define GUILE_DYN_SCM_UNDEFINED   ((SCM)0x904)
#define GUILE_DYN_SCM_EOF_VAL     ((SCM)0xA04)

#define GUILE_DYN_F_DYNWIND_REWINDABLE 16

typedef SCM (*guile_dyn_subr)();
typedef SCM (*guile_dyn_catch_body)(void *data);
typedef SCM (*guile_dyn_catch_handler)(void *data, SCM tag, SCM throw_args);

/* ============================================================
 * 平台适配层
 * ============================================================ */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE lib_handle_t;
  static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
  static void *get_sym(lib_handle_t h, const char *name) {
      return (void*)GetProcAddress(h, name);
  }
  static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *lib_error(void) { return "LoadLibrary failed"; }
  #define PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *lib_handle_t;
  static lib_handle_t load_lib(const char *path) {
      /*
       * Guile 会 dlopen 扩展模块；它们需要全局可见的 guile 符号。
       * 与 mpv 的 RTLD_LOCAL 不同，这里使用 RTLD_GLOBAL。
       */
      int flags = RTLD_LAZY | RTLD_GLOBAL;
      return dlopen(path, flags);
  }
  static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
  static const char *lib_error(void) { return dlerror(); }
  #define PATH_SEP ':'
#endif

/* ============================================================
 * 全局状态
 * ============================================================ */
static lib_handle_t guile_handle_lib = NULL;
static int init_state = 0;          /* 0=未初始化, 1=已加载, -1=失败 */
static int guile_major = 3;
static int guile_minor = 0;
static char guile_version_str[128] = "3.0";
static int *guile_initialized_p = NULL;

#define MAX_CANDIDATES 128
#define PATH_BUF_LEN   1024

/* ============================================================
 * 候选路径工具
 * ============================================================ */
static int push_candidate(const char **candidates,
                          char bufs[][PATH_BUF_LEN],
                          int n, const char *path) {
    if (n >= MAX_CANDIDATES - 1) return n;
    snprintf(bufs[n], PATH_BUF_LEN, "%s", path);
    candidates[n] = bufs[n];
    return n + 1;
}

static int push_candidate_fmt(const char **candidates,
                              char bufs[][PATH_BUF_LEN],
                              int n, const char *fmt, ...) {
    va_list ap;
    if (n >= MAX_CANDIDATES - 1) return n;
    va_start(ap, fmt);
    vsnprintf(bufs[n], PATH_BUF_LEN, fmt, ap);
    va_end(ap);
    candidates[n] = bufs[n];
    return n + 1;
}

static char *dup_string(const char *s) {
    size_t len = strlen(s) + 1;
    char *p = (char *)malloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

static void set_version_from_path(const char *path) {
    const char *p = strstr(path, "guile-");
    int maj = 0, min = 0;
    if (p && sscanf(p, "guile-%d.%d", &maj, &min) == 2) {
        guile_major = maj;
        guile_minor = min;
        snprintf(guile_version_str, sizeof(guile_version_str),
                 "%d.%d", maj, min);
    } else {
        guile_major = 3;
        guile_minor = 0;
        snprintf(guile_version_str, sizeof(guile_version_str), "3.0");
    }
}

/* ============================================================
 * 库加载
 * ============================================================ */
static int try_candidate(const char *path) {
    lib_handle_t h;
    void *sym_init;
    void *sym_eval;

    h = load_lib(path);
    if (!h) return -1;

    sym_init = get_sym(h, "scm_init_guile");
    sym_eval = get_sym(h, "scm_c_eval_string");
    if (!sym_init || !sym_eval) {
        free_lib(h);
        return -1;
    }

    guile_handle_lib = h;
    set_version_from_path(path);
    fprintf(stderr, "guile_dyn: loaded %s (Guile %s)\n",
            path, guile_version_str);
    return 0;
}

static int do_load(const char *library_path) {
    static char bufs[MAX_CANDIDATES][PATH_BUF_LEN];
    const char *candidates[MAX_CANDIDATES];
    int n = 0;
    int i;
    const char *env_path;

    if (guile_handle_lib) return 0;

    /* 1. 显式路径 */
    if (library_path && *library_path)
        n = push_candidate(candidates, bufs, n, library_path);

    /* 2. 环境变量 */
    {
        const char *explicit_lib = getenv("GUILE_DYN_LIB");
        if (explicit_lib && *explicit_lib)
            n = push_candidate(candidates, bufs, n, explicit_lib);
    }

    /* 3. 系统搜索路径目录 */
    env_path = getenv(
#ifdef _WIN32
        "PATH"
#elif defined(__APPLE__)
        "DYLD_LIBRARY_PATH"
#else
        "LD_LIBRARY_PATH"
#endif
    );
    if (env_path && *env_path) {
        char *copy = dup_string(env_path);
        if (copy) {
            char *p = copy;
            while (p && *p) {
                char *sep = strchr(p, PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
#ifdef _WIN32
                    n = push_candidate_fmt(candidates, bufs, n,
                                           "%s/libguile-3.0-1.dll", p);
                    n = push_candidate_fmt(candidates, bufs, n,
                                           "%s/libguile-3.0.dll", p);
#elif defined(__APPLE__)
                    n = push_candidate_fmt(candidates, bufs, n,
                                           "%s/libguile-3.0.dylib", p);
#else
                    n = push_candidate_fmt(candidates, bufs, n,
                                           "%s/libguile-3.0.so.1", p);
                    n = push_candidate_fmt(candidates, bufs, n,
                                           "%s/libguile-3.0.so", p);
#endif
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 4. 默认库名 */
#ifdef _WIN32
    n = push_candidate(candidates, bufs, n, "libguile-3.0-1.dll");
    n = push_candidate(candidates, bufs, n, "libguile-3.0.dll");
    n = push_candidate(candidates, bufs, n, "guile-3.0.dll");
#elif defined(__APPLE__)
    n = push_candidate(candidates, bufs, n, "libguile-3.0.dylib");
#else
    n = push_candidate(candidates, bufs, n, "libguile-3.0.so.1");
    n = push_candidate(candidates, bufs, n, "libguile-3.0.so");
#endif

    /* 5. Nix */
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        char dir[PATH_BUF_LEN];
        if (home && *home) {
            snprintf(dir, sizeof(dir), "%s/.nix-profile/lib", home);
#ifdef __APPLE__
            n = push_candidate_fmt(candidates, bufs, n,
                                   "%s/libguile-3.0.dylib", dir);
#else
            n = push_candidate_fmt(candidates, bufs, n,
                                   "%s/libguile-3.0.so.1", dir);
#endif
        }
#ifdef __APPLE__
        n = push_candidate(candidates, bufs, n,
                           "/nix/var/nix/profiles/default/lib/libguile-3.0.dylib");
#else
        n = push_candidate(candidates, bufs, n,
                           "/nix/var/nix/profiles/default/lib/libguile-3.0.so.1");
#endif
    }
#endif

    /* 6. 平台默认路径 */
#ifdef __APPLE__
    n = push_candidate(candidates, bufs, n,
                       "/opt/local/lib/libguile-3.0.dylib");
    n = push_candidate(candidates, bufs, n,
                       "/usr/local/lib/libguile-3.0.dylib");
    n = push_candidate(candidates, bufs, n,
                       "/opt/homebrew/lib/libguile-3.0.dylib");
    n = push_candidate(candidates, bufs, n,
                       "/opt/homebrew/opt/guile/lib/libguile-3.0.dylib");
#elif defined(__linux__) || defined(__ANDROID__)
    n = push_candidate(candidates, bufs, n,
                       "/usr/local/lib/libguile-3.0.so.1");
    n = push_candidate(candidates, bufs, n,
                       "/usr/lib/libguile-3.0.so.1");
    n = push_candidate(candidates, bufs, n,
                       "/usr/lib/x86_64-linux-gnu/libguile-3.0.so.1");
    n = push_candidate(candidates, bufs, n,
                       "/usr/lib/aarch64-linux-gnu/libguile-3.0.so.1");
    {
        const char *prefix = getenv("PREFIX"); /* Termux */
        if (prefix && *prefix) {
            n = push_candidate_fmt(candidates, bufs, n,
                                   "%s/lib/libguile-3.0.so.1", prefix);
        }
    }
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    n = push_candidate(candidates, bufs, n,
                       "/usr/local/lib/libguile-3.0.so.1");
    n = push_candidate(candidates, bufs, n,
                       "/usr/lib/libguile-3.0.so.1");
#endif
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        if (try_candidate(candidates[i]) == 0)
            return 0;
    }

    {
        const char *err = lib_error();
        fprintf(stderr, "guile_dyn: cannot load libguile-3.0 (%s)\n",
                err ? err : "unknown error");
    }
    return -1;
}

/* ============================================================
 * 控制函数
 * ============================================================ */
int guile_dyn_libok(void) {
    if (init_state == 0)
        init_state = (do_load(NULL) == 0) ? 1 : -1;
    return (init_state == 1 && guile_handle_lib != NULL) ? 1 : 0;
}

int guile_dyn_init(const char *library_path) {
    if (init_state == 1) return 0;
    if (init_state == -1) return -1;
    if (do_load(library_path) == 0) {
        init_state = 1;
        return 0;
    }
    init_state = -1;
    return -1;
}

int guile_dyn_init_version(int major, int minor) {
    if (major != 3 || (minor >= 0 && minor != 0)) return -1;
    if (init_state == 1) return 0;
    if (init_state == -1) init_state = 0;
    if (do_load(NULL) == 0) {
        init_state = 1;
        return 0;
    }
    init_state = -1;
    return -1;
}

/*
 * Guile 初始化后不适合卸载（线程、GC、动态栈）。
 * 只有加载失败状态才复位，允许修正路径后重试。
 */
void guile_dyn_cleanup(void) {
    if (init_state == -1)
        init_state = 0;
}

int guile_dyn_major(void) {
    if (init_state == 0) guile_dyn_libok();
    return guile_major;
}

int guile_dyn_minor(void) {
    if (init_state == 0) guile_dyn_libok();
    return guile_minor;
}

const char *guile_dyn_version(void) {
    if (init_state == 0) guile_dyn_libok();
    return guile_version_str;
}

int guile_dyn_is_initialized(void) {
    if (!guile_dyn_libok()) return 0;
    if (!guile_initialized_p) {
        guile_initialized_p =
            (int *)get_sym(guile_handle_lib, "scm_initialized_p");
    }
    return guile_initialized_p ? *guile_initialized_p : 0;
}

/* ============================================================
 * 按需解析宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                \
ret name params {                                                  \
    static ret (*fn) params = NULL;                                \
    if (!fn) {                                                     \
        if (!guile_dyn_libok()) return fallback;                   \
        *(void **)(&fn) = get_sym(guile_handle_lib, #name);        \
        if (!fn) {                                                 \
            fprintf(stderr, "guile_dyn: dlsym failed: %s\n",       \
                    #name);                                        \
            return fallback;                                       \
        }                                                          \
    }                                                              \
    return fn args;                                                \
}

#define WRAP_VOID(name, params, args)                              \
void name params {                                                 \
    static void (*fn) params = NULL;                               \
    if (!fn) {                                                     \
        if (!guile_dyn_libok()) return;                           \
        *(void **)(&fn) = get_sym(guile_handle_lib, #name);        \
        if (!fn) {                                                 \
            fprintf(stderr, "guile_dyn: dlsym failed: %s\n",       \
                    #name);                                        \
            return;                                                \
        }                                                          \
    }                                                              \
    fn args;                                                       \
}

/* ============================================================
 * 初始化
 * ============================================================ */
WRAP_VOID(scm_init_guile, (void), ())
WRAP_VOID(scm_boot_guile,
          (int argc, char **argv,
           void (*main_func)(void *, int, char **), void *closure),
          (argc, argv, main_func, closure))
WRAP_RET(void *, scm_with_guile,
         (void *(*func)(void *), void *data), (func, data), NULL)
WRAP_RET(void *, scm_without_guile,
         (void *(*func)(void *), void *data), (func, data), NULL)

/* ============================================================
 * eval / 模块
 * ============================================================ */
WRAP_RET(SCM, scm_c_eval_string, (const char *expr), (expr),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_c_eval_string_in_module,
         (const char *expr, SCM module), (expr, module),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_primitive_load_path,
         (SCM filename_and_exception_on_not_found),
         (filename_and_exception_on_not_found), GUILE_DYN_SCM_UNSPECIFIED)

WRAP_RET(SCM, scm_c_define, (const char *name, SCM value), (name, value),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_c_lookup, (const char *name), (name),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_c_define_gsubr,
         (const char *name, int req, int opt, int rst, guile_dyn_subr fcn),
         (name, req, opt, rst, fcn), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_c_define_module,
         (const char *name, void (*init)(void *), void *data),
         (name, init, data), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_c_resolve_module, (const char *name), (name),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_c_public_ref,
         (const char *module_name, const char *name),
         (module_name, name), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_c_private_ref,
         (const char *module_name, const char *name),
         (module_name, name), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_current_module, (void), (), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_set_current_module, (SCM module), (module),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_effective_version, (void), (),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_VOID(scm_c_run_hook, (SCM hook, SCM args), (hook, args))

/* ============================================================
 * 调用 / apply
 * ============================================================ */
WRAP_RET(SCM, scm_call_0, (SCM proc), (proc), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_call_1, (SCM proc, SCM a1), (proc, a1),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_call_2, (SCM proc, SCM a1, SCM a2), (proc, a1, a2),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_call_3, (SCM proc, SCM a1, SCM a2, SCM a3),
         (proc, a1, a2, a3), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_call_4, (SCM proc, SCM a1, SCM a2, SCM a3, SCM a4),
         (proc, a1, a2, a3, a4), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_call_n, (SCM proc, SCM *argv, size_t nargs),
         (proc, argv, nargs), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_apply_0, (SCM proc, SCM args), (proc, args),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_apply_1, (SCM proc, SCM a1, SCM args), (proc, a1, args),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_apply_2, (SCM proc, SCM a1, SCM a2, SCM args),
         (proc, a1, a2, args), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_apply_3, (SCM proc, SCM a1, SCM a2, SCM a3, SCM args),
         (proc, a1, a2, a3, args), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_apply, (SCM proc, SCM a1, SCM args), (proc, a1, args),
         GUILE_DYN_SCM_UNSPECIFIED)

/* ============================================================
 * 数值
 * ============================================================ */WRAP_RET(SCM, scm_from_int32, (int32_t v), (v), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(int32_t, scm_to_int32, (SCM v), (v), 0)
WRAP_RET(SCM, scm_from_int64, (int64_t v), (v), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(int64_t, scm_to_int64, (SCM v), (v), 0)
WRAP_RET(SCM, scm_from_signed_integer, (intmax_t v), (v),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(intmax_t, scm_to_signed_integer,
         (SCM v, intmax_t min, intmax_t max), (v, min, max), 0)
WRAP_RET(SCM, scm_from_unsigned_integer, (uintmax_t v), (v),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(uintmax_t, scm_to_unsigned_integer,
         (SCM v, uintmax_t min, uintmax_t max), (v, min, max), 0)
WRAP_RET(SCM, scm_from_double, (double v), (v), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(double, scm_to_double, (SCM v), (v), 0.0)
WRAP_RET(int, scm_to_bool, (SCM v), (v), 0)

/* ============================================================
 * 字符串 / symbol
 * ============================================================ */
WRAP_RET(SCM, scm_from_utf8_string, (const char *s), (s),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_from_utf8_stringn, (const char *s, size_t n), (s, n),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(char *, scm_to_utf8_string, (SCM s), (s), NULL)
WRAP_RET(char *, scm_to_utf8_stringn, (SCM s, size_t *len), (s, len), NULL)
WRAP_RET(SCM, scm_from_locale_string, (const char *s), (s),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(char *, scm_to_locale_string, (SCM s), (s), NULL)
WRAP_RET(SCM, scm_string_to_symbol, (SCM s), (s), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_from_utf8_symbol, (const char *s), (s),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_symbol_to_string, (SCM s), (s), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(int, scm_is_string, (SCM v), (v), 0)
WRAP_RET(int, scm_is_number, (SCM v), (v), 0)
WRAP_RET(int, scm_is_pair, (SCM v), (v), 0)
WRAP_RET(int, scm_is_vector, (SCM v), (v), 0)

/* ============================================================
 * list / vector / hash
 * ============================================================ */
WRAP_RET(SCM, scm_cons, (SCM a, SCM b), (a, b), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_car, (SCM p), (p), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_cdr, (SCM p), (p), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_list_1, (SCM a), (a), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_list_2, (SCM a, SCM b), (a, b), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_list_3, (SCM a, SCM b, SCM c), (a, b, c),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_list_4, (SCM a, SCM b, SCM c, SCM d), (a, b, c, d),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_list_ref, (SCM lst, SCM k), (lst, k),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(long, scm_ilength, (SCM lst), (lst), -1)
WRAP_RET(SCM, scm_vector_ref, (SCM v, SCM k), (v, k),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_c_vector_ref, (SCM v, size_t k), (v, k),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_vector_length, (SCM v), (v), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_make_hash_table, (SCM n), (n), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_hash_ref, (SCM table, SCM key, SCM dflt),
         (table, key, dflt), GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_hash_set_x, (SCM table, SCM key, SCM val),
         (table, key, val), GUILE_DYN_SCM_UNSPECIFIED)

/* ============================================================
 * 错误 / catch
 * ============================================================ */
WRAP_VOID(scm_error,
          (SCM key, const char *subr, const char *message, SCM args,
           SCM rest),
          (key, subr, message, args, rest))
WRAP_RET(SCM, scm_throw, (SCM key, SCM args), (key, args),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_c_catch,
         (SCM tag, guile_dyn_catch_body body, void *body_data,
          guile_dyn_catch_handler handler, void *handler_data,
          guile_dyn_catch_handler pre_unwind_handler,
          void *pre_unwind_handler_data),
         (tag, body, body_data, handler, handler_data,
          pre_unwind_handler, pre_unwind_handler_data),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_VOID(scm_misc_error,
          (const char *subr, const char *message, SCM args),
          (subr, message, args))
WRAP_VOID(scm_wrong_type_arg,
          (const char *subr, int pos, SCM bad_value),
          (subr, pos, bad_value))
WRAP_VOID(scm_out_of_range,
          (const char *subr, SCM bad_value), (subr, bad_value))
WRAP_VOID(scm_syserror, (const char *subr), (subr))

/* ============================================================
 * GC / dynwind
 * ============================================================ */
WRAP_RET(void *, scm_gc_malloc, (size_t size, const char *what),
         (size, what), NULL)
WRAP_VOID(scm_gc_free, (void *mem, size_t size, const char *what),
          (mem, size, what))
WRAP_RET(SCM, scm_gc_protect_object, (SCM obj), (obj),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_RET(SCM, scm_gc_unprotect_object, (SCM obj), (obj),
         GUILE_DYN_SCM_UNSPECIFIED)
WRAP_VOID(scm_dynwind_begin, (int flags), (flags))
WRAP_VOID(scm_dynwind_end, (void), ())
WRAP_VOID(scm_dynwind_unwind_handler,
          (void (*func)(void *), void *data, int flags),
          (func, data, flags))
WRAP_VOID(scm_dynwind_rewind_handler,
          (void (*func)(void *), void *data, int flags),
          (func, data, flags))

/* ============================================================
 * 宏 API 的等价 helper
 * ============================================================ */
SCM guile_dyn_from_bool(int v) {
    return v ? GUILE_DYN_SCM_BOOL_T : GUILE_DYN_SCM_BOOL_F;
}

int guile_dyn_is_false(SCM x) {
    return x == GUILE_DYN_SCM_BOOL_F || x == GUILE_DYN_SCM_ELISP_NIL;
}

int guile_dyn_is_true(SCM x) {
    return !guile_dyn_is_false(x);
}

int guile_dyn_is_null(SCM x) {
    return x == GUILE_DYN_SCM_EOL;
}

int guile_dyn_is_eq(SCM a, SCM b) {
    return a == b;
}

SCM guile_dyn_from_int(int v) {
    if (sizeof(int) > 4)
        return scm_from_int64((int64_t)v);
    return scm_from_int32((int32_t)v);
}

int guile_dyn_to_int(SCM v) {
    if (sizeof(int) > 4)
        return (int)scm_to_int64(v);
    return (int)scm_to_int32(v);
}

SCM guile_dyn_from_long(long v) {
    if (sizeof(long) > 4)
        return scm_from_int64((int64_t)v);
    return scm_from_int32((int32_t)v);
}

long guile_dyn_to_long(SCM v) {
    if (sizeof(long) > 4)
        return (long)scm_to_int64(v);
    return (long)scm_to_int32(v);
}

long guile_dyn_list_length(SCM lst) {
    return scm_ilength(lst);
}

SCM guile_dyn_from_utf8(const char *s) {
    if (!s) return GUILE_DYN_SCM_BOOL_F;
    return scm_from_utf8_string(s);
}

SCM guile_dyn_from_utf8n(const char *s, size_t n) {
    if (!s) return GUILE_DYN_SCM_BOOL_F;
    return scm_from_utf8_stringn(s, n);
}

/*
 * 返回 Guile GC 分配的 UTF-8 字符串；用 guile_dyn_free_string 释放。
 * 字符串含 NUL 时请改用 scm_to_utf8_stringn。
 */
char *guile_dyn_to_utf8(SCM str) {
    return scm_to_utf8_string(str);
}

void guile_dyn_free_string(char *s) {
    /*
     * scm_to_utf8_string / scm_to_locale_string 使用 scm_malloc 分配，
     * 必须用 free() 释放；不能交给 scm_gc_free（会导致 GC_free 崩溃）。
     */
    if (s)
        free(s);
}

/* 需要 Guile 已初始化；否则返回 NULL */
const char *guile_dyn_runtime_version(void) {
    static char buf[64];
    SCM v;
    char *s;
    size_t len = 0;

    if (!guile_dyn_is_initialized()) return NULL;
    v = scm_effective_version();
    s = scm_to_utf8_stringn(v, &len);
    if (!s) return NULL;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, s, len);
    buf[len] = '\0';
    free(s);
    return buf;
}
