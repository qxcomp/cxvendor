/* ============================================================================
 * vte_dyn.c — libvte (VTE 2.91, GTK3 / GTK4) 动态加载封装层
 *
 * 设计约束（与 mpv_dyn.c / python_dyn.c / ruby_dyn.c 等一致）：
 *   1. 链接期绝不出现 -lvte / -lvte-2.91。
 *   2. 无自定义 .h 文件，无独立 wrapper .c。
 *   3. 每个封装函数首次调用时才 dlsym 自己的符号并缓存。
 *   4. 封装函数名与原版 VTE C API 一致；缺失的可选符号安全降级。
 *   5. 编译期不需要 <vte/vte.h>；VteTerminal / VtePty / VteRegex 等
 *      在本文件内用不透明类型声明。
 *   6. 跨平台：Unix/macOS 用 dlopen/dlsym；Windows 用 LoadLibrary/GetProcAddress。
 *   7. 搜索顺序：显式路径 > VTE_DYN_LIB > 系统搜索路径 > 默认库名
 *      > Nix > 平台默认路径。
 *   8. 线程安全：首次调用 vte_libok() 或任一封装函数时完成加载。
 *
 * 版本：
 *   - 同时支持 GTK3 的 libvte-2.91.so.0 和 GTK4 的
 *     libvte-2.91-gtk4.so.0；两者 API 版本都是 2.91。
 *   - 默认 GTK3；用 VTE_DYN_GTK=4 或 vte_dyn_init_gtk(4, NULL) 选择 GTK4。
 *   - 运行时通过 vte_get_major/minor/micro_version() 获取 VTE 库版本。
 *   - 不加载旧的 libvte.so.9（GTK2/VTE 0.28 API 不兼容）。
 *
 * 重要限制：
 *   - 封装层只加载 libvte，不初始化 GTK。应用必须自己链接并初始化 GTK，
 *     运行 GLib 主循环；GTK3 用 gtk_init，GTK4 用 gtk_init/gtk_application_new。
 *   - VTE/GTK 控件只能在 GTK 主线程使用。
 *   - 信号（child-exited、commit、selection-changed、window-title-changed、
 *     bell、copy-clipboard、paste-clipboard 等）不在本文件封装，
 *     请用应用侧 g_signal_connect。
 *   - vte_terminal_get_text_format / get_text_range_format /
 *     get_text_selected* / vte_get_user_shell 返回 g_malloc 内存，
 *     调用方用 g_free。
 *   - VtePty* 用 g_object_unref；VteRegex* 用 vte_regex_unref。
 *   - 初始化成功后不要 dlclose/FreeLibrary（GTK 类型系统/全局状态）。
 *   - 文件和函数都按 Unix/GTK 场景设计；GPid 按 int 处理。
 *   - VTE 是 C++ 库。实测在 GTK 已加载后再普通 dlopen libvte，
 *     某些发行版会在首次绘制时崩溃（C++ 运行时初始化顺序）。
 *     本文件优先使用 RTLD_DEEPBIND 规避；没有该标志的平台需要保证
 *     libstdc++ 先于 GTK 初始化（例如 -lstdc++ 放在 GTK 库之前，
 *     或 LD_PRELOAD=libstdc++.so.6）。
 *
 * 使用方式：
 *   调用方可以照常 #include <vte/vte.h> 和 <gtk/gtk.h>；
 *   编译/链接时只链接 GTK，不链接 -lvte，符号会解析到本封装层。
 *
 * 典型顺序：
 *   gtk_init(&argc, &argv);
 *   vte_dyn_init(NULL);              // 或 vte_dyn_init_gtk(4, NULL)
 *   GtkWidget *term = vte_terminal_new();
 *   VtePty *pty = vte_pty_new_sync(VTE_PTY_DEFAULT, NULL, &err);
 *   vte_terminal_set_pty(VTE_TERMINAL(term), pty);
 *   vte_terminal_spawn_async(...);
 *   ...
 *   g_object_unref(pty);
 *   vte_dyn_cleanup();               // 不卸载库
 *
 * 编译：链接期不需要 -lvte；Linux 需要 -ldl（glibc 2.34+ 可省略）。
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================
 * 不透明类型 / GLib 标量
 * ============================================================ */
typedef struct _VteTerminal VteTerminal;
typedef struct _VtePty VtePty;
typedef struct _VteRegex VteRegex;
typedef struct _GtkWidget GtkWidget;
typedef struct _GError GError;
typedef struct _GCancellable GCancellable;
typedef struct _GAsyncResult GAsyncResult;
typedef struct _GOutputStream GOutputStream;
typedef struct _GArray GArray;
typedef struct _GdkEvent GdkEvent;
typedef struct _GRegex GRegex;
typedef struct _PangoFontDescription PangoFontDescription;

