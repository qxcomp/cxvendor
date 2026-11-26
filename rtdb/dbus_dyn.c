/* ============================================================================
 * dbus_dyn.c — libdbus (D-Bus 低层 C API) 动态加载封装层
 *
 * 设计约束（与 mpv_dyn.c / vte_dyn.c / notify_dyn.c 等一致）：
 *   1. 链接期绝不出现 -ldbus-1。
 *   2. 无自定义 .h 文件，无独立 wrapper .c。
 *   3. 每个封装函数首次调用时才 dlsym 自己的符号并缓存。
 *   4. 封装函数名与原版 libdbus C API 一致；缺失的可选符号安全降级。
 *   5. 编译期不需要 <dbus/dbus.h>；类型在本文件内定义（ABI 一致）。
 *   6. 跨平台：Unix/macOS 用 dlopen/dlsym；Windows 用 LoadLibrary。
 *   7. 搜索顺序：显式路径 > DBUS_DYN_LIB > 系统搜索路径 > 默认库名
 *      > Nix > 平台默认路径。
 *
 * 范围（本机 libdbus 1.16.2 已核对）：
 *   - 241 个导出 dbus_* 函数全部封装（13 个核心头文件）。
 *   - 排除 dbus-glib*.h（废弃的独立库 libdbus-glib-1.so）和
 *     dbus-python.h（Python 绑定）。
 *   - dbus_new/dbus_new0/dbus_clear_* 是宏（非导出），不封装。
 *
 * 类型 ABI（dbus-arch-deps.h / dbus-types.h 已核对）：
 *   dbus_bool_t = unsigned int（4 字节，协议固定）；dbus_int32_t = int；
 *   dbus_int16_t = short；dbus_int64_t = 64 位整型（本文件按平台取
 *   long / long long）；DBusError / DBusMessageIter / DBusSignatureIter /
 *   DBusObjectPathVTable 是公开结构体，本文件内定义一致布局，
 *   调用方在栈上分配。
 *
 * 版本：
 *   - dbus_get_version() 是运行时版本（官方文档已核对）；该函数
 *     1.10+ 才有，旧库 dlsym 失败时版本报告为 0 并 stderr 提示。
 *
 * 线程：
 *   - libdbus 1.10+ 内部默认已初始化线程；dbus_threads_init_default
 *     按需调用。dbus_threads_init 的 DBusThreadFunctions 是大公开
 *     结构体，封装签名用 void*（ABI 兼容指针直传），仅高级用途。
 *
 * 注意（官方文档已核对）：
 *   - dbus_shutdown() 只能在所有 DBusConnection/DBusServer 关闭后
 *     调用，属应用职责；本封装 cleanup 不调用它。
 *   - dbus_bus_get 返回共享连接（引用计数归库管理），不要 unref；
 *     dbus_connection_open/open_private 返回的才需要 unref。
 *   - dbus_free/dbus_malloc/dbus_realloc 与库内部内存配套使用
 *     （如 dbus_message_marshal 的返回值、dbus_get_local_machine_id）。
 *
 * 编译：gcc -std=c99 -Wall -Wextra -Wpedantic -fPIC -c dbus_dyn.c
 *       Linux 需要 -ldl（glibc 2.34+ 可省略）。
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================
 * 标量类型（与 dbus-arch-deps.h / dbus-types.h ABI 一致）
 * ============================================================ */
typedef int dbus_int32_t;
typedef unsigned int dbus_uint32_t;
typedef short dbus_int16_t;
typedef unsigned short dbus_uint16_t;
typedef dbus_uint32_t dbus_bool_t;
typedef dbus_uint32_t dbus_unichar_t;
#if defined(_WIN64) || defined(__LP64__) || defined(_LP64)
typedef long dbus_int64_t;
typedef unsigned long dbus_uint64_t;
#else
typedef long long dbus_int64_t;
typedef unsigned long long dbus_uint64_t;
#endif

typedef struct DBus8ByteStruct
{
  dbus_uint32_t first32;
  dbus_uint32_t second32;
} DBus8ByteStruct;

typedef union DBusBasicValue
{
  unsigned char bytes[8];
  dbus_int16_t i16;
  dbus_uint16_t u16;
  dbus_int32_t i32;
  dbus_uint32_t u32;
  dbus_bool_t bool_val;
  dbus_int64_t i64;
  dbus_uint64_t u64;
  DBus8ByteStruct eight;
  unsigned char byt;
  const char *str;
  void *vp;
  char byte;
  int fd;
} DBusBasicValue;

/* ============================================================
 * 对象类型（不透明）
 * ============================================================ */
typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;
typedef struct DBusPendingCall DBusPendingCall;
typedef struct DBusServer DBusServer;
typedef struct DBusWatch DBusWatch;
typedef struct DBusTimeout DBusTimeout;
typedef struct DBusPreallocatedSend DBusPreallocatedSend;
typedef struct DBusAddressEntry DBusAddressEntry;
typedef struct DBusThreadFunctions DBusThreadFunctions;

/* ============================================================
 * 公开结构体（布局与原头文件一致，调用方栈上分配）
 * ============================================================ */
typedef struct DBusError
{
  const char *name;
  const char *message;

  unsigned int dummy1 : 1;
  unsigned int dummy2 : 1;
  unsigned int dummy3 : 1;
  unsigned int dummy4 : 1;
  unsigned int dummy5 : 1;

  void *padding1;
} DBusError;

#define DBUS_DYN_ERROR_INIT { NULL, NULL, 1, 0, 0, 0, 0, NULL }

typedef struct DBusMessageIter
{
  void *dummy1;
  void *dummy2;
  dbus_uint32_t dummy3;
  int dummy4;
  int dummy5;
  int dummy6;
  int dummy7;
  int dummy8;
  int dummy9;
  int dummy10;
  int dummy11;
  int pad1;
  void *pad2;
  void *pad3;
} DBusMessageIter;

typedef struct
{
  void *dummy1;
  void *dummy2;
  dbus_uint32_t dummy8;
  int dummy12;
  int dummy17;
} DBusSignatureIter;

typedef void (*DBusObjectPathUnregisterFunction) (DBusConnection *connection,
                                                  void *user_data);
typedef int (*DBusObjectPathMessageFunction) (DBusConnection *connection,
                                              DBusMessage *message,
                                              void *user_data);

typedef struct DBusObjectPathVTable
{
  DBusObjectPathUnregisterFunction unregister_function;
  DBusObjectPathMessageFunction message_function;
  void (*dbus_internal_pad1) (void *);
  void (*dbus_internal_pad2) (void *);
  void (*dbus_internal_pad3) (void *);
  void (*dbus_internal_pad4) (void *);
} DBusObjectPathVTable;

/* ============================================================
 * 回调 typedef
 * ============================================================ */
typedef void (*DBusFreeFunction) (void *memory);
typedef dbus_bool_t (*DBusAddWatchFunction) (DBusWatch *watch, void *data);
typedef void (*DBusWatchToggledFunction) (DBusWatch *watch, void *data);
typedef void (*DBusRemoveWatchFunction) (DBusWatch *watch, void *data);
typedef dbus_bool_t (*DBusAddTimeoutFunction) (DBusTimeout *timeout, void *data);
typedef void (*DBusTimeoutToggledFunction) (DBusTimeout *timeout, void *data);
typedef void (*DBusRemoveTimeoutFunction) (DBusTimeout *timeout, void *data);
typedef void (*DBusDispatchStatusFunction) (DBusConnection *connection,
                                            unsigned int new_status,
                                            void *data);
typedef void (*DBusWakeupMainFunction) (void *data);
typedef dbus_bool_t (*DBusAllowUnixUserFunction) (DBusConnection *connection,
                                                  unsigned long uid,
                                                  void *user_data);
typedef dbus_bool_t (*DBusAllowWindowsUserFunction) (DBusConnection *connection,
                                                     const char *user_sid,
                                                     void *user_data);
typedef void (*DBusPendingCallNotifyFunction) (DBusPendingCall *pending,
                                               void *user_data);
typedef int (*DBusHandleMessageFunction) (DBusConnection *connection,
                                          DBusMessage *message,
                                          void *user_data);
typedef DBusConnection *(*DBusNewConnectionFunction) (DBusServer *server,
                                                      DBusConnection *new_connection,
                                                      void *data);

