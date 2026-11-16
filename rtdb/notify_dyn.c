/* ============================================================================
 * notify_dyn.c — libnotify 动态加载封装层
 *
 * 设计约束（与 mpv_dyn.c / vte_dyn.c 等一致）：
 *   1. 链接期绝不出现 -lnotify，也不需要 -lglib/-lgobject。
+ *   2. 无自定义 .h 文件，无独立 wrapper .c。
 *   3. 每个封装函数首次调用时才 dlsym 自己的符号并缓存。
 *   4. 封装函数名与原版 libnotify C API 一致；缺失的可选符号安全降级。
 *   5. 编译期不需要 <libnotify/notify.h>；所有类型不透明。
 *   6. 跨平台：Unix/macOS 用 dlopen/dlsym；Windows 用 LoadLibrary。
 *   7. 搜索顺序：显式路径 > NOTIFY_DYN_LIB > 系统搜索路径 > 默认库名
 *      > Nix > 平台默认路径。
 *
 * 依赖说明（dlopen(3) 已核对）：
 *   dlopen("libnotify.so.4") 会由动态链接器自动递归加载其全部
 *   DT_NEEDED 依赖（libgobject-2.0、libglib-2.0、libgio-2.0、
 *   libgdk_pixbuf-2.0 等），应用无需链接任何 GLib 库。
 *   GObject 类型系统在首次 g_type 调用时自动初始化，无需预处理。
 *
 * 版本：
 *   API 自 0.7.0 起稳定；0.8.0 新增
 *   notify_notification_get_activation_token() /
+ *   get_activation_app_launch_context()（0.7 旧库 dlsym 失败，
 *   安全降级返回 NULL）。
 *   libnotify 没有运行时版本函数；notify_dyn_minor() 按 0.8 专属
 *   符号存在与否返回 API 级别（7 或 8）。
 *
 * 运行环境（ArchWiki/libnotify 文档已核对）：
 *   - notify_init() 连接 session D-Bus（g_bus_get_sync），
 *     无通知服务也能成功。
 *   - notify_notification_show() 是同步 D-Bus 调用，简单显示
 *     不需要 GMainLoop（notify-send 即如此）。
 *   - 但 show 需要 org.freedesktop.Notifications 守护进程
 *     （dunst / xfce4-notifyd / notification-daemon 等），
 *     否则返回 FALSE + GError。
 *   - add_action() 的动作回调只有 GLib 主循环在跑时才会触发；
 *     无主循环的应用不应依赖动作回调。
 *   - 0.8 在 snap/flatpak 沙箱环境自动走 Desktop Portal，
 *     API 不变。
 *
 * 内存/所有权：
 *   - NotifyNotification 用 g_object_unref 释放。
 *   - notify_get_server_caps 返回的 GList 的 data 用 g_free，
 *     列表用 g_list_free_full(list, g_free)；在 notify_uninit
 *     之前有效。
 *   - notify_get_server_info 的 out 参数用 g_free。
 *   - 成功加载后不要 dlclose/FreeLibrary（D-Bus 连接/GType 全局
 *     状态）；cleanup 只在加载失败后复位重试。
 *   - closed 信号不封装，应用侧 g_signal_connect。
 *
 * 编译：gcc -std=c99 -Wall -Wextra -Wpedantic -fPIC -c notify_dyn.c
 *       Linux 需要 -ldl（glibc 2.34+ 可省略）。
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================
 * 不透明类型 / GLib 标量（ABI 与 glib 一致）
 * ============================================================ */
typedef struct _NotifyNotification NotifyNotification;
typedef struct _GdkPixbuf GdkPixbuf;
typedef struct _GList GList;
typedef struct _GVariant GVariant;
typedef struct _GError GError;

typedef int gboolean;
typedef int gint;
typedef unsigned int guint;
typedef char gchar;
typedef unsigned char guchar;
typedef size_t gsize;
typedef void *gpointer;

typedef void (*GDestroyNotify)(void *data);
/* void (NotifyNotification *notification, char *action, gpointer user_data) */
typedef void (*NotifyActionCallback)(NotifyNotification *notification,
                                     char *action,
                                     void *user_data);

/* 常量（与原版枚举/宏一致，C ABI 为 int） */
#define NOTIFY_DYN_EXPIRES_DEFAULT (-1)
#define NOTIFY_DYN_EXPIRES_NEVER   (0)