typedef int gboolean;
typedef long glong;
typedef double gdouble;
typedef size_t gsize;
typedef ptrdiff_t gssize;
typedef uint32_t guint32;
typedef unsigned int guint;
typedef size_t GType;
typedef uint32_t GQuark;
typedef int GPid; /* Unix/GTK 场景 */

/* 与 GdkRGBA 布局一致 */
typedef struct {
    double red, green, blue, alpha;
} VteDynRGBA;

typedef void (*VteTerminalSpawnAsyncCallback)(VteTerminal *terminal,
                                              GPid pid,
                                              GError *error,
                                              void *user_data);
typedef void (*GSpawnChildSetupFunc)(void *user_data);
typedef void (*GDestroyNotify)(void *data);
typedef void (*GAsyncReadyCallback)(void *source_object,
                                    GAsyncResult *res,
                                    void *user_data);

/* 常用枚举值（GTK3/GTK4 的 Vte 2.91 相同） */
#define VTE_DYN_FORMAT_TEXT 1
#define VTE_DYN_FORMAT_HTML 2

#define VTE_DYN_PTY_DEFAULT      0
#define VTE_DYN_PTY_NO_LASTLOG   (1 << 0)
#define VTE_DYN_PTY_NO_UTMP      (1 << 1)
#define VTE_DYN_PTY_NO_WTMP      (1 << 2)
#define VTE_DYN_PTY_NO_HELPER    (1 << 3)
#define VTE_DYN_PTY_NO_FALLBACK  (1 << 4)
#define VTE_DYN_PTY_NO_SESSION   (1 << 5)
#define VTE_DYN_PTY_NO_CTTY      (1 << 6)

#define VTE_DYN_CURSOR_BLINK_SYSTEM 0
#define VTE_DYN_CURSOR_BLINK_ON     1
#define VTE_DYN_CURSOR_BLINK_OFF    2

#define VTE_DYN_CURSOR_SHAPE_BLOCK     0
#define VTE_DYN_CURSOR_SHAPE_IBEAM     1
#define VTE_DYN_CURSOR_SHAPE_UNDERLINE 2

#define VTE_DYN_WRITE_DEFAULT 0

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
       * VTE 是 C++ 库，和 GTK 的初始化顺序有关。实测在 GTK 已加载后
       * 再普通 dlopen libvte，部分发行版会在首次绘制时崩溃。
       * RTLD_DEEPBIND 让 libvte 优先绑定自己的依赖（libstdc++ 等），
       * 可避免该问题；没有 DEEPBIND 的平台需要保证 libstdc++ 先于 GTK
       * 初始化（链接顺序或 LD_PRELOAD）。
       */
      int flags = RTLD_LAZY | RTLD_LOCAL;
  #ifdef RTLD_DEEPBIND
      flags |= RTLD_DEEPBIND;
  #endif
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
static lib_handle_t vte_handle_lib = NULL;
static int init_state = 0;          /* 0=未初始化, 1=已加载, -1=失败 */
static int vte_gtk_major = 3;       /* 3 / 4 */
static int want_gtk = 0;            /* 0=自动（默认3）, 3, 4 */
static int want_gtk_explicit = 0;   /* 是否由 env/init_gtk 明确指定 */
static int vte_lib_major = 0;
static int vte_lib_minor = 0;
static int vte_lib_micro = 0;
static char vte_version_str[128];

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

/* 目标 GTK 变体的默认库名 */
static int push_vte_dir_names(const char **candidates,
                              char bufs[][PATH_BUF_LEN], int n,
                              const char *dir) {
#if defined(_WIN32)
    if (vte_gtk_major == 4) {
        n = push_candidate_fmt(candidates, bufs, n,
                               "%s/libvte-2.91-gtk4-0.dll", dir);
        n = push_candidate_fmt(candidates, bufs, n,
                               "%s/libvte-2.91-gtk4.dll", dir);
    } else {
        n = push_candidate_fmt(candidates, bufs, n,
                               "%s/libvte-2.91-0.dll", dir);
        n = push_candidate_fmt(candidates, bufs, n,
                               "%s/libvte-2.91.dll", dir);
    }
#elif defined(__APPLE__)
    if (vte_gtk_major == 4)
        n = push_candidate_fmt(candidates, bufs, n,
                               "%s/libvte-2.91-gtk4.dylib", dir);
    else
        n = push_candidate_fmt(candidates, bufs, n,
                               "%s/libvte-2.91.dylib", dir);
#else
    if (vte_gtk_major == 4) {
        n = push_candidate_fmt(candidates, bufs, n,
                               "%s/libvte-2.91-gtk4.so.0", dir);
        n = push_candidate_fmt(candidates, bufs, n,
                               "%s/libvte-2.91-gtk4.so", dir);
    } else {
        n = push_candidate_fmt(candidates, bufs, n,
                               "%s/libvte-2.91.so.0", dir);
        n = push_candidate_fmt(candidates, bufs, n,
                               "%s/libvte-2.91.so", dir);
    }
#endif
    return n;
}