/* 枚举（int ABI） */
typedef enum { DBUS_DYN_BUS_SESSION = 0, DBUS_DYN_BUS_SYSTEM = 1, DBUS_DYN_BUS_STARTER = 2 } DBusDynBusType;
typedef enum { DBUS_DYN_HANDLER_RESULT_HANDLED = 0, DBUS_DYN_HANDLER_RESULT_NOT_YET_HANDLED = 1, DBUS_DYN_HANDLER_RESULT_NEED_MEMORY = 2 } DBusDynHandlerResult;
typedef enum { DBUS_DYN_DISPATCH_DATA_REMAINS = 0, DBUS_DYN_DISPATCH_PROCESSING = 1, DBUS_DYN_DISPATCH_COMPLETE = 2 } DBusDynDispatchStatus;
typedef enum { DBUS_DYN_WATCH_READABLE = (1 << 0), DBUS_DYN_WATCH_WRITABLE = (1 << 1), DBUS_DYN_WATCH_ERROR = (1 << 2), DBUS_DYN_WATCH_HANGUP = (1 << 3) } DBusDynWatchFlags;

/* dbus_bool_t 便捷值 */
#define DBUS_DYN_TRUE  ((dbus_bool_t)1)
#define DBUS_DYN_FALSE ((dbus_bool_t)0)

/* 常量（dbus-shared.h / dbus-protocol.h 已核对） */
#define DBUS_DYN_SERVICE_DBUS          "org.freedesktop.DBus"
#define DBUS_DYN_PATH_DBUS             "/org/freedesktop/DBus"
#define DBUS_DYN_INTERFACE_DBUS        "org.freedesktop.DBus"
#define DBUS_DYN_INTERFACE_PEER        "org.freedesktop.DBus.Peer"
#define DBUS_DYN_INTERFACE_INTROSPECTABLE "org.freedesktop.DBus.Introspectable"
#define DBUS_DYN_INTERFACE_PROPERTIES  "org.freedesktop.DBus.Properties"
#define DBUS_DYN_INTERFACE_LOCAL       "org.freedesktop.DBus.Local"

#define DBUS_DYN_MESSAGE_TYPE_INVALID       0
#define DBUS_DYN_MESSAGE_TYPE_METHOD_CALL   1
#define DBUS_DYN_MESSAGE_TYPE_METHOD_RETURN 2
#define DBUS_DYN_MESSAGE_TYPE_ERROR         3
#define DBUS_DYN_MESSAGE_TYPE_SIGNAL        4

#define DBUS_DYN_NAME_FLAG_ALLOW_REPLACEMENT 0x1
#define DBUS_DYN_NAME_FLAG_REPLACE_EXISTING  0x2
#define DBUS_DYN_NAME_FLAG_DO_NOT_QUEUE      0x4

#define DBUS_DYN_REQUEST_NAME_REPLY_PRIMARY_OWNER 1
#define DBUS_DYN_REQUEST_NAME_REPLY_IN_QUEUE      2
#define DBUS_DYN_REQUEST_NAME_REPLY_EXISTS        3
#define DBUS_DYN_REQUEST_NAME_REPLY_ALREADY_OWNER 4

#define DBUS_DYN_RELEASE_NAME_REPLY_RELEASED    1
#define DBUS_DYN_RELEASE_NAME_REPLY_NON_EXISTENT 2
#define DBUS_DYN_RELEASE_NAME_REPLY_NOT_OWNER   3

#define DBUS_DYN_START_SERVICE_SUCCESS            1
#define DBUS_DYN_START_SERVICE_REPLY              2

/* 类型码（char 字面量，int ABI） */
#define DBUS_DYN_TYPE_INVALID       ((int) '\0')
#define DBUS_DYN_TYPE_BYTE          ((int) 'y')
#define DBUS_DYN_TYPE_BOOLEAN       ((int) 'b')
#define DBUS_DYN_TYPE_INT16         ((int) 'n')
#define DBUS_DYN_TYPE_UINT16        ((int) 'q')
#define DBUS_DYN_TYPE_INT32         ((int) 'i')
#define DBUS_DYN_TYPE_UINT32        ((int) 'u')
#define DBUS_DYN_TYPE_INT64         ((int) 'x')
#define DBUS_DYN_TYPE_UINT64        ((int) 't')
#define DBUS_DYN_TYPE_DOUBLE        ((int) 'd')
#define DBUS_DYN_TYPE_STRING        ((int) 's')
#define DBUS_DYN_TYPE_OBJECT_PATH   ((int) 'o')
#define DBUS_DYN_TYPE_SIGNATURE     ((int) 'g')
#define DBUS_DYN_TYPE_UNIX_FD       ((int) 'h')
#define DBUS_DYN_TYPE_ARRAY         ((int) 'a')
#define DBUS_DYN_TYPE_VARIANT       ((int) 'v')
#define DBUS_DYN_TYPE_STRUCT        ((int) 'r')
#define DBUS_DYN_TYPE_DICT_ENTRY    ((int) 'e')

#define DBUS_DYN_STRUCT_FIELD 0
#define DBUS_DYN_STRUCT_END   1

