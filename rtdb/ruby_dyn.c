/* ============================================================================
 * ruby_dyn.c — libruby 动态加载封装层（Ruby 2.7 / 3.x）
 *
 * 设计约束（与 mpv_dyn.c / python_dyn.c / guile_dyn.c 一致）：
 *   1. 链接期绝不出现 -lruby。
 *   2. 无自定义 .h 文件，无独立 wrapper .c。
 *   3. 每个封装函数首次调用时才 dlsym 自己的符号并缓存。
 *   4. 封装函数名与原版 Ruby C API 一致；宏 API 用 ruby_dyn_* helper 补齐。
 *   5. 编译期不需要 <ruby.h>；VALUE/ID 在本文件内按机器字定义。
 *   6. 跨平台：Unix/macOS 用 dlopen/dlsym；Windows 用 LoadLibrary/GetProcAddress。
 *   7. 搜索顺序：显式路径 > RUBY_DYN_LIB > 系统搜索路径 > 默认库名
 *      > Nix / Homebrew / rbenv / asdf / 平台默认路径。
 *   8. 线程安全：首次调用 ruby_libok() 或任一封装函数时完成加载。
 *
 * 版本：
 *   - 面向 Ruby 2.7 与 3.x（3.0 - 3.6 库名都会尝试）。
 *   - 通过 ruby_version / ruby_api_version 数据符号运行时探测版本。
 *   - 默认优先最新 3.x，找不到再回退 2.7。
 *
 * 重要限制：
 *   - Ruby C API 中的可变参数函数无法透明转发：
 *     rb_funcall / rb_raise 等不封装；分别使用 rb_funcallv /
 *     rb_exc_new_cstr + rb_exc_raise。
 *   - VALUE 是机器字（uintptr_t），VALUE 不是指针；Qnil/Qtrue/Qfalse
 *     在 64 位（USE_FLONUM）和 32 位下不同，使用 ruby_dyn_qnil() 等 helper。
 *   - Ruby API 默认要求调用方持有 GVL；Ruby 线程/栈规则请遵循官方文档。
 *   - ruby_init 应在主线程调用；非主线程场景需要 ruby_init_stack。
 *   - ruby_init_loadpath 不调用会导致 require 找不到标准库。
 *   - rb_eval_string 出错会 longjmp；安全路径是 rb_eval_string_protect /
 *     rb_protect / rb_rescue / rb_ensure。
 *   - rb_string_value_cstr 返回内部指针，GC 后可能失效；
 *     ruby_dyn_to_cstr() 会复制到 malloc 缓冲区。
 *   - ruby_cleanup 之后不要卸载 libruby。
 *
 * 使用顺序示例：
 *
 *   ruby_sysinit(&argc, &argv);      // 可选
 *   ruby_init();
 *   ruby_init_loadpath();
 *   VALUE v = rb_eval_string_protect("1 + 2", &state);
 *   printf("%ld\n", rb_num2long(v));
 *   char *s = ruby_dyn_to_cstr(v);
 *   ruby_dyn_free(s);
 *   ruby_cleanup(0);
 *   ruby_dyn_cleanup();              // 不卸载库
 *
 * 编译：链接期只需要 -ldl（Linux/BSD），不要链接 -lruby。
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================
 * VALUE / ID 与特殊常量
 * ============================================================ */
typedef uintptr_t VALUE;
typedef uintptr_t ID;

#if UINTPTR_MAX > 0xFFFFFFFFu
  /* 64 位平台，USE_FLONUM */
  #define RUBY_DYN_QFALSE ((VALUE)0x00)
  #define RUBY_DYN_QNIL   ((VALUE)0x04)
  #define RUBY_DYN_QTRUE  ((VALUE)0x14)
  #define RUBY_DYN_QUNDEF ((VALUE)0x24)
#else
  /* 32 位平台，无 Flonum */
  #define RUBY_DYN_QFALSE ((VALUE)0x00)
  #define RUBY_DYN_QNIL   ((VALUE)0x02)
  #define RUBY_DYN_QTRUE  ((VALUE)0x06)
  #define RUBY_DYN_QUNDEF ((VALUE)0x0A)