static int push_vte_bare_names(const char **candidates,
                               char bufs[][PATH_BUF_LEN], int n) {
#if defined(_WIN32)
    if (vte_gtk_major == 4) {
        n = push_candidate(candidates, bufs, n, "libvte-2.91-gtk4-0.dll");
        n = push_candidate(candidates, bufs, n, "libvte-2.91-gtk4.dll");
    } else {
        n = push_candidate(candidates, bufs, n, "libvte-2.91-0.dll");
        n = push_candidate(candidates, bufs, n, "libvte-2.91.dll");
    }
#elif defined(__APPLE__)
    if (vte_gtk_major == 4)
        n = push_candidate(candidates, bufs, n, "libvte-2.91-gtk4.dylib");
    else
        n = push_candidate(candidates, bufs, n, "libvte-2.91.dylib");
#else
    if (vte_gtk_major == 4) {
        n = push_candidate(candidates, bufs, n, "libvte-2.91-gtk4.so.0");
        n = push_candidate(candidates, bufs, n, "libvte-2.91-gtk4.so");
    } else {
        n = push_candidate(candidates, bufs, n, "libvte-2.91.so.0");
        n = push_candidate(candidates, bufs, n, "libvte-2.91.so");
    }
#endif
    return n;
}

/* ============================================================
 * 加载与版本探测
 * ============================================================ */
static int try_candidate(const char *path, int idx) {
    lib_handle_t h;
    void *sym_new;
    void *sym_type;
    void *sym_major;
    void *sym_minor;
    void *sym_micro;
    guint (*fn_major)(void) = NULL;
    guint (*fn_minor)(void) = NULL;
    guint (*fn_micro)(void) = NULL;
    int is_gtk4;

    h = load_lib(path);
    if (!h) return -1;

    sym_new = get_sym(h, "vte_terminal_new");
    sym_type = get_sym(h, "vte_terminal_get_type");
    sym_major = get_sym(h, "vte_get_major_version");
    sym_minor = get_sym(h, "vte_get_minor_version");
    sym_micro = get_sym(h, "vte_get_micro_version");
    if (!sym_new || !sym_type || !sym_major || !sym_minor || !sym_micro) {
        free_lib(h);
        return -1;
    }

    is_gtk4 = (strstr(path, "gtk4") != NULL) ? 1 : 0;
    if (want_gtk_explicit && want_gtk != 0 &&
        is_gtk4 != (want_gtk == 4)) {
        free_lib(h);
        return -1;
    }

    vte_handle_lib = h;
    *(void **)(&fn_major) = sym_major;
    *(void **)(&fn_minor) = sym_minor;
    *(void **)(&fn_micro) = sym_micro;
    vte_lib_major = (int)fn_major();
    vte_lib_minor = (int)fn_minor();
    vte_lib_micro = (int)fn_micro();
    vte_gtk_major = is_gtk4 ? 4 : 3;
    snprintf(vte_version_str, sizeof(vte_version_str), "%d.%d.%d",
             vte_lib_major, vte_lib_minor, vte_lib_micro);

    fprintf(stderr, "vte_dyn: loaded %d %s (VTE %s, GTK%d)\n", idx, path, vte_version_str, vte_gtk_major);
    return 0;
}

static void load_want_from_env(void) {
    const char *v = getenv("VTE_DYN_GTK");
    if (!v || !*v) return;
    if (v[0] == '4') {
        want_gtk = 4;
        want_gtk_explicit = 1;
    } else if (v[0] == '3') {
        want_gtk = 3;
        want_gtk_explicit = 1;
    }
}