/* 错误名字符串（dbus-protocol.h 全部 48 个） */
#define DBUS_DYN_ERROR_FAILED                        "org.freedesktop.DBus.Error.Failed"
#define DBUS_DYN_ERROR_NO_MEMORY                     "org.freedesktop.DBus.Error.NoMemory"
#define DBUS_DYN_ERROR_SERVICE_UNKNOWN               "org.freedesktop.DBus.Error.ServiceUnknown"
#define DBUS_DYN_ERROR_NAME_HAS_NO_OWNER             "org.freedesktop.DBus.Error.NameHasNoOwner"
#define DBUS_DYN_ERROR_NO_REPLY                      "org.freedesktop.DBus.Error.NoReply"
#define DBUS_DYN_ERROR_IO_ERROR                      "org.freedesktop.DBus.Error.IOError"
#define DBUS_DYN_ERROR_BAD_ADDRESS                   "org.freedesktop.DBus.Error.BadAddress"
#define DBUS_DYN_ERROR_NOT_SUPPORTED                 "org.freedesktop.DBus.Error.NotSupported"
#define DBUS_DYN_ERROR_LIMITS_EXCEEDED               "org.freedesktop.DBus.Error.LimitsExceeded"
#define DBUS_DYN_ERROR_ACCESS_DENIED                 "org.freedesktop.DBus.Error.AccessDenied"
#define DBUS_DYN_ERROR_AUTH_FAILED                   "org.freedesktop.DBus.Error.AuthFailed"
#define DBUS_DYN_ERROR_NO_SERVER                     "org.freedesktop.DBus.Error.NoServer"
#define DBUS_DYN_ERROR_TIMEOUT                       "org.freedesktop.DBus.Error.Timeout"
#define DBUS_DYN_ERROR_NO_NETWORK                    "org.freedesktop.DBus.Error.NoNetwork"
#define DBUS_DYN_ERROR_ADDRESS_IN_USE                "org.freedesktop.DBus.Error.AddressInUse"
#define DBUS_DYN_ERROR_DISCONNECTED                  "org.freedesktop.DBus.Error.Disconnected"
#define DBUS_DYN_ERROR_INVALID_ARGS                  "org.freedesktop.DBus.Error.InvalidArgs"
#define DBUS_DYN_ERROR_FILE_NOT_FOUND                "org.freedesktop.DBus.Error.FileNotFound"
#define DBUS_DYN_ERROR_FILE_EXISTS                   "org.freedesktop.DBus.Error.FileExists"
#define DBUS_DYN_ERROR_UNKNOWN_METHOD                "org.freedesktop.DBus.Error.UnknownMethod"
#define DBUS_DYN_ERROR_UNKNOWN_OBJECT                "org.freedesktop.DBus.Error.UnknownObject"
#define DBUS_DYN_ERROR_UNKNOWN_INTERFACE             "org.freedesktop.DBus.Error.UnknownInterface"
#define DBUS_DYN_ERROR_UNKNOWN_PROPERTY              "org.freedesktop.DBus.Error.UnknownProperty"
#define DBUS_DYN_ERROR_PROPERTY_READ_ONLY            "org.freedesktop.DBus.Error.PropertyReadOnly"
#define DBUS_DYN_ERROR_TIMED_OUT                     "org.freedesktop.DBus.Error.TimedOut"
#define DBUS_DYN_ERROR_MATCH_RULE_NOT_FOUND          "org.freedesktop.DBus.Error.MatchRuleNotFound"
#define DBUS_DYN_ERROR_MATCH_RULE_INVALID            "org.freedesktop.DBus.Error.MatchRuleInvalid"
#define DBUS_DYN_ERROR_SPAWN_EXEC_FAILED             "org.freedesktop.DBus.Error.Spawn.ExecFailed"
#define DBUS_DYN_ERROR_SPAWN_FORK_FAILED             "org.freedesktop.DBus.Error.Spawn.ForkFailed"
#define DBUS_DYN_ERROR_SPAWN_CHILD_EXITED            "org.freedesktop.DBus.Error.Spawn.ChildExited"
#define DBUS_DYN_ERROR_SPAWN_CHILD_SIGNALED          "org.freedesktop.DBus.Error.Spawn.ChildSignaled"
#define DBUS_DYN_ERROR_SPAWN_FAILED                  "org.freedesktop.DBus.Error.Spawn.Failed"
#define DBUS_DYN_ERROR_SPAWN_SETUP_FAILED            "org.freedesktop.DBus.Error.Spawn.FailedToSetup"
#define DBUS_DYN_ERROR_SPAWN_CONFIG_INVALID          "org.freedesktop.DBus.Error.Spawn.ConfigInvalid"
#define DBUS_DYN_ERROR_SPAWN_SERVICE_INVALID         "org.freedesktop.DBus.Error.Spawn.ServiceNotValid"
#define DBUS_DYN_ERROR_SPAWN_SERVICE_NOT_FOUND       "org.freedesktop.DBus.Error.Spawn.ServiceNotFound"
#define DBUS_DYN_ERROR_SPAWN_PERMISSIONS_INVALID     "org.freedesktop.DBus.Error.Spawn.PermissionsInvalid"
#define DBUS_DYN_ERROR_SPAWN_FILE_INVALID            "org.freedesktop.DBus.Error.Spawn.FileInvalid"
#define DBUS_DYN_ERROR_SPAWN_NO_MEMORY               "org.freedesktop.DBus.Error.Spawn.NoMemory"
#define DBUS_DYN_ERROR_UNIX_PROCESS_ID_UNKNOWN       "org.freedesktop.DBus.Error.UnixProcessIdUnknown"
#define DBUS_DYN_ERROR_INVALID_SIGNATURE             "org.freedesktop.DBus.Error.InvalidSignature"
#define DBUS_DYN_ERROR_INVALID_FILE_CONTENT          "org.freedesktop.DBus.Error.InvalidFileContent"
#define DBUS_DYN_ERROR_SELINUX_SECURITY_CONTEXT_UNKNOWN "org.freedesktop.DBus.Error.SELinuxSecurityContextUnknown"
#define DBUS_DYN_ERROR_ADT_AUDIT_DATA_UNKNOWN        "org.freedesktop.DBus.Error.AdtAuditDataUnknown"
#define DBUS_DYN_ERROR_OBJECT_PATH_IN_USE            "org.freedesktop.DBus.Error.ObjectPathInUse"
#define DBUS_DYN_ERROR_INCONSISTENT_MESSAGE          "org.freedesktop.DBus.Error.InconsistentMessage"
#define DBUS_DYN_ERROR_INTERACTIVE_AUTHORIZATION_REQUIRED "org.freedesktop.DBus.Error.InteractiveAuthorizationRequired"
#define DBUS_DYN_ERROR_NOT_CONTAINER                 "org.freedesktop.DBus.Error.NotContainer"

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
      /* 纯 C 库，无 C++ 初始化问题 */
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
static lib_handle_t dbus_handle_lib = NULL;
static int init_state = 0;          /* 0=未初始化, 1=已加载, -1=失败 */
static int dbus_lib_major = 0;
static int dbus_lib_minor = 0;
static int dbus_lib_micro = 0;
static char dbus_version_str[64];

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
    n = push_candidate_fmt(candidates, bufs, n, "%s/dbus-1.dll", dir);
#elif defined(__APPLE__)
    n = push_candidate_fmt(candidates, bufs, n, "%s/libdbus-1.dylib", dir);
#else
    n = push_candidate_fmt(candidates, bufs, n, "%s/libdbus-1.so.3", dir);
    n = push_candidate_fmt(candidates, bufs, n, "%s/libdbus-1.so", dir);
#endif
    return n;
}

static int push_bare_names(const char **candidates,
                           char bufs[][PATH_BUF_LEN], int n) {
#if defined(_WIN32)
    n = push_candidate(candidates, bufs, n, "dbus-1.dll");
#elif defined(__APPLE__)
    n = push_candidate(candidates, bufs, n, "libdbus-1.dylib");
#else
    n = push_candidate(candidates, bufs, n, "libdbus-1.so.3");
    n = push_candidate(candidates, bufs, n, "libdbus-1.so");
#endif
    return n;
}

/* ============================================================
 * 加载与锚点符号验证
 * ============================================================ */
static int try_candidate(const char *path, int idx) {
    lib_handle_t h;
    void *sym_err;
    void *sym_msg;
    void *sym_ver;

    h = load_lib(path);
    if (!h) return -1;

    sym_err = get_sym(h, "dbus_error_init");
    sym_msg = get_sym(h, "dbus_message_new_method_call");
    if (!sym_err || !sym_msg) {
        free_lib(h);
        return -1;
    }

    dbus_handle_lib = h;
    sym_ver = get_sym(h, "dbus_get_version");
    if (sym_ver) {
        void (*fn_ver)(int *, int *, int *) = NULL;
        *(void **)(&fn_ver) = sym_ver;
        fn_ver(&dbus_lib_major, &dbus_lib_minor, &dbus_lib_micro);
        snprintf(dbus_version_str, sizeof(dbus_version_str), "%d.%d.%d",
                 dbus_lib_major, dbus_lib_minor, dbus_lib_micro);
    } else {
        fprintf(stderr, "dbus_dyn: dbus_get_version not found "
                        "(libdbus < 1.10?), version reported as 0\n");
    }
    fprintf(stderr, "dbus_dyn: loaded %d %s (libdbus %s)\n", idx, path, dbus_version_str);
    return 0;
}

static int do_load(const char *library_path) {
    static char bufs[MAX_CANDIDATES][PATH_BUF_LEN];
    const char *candidates[MAX_CANDIDATES];
    int n = 0;
    int i;
    const char *env_path;

    if (dbus_handle_lib) return 0;

    /* 1. 显式路径 */
    if (library_path && *library_path)
        n = push_candidate(candidates, bufs, n, library_path);

    /* 2. 环境变量 */
    {
        const char *explicit_lib = getenv("DBUS_DYN_LIB");
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
                                   "%s/lib/libdbus-1.so.3", prefix);
        }
    }
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    n = push_dir_names(candidates, bufs, n, "/usr/local/lib");
    n = push_dir_names(candidates, bufs, n, "/usr/lib");
#endif
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        if (try_candidate(candidates[i], i) == 0)
            return 0;
    }

    {
        const char *err = lib_error();
        fprintf(stderr, "dbus_dyn: cannot load libdbus-1 (%s)\n",
                err ? err : "unknown error");
    }
    return -1;
}

/* ============================================================
 * 控制函数
 * ============================================================ */
int dbus_libok(void) {
    if (init_state == 0)
        init_state = (do_load(NULL) == 0) ? 1 : -1;
    return (init_state == 1 && dbus_handle_lib != NULL) ? 1 : 0;
}

int dbus_dyn_init(const char *library_path) {
    if (init_state == 1) return 0;
    if (init_state == -1) return -1;
    if (do_load(library_path) == 0) {
        init_state = 1;
        return 0;
    }
    init_state = -1;
    return -1;
}

/* 成功加载后不卸载库（连接/线程全局状态）。
 * 只有加载失败状态才复位，允许修正路径后重试。
 * 注意：dbus_shutdown() 由应用在关闭所有连接后自行调用。 */
void dbus_dyn_cleanup(void) {
    if (init_state == -1)
        init_state = 0;
}

const char *dbus_dyn_version(void) {
    if (init_state == 0) dbus_libok();
    return dbus_version_str;
}

