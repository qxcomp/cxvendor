/* ============================================================================
 * pgsql_dyn.c — PostgreSQL libpq 动态加载封装层
 *
 * 设计目标与约束（与 sqlite3_dyn.c / mysql_dyn.c 一致）：
 *   1. 链接期绝不出现 -lpq。
 *   2. 无自定义 .h 文件，无独立 wrapper .c。
 *   3. 每个封装函数首次调用时才 dlsym 自己的符号并缓存。
 *   4. 封装函数名与原版 libpq 一致（PQconnectdb 等）。
 *   5. 编译期需要 <libpq-fe.h>（类型和常量），链接期不需要。
 *   6. 跨平台：Unix/macOS 用 dlopen/dlsym；Windows 用 LoadLibrary/GetProcAddress。
 *   7. 库搜索顺序：显式路径 > PGSQL_DYN_LIB 环境变量 > 系统搜索路径
 *      > 默认库名 > Nix 路径 > 平台默认路径。
 *   8. 线程安全：首次调用 pgsql_libok() 或任一封装函数时完成加载，
 *      多线程并发首次调用需外部保证或由主线程提前触发。
 *
 * 与 MySQL 方案的关键差异：
 *   - libpq 无需显式调用 mysql_library_init 那样的库初始化函数。
 *     PQconnectdb 首次调用时 libpq 内部会完成所需初始化。
 *   - 基础符号验证使用 PQlibVersion（libpq 必然导出）。
 * ========================================================================== */

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<libpq-fe.h>)
#    include <libpq-fe.h>
#    define PGSQL_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef PGSQL_DYN_HAVE_HEADER
/* ---------- 类型（头文件缺失时的最小 ABI 回退） ---------- */
typedef struct PGconn PGconn;
typedef struct PGresult PGresult;
typedef enum { CONNECTION_BAD = 1 } ConnStatusType;
typedef enum { PGRES_FATAL_ERROR = 7 } ExecStatusType;