static int do_load(const char *library_path) {
    static char bufs[MAX_CANDIDATES][PATH_BUF_LEN];
    const char *candidates[MAX_CANDIDATES];
    int n = 0;
    int i;
    const char *env_path;

    if (vte_handle_lib) return 0;

    load_want_from_env();
    /* 未明确指定 GTK 版本时，默认按 GTK3 生成候选库名；
       显式路径若指向 gtk4 变体，try_candidate 会按实际路径接受。 */
    vte_gtk_major = (want_gtk == 4) ? 4 : 3;

    /* 1. 显式路径 */
    if (library_path && *library_path)
        n = push_candidate(candidates, bufs, n, library_path);

    /* 2. 环境变量 */
    {
        const char *explicit_lib = getenv("VTE_DYN_LIB");
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
                if (*p)
                    n = push_vte_dir_names(candidates, bufs, n, p);
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 4. 裸库名 */
    n = push_vte_bare_names(candidates, bufs, n);

    /* 5. Nix */
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        char dir[PATH_BUF_LEN];
        if (home && *home) {
            snprintf(dir, sizeof(dir), "%s/.nix-profile/lib", home);
            n = push_vte_dir_names(candidates, bufs, n, dir);
        }
        n = push_vte_dir_names(candidates, bufs, n,
                               "/nix/var/nix/profiles/default/lib");
    }
#endif

    /* 6. 平台默认路径 */
#ifdef __APPLE__
    n = push_vte_dir_names(candidates, bufs, n, "/opt/local/lib");
    n = push_vte_dir_names(candidates, bufs, n, "/usr/local/lib");
    n = push_vte_dir_names(candidates, bufs, n, "/opt/homebrew/lib");
    n = push_vte_dir_names(candidates, bufs, n,
                           "/opt/homebrew/opt/vte/lib");
#elif defined(__linux__) || defined(__ANDROID__)
    n = push_vte_dir_names(candidates, bufs, n, "/usr/local/lib");
    n = push_vte_dir_names(candidates, bufs, n, "/usr/lib");
    n = push_vte_dir_names(candidates, bufs, n,
                           "/usr/lib/x86_64-linux-gnu");
    n = push_vte_dir_names(candidates, bufs, n,
                           "/usr/lib/aarch64-linux-gnu");
    {
        const char *prefix = getenv("PREFIX"); /* Termux */
        if (prefix && *prefix) {
            n = push_candidate_fmt(candidates, bufs, n,
                                   "%s/lib/libvte-2.91.so.0", prefix);
        }
    }
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    n = push_vte_dir_names(candidates, bufs, n, "/usr/local/lib");
    n = push_vte_dir_names(candidates, bufs, n, "/usr/lib");
#endif
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        if (try_candidate(candidates[i], i) == 0)
            return 0;
    }

    {
        const char *err = lib_error();
        fprintf(stderr, "vte_dyn: cannot load libvte-2.91%s (%s)\n",
                want_gtk == 4 ? "-gtk4" : "",
                err ? err : "unknown error");
    }
    return -1;
}

/* ============================================================
 * 控制函数
 * ============================================================ */
int vte_libok(void) {
    if (init_state == 0)
        init_state = (do_load(NULL) == 0) ? 1 : -1;
    return (init_state == 1 && vte_handle_lib != NULL) ? 1 : 0;
}

int vte_dyn_init(const char *library_path) {
    if (init_state == 1) return 0;
    if (init_state == -1) return -1;
    if (do_load(library_path) == 0) {
        init_state = 1;
        return 0;
    }
    init_state = -1;
    return -1;
}

int vte_dyn_init_gtk(int gtk_major, const char *library_path) {
    if (gtk_major != 3 && gtk_major != 4) return -1;
    if (init_state == 1) {
        return (vte_gtk_major == gtk_major) ? 0 : -1;
    }
    if (init_state == -1) init_state = 0;
    want_gtk = gtk_major;
    want_gtk_explicit = 1;
    if (do_load(library_path) == 0) {
        init_state = 1;
        return 0;
    }
    init_state = -1;
    return -1;
}

/*
 * GTK/VTE 初始化后不适合卸载（GType 系统、线程、主循环）。
 * 只有加载失败状态才复位，允许修正路径/GTK 变体后重试。
 */
void vte_dyn_cleanup(void) {
    if (init_state == -1)
        init_state = 0;
}

const char *vte_dyn_version(void) {
    if (init_state == 0) vte_libok();
    return vte_version_str;
}

int vte_dyn_major(void) {
    if (init_state == 0) vte_libok();
    return vte_lib_major;
}

int vte_dyn_minor(void) {
    if (init_state == 0) vte_libok();
    return vte_lib_minor;
}

int vte_dyn_micro(void) {
    if (init_state == 0) vte_libok();
    return vte_lib_micro;
}