#endif

/* Ruby 方法入口（ANYARGS） */
typedef VALUE (*ruby_dyn_method)();
typedef VALUE (*ruby_dyn_protect_func)(VALUE);
typedef VALUE (*ruby_dyn_rescue_func)(VALUE, VALUE);
typedef VALUE (*ruby_dyn_ensure_func)(VALUE);
typedef int (*ruby_dyn_hash_foreach_func)(VALUE, VALUE, VALUE);
typedef void *(*ruby_dyn_gvl_func)(void *);
typedef void (*ruby_dyn_ubf_func)(void *);
typedef VALUE (*ruby_dyn_thread_func)(void *);

/* ============================================================
 * 平台适配层
 * ============================================================ */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE ruby_lib_handle_t;
  static ruby_lib_handle_t ruby_load_lib(const char *path) { return LoadLibraryA(path); }
  static void *ruby_get_sym(ruby_lib_handle_t h, const char *name) {
      return (void*)GetProcAddress(h, name);
  }
  static void ruby_free_lib(ruby_lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *ruby_lib_error(void) { return "LoadLibrary failed"; }
  #define RUBY_PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *ruby_lib_handle_t;
  static ruby_lib_handle_t ruby_load_lib(const char *path) {
      /*
       * Ruby 扩展模块需要全局可见的 ruby 符号，使用 RTLD_GLOBAL。
       * 与 mpv_dyn.c 的 RTLD_LOCAL 不同。
       */
      int flags = RTLD_LAZY | RTLD_GLOBAL;
      return dlopen(path, flags);
  }
  static void *ruby_get_sym(ruby_lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void ruby_free_lib(ruby_lib_handle_t h) { if (h) dlclose(h); }
  static const char *ruby_lib_error(void) { return dlerror(); }
  #define RUBY_PATH_SEP ':'
#endif

/* ============================================================
 * 全局状态
 * ============================================================ */
static ruby_lib_handle_t ruby_handle_lib = NULL;
static int ruby_init_state = 0;          /* 0=未初始化, 1=已加载, -1=失败 */
static int ruby_major = 0;
static int ruby_minor = 0;
static char ruby_version_str[128];
static const char *ruby_platform_str = NULL;
static int ruby_want_major = 0;          /* 0=自动（优先3）, 2, 3 */
static int ruby_want_minor = -1;         /* -1=最新 */
static int ruby_want_loaded = 0;

#define RUBY_MAX_CANDIDATES 256
#define RUBY_PATH_BUF_LEN   1024

/* ============================================================
 * 候选路径工具
 * ============================================================ */
static int ruby_push_candidate(const char **candidates,
                          char bufs[][RUBY_PATH_BUF_LEN],
                          int n, const char *path) {
    if (n >= RUBY_MAX_CANDIDATES - 1) return n;
    snprintf(bufs[n], RUBY_PATH_BUF_LEN, "%s", path);
    candidates[n] = bufs[n];
    return n + 1;
}

static int ruby_push_candidate_fmt(const char **candidates,
                              char bufs[][RUBY_PATH_BUF_LEN],
                              int n, const char *fmt, ...) {
    va_list ap;
    if (n >= RUBY_MAX_CANDIDATES - 1) return n;
    va_start(ap, fmt);
    vsnprintf(bufs[n], RUBY_PATH_BUF_LEN, fmt, ap);
    va_end(ap);
    candidates[n] = bufs[n];
    return n + 1;
}

static char *ruby_dup_string(const char *s) {
    size_t len = strlen(s) + 1;
    char *p = (char *)malloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

/* ============================================================
 * 版本选择
 * ============================================================ */
static void ruby_load_want_from_env(void) {
    const char *v;
    int maj = 0, min = -1;
    if (ruby_want_loaded) return;
    ruby_want_loaded = 1;
    v = getenv("RUBY_DYN_VER");
    if (!v || !*v) return;
    if (sscanf(v, "%d.%d", &maj, &min) < 1) return;
    if (maj != 2 && maj != 3) return;
    ruby_want_major = maj;
    ruby_want_minor = (min >= 0) ? min : -1;
}

static int push_ruby_bare(const char **candidates,
                          char bufs[][RUBY_PATH_BUF_LEN], int n) {
    int lo, hi, maj, min;
    if (ruby_want_major == 0 || ruby_want_major == 3) {
        lo = (ruby_want_major == 3 && ruby_want_minor >= 0) ? ruby_want_minor : 0;
        hi = (ruby_want_major == 3 && ruby_want_minor >= 0) ? ruby_want_minor : 6;
        for (min = hi; min >= lo; --min) {
            maj = 3;
#ifdef _WIN32
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "ruby%d%d0.dll", maj, min);
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "ruby%d%d.dll", maj, min);
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "msvcrt-ruby%d%d0.dll", maj, min);
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "x64-msvcrt-ruby%d%d0.dll", maj, min);
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "x64-ucrt-ruby%d%d0.dll", maj, min);
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "ucrt-ruby%d%d0.dll", maj, min);
#elif defined(__APPLE__)
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "libruby.%d.%d.dylib", maj, min);
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "libruby-%d.%d.dylib", maj, min);
#else
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "libruby.so.%d.%d", maj, min);
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "libruby-%d.%d.so.%d.%d", maj, min, maj, min);
#endif
        }
        if (ruby_want_minor < 0) {
#ifdef _WIN32
            /* Windows 版本化 DLL 已在上面逐个尝试，无需通用名 */
#elif defined(__APPLE__)
            n = ruby_push_candidate(candidates, bufs, n, "libruby.dylib");
#else
            n = ruby_push_candidate(candidates, bufs, n, "libruby.so");
#endif
        }
    }

    if (ruby_want_major == 0 || ruby_want_major == 2) {
        if (ruby_want_minor < 0 || ruby_want_minor == 7) {
#ifdef _WIN32
            n = ruby_push_candidate(candidates, bufs, n, "ruby270.dll");
            n = ruby_push_candidate(candidates, bufs, n, "x64-msvcrt-ruby270.dll");
#elif defined(__APPLE__)
            n = ruby_push_candidate(candidates, bufs, n, "libruby.2.7.dylib");
            n = ruby_push_candidate(candidates, bufs, n, "libruby-2.7.dylib");
#else
            n = ruby_push_candidate(candidates, bufs, n, "libruby.so.2.7");
            n = ruby_push_candidate(candidates, bufs, n, "libruby-2.7.so.2.7");
#endif
        }
    }
    return n;
}