#endif /* PGSQL_DYN_HAVE_HEADER */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 平台适配层
 * ============================================================ */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE pgsql_lib_handle_t;
  static pgsql_lib_handle_t pgsql_load_lib(const char *path) { return LoadLibraryA(path); }
  static void *pgsql_get_sym(pgsql_lib_handle_t h, const char *name) {
      return (void*)GetProcAddress(h, name);
  }
  static void pgsql_free_lib(pgsql_lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *pgsql_lib_error(void) { return "LoadLibrary failed"; }
  #define PGSQL_DEFAULT_LIB_NAME "libpq.dll"
  #define PGSQL_PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *pgsql_lib_handle_t;
  static pgsql_lib_handle_t pgsql_load_lib(const char *path) {
      int flags = RTLD_LAZY | RTLD_LOCAL;
  #ifdef RTLD_DEEPBIND
      flags |= RTLD_DEEPBIND;
  #endif
      return dlopen(path, flags);
  }
  static void *pgsql_get_sym(pgsql_lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void pgsql_free_lib(pgsql_lib_handle_t h) { if (h) dlclose(h); }
  static const char *pgsql_lib_error(void) { return dlerror(); }
  #ifdef __APPLE__
    #define PGSQL_DEFAULT_LIB_NAME "libpq.dylib"
  #else
    #define PGSQL_DEFAULT_LIB_NAME "libpq.so"
  #endif
  #define PGSQL_PATH_SEP ':'
#endif

/* ============================================================
 * 全局状态
 * ============================================================ */
static pgsql_lib_handle_t pgsql_handle = NULL;
static int pgsql_init_state = 0;   /* 0=未初始化, 1=成功, -1=失败 */

#define PGSQL_MAX_CANDIDATES 64
#define PGSQL_PATH_BUF_LEN   1024

/* ============================================================
 * 库加载：按优先级构造候选路径
 * ============================================================ */
static int pgsql_do_load(const char *library_path) {
    if (pgsql_handle) return 0;

    static char bufs[PGSQL_MAX_CANDIDATES][PGSQL_PATH_BUF_LEN];
    const char *candidates[PGSQL_MAX_CANDIDATES];
    int n = 0;

    /* 1. 显式传入的路径 */
    if (library_path && *library_path && n < PGSQL_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PGSQL_PATH_BUF_LEN, "%s", library_path);
        { candidates[n] = bufs[n]; n++; }
    }

    /* 2. 项目专属环境变量 */
    const char *explicit_lib = getenv("PGSQL_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < PGSQL_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PGSQL_PATH_BUF_LEN, "%s", explicit_lib);
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
            while (p && *p && n < PGSQL_MAX_CANDIDATES - 2) {
                char *sep = strchr(p, PGSQL_PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
                    snprintf(bufs[n], PGSQL_PATH_BUF_LEN, "%s/%s", p, PGSQL_DEFAULT_LIB_NAME);
                    { candidates[n] = bufs[n]; n++; }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 4. 直接给库名 */
    candidates[n++] = PGSQL_DEFAULT_LIB_NAME;

    /* 5. Nix 路径 */
    const char *home = getenv("HOME");
    if (home && *home && n < PGSQL_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PGSQL_PATH_BUF_LEN, "%s/.nix-profile/lib/%s",
                 home, PGSQL_DEFAULT_LIB_NAME);
        { candidates[n] = bufs[n]; n++; }
    }
    if (n < PGSQL_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PGSQL_PATH_BUF_LEN, "/nix/var/nix/profiles/default/lib/%s",
                 PGSQL_DEFAULT_LIB_NAME);
        { candidates[n] = bufs[n]; n++; }
    }

    /* 6. 平台默认路径 */
#ifdef __APPLE__
    candidates[n++] = "/opt/local/lib/libpq.dylib";
    candidates[n++] = "/usr/local/lib/libpq.dylib";
    candidates[n++] = "/opt/homebrew/lib/libpq.dylib";
#elif defined(__linux__) || defined(__ANDROID__)
    candidates[n++] = "/usr/local/lib/libpq.so";
    candidates[n++] = "/usr/lib/x86_64-linux-gnu/libpq.so";
    candidates[n++] = "/usr/lib/libpq.so";
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    candidates[n++] = "/usr/local/lib/libpq.so";
    candidates[n++] = "/usr/lib/libpq.so";
#endif
    candidates[n] = NULL;

    int i;
    for (i = 0; i < n; i++) {
        pgsql_handle = pgsql_load_lib(candidates[i]);
        if (pgsql_handle) break;
    }
    if (!pgsql_handle) {
        fprintf(stderr, "pgsql_dyn: cannot load libpq (%s)\n", pgsql_lib_error());
        return -1;
    }

    /* 验证基础符号 */
    if (!pgsql_get_sym(pgsql_handle, "PQlibVersion")) {
        fprintf(stderr, "pgsql_dyn: loaded library does not export PQlibVersion\n");
        pgsql_free_lib(pgsql_handle);
        pgsql_handle = NULL;
        return -1;
    }
    fprintf(stderr, "pgsql_dyn: loaded %d %s\n", i, candidates[i]);
    return 0;
}

/* ============================================================
 * 一次性初始化：加载库
 * ============================================================ */
int pgsql_libok(void) {
    if (pgsql_init_state == 0) {
        pgsql_init_state = (pgsql_do_load(NULL) == 0) ? 1 : -1;
    }
    return (pgsql_init_state == 1 && pgsql_handle != NULL) ? 1 : 0;
}

int pgsql_dyn_init(const char *library_path) {
    if (pgsql_init_state == 1) return 0;
    if (pgsql_init_state == -1) return -1;
    if (pgsql_do_load(library_path) == 0) {
        pgsql_init_state = 1;
        return 0;
    }
    pgsql_init_state = -1;
    return -1;
}

void pgsql_dyn_cleanup(void) {
    if (pgsql_handle) {
        pgsql_free_lib(pgsql_handle);
        pgsql_handle = NULL;
    }
    pgsql_init_state = 0;
}

/* ============================================================
 * 按需解析宏
 * ============================================================ */
#define PGSQL_RESOLVE(var, name, fallback) do {                      \
    if (!(var)) {                                              \
        if (!pgsql_libok()) return fallback;                   \
        *(void **)(&(var)) = pgsql_get_sym(pgsql_handle, name);      \
        if (!(var)) {                                          \
            fprintf(stderr, "dlsym failed: %s\n", name);       \
            return fallback;                                   \
        }                                                      \
    }                                                          \
} while (0)

/* ============================================================
 * 封装函数：与原版 libpq 同名
 * ============================================================ */
PGconn *PQconnectdb(const char *conninfo) {
    static PGconn *(*fn)(const char*) = NULL;
    if (!fn) {
        if (!pgsql_libok()) return NULL;
        *(void **)(&fn) = pgsql_get_sym(pgsql_handle, "PQconnectdb");
        if (!fn) return NULL;
    }
    return fn(conninfo);
}

ConnStatusType PQstatus(const PGconn *conn) {
    static ConnStatusType (*fn)(const PGconn*) = NULL;
    PGSQL_RESOLVE(fn, "PQstatus", CONNECTION_BAD);
    return fn(conn);
}

void PQfinish(PGconn *conn) {
    static void (*fn)(PGconn*) = NULL;
    if (!fn) {
        if (!pgsql_libok()) return;
        *(void **)(&fn) = pgsql_get_sym(pgsql_handle, "PQfinish");
        if (!fn) return;
    }
    fn(conn);
}

char *PQerrorMessage(const PGconn *conn) {
    static char *(*fn)(const PGconn*) = NULL;
    if (!fn) {
        if (!pgsql_libok()) return "pgsql not initialized";
        *(void **)(&fn) = pgsql_get_sym(pgsql_handle, "PQerrorMessage");
        if (!fn) return "PQerrorMessage not resolved";
    }
    return fn(conn);
}

PGresult *PQexec(PGconn *conn, const char *query) {
    static PGresult *(*fn)(PGconn*, const char*) = NULL;
    if (!fn) {
        if (!pgsql_libok()) return NULL;
        *(void **)(&fn) = pgsql_get_sym(pgsql_handle, "PQexec");
        if (!fn) return NULL;
    }
    return fn(conn, query);
}

ExecStatusType PQresultStatus(const PGresult *res) {
    static ExecStatusType (*fn)(const PGresult*) = NULL;
    PGSQL_RESOLVE(fn, "PQresultStatus", PGRES_FATAL_ERROR);
    return fn(res);
}

void PQclear(PGresult *res) {
    static void (*fn)(PGresult*) = NULL;
    if (!fn) {
        if (!pgsql_libok()) return;
        *(void **)(&fn) = pgsql_get_sym(pgsql_handle, "PQclear");
        if (!fn) return;
    }
    fn(res);
}

int PQntuples(const PGresult *res) {
    static int (*fn)(const PGresult*) = NULL;
    PGSQL_RESOLVE(fn, "PQntuples", 0);
    return fn(res);
}

int PQnfields(const PGresult *res) {
    static int (*fn)(const PGresult*) = NULL;
    PGSQL_RESOLVE(fn, "PQnfields", 0);
    return fn(res);
}

char *PQgetvalue(const PGresult *res, int tup_num, int field_num) {
    static char *(*fn)(const PGresult*, int, int) = NULL;
    if (!fn) {
        if (!pgsql_libok()) return NULL;
        *(void **)(&fn) = pgsql_get_sym(pgsql_handle, "PQgetvalue");
        if (!fn) return NULL;
    }
    return fn(res, tup_num, field_num);
}

char *PQfname(const PGresult *res, int field_num) {
    static char *(*fn)(const PGresult*, int) = NULL;
    if (!fn) {
        if (!pgsql_libok()) return NULL;
        *(void **)(&fn) = pgsql_get_sym(pgsql_handle, "PQfname");
        if (!fn) return NULL;
    }
    return fn(res, field_num);
}

int PQlibVersion(void) {
    static int (*fn)(void) = NULL;
    PGSQL_RESOLVE(fn, "PQlibVersion", 0);
    return fn();
}