int vte_dyn_gtk_major(void) {
    if (init_state == 0) vte_libok();
    return vte_gtk_major;
}

int vte_dyn_has_symbol(const char *name) {
    if (!name || !*name) return 0;
    if (!vte_libok()) return 0;
    return get_sym(vte_handle_lib, name) != NULL;
}

/* ============================================================
 * 按需解析宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                \
ret name params {                                                  \
    static ret (*fn) params = NULL;                                \
    if (!fn) {                                                     \
        if (!vte_libok()) return fallback;                         \
        *(void **)(&fn) = get_sym(vte_handle_lib, #name);          \
        if (!fn) {                                                 \
            fprintf(stderr, "vte_dyn: dlsym failed: %s\n",         \
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
        if (!vte_libok()) return;                                  \
        *(void **)(&fn) = get_sym(vte_handle_lib, #name);          \
        if (!fn) {                                                 \
            fprintf(stderr, "vte_dyn: dlsym failed: %s\n",         \
                    #name);                                        \
            return;                                                \
        }                                                          \
    }                                                              \
    fn args;                                                       \
}

/* ============================================================
 * VTE 全局 / 版本
 * ============================================================ */
WRAP_RET(guint, vte_get_major_version, (void), (), 0)
WRAP_RET(guint, vte_get_minor_version, (void), (), 0)
WRAP_RET(guint, vte_get_micro_version, (void), (), 0)
WRAP_RET(const char *, vte_get_features, (void), (), NULL)
WRAP_RET(int, vte_get_feature_flags, (void), (), 0)
WRAP_RET(char *, vte_get_user_shell, (void), (), NULL)

/* ============================================================
 * Terminal：生命周期 / IO
 * ============================================================ */
WRAP_RET(GtkWidget *, vte_terminal_new, (void), (), NULL)
WRAP_RET(GType, vte_terminal_get_type, (void), (), 0)
WRAP_VOID(vte_terminal_reset,
          (VteTerminal *terminal, gboolean clear_tabstops,
           gboolean clear_history),
          (terminal, clear_tabstops, clear_history))

WRAP_VOID(vte_terminal_spawn_async,
          (VteTerminal *terminal, int pty_flags,
           const char *working_directory, char **argv, char **envv,
           int spawn_flags, GSpawnChildSetupFunc child_setup,
           void *child_setup_data,
           GDestroyNotify child_setup_data_destroy,
           int timeout, GCancellable *cancellable,
           VteTerminalSpawnAsyncCallback callback, void *user_data),
          (terminal, pty_flags, working_directory, argv, envv,
           spawn_flags, child_setup, child_setup_data,
           child_setup_data_destroy, timeout, cancellable,
           callback, user_data))
WRAP_RET(gboolean, vte_terminal_spawn_sync,
         (VteTerminal *terminal, int pty_flags,
          const char *working_directory, char **argv, char **envv,
          int spawn_flags, GSpawnChildSetupFunc child_setup,
          void *child_setup_data, GPid *child_pid,
          GCancellable *cancellable, GError **error),
         (terminal, pty_flags, working_directory, argv, envv,
          spawn_flags, child_setup, child_setup_data, child_pid,
          cancellable, error), 0)
WRAP_VOID(vte_terminal_watch_child,
          (VteTerminal *terminal, GPid child_pid),
          (terminal, child_pid))

WRAP_VOID(vte_terminal_feed,
          (VteTerminal *terminal, const char *data, gssize length),
          (terminal, data, length))
WRAP_VOID(vte_terminal_feed_child,
          (VteTerminal *terminal, const char *data, gssize length),
          (terminal, data, length))
WRAP_VOID(vte_terminal_feed_child_binary,
          (VteTerminal *terminal, const unsigned char *data, gsize length),
          (terminal, data, length))

WRAP_VOID(vte_terminal_set_pty,
          (VteTerminal *terminal, VtePty *pty), (terminal, pty))
WRAP_RET(VtePty *, vte_terminal_get_pty, (VteTerminal *terminal),
         (terminal), NULL)
WRAP_RET(gboolean, vte_terminal_write_contents_sync,
         (VteTerminal *terminal, GOutputStream *stream, int flags,
          GCancellable *cancellable, GError **error),
         (terminal, stream, flags, cancellable, error), 0)

WRAP_RET(const char *, vte_terminal_get_current_directory_uri,
         (VteTerminal *terminal), (terminal), NULL)
WRAP_RET(const char *, vte_terminal_get_current_file_uri,
         (VteTerminal *terminal), (terminal), NULL)