#define NOTIFY_DYN_URGENCY_LOW      0
#define NOTIFY_DYN_URGENCY_NORMAL   1
#define NOTIFY_DYN_URGENCY_CRITICAL 2

#define NOTIFY_DYN_CLOSED_REASON_UNSET       (-1)
#define NOTIFY_DYN_CLOSED_REASON_EXPIRED     1
#define NOTIFY_DYN_CLOSED_REASON_API_REQUEST 2
#define NOTIFY_DYN_CLOSED_REASON_CLOSED      3

/* ============================================================
 * 平台适配层
 * ============================================================ */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE lib_handle_t;
  static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
  static void *get_sym(lib_handle_t h, const char *name) {
      return (void *)GetProcAddress(h, name);
  }
  static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *lib_error(void) { return "LoadLibrary failed"; }
  #define PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *lib_handle_t;
  static lib_handle_t load_lib(const char *path) {
      /* 纯 C 库，无 VTE 的 C++ 初始化问题；LOCAL 即可 */
      return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
  }
  static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
  static const char *lib_error(void) { return dlerror(); }
  #define PATH_SEP ':'
#endif

/* ============================================================
 * 全局状态
 * ============================================================ */
static lib_handle_t notify_handle_lib = NULL;
static int init_state = 0;          /* 0=未初始化, 1=已加载, -1=失败 */
static int notify_api_level = 0;    /* 7 或 8，加载后探测 */
static char notify_version_str[64];

#define MAX_CANDIDATES 64
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

static int push_dir_names(const char **candidates,
                          char bufs[][PATH_BUF_LEN], int n,
                          const char *dir) {
#if defined(_WIN32)
    n = push_candidate_fmt(candidates, bufs, n, "%s/libnotify-4.dll", dir);
#elif defined(__APPLE__)
    n = push_candidate_fmt(candidates, bufs, n, "%s/libnotify.dylib", dir);
#else
    n = push_candidate_fmt(candidates, bufs, n, "%s/libnotify.so.4", dir);
    n = push_candidate_fmt(candidates, bufs, n, "%s/libnotify.so", dir);
#endif
    return n;
}

static int push_bare_names(const char **candidates,
                           char bufs[][PATH_BUF_LEN], int n) {
#if defined(_WIN32)
    n = push_candidate(candidates, bufs, n, "libnotify-4.dll");
#elif defined(__APPLE__)
    n = push_candidate(candidates, bufs, n, "libnotify.dylib");
#else
    n = push_candidate(candidates, bufs, n, "libnotify.so.4");
    n = push_candidate(candidates, bufs, n, "libnotify.so");
#endif
    return n;
}

/* ============================================================
 * 加载与锚点符号验证
 * ============================================================ */
static int try_candidate(const char *path) {
    lib_handle_t h;
    void *sym_init;
    void *sym_get_type;
    void *sym_act_token;

    h = load_lib(path);
    if (!h) return -1;

    sym_init = get_sym(h, "notify_init");
    sym_get_type = get_sym(h, "notify_notification_get_type");
    if (!sym_init || !sym_get_type) {
        free_lib(h);
        return -1;
    }

    notify_handle_lib = h;
    sym_act_token = get_sym(h, "notify_notification_get_activation_token");
    notify_api_level = sym_act_token ? 8 : 7;
    snprintf(notify_version_str, sizeof(notify_version_str),
             "API level %d (0.%d.x)", notify_api_level, notify_api_level);
    fprintf(stderr, "notify_dyn: loaded %s (libnotify %s)\n",
            path, notify_version_str);
    return 0;
}