int dbus_dyn_major(void) {
    if (init_state == 0) dbus_libok();
    return dbus_lib_major;
}

int dbus_dyn_minor(void) {
    if (init_state == 0) dbus_libok();
    return dbus_lib_minor;
}

int dbus_dyn_micro(void) {
    if (init_state == 0) dbus_libok();
    return dbus_lib_micro;
}

int dbus_dyn_has_symbol(const char *name) {
    if (!name || !*name) return 0;
    if (!dbus_libok()) return 0;
    return get_sym(dbus_handle_lib, name) != NULL;
}

/* ============================================================
 * 按需解析宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                \
ret name params {                                                  \
    static ret (*fn) params = NULL;                                \
    if (!fn) {                                                     \
        if (!dbus_libok()) return fallback;                        \
        *(void **)(&fn) = get_sym(dbus_handle_lib, #name);         \
        if (!fn) {                                                 \
            fprintf(stderr, "dbus_dyn: dlsym failed: %s\n",        \
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
        if (!dbus_libok()) return;                                 \
        *(void **)(&fn) = get_sym(dbus_handle_lib, #name);         \
        if (!fn) {                                                 \
            fprintf(stderr, "dbus_dyn: dlsym failed: %s\n",        \
                    #name);                                        \
            return;                                                \
        }                                                          \
    }                                                              \
    fn args;                                                       \
}

/* ============================================================
 * Errors
 * ============================================================ */
WRAP_VOID(dbus_error_init, (DBusError *error), (error))
WRAP_VOID(dbus_error_free, (DBusError *error), (error))
WRAP_RET(dbus_bool_t, dbus_error_has_name,
         (const DBusError *error, const char *name), (error, name), 0)
WRAP_RET(dbus_bool_t, dbus_error_is_set, (const DBusError *error),
         (error), 0)
WRAP_VOID(dbus_move_error, (DBusError *src, DBusError *dest),
          (src, dest))
WRAP_VOID(dbus_set_error_const,
          (DBusError *error, const char *name, const char *message),
          (error, name, message))
WRAP_RET(dbus_bool_t, dbus_set_error_from_message,
         (DBusError *error, DBusMessage *message), (error, message), 0)

/* dbus_set_error 的 message 是 printf 格式串（可变参数），无法直传。
 * 策略：自行格式化后以 "%s" 调用原函数。 */
void dbus_set_error(DBusError *error, const char *name,
                    const char *message, ...) {
    static void (*fn)(DBusError *, const char *, const char *, ...) = NULL;
    char buf[1024];
    if (!fn) {
        if (!dbus_libok()) return;
        *(void **)(&fn) = get_sym(dbus_handle_lib, "dbus_set_error");
        if (!fn) {
            fprintf(stderr, "dbus_dyn: dlsym failed: dbus_set_error\n");
            return;
        }
    }
    if (message) {
        va_list ap;
        va_start(ap, message);
        vsnprintf(buf, sizeof(buf), message, ap);
        va_end(ap);
        fn(error, name, "%s", buf);
    } else {
        fn(error, name, NULL);
    }
}

/* ============================================================
 * Memory
 * ============================================================ */
WRAP_RET(void *, dbus_malloc, (size_t bytes), (bytes), NULL)
WRAP_RET(void *, dbus_malloc0, (size_t bytes), (bytes), NULL)
WRAP_RET(void *, dbus_realloc, (void *memory, size_t bytes),
         (memory, bytes), NULL)
WRAP_VOID(dbus_free, (void *memory), (memory))
WRAP_VOID(dbus_free_string_array, (char **str_array), (str_array))
WRAP_VOID(dbus_shutdown, (void), ())

/* ============================================================
 * Misc
 * ============================================================ */
WRAP_RET(char *, dbus_get_local_machine_id, (void), (), NULL)
WRAP_RET(char *, dbus_try_get_local_machine_id, (DBusError *error),
         (error), NULL)
WRAP_RET(dbus_bool_t, dbus_setenv,
         (const char *variable, const char *value), (variable, value), 0)

void dbus_get_version(int *major_version_p, int *minor_version_p,
                      int *micro_version_p) {
    if (init_state == 0) dbus_libok();
    if (major_version_p) *major_version_p = dbus_lib_major;
    if (minor_version_p) *minor_version_p = dbus_lib_minor;
    if (micro_version_p) *micro_version_p = dbus_lib_micro;
}

/* ============================================================
 * Address
 * ============================================================ */
WRAP_RET(dbus_bool_t, dbus_parse_address,
         (const char *address, DBusAddressEntry ***entry_result,
          int *array_len, DBusError *error),
         (address, entry_result, array_len, error), 0)
WRAP_RET(const char *, dbus_address_entry_get_value,
         (DBusAddressEntry *entry, const char *key), (entry, key), NULL)
WRAP_RET(const char *, dbus_address_entry_get_method,
         (DBusAddressEntry *entry), (entry), NULL)
WRAP_VOID(dbus_address_entries_free, (DBusAddressEntry **entries),
          (entries))
WRAP_RET(char *, dbus_address_escape_value, (const char *value),
         (value), NULL)
WRAP_RET(char *, dbus_address_unescape_value,
         (const char *value, DBusError *error), (value, error), NULL)

/* ============================================================
 * Bus
 * ============================================================ */
WRAP_RET(DBusConnection *, dbus_bus_get,
         (int type, DBusError *error), (type, error), NULL)
WRAP_RET(DBusConnection *, dbus_bus_get_private,
         (int type, DBusError *error), (type, error), NULL)
WRAP_RET(dbus_bool_t, dbus_bus_register,
         (DBusConnection *connection, DBusError *error),
         (connection, error), 0)
WRAP_RET(dbus_bool_t, dbus_bus_set_unique_name,
         (DBusConnection *connection, const char *unique_name),
         (connection, unique_name), 0)
WRAP_RET(const char *, dbus_bus_get_unique_name,
         (DBusConnection *connection), (connection), NULL)
WRAP_RET(unsigned long, dbus_bus_get_unix_user,
         (DBusConnection *connection, const char *name, DBusError *error),
         (connection, name, error), 0)
WRAP_RET(char *, dbus_bus_get_id,
         (DBusConnection *connection, DBusError *error),
         (connection, error), NULL)
WRAP_RET(int, dbus_bus_request_name,
         (DBusConnection *connection, const char *name, unsigned int flags,
          DBusError *error),
         (connection, name, flags, error), -1)
WRAP_RET(int, dbus_bus_release_name,
         (DBusConnection *connection, const char *name, DBusError *error),
         (connection, name, error), -1)
WRAP_RET(dbus_bool_t, dbus_bus_name_has_owner,
         (DBusConnection *connection, const char *name, DBusError *error),
         (connection, name, error), 0)
WRAP_RET(dbus_bool_t, dbus_bus_start_service_by_name,
         (DBusConnection *connection, const char *name, dbus_uint32_t flags,
          dbus_uint32_t *reply, DBusError *error),
         (connection, name, flags, reply, error), 0)
WRAP_VOID(dbus_bus_add_match,
          (DBusConnection *connection, const char *rule, DBusError *error),
          (connection, rule, error))
WRAP_VOID(dbus_bus_remove_match,
          (DBusConnection *connection, const char *rule, DBusError *error),
          (connection, rule, error))

/* ============================================================
 * Connection：生命周期 / 状态
 * ============================================================ */
WRAP_RET(DBusConnection *, dbus_connection_open,
         (const char *address, DBusError *error), (address, error), NULL)
WRAP_RET(DBusConnection *, dbus_connection_open_private,
         (const char *address, DBusError *error), (address, error), NULL)
WRAP_RET(DBusConnection *, dbus_connection_ref,
         (DBusConnection *connection), (connection), NULL)
WRAP_VOID(dbus_connection_unref, (DBusConnection *connection),
          (connection))
WRAP_VOID(dbus_connection_close, (DBusConnection *connection),
          (connection))
WRAP_RET(dbus_bool_t, dbus_connection_get_is_connected,
         (DBusConnection *connection), (connection), 0)
WRAP_RET(dbus_bool_t, dbus_connection_get_is_authenticated,
         (DBusConnection *connection), (connection), 0)
WRAP_RET(dbus_bool_t, dbus_connection_get_is_anonymous,
         (DBusConnection *connection), (connection), 0)