WRAP_RET(const char *, vte_terminal_get_icon_title,
         (VteTerminal *terminal), (terminal), NULL)
WRAP_RET(const char *, vte_terminal_get_window_title,
         (VteTerminal *terminal), (terminal), NULL)

WRAP_RET(glong, vte_terminal_get_column_count,
         (VteTerminal *terminal), (terminal), -1)
WRAP_RET(glong, vte_terminal_get_row_count,
         (VteTerminal *terminal), (terminal), -1)
WRAP_VOID(vte_terminal_set_size,
          (VteTerminal *terminal, glong columns, glong rows),
          (terminal, columns, rows))
WRAP_RET(glong, vte_terminal_get_char_width,
         (VteTerminal *terminal), (terminal), -1)
WRAP_RET(glong, vte_terminal_get_char_height,
         (VteTerminal *terminal), (terminal), -1)
WRAP_VOID(vte_terminal_get_cursor_position,
          (VteTerminal *terminal, glong *column, glong *row),
          (terminal, column, row))

/* ============================================================
 * Terminal：外观 / 选项
 * ============================================================ */
WRAP_VOID(vte_terminal_set_font,
          (VteTerminal *terminal, const PangoFontDescription *font_desc),
          (terminal, font_desc))
WRAP_RET(const PangoFontDescription *, vte_terminal_get_font,
         (VteTerminal *terminal), (terminal), NULL)
WRAP_VOID(vte_terminal_set_font_scale,
          (VteTerminal *terminal, gdouble scale), (terminal, scale))
WRAP_RET(gdouble, vte_terminal_get_font_scale,
         (VteTerminal *terminal), (terminal), 0.0)

WRAP_VOID(vte_terminal_set_colors,
          (VteTerminal *terminal, const VteDynRGBA *foreground,
           const VteDynRGBA *background, const VteDynRGBA *palette,
           gsize palette_size),
          (terminal, foreground, background, palette, palette_size))
WRAP_VOID(vte_terminal_set_default_colors,
          (VteTerminal *terminal), (terminal))
WRAP_VOID(vte_terminal_set_color_background,
          (VteTerminal *terminal, const VteDynRGBA *background),
          (terminal, background))
WRAP_VOID(vte_terminal_set_color_foreground,
          (VteTerminal *terminal, const VteDynRGBA *foreground),
          (terminal, foreground))
WRAP_VOID(vte_terminal_set_color_bold,
          (VteTerminal *terminal, const VteDynRGBA *bold),
          (terminal, bold))
WRAP_VOID(vte_terminal_set_color_cursor,
          (VteTerminal *terminal, const VteDynRGBA *cursor_background),
          (terminal, cursor_background))
WRAP_VOID(vte_terminal_set_color_cursor_foreground,
          (VteTerminal *terminal, const VteDynRGBA *cursor_foreground),
          (terminal, cursor_foreground))
WRAP_VOID(vte_terminal_set_color_highlight,
          (VteTerminal *terminal, const VteDynRGBA *highlight_background),
          (terminal, highlight_background))
WRAP_VOID(vte_terminal_set_color_highlight_foreground,
          (VteTerminal *terminal, const VteDynRGBA *highlight_foreground),
          (terminal, highlight_foreground))

WRAP_VOID(vte_terminal_set_scrollback_lines,
          (VteTerminal *terminal, glong lines), (terminal, lines))
WRAP_RET(glong, vte_terminal_get_scrollback_lines,
         (VteTerminal *terminal), (terminal), -1)
WRAP_VOID(vte_terminal_set_scroll_on_output,
          (VteTerminal *terminal, gboolean scroll), (terminal, scroll))
WRAP_RET(gboolean, vte_terminal_get_scroll_on_output,
         (VteTerminal *terminal), (terminal), 0)
WRAP_VOID(vte_terminal_set_scroll_on_keystroke,
          (VteTerminal *terminal, gboolean scroll), (terminal, scroll))
WRAP_RET(gboolean, vte_terminal_get_scroll_on_keystroke,
         (VteTerminal *terminal), (terminal), 0)
WRAP_VOID(vte_terminal_set_mouse_autohide,
          (VteTerminal *terminal, gboolean setting), (terminal, setting))
WRAP_RET(gboolean, vte_terminal_get_mouse_autohide,
         (VteTerminal *terminal), (terminal), 0)
WRAP_VOID(vte_terminal_set_allow_bold,
          (VteTerminal *terminal, gboolean allow_bold),
          (terminal, allow_bold))