/* ============================================================
 * 版本探测
 * ============================================================ */
static int ruby_try_candidate(const char *path, int idx) {
    ruby_lib_handle_t h;
    void *sym_init;
    void *sym_setup;
    void *sym_eval;
    const char *ver;
    const int *api;
    int maj = 0, min = 0;

    h = ruby_load_lib(path);
    if (!h) return -1;

    sym_init = ruby_get_sym(h, "ruby_init");
    sym_setup = ruby_get_sym(h, "ruby_setup");
    sym_eval = ruby_get_sym(h, "rb_eval_string");
    ver = (const char *)ruby_get_sym(h, "ruby_version");
    if ((!sym_init && !sym_setup) || !sym_eval || !ver) {
        ruby_free_lib(h);
        return -1;
    }

    api = (const int *)ruby_get_sym(h, "ruby_api_version");
    if (api) {
        maj = api[0];
        min = api[1];
    } else if (sscanf(ver, "%d.%d", &maj, &min) < 2) {
        ruby_free_lib(h);
        return -1;
    }

    if (maj != 2 && maj != 3) {
        ruby_free_lib(h);
        return -1;
    }
    if (ruby_want_major != 0 && maj != ruby_want_major) {
        ruby_free_lib(h);
        return -1;
    }
    if (ruby_want_minor >= 0 && min != ruby_want_minor) {
        ruby_free_lib(h);
        return -1;
    }

    ruby_handle_lib = h;
    ruby_major = maj;
    ruby_minor = min;
    snprintf(ruby_version_str, sizeof(ruby_version_str), "%s", ver);
    ruby_platform_str = (const char *)ruby_get_sym(h, "ruby_platform");
    fprintf(stderr, "ruby_dyn: loaded %d %s (Ruby %s)\n", idx, path, ruby_version_str);
    return 0;
}

