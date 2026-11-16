/* ============================================================================
 * mysql_dyn.c — MySQL C API 动态加载封装层
 *
 * 设计目标与约束（与 sqlite3_dyn.c 一致）：
 *   1. 链接期绝不出现 -lmysqlclient。
 *   2. 无自定义 .h 文件，无独立 wrapper .c。
 *   3. 每个封装函数首次调用时才 dlsym 自己的符号并缓存。
 *   4. 封装函数名与原版 MySQL C API 一致（mysql_init 等）。
 *   5. 编译期需要 <mysql.h>（类型和常量），链接期不需要。
 *   6. 跨平台：Unix/macOS 用 dlopen/dlsym；Windows 用 LoadLibrary/GetProcAddress。
 *   7. 库搜索顺序：显式路径 > MYSQL_DYN_LIB 环境变量 > 系统搜索路径
 *      > 默认库名 > Nix 路径 > 平台默认路径。
 *   8. 线程安全：首次调用 mysql_libok() 或任一封装函数时完成加载，
 *      多线程并发首次调用需外部保证或由主线程提前触发。
 *
 * 与 SQLite 方案的关键差异：
 *   - MySQL C API 要求先调用 mysql_library_init() 才能使用其他函数。
 *     本封装在首次加载成功后自动调用一次 mysql_library_init()。
 *   - 错误信息通过 mysql_error() 获取，而非全局状态，封装层不额外缓存。
 * ========================================================================== */

#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  #define DEFAULT_LIB_NAME "libmysql.dll"
  #define PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *lib_handle_t;
  static lib_handle_t load_lib(const char *path) {
      int flags = RTLD_LAZY | RTLD_LOCAL;
  #ifdef RTLD_DEEPBIND
      flags |= RTLD_DEEPBIND;
  #endif
      return dlopen(path, flags);
  }
  static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
  static const char *lib_error(void) { return dlerror(); }
  #ifdef __APPLE__
    #define DEFAULT_LIB_NAME "libmysqlclient.dylib"
  #else
    #define DEFAULT_LIB_NAME "libmysqlclient.so"
  #endif
  #define PATH_SEP ':'
#endif

/* ============================================================
 * 全局状态
 * ============================================================ */
static lib_handle_t mysql_handle = NULL;
static int init_state = 0;   /* 0=未初始化, 1=成功, -1=失败 */

#define MAX_CANDIDATES 64
#define PATH_BUF_LEN   1024

/* ============================================================
 * 库加载：按优先级构造候选路径
 * ============================================================ */
static int do_load(const char *library_path) {
    if (mysql_handle) return 0;

    static char bufs[MAX_CANDIDATES][PATH_BUF_LEN];
    const char *candidates[MAX_CANDIDATES];
    int n = 0;

    /* 1. 显式传入的路径 */
    if (library_path && *library_path && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", library_path);
        { candidates[n] = bufs[n]; n++; }
    }

    /* 2. 项目专属环境变量 */
    const char *explicit_lib = getenv("MYSQL_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", explicit_lib);
        { candidates[n] = bufs[n]; n++; }
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
            while (p && *p && n < MAX_CANDIDATES - 2) {
                char *sep = strchr(p, PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
                    snprintf(bufs[n], PATH_BUF_LEN, "%s/%s", p, DEFAULT_LIB_NAME);
                    { candidates[n] = bufs[n]; n++; }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 4. 直接给库名 */
    candidates[n++] = DEFAULT_LIB_NAME;

    /* 5. Nix 路径 */
    const char *home = getenv("HOME");
    if (home && *home && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s/.nix-profile/lib/%s",
                 home, DEFAULT_LIB_NAME);
        { candidates[n] = bufs[n]; n++; }
    }
    if (n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "/nix/var/nix/profiles/default/lib/%s",
                 DEFAULT_LIB_NAME);
        { candidates[n] = bufs[n]; n++; }
    }

    /* 6. 平台默认路径 */
#ifdef __APPLE__
    candidates[n++] = "/opt/local/lib/libmysqlclient.dylib";
    candidates[n++] = "/usr/local/lib/libmysqlclient.dylib";
    candidates[n++] = "/opt/homebrew/lib/libmysqlclient.dylib";
#elif defined(__linux__) || defined(__ANDROID__)
    candidates[n++] = "/usr/local/lib/libmysqlclient.so";
    candidates[n++] = "/usr/lib/x86_64-linux-gnu/libmysqlclient.so";
    candidates[n++] = "/usr/lib/libmysqlclient.so";
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    candidates[n++] = "/usr/local/lib/libmysqlclient.so";
    candidates[n++] = "/usr/lib/libmysqlclient.so";
#endif
    candidates[n] = NULL;

    for (int i = 0; i < n; i++) {
        mysql_handle = load_lib(candidates[i]);
        if (mysql_handle) break;
    }
    if (!mysql_handle) {
        fprintf(stderr, "mysql_dyn: cannot load libmysqlclient (%s)\n", lib_error());
        return -1;
    }

    /* 验证基础符号 */
    if (!get_sym(mysql_handle, "mysql_get_client_info")) {
        fprintf(stderr, "mysql_dyn: loaded library does not export mysql_get_client_info\n");
        free_lib(mysql_handle);
        mysql_handle = NULL;
        return -1;
    }
    return 0;
}