WRAP_RET(gboolean, vte_terminal_get_allow_bold,
         (VteTerminal *terminal), (terminal), 0)
WRAP_VOID(vte_terminal_set_allow_hyperlink,
          (VteTerminal *terminal, gboolean allow_hyperlink),
          (terminal, allow_hyperlink))
WRAP_RET(gboolean, vte_terminal_get_allow_hyperlink,
         (VteTerminal *terminal), (terminal), 0)
WRAP_VOID(vte_terminal_set_input_enabled,
          (VteTerminal *terminal, gboolean enabled), (terminal, enabled))
WRAP_RET(gboolean, vte_terminal_get_input_enabled,
         (VteTerminal *terminal), (terminal), 0)
WRAP_RET(gboolean, vte_terminal_set_encoding,
         (VteTerminal *terminal, const char *codeset, GError **error),
         (terminal, codeset, error), 0)
WRAP_RET(const char *, vte_terminal_get_encoding,
         (VteTerminal *terminal), (terminal), NULL)
WRAP_VOID(vte_terminal_set_word_char_exceptions,
          (VteTerminal *terminal, const char *exceptions),
          (terminal, exceptions))
WRAP_RET(const char *, vte_terminal_get_word_char_exceptions,
         (VteTerminal *terminal), (terminal), NULL)
WRAP_VOID(vte_terminal_set_cjk_ambiguous_width,
          (VteTerminal *terminal, int width), (terminal, width))
WRAP_RET(int, vte_terminal_get_cjk_ambiguous_width,
         (VteTerminal *terminal), (terminal), -1)
WRAP_VOID(vte_terminal_set_cursor_blink_mode,
          (VteTerminal *terminal, int mode), (terminal, mode))
WRAP_RET(int, vte_terminal_get_cursor_blink_mode,
         (VteTerminal *terminal), (terminal), 0)
WRAP_VOID(vte_terminal_set_cursor_shape,
          (VteTerminal *terminal, int shape), (terminal, shape))
WRAP_RET(int, vte_terminal_get_cursor_shape,
         (VteTerminal *terminal), (terminal), 0)

/* ============================================================
 * Terminal：选择 / 剪贴板 / 文本
 * ============================================================ */
WRAP_VOID(vte_terminal_select_all, (VteTerminal *terminal), (terminal))
WRAP_VOID(vte_terminal_unselect_all, (VteTerminal *terminal), (terminal))
WRAP_RET(gboolean, vte_terminal_get_has_selection,
         (VteTerminal *terminal), (terminal), 0)
WRAP_VOID(vte_terminal_copy_clipboard,
          (VteTerminal *terminal), (terminal))
WRAP_VOID(vte_terminal_copy_clipboard_format,
          (VteTerminal *terminal, int format), (terminal, format))
WRAP_VOID(vte_terminal_paste_clipboard,
          (VteTerminal *terminal), (terminal))

WRAP_RET(char *, vte_terminal_get_text_format,
         (VteTerminal *terminal, int format), (terminal, format), NULL)
WRAP_RET(char *, vte_terminal_get_text_range_format,
         (VteTerminal *terminal, int format,
          long start_row, long start_col, long end_row, long end_col,
          gsize *length),
         (terminal, format, start_row, start_col, end_row, end_col,
          length), NULL)
WRAP_RET(char *, vte_terminal_get_text_selected,
         (VteTerminal *terminal, int format), (terminal, format), NULL)
WRAP_RET(char *, vte_terminal_get_text_selected_full,
         (VteTerminal *terminal, int format, gsize *length),
         (terminal, format, length), NULL)

/* ============================================================
 * Terminal：搜索 / 匹配
 * ============================================================ */
WRAP_VOID(vte_terminal_search_set_regex,
          (VteTerminal *terminal, VteRegex *regex, guint32 flags),
          (terminal, regex, flags))
WRAP_RET(VteRegex *, vte_terminal_search_get_regex,
         (VteTerminal *terminal), (terminal), NULL)
WRAP_VOID(vte_terminal_search_set_wrap_around,
          (VteTerminal *terminal, gboolean wrap_around),
          (terminal, wrap_around))
WRAP_RET(gboolean, vte_terminal_search_get_wrap_around,
         (VteTerminal *terminal), (terminal), 0)
WRAP_RET(gboolean, vte_terminal_search_find_next,
         (VteTerminal *terminal), (terminal), 0)
WRAP_RET(gboolean, vte_terminal_search_find_previous,
         (VteTerminal *terminal), (terminal), 0)