WRAP_RET(char *, dbus_connection_get_server_id,
         (DBusConnection *connection), (connection), NULL)
WRAP_RET(dbus_bool_t, dbus_connection_can_send_type,
         (DBusConnection *connection, int type), (connection, type), 0)
WRAP_VOID(dbus_connection_set_exit_on_disconnect,
          (DBusConnection *connection, dbus_bool_t exit_on_disconnect),
          (connection, exit_on_disconnect))
WRAP_VOID(dbus_connection_flush, (DBusConnection *connection),
          (connection))
WRAP_RET(dbus_bool_t, dbus_connection_read_write_dispatch,
         (DBusConnection *connection, int timeout_milliseconds),
         (connection, timeout_milliseconds), 0)
WRAP_RET(dbus_bool_t, dbus_connection_read_write,
         (DBusConnection *connection, int timeout_milliseconds),
         (connection, timeout_milliseconds), 0)

/* ============================================================
 * Connection：消息收发
 * ============================================================ */
WRAP_RET(DBusMessage *, dbus_connection_borrow_message,
         (DBusConnection *connection), (connection), NULL)
WRAP_VOID(dbus_connection_return_message,
          (DBusConnection *connection, DBusMessage *message),
          (connection, message))
WRAP_VOID(dbus_connection_steal_borrowed_message,
          (DBusConnection *connection, DBusMessage *message),
          (connection, message))
WRAP_RET(DBusMessage *, dbus_connection_pop_message,
         (DBusConnection *connection), (connection), NULL)
WRAP_RET(int, dbus_connection_get_dispatch_status,
         (DBusConnection *connection), (connection),
         DBUS_DYN_DISPATCH_COMPLETE)
WRAP_RET(int, dbus_connection_dispatch, (DBusConnection *connection),
         (connection), DBUS_DYN_DISPATCH_COMPLETE)
WRAP_RET(dbus_bool_t, dbus_connection_has_messages_to_send,
         (DBusConnection *connection), (connection), 0)
WRAP_RET(dbus_bool_t, dbus_connection_send,
         (DBusConnection *connection, DBusMessage *message,
          dbus_uint32_t *client_serial),
         (connection, message, client_serial), 0)
WRAP_RET(dbus_bool_t, dbus_connection_send_with_reply,
         (DBusConnection *connection, DBusMessage *message,
          DBusPendingCall **pending_return, int timeout_milliseconds),
         (connection, message, pending_return, timeout_milliseconds), 0)
WRAP_RET(DBusMessage *, dbus_connection_send_with_reply_and_block,
         (DBusConnection *connection, DBusMessage *message,
          int timeout_milliseconds, DBusError *error),
         (connection, message, timeout_milliseconds, error), NULL)

/* ============================================================
 * Connection：watch / timeout / 过滤 / 用户函数
 * ============================================================ */
WRAP_RET(dbus_bool_t, dbus_connection_set_watch_functions,
         (DBusConnection *connection, DBusAddWatchFunction add_function,
          DBusRemoveWatchFunction remove_function,
          DBusWatchToggledFunction toggled_function, void *data,
          DBusFreeFunction free_data_function),
         (connection, add_function, remove_function, toggled_function,
          data, free_data_function), 0)
WRAP_RET(dbus_bool_t, dbus_connection_set_timeout_functions,
         (DBusConnection *connection,
          DBusAddTimeoutFunction add_function,
          DBusRemoveTimeoutFunction remove_function,
          DBusTimeoutToggledFunction toggled_function, void *data,
          DBusFreeFunction free_data_function),
         (connection, add_function, remove_function, toggled_function,
          data, free_data_function), 0)
WRAP_VOID(dbus_connection_set_wakeup_main_function,
          (DBusConnection *connection,
           DBusWakeupMainFunction wakeup_main_function, void *data,
           DBusFreeFunction free_data_function),
          (connection, wakeup_main_function, data, free_data_function))
WRAP_VOID(dbus_connection_set_dispatch_status_function,
          (DBusConnection *connection,
           DBusDispatchStatusFunction function, void *data,
           DBusFreeFunction free_data_function),
          (connection, function, data, free_data_function))
WRAP_RET(dbus_bool_t, dbus_connection_get_unix_user,
         (DBusConnection *connection, unsigned long *uid),
         (connection, uid), 0)
WRAP_RET(dbus_bool_t, dbus_connection_get_unix_process_id,
         (DBusConnection *connection, unsigned long *pid),
         (connection, pid), 0)
WRAP_RET(dbus_bool_t, dbus_connection_get_adt_audit_session_data,
         (DBusConnection *connection, void **data,
          dbus_int32_t *data_size),
         (connection, data, data_size), 0)
WRAP_VOID(dbus_connection_set_unix_user_function,
          (DBusConnection *connection,
           DBusAllowUnixUserFunction function, void *data,
           DBusFreeFunction free_data_function),
          (connection, function, data, free_data_function))
WRAP_RET(dbus_bool_t, dbus_connection_get_windows_user,
         (DBusConnection *connection, char **windows_sid_p),
         (connection, windows_sid_p), 0)
WRAP_VOID(dbus_connection_set_windows_user_function,
          (DBusConnection *connection,
           DBusAllowWindowsUserFunction function, void *data,
           DBusFreeFunction free_data_function),
          (connection, function, data, free_data_function))
WRAP_VOID(dbus_connection_set_allow_anonymous,
          (DBusConnection *connection, dbus_bool_t value),
          (connection, value))
WRAP_VOID(dbus_connection_set_builtin_filters_enabled,
          (DBusConnection *connection, dbus_bool_t value),
          (connection, value))
WRAP_VOID(dbus_connection_set_route_peer_messages,
          (DBusConnection *connection, dbus_bool_t value),
          (connection, value))
WRAP_RET(dbus_bool_t, dbus_connection_add_filter,
         (DBusConnection *connection,
          DBusHandleMessageFunction function, void *user_data,
          DBusFreeFunction free_data_function),
         (connection, function, user_data, free_data_function), 0)
WRAP_VOID(dbus_connection_remove_filter,
          (DBusConnection *connection,
           DBusHandleMessageFunction function, void *user_data),
          (connection, function, user_data))

/* ============================================================
 * Connection：data slot / 限额 / 预分配 / 对象树 / fd
 * ============================================================ */
WRAP_RET(dbus_bool_t, dbus_connection_allocate_data_slot,
         (dbus_int32_t *slot_p), (slot_p), 0)
WRAP_VOID(dbus_connection_free_data_slot, (dbus_int32_t *slot_p),
          (slot_p))
WRAP_RET(dbus_bool_t, dbus_connection_set_data,
         (DBusConnection *connection, dbus_int32_t slot, void *data,
          DBusFreeFunction free_data_func),
         (connection, slot, data, free_data_func), 0)
WRAP_RET(void *, dbus_connection_get_data,
         (DBusConnection *connection, dbus_int32_t slot),
         (connection, slot), NULL)
WRAP_VOID(dbus_connection_set_change_sigpipe,
          (dbus_bool_t will_modify_sigpipe), (will_modify_sigpipe))
WRAP_VOID(dbus_connection_set_max_message_size,
          (DBusConnection *connection, long size), (connection, size))
WRAP_RET(long, dbus_connection_get_max_message_size,
         (DBusConnection *connection), (connection), -1)
WRAP_VOID(dbus_connection_set_max_received_size,
          (DBusConnection *connection, long size), (connection, size))
WRAP_RET(long, dbus_connection_get_max_received_size,
         (DBusConnection *connection), (connection), -1)
WRAP_VOID(dbus_connection_set_max_message_unix_fds,
          (DBusConnection *connection, long n), (connection, n))
WRAP_RET(long, dbus_connection_get_max_message_unix_fds,
         (DBusConnection *connection), (connection), -1)
WRAP_VOID(dbus_connection_set_max_received_unix_fds,
          (DBusConnection *connection, long n), (connection, n))
WRAP_RET(long, dbus_connection_get_max_received_unix_fds,
         (DBusConnection *connection), (connection), -1)
WRAP_RET(long, dbus_connection_get_outgoing_size,
         (DBusConnection *connection), (connection), -1)
WRAP_RET(long, dbus_connection_get_outgoing_unix_fds,
         (DBusConnection *connection), (connection), -1)