/* ============================================================
 * 库加载
 * ============================================================ */
static int ruby_do_load(const char *library_path) {
    static char bufs[RUBY_MAX_CANDIDATES][RUBY_PATH_BUF_LEN];
    const char *candidates[RUBY_MAX_CANDIDATES];
    int n = 0;
    int i;
    const char *env_path;

    if (ruby_handle_lib) return 0;
    ruby_load_want_from_env();

    /* 1. 显式路径 */
    if (library_path && *library_path)
        n = ruby_push_candidate(candidates, bufs, n, library_path);

    /* 2. 环境变量 */
    {
        const char *explicit_lib = getenv("RUBY_DYN_LIB");
        if (explicit_lib && *explicit_lib)
            n = ruby_push_candidate(candidates, bufs, n, explicit_lib);
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
        char *copy = ruby_dup_string(env_path);
        if (copy) {
            char *p = copy;
            while (p && *p) {
                char *sep = strchr(p, RUBY_PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
#ifdef _WIN32
                    n = ruby_push_candidate_fmt(candidates, bufs, n,
                                           "%s/ruby340.dll", p);
#elif defined(__APPLE__)
                    n = ruby_push_candidate_fmt(candidates, bufs, n,
                                           "%s/libruby.dylib", p);
#else
                    n = ruby_push_candidate_fmt(candidates, bufs, n,
                                           "%s/libruby.so", p);
#endif
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 4. 默认库名 */
    n = push_ruby_bare(candidates, bufs, n);

    /* 5. Nix / Homebrew / rbenv / asdf */
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        const char *rbenv_root = getenv("RBENV_ROOT");
        const char *asdf_root = getenv("ASDF_DATA_DIR");
        char dir[RUBY_PATH_BUF_LEN];

        if (home && *home) {
            snprintf(dir, sizeof(dir), "%s/.nix-profile/lib", home);
#ifdef __APPLE__
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "%s/libruby.dylib", dir);
#else
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "%s/libruby.so", dir);
#endif
        }
#ifdef __APPLE__
        n = ruby_push_candidate(candidates, bufs, n,
                           "/opt/homebrew/opt/ruby/lib/libruby.dylib");
        n = ruby_push_candidate(candidates, bufs, n,
                           "/usr/local/opt/ruby/lib/libruby.dylib");
        n = ruby_push_candidate(candidates, bufs, n,
                           "/opt/homebrew/lib/libruby.dylib");
#else
        n = ruby_push_candidate(candidates, bufs, n,
                           "/usr/local/lib/libruby.so");
        n = ruby_push_candidate(candidates, bufs, n,
                           "/usr/lib/libruby.so");
        n = ruby_push_candidate(candidates, bufs, n,
                           "/usr/lib/x86_64-linux-gnu/libruby.so");
        n = ruby_push_candidate(candidates, bufs, n,
                           "/usr/lib/aarch64-linux-gnu/libruby.so");
#endif
        if (rbenv_root && *rbenv_root && ruby_want_major != 0) {
            snprintf(dir, sizeof(dir), "%s/versions/%d.%d.0/lib", rbenv_root,
                     ruby_want_major, ruby_want_minor < 0 ? 0 : ruby_want_minor);
#ifdef __APPLE__
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "%s/libruby.dylib", dir);
#else
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "%s/libruby.so", dir);
#endif
        }
        if (asdf_root && *asdf_root && ruby_want_major != 0) {
            snprintf(dir, sizeof(dir), "%s/installs/ruby/%d.%d.0/lib", asdf_root,
                     ruby_want_major, ruby_want_minor < 0 ? 0 : ruby_want_minor);
#ifdef __APPLE__
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "%s/libruby.dylib", dir);
#else
            n = ruby_push_candidate_fmt(candidates, bufs, n,
                                   "%s/libruby.so", dir);
#endif
        }
    }
#endif
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        if (ruby_try_candidate(candidates[i], i) == 0)
            return 0;
    }

    {
        const char *err = ruby_lib_error();
        fprintf(stderr, "ruby_dyn: cannot load libruby (%s)\n",
                err ? err : "unknown error");
    }
    return -1;
}