WRAP_RET(int, vte_terminal_match_add_regex,
         (VteTerminal *terminal, VteRegex *regex, guint32 flags),
         (terminal, regex, flags), -1)
WRAP_VOID(vte_terminal_match_remove,
          (VteTerminal *terminal, int tag), (terminal, tag))
WRAP_VOID(vte_terminal_match_remove_all,
          (VteTerminal *terminal), (terminal))
WRAP_VOID(vte_terminal_match_set_cursor_name,
          (VteTerminal *terminal, int tag, const char *cursor_name),
          (terminal, tag, cursor_name))

/* GTK3 专属（GTK4 上 dlsym 失败，安全降级） */
WRAP_RET(char *, vte_terminal_hyperlink_check_event,
         (VteTerminal *terminal, GdkEvent *event),
         (terminal, event), NULL)
WRAP_RET(char *, vte_terminal_match_check_event,
         (VteTerminal *terminal, GdkEvent *event, int *tag),
         (terminal, event, tag), NULL)
WRAP_RET(int, vte_terminal_match_add_gregex,
         (VteTerminal *terminal, GRegex *gregex, int gflags),
         (terminal, gregex, gflags), -1)
WRAP_RET(gboolean, vte_terminal_event_check_regex_simple,
         (VteTerminal *terminal, GdkEvent *event,
          VteRegex **regexes, gsize n_regexes,
          guint32 match_flags, char **matches),
         (terminal, event, regexes, n_regexes, match_flags, matches), 0)

/* GTK4 专属（GTK3 上 dlsym 失败，安全降级） */
WRAP_RET(char *, vte_terminal_check_hyperlink_at,
         (VteTerminal *terminal, double x, double y),
         (terminal, x, y), NULL)
WRAP_RET(char *, vte_terminal_check_match_at,
         (VteTerminal *terminal, double x, double y, int *tag),
         (terminal, x, y, tag), NULL)
WRAP_RET(gboolean, vte_terminal_check_regex_simple_at,
         (VteTerminal *terminal, double x, double y,
          VteRegex **regexes, gsize n_regexes,
          guint32 match_flags, char **matches),
         (terminal, x, y, regexes, n_regexes, match_flags, matches), 0)

/* ============================================================
 * PTY
 * ============================================================ */
WRAP_RET(GType, vte_pty_get_type, (void), (), 0)
WRAP_RET(VtePty *, vte_pty_new_sync,
         (int flags, GCancellable *cancellable, GError **error),
         (flags, cancellable, error), NULL)
WRAP_RET(VtePty *, vte_pty_new_foreign_sync,
         (int fd, GCancellable *cancellable, GError **error),
         (fd, cancellable, error), NULL)
WRAP_RET(int, vte_pty_get_fd, (VtePty *pty), (pty), -1)
WRAP_VOID(vte_pty_child_setup, (VtePty *pty), (pty))
WRAP_RET(gboolean, vte_pty_get_size,
         (VtePty *pty, int *rows, int *columns, GError **error),
         (pty, rows, columns, error), 0)
WRAP_RET(gboolean, vte_pty_set_size,
         (VtePty *pty, int rows, int columns, GError **error),
         (pty, rows, columns, error), 0)
WRAP_RET(gboolean, vte_pty_set_utf8,
         (VtePty *pty, gboolean utf8, GError **error),
         (pty, utf8, error), 0)
WRAP_VOID(vte_pty_close, (VtePty *pty), (pty))

/* ============================================================
 * Regex
 * ============================================================ */
WRAP_RET(GType, vte_regex_get_type, (void), (), 0)
WRAP_RET(VteRegex *, vte_regex_new_for_match,
         (const char *pattern, gssize pattern_length,
          guint32 flags, GError **error),
         (pattern, pattern_length, flags, error), NULL)
WRAP_RET(VteRegex *, vte_regex_new_for_search,
         (const char *pattern, gssize pattern_length,
          guint32 flags, GError **error),
         (pattern, pattern_length, flags, error), NULL)
WRAP_RET(VteRegex *, vte_regex_ref, (VteRegex *regex), (regex), NULL)
WRAP_RET(VteRegex *, vte_regex_unref, (VteRegex *regex), (regex), NULL)
WRAP_RET(gboolean, vte_regex_jit,
         (VteRegex *regex, guint32 flags, GError **error),
         (regex, flags, error), 0)
WRAP_RET(char *, vte_regex_substitute,
         (VteRegex *regex, const char *subject, const char *replacement,
          guint32 flags, GError **error),
         (regex, subject, replacement, flags, error), NULL)