WRAP_RET(DBusPreallocatedSend *, dbus_connection_preallocate_send,
         (DBusConnection *connection), (connection), NULL)
WRAP_VOID(dbus_connection_free_preallocated_send,
          (DBusConnection *connection,
           DBusPreallocatedSend *preallocated),
          (connection, preallocated))
WRAP_VOID(dbus_connection_send_preallocated,
          (DBusConnection *connection,
           DBusPreallocatedSend *preallocated, DBusMessage *message,
           dbus_uint32_t *client_serial),
          (connection, preallocated, message, client_serial))
WRAP_RET(dbus_bool_t, dbus_connection_try_register_object_path,
         (DBusConnection *connection, const char *path,
          const DBusObjectPathVTable *vtable, void *user_data,
          DBusError *error),
         (connection, path, vtable, user_data, error), 0)
WRAP_RET(dbus_bool_t, dbus_connection_register_object_path,
         (DBusConnection *connection, const char *path,
          const DBusObjectPathVTable *vtable, void *user_data),
         (connection, path, vtable, user_data), 0)
WRAP_RET(dbus_bool_t, dbus_connection_try_register_fallback,
         (DBusConnection *connection, const char *path,
          const DBusObjectPathVTable *vtable, void *user_data,
          DBusError *error),
         (connection, path, vtable, user_data, error), 0)
WRAP_RET(dbus_bool_t, dbus_connection_register_fallback,
         (DBusConnection *connection, const char *path,
          const DBusObjectPathVTable *vtable, void *user_data),
         (connection, path, vtable, user_data), 0)
WRAP_RET(dbus_bool_t, dbus_connection_unregister_object_path,
         (DBusConnection *connection, const char *path),
         (connection, path), 0)
WRAP_RET(dbus_bool_t, dbus_connection_get_object_path_data,
         (DBusConnection *connection, const char *path, void **data_p),
         (connection, path, data_p), 0)
WRAP_RET(dbus_bool_t, dbus_connection_list_registered,
         (DBusConnection *connection, const char *parent_path,
          char ***child_entries),
         (connection, parent_path, child_entries), 0)
WRAP_RET(dbus_bool_t, dbus_connection_get_unix_fd,
         (DBusConnection *connection, int *fd), (connection, fd), 0)
WRAP_RET(dbus_bool_t, dbus_connection_get_socket,
         (DBusConnection *connection, int *fd), (connection, fd), 0)

/* ============================================================
 * Watch / Timeout
 * ============================================================ */
WRAP_RET(int, dbus_watch_get_fd, (DBusWatch *watch), (watch), -1)
WRAP_RET(int, dbus_watch_get_unix_fd, (DBusWatch *watch), (watch), -1)
WRAP_RET(int, dbus_watch_get_socket, (DBusWatch *watch), (watch), -1)
WRAP_RET(unsigned int, dbus_watch_get_flags, (DBusWatch *watch),
         (watch), 0)
WRAP_RET(void *, dbus_watch_get_data, (DBusWatch *watch), (watch), NULL)
WRAP_VOID(dbus_watch_set_data,
          (DBusWatch *watch, void *data,
           DBusFreeFunction free_data_function),
          (watch, data, free_data_function))
WRAP_RET(dbus_bool_t, dbus_watch_handle,
         (DBusWatch *watch, unsigned int flags), (watch, flags), 0)
WRAP_RET(dbus_bool_t, dbus_watch_get_enabled, (DBusWatch *watch),
         (watch), 0)
WRAP_RET(int, dbus_timeout_get_interval, (DBusTimeout *timeout),
         (timeout), -1)
WRAP_RET(void *, dbus_timeout_get_data, (DBusTimeout *timeout),
         (timeout), NULL)
WRAP_VOID(dbus_timeout_set_data,
          (DBusTimeout *timeout, void *data,
           DBusFreeFunction free_data_function),
          (timeout, data, free_data_function))
WRAP_RET(dbus_bool_t, dbus_timeout_handle, (DBusTimeout *timeout),
         (timeout), 0)
WRAP_RET(dbus_bool_t, dbus_timeout_get_enabled, (DBusTimeout *timeout),
         (timeout), 0)

/* ============================================================
 * Message：创建 / 生命周期
 * ============================================================ */
WRAP_RET(DBusMessage *, dbus_message_new, (int message_type),
         (message_type), NULL)
WRAP_RET(DBusMessage *, dbus_message_new_method_call,
         (const char *bus_name, const char *path, const char *iface,
          const char *method),
         (bus_name, path, iface, method), NULL)
WRAP_RET(DBusMessage *, dbus_message_new_method_return,
         (DBusMessage *method_call), (method_call), NULL)
WRAP_RET(DBusMessage *, dbus_message_new_signal,
         (const char *path, const char *iface, const char *name),
         (path, iface, name), NULL)
WRAP_RET(DBusMessage *, dbus_message_new_error,
         (DBusMessage *reply_to, const char *error_name,
          const char *error_message),
         (reply_to, error_name, error_message), NULL)
WRAP_RET(DBusMessage *, dbus_message_copy,
         (const DBusMessage *message), (message), NULL)
WRAP_RET(DBusMessage *, dbus_message_ref, (DBusMessage *message),
         (message), NULL)
WRAP_VOID(dbus_message_unref, (DBusMessage *message), (message))
WRAP_VOID(dbus_message_lock, (DBusMessage *message), (message))

/* dbus_message_new_error_printf：printf 风格可变参数无法直传。
 * 策略：自行格式化后以 "%s" 调用原函数。 */
DBusMessage *dbus_message_new_error_printf(
    DBusMessage *reply_to, const char *error_name,
    const char *error_format, ...) {
    static DBusMessage *(*fn)(DBusMessage *, const char *, const char *,
                              ...) = NULL;
    char buf[1024];
    va_list ap;
    if (!fn) {
        if (!dbus_libok()) return NULL;
        *(void **)(&fn) =
            get_sym(dbus_handle_lib, "dbus_message_new_error_printf");
        if (!fn) {
            fprintf(stderr, "dbus_dyn: dlsym failed: "
                            "dbus_message_new_error_printf\n");
            return NULL;
        }
    }
    va_start(ap, error_format);
    vsnprintf(buf, sizeof(buf), error_format, ap);
    va_end(ap);
    return fn(reply_to, error_name, "%s", buf);
}

/* ============================================================
 * Message：头字段
 * ============================================================ */
WRAP_RET(int, dbus_message_get_type, (DBusMessage *message),
         (message), DBUS_DYN_MESSAGE_TYPE_INVALID)
WRAP_RET(dbus_bool_t, dbus_message_set_path,
         (DBusMessage *message, const char *object_path),
         (message, object_path), 0)
WRAP_RET(const char *, dbus_message_get_path, (DBusMessage *message),
         (message), NULL)
WRAP_RET(dbus_bool_t, dbus_message_has_path,
         (DBusMessage *message, const char *object_path),
         (message, object_path), 0)
WRAP_RET(dbus_bool_t, dbus_message_set_interface,
         (DBusMessage *message, const char *iface), (message, iface), 0)
WRAP_RET(const char *, dbus_message_get_interface,
         (DBusMessage *message), (message), NULL)
WRAP_RET(dbus_bool_t, dbus_message_has_interface,
         (DBusMessage *message, const char *iface), (message, iface), 0)
WRAP_RET(dbus_bool_t, dbus_message_set_member,
         (DBusMessage *message, const char *member), (message, member), 0)
WRAP_RET(const char *, dbus_message_get_member, (DBusMessage *message),
         (message), NULL)
WRAP_RET(dbus_bool_t, dbus_message_has_member,
         (DBusMessage *message, const char *member), (message, member), 0)
WRAP_RET(dbus_bool_t, dbus_message_set_error_name,
         (DBusMessage *message, const char *name), (message, name), 0)
WRAP_RET(const char *, dbus_message_get_error_name,
         (DBusMessage *message), (message), NULL)
WRAP_RET(dbus_bool_t, dbus_message_set_destination,
         (DBusMessage *message, const char *destination),
         (message, destination), 0)
WRAP_RET(const char *, dbus_message_get_destination,
         (DBusMessage *message), (message), NULL)
WRAP_RET(dbus_bool_t, dbus_message_set_sender,
         (DBusMessage *message, const char *sender), (message, sender), 0)