static int do_load(const char *library_path) {
    static char bufs[MAX_CANDIDATES][PATH_BUF_LEN];
    const char *candidates[MAX_CANDIDATES];
    int n = 0;
    int i;
    const char *env_path;

    if (notify_handle_lib) return 0;

    /* 1. 显式路径 */
    if (library_path && *library_path)
        n = push_candidate(candidates, bufs, n, library_path);

    /* 2. 环境变量 */
    {
        const char *explicit_lib = getenv("NOTIFY_DYN_LIB");
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
                    n = push_dir_names(candidates, bufs, n, p);
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 4. 裸库名 */
    n = push_bare_names(candidates, bufs, n);

    /* 5. Nix */
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        char dir[PATH_BUF_LEN];
        if (home && *home) {
            snprintf(dir, sizeof(dir), "%s/.nix-profile/lib", home);
            n = push_dir_names(candidates, bufs, n, dir);
        }
        n = push_dir_names(candidates, bufs, n,
                           "/nix/var/nix/profiles/default/lib");
    }
#endif

    /* 6. 平台默认路径 */
#ifdef __APPLE__
    n = push_dir_names(candidates, bufs, n, "/opt/local/lib");
    n = push_dir_names(candidates, bufs, n, "/usr/local/lib");
    n = push_dir_names(candidates, bufs, n, "/opt/homebrew/lib");
#elif defined(__linux__) || defined(__ANDROID__)
    n = push_dir_names(candidates, bufs, n, "/usr/local/lib");
    n = push_dir_names(candidates, bufs, n, "/usr/lib");
    n = push_dir_names(candidates, bufs, n, "/usr/lib/x86_64-linux-gnu");
    n = push_dir_names(candidates, bufs, n, "/usr/lib/aarch64-linux-gnu");
    {
        const char *prefix = getenv("PREFIX"); /* Termux */
        if (prefix && *prefix) {
            n = push_candidate_fmt(candidates, bufs, n,
                                   "%s/lib/libnotify.so.4", prefix);
        }
    }
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    n = push_dir_names(candidates, bufs, n, "/usr/local/lib");
    n = push_dir_names(candidates, bufs, n, "/usr/lib");
#endif
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        if (try_candidate(candidates[i]) == 0)
            return 0;
    }

    {
        const char *err = lib_error();
        fprintf(stderr, "notify_dyn: cannot load libnotify.so.4 (%s)\n",
                err ? err : "unknown error");
    }
    return -1;
}

/* ============================================================
 * 控制函数
 * ============================================================ */
int notify_dyn_libok(void) {
    if (init_state == 0)
        init_state = (do_load(NULL) == 0) ? 1 : -1;
    return (init_state == 1 && notify_handle_lib != NULL) ? 1 : 0;
}

int notify_dyn_init(const char *library_path) {
    if (init_state == 1) return 0;
    if (init_state == -1) return -1;
    if (do_load(library_path) == 0) {
        init_state = 1;
        return 0;
    }
    init_state = -1;
    return -1;
}

/* 成功加载后不卸载库（D-Bus 连接 / GType 全局状态）。
 * 只有加载失败状态才复位，允许修正路径后重试。 */
void notify_dyn_cleanup(void) {
    if (init_state == -1)
        init_state = 0;
}

const char *notify_dyn_version(void) {
    if (init_state == 0) notify_dyn_libok();
    return notify_version_str;
}

int notify_dyn_major(void) {
    return 0;
}

int notify_dyn_minor(void) {
    if (init_state == 0) notify_dyn_libok();
    return notify_api_level;   /* 7 或 8 */
}

int notify_dyn_micro(void) {
    return 0; /* libnotify 无运行时 micro 探测途径 */
}

int notify_dyn_api_level(void) {
    if (init_state == 0) notify_dyn_libok();
    return notify_api_level;
}

int notify_dyn_has_symbol(const char *name) {
    if (!name || !*name) return 0;
    if (!notify_dyn_libok()) return 0;
    return get_sym(notify_handle_lib, name) != NULL;
}

/* ============================================================
 * 按需解析宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                \
ret name params {                                                  \
    static ret (*fn) params = NULL;                                \
    if (!fn) {                                                     \
        if (!notify_dyn_libok()) return fallback;                  \
        *(void **)(&fn) = get_sym(notify_handle_lib, #name);       \
        if (!fn) {                                                 \
            fprintf(stderr, "notify_dyn: dlsym failed: %s\n",      \
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
        if (!notify_dyn_libok()) return;                           \
        *(void **)(&fn) = get_sym(notify_handle_lib, #name);       \
        if (!fn) {                                                 \
            fprintf(stderr, "notify_dyn: dlsym failed: %s\n",      \
                    #name);                                        \
            return;                                                \
        }                                                          \
    }                                                              \
    fn args;                                                       \
}

/* ============================================================
 * 全局 / 服务器信息
 * ============================================================ */