/* ============================================================
 * 控制函数
 * ============================================================ */
int ruby_libok(void) {
    if (ruby_init_state == 0)
        ruby_init_state = (ruby_do_load(NULL) == 0) ? 1 : -1;
    return (ruby_init_state == 1 && ruby_handle_lib != NULL) ? 1 : 0;
}

int ruby_dyn_init(const char *library_path) {
    if (ruby_init_state == 1) return 0;
    if (ruby_init_state == -1) return -1;
    if (ruby_do_load(library_path) == 0) {
        ruby_init_state = 1;
        return 0;
    }
    ruby_init_state = -1;
    return -1;
}

int ruby_dyn_init_version(int major, int minor) {
    if (major != 2 && major != 3) return -1;
    if (major == 2 && minor >= 0 && minor != 7) return -1;
    if (ruby_init_state == 1) {
        if (ruby_major == major && (minor < 0 || ruby_minor == minor))
            return 0;
        return -1;
    }
    if (ruby_init_state == -1) ruby_init_state = 0;
    ruby_want_major = major;
    ruby_want_minor = minor;
    ruby_want_loaded = 1;
    if (ruby_do_load(NULL) == 0) {
        ruby_init_state = 1;
        return 0;
    }
    ruby_init_state = -1;
    return -1;
}

/*
 * Ruby 初始化后不适合 dlclose（VM、线程、GC 状态）。
 * 只有加载失败状态才复位，允许修正路径/版本后重试。
 */
void ruby_dyn_cleanup(void) {
    if (ruby_init_state == -1)
        ruby_init_state = 0;
}

int ruby_dyn_major(void) {
    if (ruby_init_state == 0) ruby_libok();
    return ruby_major;
}

int ruby_dyn_minor(void) {
    if (ruby_init_state == 0) ruby_libok();
    return ruby_minor;
}

const char *ruby_dyn_version(void) {
    if (ruby_init_state == 0) ruby_libok();
    return ruby_version_str;
}

const char *ruby_dyn_platform(void) {
    if (ruby_init_state == 0) ruby_libok();
    return ruby_platform_str;
}

VALUE ruby_dyn_qfalse(void) { return RUBY_DYN_QFALSE; }
VALUE ruby_dyn_qnil(void)   { return RUBY_DYN_QNIL; }
VALUE ruby_dyn_qtrue(void)  { return RUBY_DYN_QTRUE; }
VALUE ruby_dyn_qundef(void) { return RUBY_DYN_QUNDEF; }

/* ============================================================
 * 按需解析宏
 * ============================================================ */