WRAP_RET(const char *, dbus_message_get_sender, (DBusMessage *message),
         (message), NULL)
WRAP_RET(const char *, dbus_message_get_signature,
         (DBusMessage *message), (message), NULL)
WRAP_VOID(dbus_message_set_no_reply,
          (DBusMessage *message, dbus_bool_t no_reply),
          (message, no_reply))
WRAP_RET(dbus_bool_t, dbus_message_get_no_reply, (DBusMessage *message),
         (message), 0)
WRAP_RET(dbus_bool_t, dbus_message_is_method_call,
         (DBusMessage *message, const char *iface, const char *method),
         (message, iface, method), 0)
WRAP_RET(dbus_bool_t, dbus_message_is_signal,
         (DBusMessage *message, const char *iface,
          const char *signal_name),
         (message, iface, signal_name), 0)
WRAP_RET(dbus_bool_t, dbus_message_is_error,
         (DBusMessage *message, const char *error_name),
         (message, error_name), 0)
WRAP_RET(dbus_bool_t, dbus_message_has_destination,
         (DBusMessage *message, const char *bus_name),
         (message, bus_name), 0)
WRAP_RET(dbus_bool_t, dbus_message_has_sender,
         (DBusMessage *message, const char *unique_bus_name),
         (message, unique_bus_name), 0)
WRAP_RET(dbus_bool_t, dbus_message_has_signature,
         (DBusMessage *message, const char *signature),
         (message, signature), 0)
WRAP_RET(dbus_uint32_t, dbus_message_get_serial, (DBusMessage *message),
         (message), 0)
WRAP_VOID(dbus_message_set_serial,
          (DBusMessage *message, dbus_uint32_t serial),
          (message, serial))
WRAP_RET(dbus_bool_t, dbus_message_set_reply_serial,
         (DBusMessage *message, dbus_uint32_t reply_serial),
         (message, reply_serial), 0)
WRAP_RET(dbus_uint32_t, dbus_message_get_reply_serial,
         (DBusMessage *message), (message), 0)
WRAP_VOID(dbus_message_set_auto_start,
          (DBusMessage *message, dbus_bool_t auto_start),
          (message, auto_start))
WRAP_RET(dbus_bool_t, dbus_message_get_auto_start,
         (DBusMessage *message), (message), 0)
WRAP_RET(dbus_bool_t, dbus_message_get_path_decomposed,
         (DBusMessage *message, char ***path), (message, path), 0)
WRAP_RET(const char *, dbus_message_get_container_instance,
         (DBusMessage *message), (message), NULL)
WRAP_RET(dbus_bool_t, dbus_message_set_container_instance,
         (DBusMessage *message, const char *object_path),
         (message, object_path), 0)
WRAP_VOID(dbus_message_set_allow_interactive_authorization,
          (DBusMessage *message, dbus_bool_t allow), (message, allow))
WRAP_RET(dbus_bool_t, dbus_message_get_allow_interactive_authorization,
         (DBusMessage *message), (message), 0)
WRAP_RET(dbus_bool_t, dbus_message_contains_unix_fds,
         (DBusMessage *message), (message), 0)

/* ============================================================
 * Message：args / 迭代器
 * ============================================================ */
/* dbus_message_append_args / get_args 是可变参数，无法直传；
 * 通过已导出的 *_valist 变体转发 va_list。 */
dbus_bool_t dbus_message_append_args(DBusMessage *message,
                                     int first_arg_type, ...) {
    static dbus_bool_t (*fn)(DBusMessage *, int, va_list) = NULL;
    va_list ap;
    dbus_bool_t r;
    if (!fn) {
        if (!dbus_libok()) return 0;
        *(void **)(&fn) =
            get_sym(dbus_handle_lib, "dbus_message_append_args_valist");
        if (!fn) {
            fprintf(stderr, "dbus_dyn: dlsym failed: "
                            "dbus_message_append_args_valist\n");
            return 0;
        }
    }
    va_start(ap, first_arg_type);
    r = fn(message, first_arg_type, ap);
    va_end(ap);
    return r;
}

dbus_bool_t dbus_message_get_args(DBusMessage *message, DBusError *error,
                                  int first_arg_type, ...) {
    static dbus_bool_t (*fn)(DBusMessage *, DBusError *, int, va_list) =
        NULL;
    va_list ap;
    dbus_bool_t r;
    if (!fn) {
        if (!dbus_libok()) return 0;
        *(void **)(&fn) =
            get_sym(dbus_handle_lib, "dbus_message_get_args_valist");
        if (!fn) {
            fprintf(stderr, "dbus_dyn: dlsym failed: "
                            "dbus_message_get_args_valist\n");
            return 0;
        }
    }
    va_start(ap, first_arg_type);
    r = fn(message, error, first_arg_type, ap);
    va_end(ap);
    return r;
}

WRAP_RET(dbus_bool_t, dbus_message_append_args_valist,
         (DBusMessage *message, int first_arg_type, va_list var_args),
         (message, first_arg_type, var_args), 0)
WRAP_RET(dbus_bool_t, dbus_message_get_args_valist,
         (DBusMessage *message, DBusError *error, int first_arg_type,
          va_list var_args),
         (message, error, first_arg_type, var_args), 0)

WRAP_VOID(dbus_message_iter_init_closed, (DBusMessageIter *iter),
          (iter))
WRAP_RET(dbus_bool_t, dbus_message_iter_init,
         (DBusMessage *message, DBusMessageIter *iter),
         (message, iter), 0)
WRAP_RET(dbus_bool_t, dbus_message_iter_has_next,
         (DBusMessageIter *iter), (iter), 0)
WRAP_RET(dbus_bool_t, dbus_message_iter_next, (DBusMessageIter *iter),
         (iter), 0)
WRAP_RET(char *, dbus_message_iter_get_signature, (DBusMessageIter *iter),
         (iter), NULL)
WRAP_RET(int, dbus_message_iter_get_arg_type, (DBusMessageIter *iter),
         (iter), DBUS_DYN_TYPE_INVALID)
WRAP_RET(int, dbus_message_iter_get_element_type,
         (DBusMessageIter *iter), (iter), DBUS_DYN_TYPE_INVALID)
WRAP_VOID(dbus_message_iter_recurse,
          (DBusMessageIter *iter, DBusMessageIter *sub), (iter, sub))
WRAP_VOID(dbus_message_iter_get_basic,
          (DBusMessageIter *iter, void *value), (iter, value))
WRAP_RET(int, dbus_message_iter_get_element_count,
         (DBusMessageIter *iter), (iter), -1)
WRAP_RET(int, dbus_message_iter_get_array_len, (DBusMessageIter *iter),
         (iter), -1)
WRAP_VOID(dbus_message_iter_get_fixed_array,
          (DBusMessageIter *iter, void *value, int *n_elements),
          (iter, value, n_elements))
WRAP_VOID(dbus_message_iter_init_append,
          (DBusMessage *message, DBusMessageIter *iter), (message, iter))
WRAP_RET(dbus_bool_t, dbus_message_iter_append_basic,
         (DBusMessageIter *iter, int type, const void *value),
         (iter, type, value), 0)
WRAP_RET(dbus_bool_t, dbus_message_iter_append_fixed_array,
         (DBusMessageIter *iter, int element_type, const void *value,
          int n_elements),
         (iter, element_type, value, n_elements), 0)
WRAP_RET(dbus_bool_t, dbus_message_iter_open_container,
         (DBusMessageIter *iter, int type, const char *contained_signature,
          DBusMessageIter *sub),
         (iter, type, contained_signature, sub), 0)
WRAP_RET(dbus_bool_t, dbus_message_iter_close_container,
         (DBusMessageIter *iter, DBusMessageIter *sub), (iter, sub), 0)
WRAP_VOID(dbus_message_iter_abandon_container,
          (DBusMessageIter *iter, DBusMessageIter *sub), (iter, sub))
WRAP_VOID(dbus_message_iter_abandon_container_if_open,
          (DBusMessageIter *iter, DBusMessageIter *sub), (iter, sub))

/* ============================================================
 * Message：data slot / marshal / type 转换
 * ============================================================ */
WRAP_RET(dbus_bool_t, dbus_message_allocate_data_slot,
         (dbus_int32_t *slot_p), (slot_p), 0)