/* ============================================================
 * 一次性初始化：加载库 + 调用 mysql_library_init
 * ============================================================ */
int mysql_libok(void) {
    if (init_state == 0) {
        if (do_load(NULL) != 0) {
            init_state = -1;
            return 0;
        }
        /* MySQL C API 要求先初始化库 */
        int (*fn_lib_init)(void) = NULL;
        *(void **)(&fn_lib_init) = get_sym(mysql_handle, "mysql_library_init");
        if (!fn_lib_init) {
            fprintf(stderr, "mysql_dyn: mysql_library_init not found\n");
            free_lib(mysql_handle);
            mysql_handle = NULL;
            init_state = -1;
            return 0;
        }
        if (fn_lib_init() != 0) {
            fprintf(stderr, "mysql_dyn: mysql_library_init failed\n");
            free_lib(mysql_handle);
            mysql_handle = NULL;
            init_state = -1;
            return 0;
        }
        init_state = 1;
    }
    return (init_state == 1 && mysql_handle != NULL) ? 1 : 0;
}

int mysql_dyn_init(const char *library_path) {
    if (init_state == 1) return 0;
    if (init_state == -1) return -1;
    if (do_load(library_path) != 0) {
        init_state = -1;
        return -1;
    }
    int (*fn_lib_init)(void) = NULL;
    *(void **)(&fn_lib_init) = get_sym(mysql_handle, "mysql_library_init");
    if (!fn_lib_init || fn_lib_init() != 0) {
        free_lib(mysql_handle);
        mysql_handle = NULL;
        init_state = -1;
        return -1;
    }
    init_state = 1;
    return 0;
}

void mysql_dyn_cleanup(void) {
    if (mysql_handle) {
        int (*fn_lib_end)(void) = NULL;
        *(void **)(&fn_lib_end) = get_sym(mysql_handle, "mysql_library_end");
        if (fn_lib_end) fn_lib_end();
        free_lib(mysql_handle);
        mysql_handle = NULL;
    }
    init_state = 0;
}

/* ============================================================
 * 按需解析宏
 * ============================================================ */
#define RESOLVE(var, name, fallback) do {                      \
    if (!(var)) {                                              \
        if (!mysql_libok()) return fallback;                   \
        *(void **)(&(var)) = get_sym(mysql_handle, name);      \
        if (!(var)) {                                          \
            fprintf(stderr, "dlsym failed: %s\n", name);       \
            return fallback;                                   \
        }                                                      \
    }                                                          \
} while (0)

/* ============================================================
 * 封装函数：与原版 MySQL C API 同名
 * ============================================================ */