WRAP_RET(gboolean, notify_init, (const char *app_name), (app_name), 0)
WRAP_VOID(notify_uninit, (void), ())
WRAP_RET(gboolean, notify_is_initted, (void), (), 0)
WRAP_RET(const char *, notify_get_app_name, (void), (), NULL)
WRAP_RET(const char *, notify_get_app_icon, (void), (), NULL)
WRAP_RET(GList *, notify_get_server_caps, (void), (), NULL)
WRAP_RET(gboolean, notify_get_server_info,
         (char **ret_name, char **ret_vendor, char **ret_version),
         (ret_name, ret_vendor, ret_version), 0)

/* ============================================================
 * NotifyNotification：生命周期 / 显示
 * ============================================================ */
WRAP_RET(gpointer, notify_notification_get_type, (void), (), 0)
WRAP_RET(NotifyNotification *, notify_notification_new,
         (const char *summary, const char *body, const char *icon),
         (summary, body, icon), NULL)
WRAP_RET(gboolean, notify_notification_update,
         (NotifyNotification *notification, const char *summary,
          const char *body, const char *icon),
         (notification, summary, body, icon), 0)
WRAP_RET(gboolean, notify_notification_show,
         (NotifyNotification *notification, GError **error),
         (notification, error), 0)
WRAP_VOID(notify_notification_set_timeout,
          (NotifyNotification *notification, gint timeout),
          (notification, timeout))
WRAP_RET(gboolean, notify_notification_close,
         (NotifyNotification *notification, GError **error),
         (notification, error), 0)
WRAP_RET(gint, notify_notification_get_closed_reason,
         (const NotifyNotification *notification), (notification), -1)

/* ============================================================
 * 属性 / hints
 * ============================================================ */
WRAP_VOID(notify_notification_set_category,
          (NotifyNotification *notification, const char *category),
          (notification, category))
WRAP_VOID(notify_notification_set_urgency,
          (NotifyNotification *notification, gint urgency),
          (notification, urgency))
WRAP_VOID(notify_notification_set_image_from_pixbuf,
          (NotifyNotification *notification, GdkPixbuf *pixbuf),
          (notification, pixbuf))
WRAP_VOID(notify_notification_set_icon_from_pixbuf,
          (NotifyNotification *notification, GdkPixbuf *pixbuf),
          (notification, pixbuf))
WRAP_VOID(notify_notification_set_hint_int32,
          (NotifyNotification *notification, const char *key,
           gint value), (notification, key, value))
WRAP_VOID(notify_notification_set_hint_uint32,
          (NotifyNotification *notification, const char *key,
           guint value), (notification, key, value))
WRAP_VOID(notify_notification_set_hint_double,
          (NotifyNotification *notification, const char *key,
           double value), (notification, key, value))
WRAP_VOID(notify_notification_set_hint_string,
          (NotifyNotification *notification, const char *key,
           const char *value), (notification, key, value))
WRAP_VOID(notify_notification_set_hint_byte,
          (NotifyNotification *notification, const char *key,
           gchar value), (notification, key, value))
WRAP_VOID(notify_notification_set_hint_byte_array,
          (NotifyNotification *notification, const char *key,
           const guchar *value, gsize len),
          (notification, key, value, len))
WRAP_VOID(notify_notification_set_hint,
          (NotifyNotification *notification, const char *key,
           GVariant *value), (notification, key, value))
WRAP_VOID(notify_notification_set_app_name,
          (NotifyNotification *notification, const char *app_name),
          (notification, app_name))
WRAP_VOID(notify_notification_set_app_icon,
          (NotifyNotification *notification, const char *app_icon),
          (notification, app_icon))
WRAP_VOID(notify_notification_clear_hints,
          (NotifyNotification *notification), (notification))

/* ============================================================
 * 动作 / 0.8 新增
 * ============================================================ */
WRAP_VOID(notify_notification_add_action,
          (NotifyNotification *notification, const char *action,
           const char *label, NotifyActionCallback callback,
           void *user_data, GDestroyNotify free_func),
          (notification, action, label, callback, user_data, free_func))
WRAP_VOID(notify_notification_clear_actions,
          (NotifyNotification *notification), (notification))
WRAP_RET(const char *, notify_notification_get_activation_token,
         (NotifyNotification *notification), (notification), NULL)
WRAP_RET(gpointer, notify_notification_get_activation_app_launch_context,
         (NotifyNotification *notification), (notification), NULL)