WRAP_VOID(dbus_message_free_data_slot, (dbus_int32_t *slot_p), (slot_p))
WRAP_RET(dbus_bool_t, dbus_message_set_data,
         (DBusMessage *message, dbus_int32_t slot, void *data,
          DBusFreeFunction free_data_func),
         (message, slot, data, free_data_func), 0)
WRAP_RET(void *, dbus_message_get_data,
         (DBusMessage *message, dbus_int32_t slot), (message, slot), NULL)
WRAP_RET(int, dbus_message_type_from_string, (const char *type_str),
         (type_str), DBUS_DYN_MESSAGE_TYPE_INVALID)
WRAP_RET(const char *, dbus_message_type_to_string, (int type),
         (type), NULL)
WRAP_RET(dbus_bool_t, dbus_message_marshal,
         (DBusMessage *msg, char **marshalled_data_p, int *len_p),
         (msg, marshalled_data_p, len_p), 0)
WRAP_RET(DBusMessage *, dbus_message_demarshal,
         (const char *str, int len, DBusError *error),
         (str, len, error), NULL)
WRAP_RET(int, dbus_message_demarshal_bytes_needed,
         (const char *str, int len), (str, len), 0)

/* ============================================================
 * PendingCall
 * ============================================================ */
WRAP_RET(DBusPendingCall *, dbus_pending_call_ref,
         (DBusPendingCall *pending), (pending), NULL)
WRAP_VOID(dbus_pending_call_unref, (DBusPendingCall *pending),
          (pending))
WRAP_RET(dbus_bool_t, dbus_pending_call_set_notify,
         (DBusPendingCall *pending,
          DBusPendingCallNotifyFunction function, void *user_data,
          DBusFreeFunction free_user_data),
         (pending, function, user_data, free_user_data), 0)
WRAP_VOID(dbus_pending_call_cancel, (DBusPendingCall *pending),
          (pending))
WRAP_RET(dbus_bool_t, dbus_pending_call_get_completed,
         (DBusPendingCall *pending), (pending), 0)
WRAP_RET(DBusMessage *, dbus_pending_call_steal_reply,
         (DBusPendingCall *pending), (pending), NULL)
WRAP_VOID(dbus_pending_call_block, (DBusPendingCall *pending), (pending))
WRAP_RET(dbus_bool_t, dbus_pending_call_allocate_data_slot,
         (dbus_int32_t *slot_p), (slot_p), 0)
WRAP_VOID(dbus_pending_call_free_data_slot, (dbus_int32_t *slot_p),
          (slot_p))
WRAP_RET(dbus_bool_t, dbus_pending_call_set_data,
         (DBusPendingCall *pending, dbus_int32_t slot, void *data,
          DBusFreeFunction free_data_func),
         (pending, slot, data, free_data_func), 0)
WRAP_RET(void *, dbus_pending_call_get_data,
         (DBusPendingCall *pending, dbus_int32_t slot),
         (pending, slot), NULL)

/* ============================================================
 * Server
 * ============================================================ */
WRAP_RET(DBusServer *, dbus_server_listen,
         (const char *address, DBusError *error), (address, error), NULL)
WRAP_RET(DBusServer *, dbus_server_ref, (DBusServer *server),
         (server), NULL)
WRAP_VOID(dbus_server_unref, (DBusServer *server), (server))
WRAP_VOID(dbus_server_disconnect, (DBusServer *server), (server))
WRAP_RET(dbus_bool_t, dbus_server_get_is_connected,
         (DBusServer *server), (server), 0)
WRAP_RET(char *, dbus_server_get_address, (DBusServer *server),
         (server), NULL)
WRAP_RET(char *, dbus_server_get_id, (DBusServer *server), (server), NULL)
WRAP_VOID(dbus_server_set_new_connection_function,
          (DBusServer *server, DBusNewConnectionFunction function,
           void *data, DBusFreeFunction free_data_function),
          (server, function, data, free_data_function))
WRAP_RET(dbus_bool_t, dbus_server_set_watch_functions,
         (DBusServer *server, DBusAddWatchFunction add_function,
          DBusRemoveWatchFunction remove_function,
          DBusWatchToggledFunction toggled_function, void *data,
          DBusFreeFunction free_data_function),
         (server, add_function, remove_function, toggled_function, data,
          free_data_function), 0)
WRAP_RET(dbus_bool_t, dbus_server_set_timeout_functions,
         (DBusServer *server, DBusAddTimeoutFunction add_function,
          DBusRemoveTimeoutFunction remove_function,
          DBusTimeoutToggledFunction toggled_function, void *data,
          DBusFreeFunction free_data_function),
         (server, add_function, remove_function, toggled_function, data,
          free_data_function), 0)
WRAP_RET(dbus_bool_t, dbus_server_set_auth_mechanisms,
         (DBusServer *server, const char **mechanisms),
         (server, mechanisms), 0)
WRAP_RET(dbus_bool_t, dbus_server_allocate_data_slot,
         (dbus_int32_t *slot_p), (slot_p), 0)
WRAP_VOID(dbus_server_free_data_slot, (dbus_int32_t *slot_p), (slot_p))
WRAP_RET(dbus_bool_t, dbus_server_set_data,
         (DBusServer *server, int slot, void *data,
          DBusFreeFunction free_data_func),
         (server, slot, data, free_data_func), 0)
WRAP_RET(void *, dbus_server_get_data, (DBusServer *server, int slot),
         (server, slot), NULL)

/* ============================================================
 * Signature
 * ============================================================ */
WRAP_VOID(dbus_signature_iter_init,
          (DBusSignatureIter *iter, const char *signature),
          (iter, signature))
WRAP_RET(int, dbus_signature_iter_get_current_type,
         (const DBusSignatureIter *iter), (iter), DBUS_DYN_TYPE_INVALID)
WRAP_RET(char *, dbus_signature_iter_get_signature,
         (const DBusSignatureIter *iter), (iter), NULL)
WRAP_RET(int, dbus_signature_iter_get_element_type,
         (const DBusSignatureIter *iter), (iter), DBUS_DYN_TYPE_INVALID)
WRAP_RET(dbus_bool_t, dbus_signature_iter_next,
         (DBusSignatureIter *iter), (iter), 0)
WRAP_VOID(dbus_signature_iter_recurse,
          (const DBusSignatureIter *iter, DBusSignatureIter *subiter),
          (iter, subiter))
WRAP_RET(dbus_bool_t, dbus_signature_validate,
         (const char *signature, DBusError *error), (signature, error), 0)
WRAP_RET(dbus_bool_t, dbus_signature_validate_single,
         (const char *signature, DBusError *error), (signature, error), 0)
WRAP_RET(dbus_bool_t, dbus_type_is_valid, (int typecode), (typecode), 0)
WRAP_RET(dbus_bool_t, dbus_type_is_basic, (int typecode), (typecode), 0)
WRAP_RET(dbus_bool_t, dbus_type_is_container, (int typecode),
         (typecode), 0)
WRAP_RET(dbus_bool_t, dbus_type_is_fixed, (int typecode), (typecode), 0)

/* ============================================================
 * Syntax 校验
 * ============================================================ */
WRAP_RET(dbus_bool_t, dbus_validate_path,
         (const char *path, DBusError *error), (path, error), 0)
WRAP_RET(dbus_bool_t, dbus_validate_interface,
         (const char *name, DBusError *error), (name, error), 0)
WRAP_RET(dbus_bool_t, dbus_validate_member,
         (const char *name, DBusError *error), (name, error), 0)
WRAP_RET(dbus_bool_t, dbus_validate_error_name,
         (const char *name, DBusError *error), (name, error), 0)
WRAP_RET(dbus_bool_t, dbus_validate_bus_name,
         (const char *name, DBusError *error), (name, error), 0)
WRAP_RET(dbus_bool_t, dbus_validate_utf8,
         (const char *alleged_utf8, DBusError *error),
         (alleged_utf8, error), 0)

/* ============================================================
 * Threads
 * ============================================================ */
/* DBusThreadFunctions 是大公开结构体，仅高级用途；
 * 签名用 void*（ABI 兼容指针直传），常规用 threads_init_default。 */
WRAP_RET(dbus_bool_t, dbus_threads_init,
         (const void *functions), (functions), 0)
WRAP_RET(dbus_bool_t, dbus_threads_init_default, (void), (), 0)