MYSQL *mysql_init(MYSQL *mysql) {
    static MYSQL *(*fn)(MYSQL*) = NULL;
    if (!fn) {
        if (!mysql_libok()) return NULL;
        *(void **)(&fn) = get_sym(mysql_handle, "mysql_init");
        if (!fn) return NULL;
    }
    return fn(mysql);
}

MYSQL *mysql_real_connect(MYSQL *mysql, const char *host, const char *user,
                          const char *passwd, const char *db,
                          unsigned int port, const char *unix_socket,
                          unsigned long clientflag) {
    static MYSQL *(*fn)(MYSQL*, const char*, const char*, const char*,
                        const char*, unsigned int, const char*, unsigned long) = NULL;
    if (!fn) {
        if (!mysql_libok()) return NULL;
        *(void **)(&fn) = get_sym(mysql_handle, "mysql_real_connect");
        if (!fn) return NULL;
    }
    return fn(mysql, host, user, passwd, db, port, unix_socket, clientflag);
}

void mysql_close(MYSQL *mysql) {
    static void (*fn)(MYSQL*) = NULL;
    if (!fn) {
        if (!mysql_libok()) return;
        *(void **)(&fn) = get_sym(mysql_handle, "mysql_close");
        if (!fn) return;
    }
    fn(mysql);
}

int mysql_query(MYSQL *mysql, const char *query) {
    static int (*fn)(MYSQL*, const char*) = NULL;
    RESOLVE(fn, "mysql_query", -1);
    return fn(mysql, query);
}

MYSQL_RES *mysql_store_result(MYSQL *mysql) {
    static MYSQL_RES *(*fn)(MYSQL*) = NULL;
    if (!fn) {
        if (!mysql_libok()) return NULL;
        *(void **)(&fn) = get_sym(mysql_handle, "mysql_store_result");
        if (!fn) return NULL;
    }
    return fn(mysql);
}

MYSQL_ROW mysql_fetch_row(MYSQL_RES *result) {
    static MYSQL_ROW (*fn)(MYSQL_RES*) = NULL;
    if (!fn) {
        if (!mysql_libok()) return NULL;
        *(void **)(&fn) = get_sym(mysql_handle, "mysql_fetch_row");
        if (!fn) return NULL;
    }
    return fn(result);
}

void mysql_free_result(MYSQL_RES *result) {
    static void (*fn)(MYSQL_RES*) = NULL;
    if (!fn) {
        if (!mysql_libok()) return;
        *(void **)(&fn) = get_sym(mysql_handle, "mysql_free_result");
        if (!fn) return;
    }
    fn(result);
}

unsigned int mysql_num_fields(MYSQL_RES *result) {
    static unsigned int (*fn)(MYSQL_RES*) = NULL;
    RESOLVE(fn, "mysql_num_fields", 0);
    return fn(result);
}

unsigned long long mysql_num_rows(MYSQL_RES *result) {
    static unsigned long long (*fn)(MYSQL_RES*) = NULL;
    RESOLVE(fn, "mysql_num_rows", 0);
    return fn(result);
}

unsigned int mysql_errno(MYSQL *mysql) {
    static unsigned int (*fn)(MYSQL*) = NULL;
    RESOLVE(fn, "mysql_errno", 0);
    return fn(mysql);
}

const char *mysql_error(MYSQL *mysql) {
    static const char *(*fn)(MYSQL*) = NULL;
    if (!fn) {
        if (!mysql_libok()) return "mysql not initialized";
        *(void **)(&fn) = get_sym(mysql_handle, "mysql_error");
        if (!fn) return "mysql_error not resolved";
    }
    return fn(mysql);
}

const char *mysql_get_client_info(void) {
    static const char *(*fn)(void) = NULL;
    if (!fn) {
        if (!mysql_libok()) return "unknown";
        *(void **)(&fn) = get_sym(mysql_handle, "mysql_get_client_info");
        if (!fn) return "unknown";
    }
    return fn();
}

unsigned long mysql_get_client_version(void) {
    static unsigned long (*fn)(void) = NULL;
    RESOLVE(fn, "mysql_get_client_version", 0);
    return fn();
}