#define RUBY_WRAP_RET(ret, name, params, args, fallback)                \
ret name params {                                                  \
    static ret (*fn) params = NULL;                                \
    if (!fn) {                                                     \
        if (!ruby_libok()) return fallback;                        \
        *(void **)(&fn) = ruby_get_sym(ruby_handle_lib, #name);         \
        if (!fn) {                                                 \
            fprintf(stderr, "ruby_dyn: dlsym failed: %s\n",        \
                    #name);                                        \
            return fallback;                                       \
        }                                                          \
    }                                                              \
    return fn args;                                                \
}

#define RUBY_WRAP_VOID(name, params, args)                              \
void name params {                                                 \
    static void (*fn) params = NULL;                               \
    if (!fn) {                                                     \
        if (!ruby_libok()) return;                                 \
        *(void **)(&fn) = ruby_get_sym(ruby_handle_lib, #name);         \
        if (!fn) {                                                 \
            fprintf(stderr, "ruby_dyn: dlsym failed: %s\n",        \
                    #name);                                        \
            return;                                                \
        }                                                          \
    }                                                              \
    fn args;                                                       \
}

/* ============================================================
 * 初始化 / 清理
 * ============================================================ */
RUBY_WRAP_VOID(ruby_sysinit, (int *argc, char ***argv), (argc, argv))
RUBY_WRAP_VOID(ruby_init, (void), ())
RUBY_WRAP_RET(int, ruby_setup, (void), (), -1)
RUBY_WRAP_VOID(ruby_init_stack, (volatile VALUE *addr), (addr))
RUBY_WRAP_VOID(ruby_init_loadpath, (void), ())
RUBY_WRAP_VOID(ruby_script, (const char *name), (name))
RUBY_WRAP_RET(int, ruby_cleanup, (int ex), (ex), -1)
RUBY_WRAP_VOID(ruby_finalize, (void), ())
RUBY_WRAP_RET(void *, ruby_options, (int argc, char **argv), (argc, argv), NULL)
RUBY_WRAP_RET(int, ruby_run_node, (void *node), (node), -1)

/* ============================================================
 * 执行 / 加载
 * ============================================================ */
RUBY_WRAP_RET(VALUE, rb_eval_string, (const char *s), (s), 0)
RUBY_WRAP_RET(VALUE, rb_eval_string_protect, (const char *s, int *state),
         (s, state), 0)
RUBY_WRAP_RET(VALUE, rb_eval_string_wrap, (const char *s, int *state),
         (s, state), 0)
RUBY_WRAP_VOID(rb_load, (VALUE path, int wrap), (path, wrap))
RUBY_WRAP_VOID(rb_load_protect, (VALUE path, int wrap, int *state),
          (path, wrap, state))
RUBY_WRAP_RET(VALUE, rb_require, (const char *feature), (feature), 0)
RUBY_WRAP_RET(VALUE, rb_protect,
         (ruby_dyn_protect_func func, VALUE data, int *state),
         (func, data, state), 0)
RUBY_WRAP_RET(VALUE, rb_rescue,
         (ruby_dyn_protect_func b_proc, VALUE data1,
          ruby_dyn_rescue_func r_proc, VALUE data2),
         (b_proc, data1, r_proc, data2), 0)
RUBY_WRAP_RET(VALUE, rb_ensure,
         (ruby_dyn_protect_func b_proc, VALUE data1,
          ruby_dyn_ensure_func e_proc, VALUE data2),
         (b_proc, data1, e_proc, data2), 0)
RUBY_WRAP_VOID(rb_jump_tag, (int state), (state))

/* ============================================================
 * 错误
 * ============================================================ */
RUBY_WRAP_RET(VALUE, rb_errinfo, (void), (), 0)
RUBY_WRAP_VOID(rb_set_errinfo, (VALUE err), (err))
RUBY_WRAP_VOID(rb_exc_raise, (VALUE exc), (exc))
RUBY_WRAP_RET(VALUE, rb_exc_new_cstr, (VALUE etype, const char *str),
         (etype, str), 0)

/* ============================================================
 * 调用 / 对象
 * ============================================================ */
RUBY_WRAP_RET(VALUE, rb_funcallv,
         (VALUE recv, ID mid, int argc, const VALUE *argv),
         (recv, mid, argc, argv), 0)
RUBY_WRAP_RET(VALUE, rb_funcallv_public,
         (VALUE recv, ID mid, int argc, const VALUE *argv),
         (recv, mid, argc, argv), 0)
RUBY_WRAP_RET(VALUE, rb_check_funcall,
         (VALUE recv, ID mid, int argc, const VALUE *argv),
         (recv, mid, argc, argv), 0)
RUBY_WRAP_RET(VALUE, rb_class_new_instance,
         (int argc, const VALUE *argv, VALUE klass),
         (argc, argv, klass), 0)
RUBY_WRAP_RET(VALUE, rb_obj_as_string, (VALUE obj), (obj), 0)
RUBY_WRAP_RET(VALUE, rb_inspect, (VALUE obj), (obj), 0)
RUBY_WRAP_RET(const char *, rb_obj_classname, (VALUE obj), (obj), NULL)
RUBY_WRAP_RET(VALUE, rb_obj_class, (VALUE obj), (obj), 0)
RUBY_WRAP_RET(VALUE, rb_obj_is_kind_of, (VALUE obj, VALUE klass),
         (obj, klass), 0)
RUBY_WRAP_RET(VALUE, rb_obj_is_instance_of, (VALUE obj, VALUE klass),
         (obj, klass), 0)
RUBY_WRAP_RET(int, rb_respond_to, (VALUE obj, ID mid), (obj, mid), 0)
RUBY_WRAP_RET(VALUE, rb_obj_freeze, (VALUE obj), (obj), 0)
RUBY_WRAP_RET(VALUE, rb_obj_frozen_p, (VALUE obj), (obj), 0)

/* ============================================================
 * symbol / 全局变量
 * ============================================================ */
RUBY_WRAP_RET(ID, rb_intern, (const char *name), (name), 0)
RUBY_WRAP_RET(ID, rb_intern2, (const char *name, long len), (name, len), 0)
RUBY_WRAP_RET(const char *, rb_id2name, (ID id), (id), NULL)
RUBY_WRAP_RET(VALUE, rb_sym2str, (VALUE sym), (sym), 0)
RUBY_WRAP_RET(VALUE, rb_str_intern, (VALUE str), (str), 0)
RUBY_WRAP_RET(VALUE, rb_gv_get, (const char *name), (name), 0)
RUBY_WRAP_RET(VALUE, rb_gv_set, (const char *name, VALUE val), (name, val), 0)
RUBY_WRAP_RET(VALUE, rb_ivar_get, (VALUE obj, ID name), (obj, name), 0)
RUBY_WRAP_RET(VALUE, rb_ivar_set, (VALUE obj, ID name, VALUE val),
         (obj, name, val), 0)
RUBY_WRAP_RET(VALUE, rb_const_get, (VALUE space, ID name), (space, name), 0)
RUBY_WRAP_VOID(rb_const_set, (VALUE space, ID name, VALUE val),
          (space, name, val))

/* ============================================================
 * 数值
 * ============================================================ */
RUBY_WRAP_RET(VALUE, rb_int2inum, (intptr_t v), (v), 0)
RUBY_WRAP_RET(long, rb_num2long, (VALUE v), (v), -1)
RUBY_WRAP_RET(long long, rb_num2ll, (VALUE v), (v), -1)
RUBY_WRAP_RET(VALUE, rb_ll2inum, (long long v), (v), 0)
RUBY_WRAP_RET(VALUE, rb_float_new, (double v), (v), 0)
RUBY_WRAP_RET(double, rb_num2dbl, (VALUE v), (v), -1.0)

/* ============================================================
 * 字符串
 * ============================================================ */
RUBY_WRAP_RET(VALUE, rb_str_new, (const char *ptr, long len), (ptr, len), 0)
RUBY_WRAP_RET(VALUE, rb_str_new_cstr, (const char *ptr), (ptr), 0)
RUBY_WRAP_RET(VALUE, rb_utf8_str_new_cstr, (const char *ptr), (ptr), 0)
RUBY_WRAP_RET(VALUE, rb_string_value, (volatile VALUE *ptr), (ptr), 0)
RUBY_WRAP_RET(char *, rb_string_value_ptr, (volatile VALUE *ptr), (ptr), NULL)
RUBY_WRAP_RET(char *, rb_string_value_cstr, (volatile VALUE *ptr), (ptr), NULL)
RUBY_WRAP_RET(VALUE, rb_str_cat, (VALUE dst, const char *src, long len),
         (dst, src, len), 0)
RUBY_WRAP_RET(VALUE, rb_str_cat_cstr, (VALUE dst, const char *src),
         (dst, src), 0)

/* ============================================================
 * 数组 / 哈希
 * ============================================================ */
RUBY_WRAP_RET(VALUE, rb_ary_new, (void), (), 0)
RUBY_WRAP_RET(VALUE, rb_ary_new_capa, (long capa), (capa), 0)
RUBY_WRAP_RET(VALUE, rb_ary_push, (VALUE ary, VALUE elem), (ary, elem), 0)
RUBY_WRAP_RET(VALUE, rb_ary_pop, (VALUE ary), (ary), 0)
RUBY_WRAP_RET(VALUE, rb_ary_entry, (VALUE ary, long off), (ary, off), 0)
RUBY_WRAP_RET(VALUE, rb_ary_join, (VALUE ary, VALUE sep), (ary, sep), 0)
RUBY_WRAP_RET(VALUE, rb_ary_concat, (VALUE lhs, VALUE rhs), (lhs, rhs), 0)
RUBY_WRAP_RET(VALUE, rb_hash_new, (void), (), 0)
RUBY_WRAP_RET(VALUE, rb_hash_aset, (VALUE hash, VALUE key, VALUE val),
         (hash, key, val), 0)
RUBY_WRAP_RET(VALUE, rb_hash_aref, (VALUE hash, VALUE key), (hash, key), 0)
RUBY_WRAP_RET(VALUE, rb_hash_delete, (VALUE hash, VALUE key), (hash, key), 0)
RUBY_WRAP_VOID(rb_hash_foreach,
          (VALUE hash, ruby_dyn_hash_foreach_func func, VALUE arg),
          (hash, func, arg))
RUBY_WRAP_RET(VALUE, rb_hash_size, (VALUE hash), (hash), 0)

/* ============================================================
 * GC / 线程
 * ============================================================ */
RUBY_WRAP_RET(VALUE, rb_gc_start, (void), (), 0)
RUBY_WRAP_RET(VALUE, rb_gc_disable, (void), (), 0)
RUBY_WRAP_RET(VALUE, rb_gc_enable, (void), (), 0)
RUBY_WRAP_VOID(rb_gc_register_address, (VALUE *valptr), (valptr))
RUBY_WRAP_VOID(rb_gc_unregister_address, (VALUE *valptr), (valptr))
RUBY_WRAP_RET(void *, rb_thread_call_without_gvl,
         (ruby_dyn_gvl_func func, void *data1,
          ruby_dyn_ubf_func ubf, void *data2),
         (func, data1, ubf, data2), NULL)
RUBY_WRAP_RET(void *, rb_thread_call_with_gvl,
         (ruby_dyn_gvl_func func, void *data), (func, data), NULL)
RUBY_WRAP_RET(VALUE, rb_thread_create,
         (ruby_dyn_thread_func func, void *arg), (func, arg), 0)
RUBY_WRAP_RET(VALUE, rb_thread_current, (void), (), 0)

/* ============================================================
 * 扩展定义
 * ============================================================ */
RUBY_WRAP_RET(VALUE, rb_define_module, (const char *name), (name), 0)
RUBY_WRAP_RET(VALUE, rb_define_class, (const char *name, VALUE super),
         (name, super), 0)
RUBY_WRAP_RET(VALUE, rb_define_class_under,
         (VALUE outer, const char *name, VALUE super),
         (outer, name, super), 0)
RUBY_WRAP_VOID(rb_define_method,
          (VALUE klass, const char *name, ruby_dyn_method func, int arity),
          (klass, name, func, arity))
RUBY_WRAP_VOID(rb_define_singleton_method,
          (VALUE obj, const char *name, ruby_dyn_method func, int arity),
          (obj, name, func, arity))
RUBY_WRAP_VOID(rb_define_const,
          (VALUE klass, const char *name, VALUE val), (klass, name, val))
RUBY_WRAP_VOID(rb_define_global_const, (const char *name, VALUE val),
          (name, val))
RUBY_WRAP_VOID(rb_define_global_function,
          (const char *name, ruby_dyn_method func, int arity),
          (name, func, arity))
RUBY_WRAP_VOID(rb_define_module_function,
          (VALUE module, const char *name, ruby_dyn_method func, int arity),
          (module, name, func, arity))

/* ============================================================
 * 跨版本辅助
 * ============================================================ */
VALUE ruby_dyn_from_utf8(const char *s) {
    VALUE v;
    if (!s) return RUBY_DYN_QNIL;
    v = rb_utf8_str_new_cstr(s);
    if (v == 0)
        v = rb_str_new_cstr(s);
    return v;
}

/*
 * 复制成 malloc 分配的 C 字符串；调用方用 ruby_dyn_free() 释放。
 * Ruby 异常可能从 rb_string_value 抛出。
 */
char *ruby_dyn_to_cstr(VALUE v) {
    volatile VALUE str = v;
    char *p;

    str = rb_string_value(&str);
    p = rb_string_value_cstr(&str);
    if (!p) return NULL;
    return ruby_dup_string(p);
}

void ruby_dyn_free(void *p) {
    free(p);
}
