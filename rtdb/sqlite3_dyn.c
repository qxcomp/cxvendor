



/* ============================================================================
 * sqlite3_dyn.c — SQLite 动态加载封装层
 *
 * ============================ 需求文档 ======================================
 *
 * 【项目目标】
 *   在不链接 libsqlite3 的前提下，通过运行时动态加载（dlopen/LoadLibrary）
 *   获得与正常链接几乎一致的 SQLite 使用体验。业务代码直接调用
 *   sqlite3_open / sqlite3_step 等标准 API，编译和链接阶段均不依赖
 *   libsqlite3，运行时才解析真实函数地址。
 *
 * 【硬性约束】
 *   1. 链接时绝不出现 -lsqlite3，最终可执行文件不得依赖 libsqlite3。
 *   2. 无任何自定义 .h 文件；无独立的 wrapper .c 文件。
 *   3. 不使用集中的函数指针表（跳板）。每个封装函数在首次被调用时
 *      才 dlsym 解析自己那一个符号，并缓存到函数内部的 static 变量。
 *      未被调用到的 API 永远不会被解析。
 *   4. 封装函数名与原版 SQLite 完全一致（sqlite3_open 等），
 *      由本文件提供这些符号的定义，避免与官方库冲突。
 *   5. 编译期允许且需要 <sqlite3.h>（仅用于类型和常量），
 *      链接期不需要 libsqlite3。
 *
 * 【自动化要求】
 *   无需显式调用 sqlite3_dyn_init。任一封装函数首次被调用时，
 *   内部通过 sqlite3_libok() 触发一次性库加载。sqlite3_libok()
 *   保证加载逻辑只执行一次，并返回当前是否就绪。
 *
 * 【跨平台要求】
 *   支持 macOS、Linux、Android、FreeBSD/OpenBSD/NetBSD、Windows。
 *   - 动态库 API：Unix 用 dlopen/dlsym/dlclose/dlerror；
 *     Windows 用 LoadLibraryA/GetProcAddress/FreeLibrary。
 *   - 默认库名：macOS 为 libsqlite3.dylib；Linux/Android/BSD 为
 *     libsqlite3.so；Windows 为 sqlite3.dll。
 *   - Linux/Android/BSD 需链接 -ldl；macOS 不需要。
 *
 * 【库搜索顺序（优先级从高到低）】
 *   1. sqlite3_dyn_init(path) 显式传入的路径（若调用）。
 *   2. 环境变量 SQLITE3_DYN_LIB：视为完整库文件路径。
 *   3. 系统动态库搜索路径环境变量中的目录 + 默认库名：
 *        Linux/Android/BSD : LD_LIBRARY_PATH
 *        macOS             : DYLD_LIBRARY_PATH（受 SIP 限制时可能为空）
 *        Windows           : PATH
 *   4. 直接给出默认库名，交由系统链接器搜索。
 *   5. Nix 安装的库目录：
 *        $HOME/.nix-profile/lib/            （单用户/用户级 profile）
 *        /nix/var/nix/profiles/default/lib/ （多用户默认 profile）
 *   6. 平台默认安装路径回退（MacPorts/Homebrew/系统目录）。
 *
 * 【线程安全】
 *   init_state 的一次性初始化在单线程下正确。多线程并发首次调用
 *   sqlite3_libok() 需由主线程在启动阶段先调用一次完成初始化，
 *   或由调用方自行加锁。初始化完成后状态只读，并发读取安全。
 *
 * 【已知限制】
 *   - 封装函数签名必须与 <sqlite3.h> 中的原型严格一致，编译器无法
 *     验证 dlsym 得到的地址与函数指针类型是否匹配。
 *   - macOS SIP 可能剥离 DYLD_* 环境变量，此时需用 SQLITE3_DYN_LIB
 *     显式指定路径。
 *   - 路径候选缓冲区为静态固定大小，超长路径会被截断。
 *
 * ========================================================================== */

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<sqlite3.h>)
#    include <sqlite3.h>
#    define SQLITE3_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef SQLITE3_DYN_HAVE_HEADER
/* ---------- 类型（头文件缺失时的最小 ABI 回退） ---------- */
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
#define SQLITE_ERROR 1
#define SQLITE_NULL 5

#endif /* SQLITE3_DYN_HAVE_HEADER */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 平台适配层：统一动态库 API 与默认库名
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
  #define DEFAULT_LIB_NAME "sqlite3.dll"
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
    #define DEFAULT_LIB_NAME "libsqlite3.dylib"
  #else
    #define DEFAULT_LIB_NAME "libsqlite3.so"
  #endif
  #define PATH_SEP ':'
#endif

/* ============================================================
 * 全局状态
 * ============================================================ */
static lib_handle_t sqlite_handle = NULL;
static int init_state = 0;   /* 0=未初始化, 1=成功, -1=失败 */

#define MAX_CANDIDATES 64
#define PATH_BUF_LEN   1024

/* ============================================================
 * 库加载：按优先级构造候选路径并逐个尝试
 * ============================================================ */
static int do_load(const char *library_path) {
    if (sqlite_handle) return 0;

    static char bufs[MAX_CANDIDATES][PATH_BUF_LEN];
    const char *candidates[MAX_CANDIDATES];
    int n = 0;

    /* 1. 显式传入的路径 */
    if (library_path && *library_path && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", library_path);
        { candidates[n] = bufs[n]; n++; }
    }

    /* 2. 项目专属环境变量 SQLITE3_DYN_LIB（完整文件路径） */
    const char *explicit_lib = getenv("SQLITE3_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", explicit_lib);
        { candidates[n] = bufs[n]; n++; }
    }

    /* 3. 系统库搜索路径环境变量中的目录 + 默认库名 */
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
                if (*p) {  /* 跳过空项，避免意外加载当前目录 */
                    snprintf(bufs[n], PATH_BUF_LEN, "%s/%s", p, DEFAULT_LIB_NAME);
                    { candidates[n] = bufs[n]; n++; }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 4. 直接给库名，交由系统链接器搜索 */
    candidates[n++] = DEFAULT_LIB_NAME;

    /* 5. Nix 安装的库目录 */
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

    /* 6. 平台默认安装路径回退 */
#ifdef __APPLE__
    candidates[n++] = "/opt/local/lib/libsqlite3.dylib";
    candidates[n++] = "/usr/local/lib/libsqlite3.dylib";
    candidates[n++] = "/opt/homebrew/lib/libsqlite3.dylib";
#elif defined(__linux__) || defined(__ANDROID__)
    candidates[n++] = "/usr/local/lib/libsqlite3.so";
    candidates[n++] = "/usr/lib/x86_64-linux-gnu/libsqlite3.so.0";
    candidates[n++] = "/usr/lib/libsqlite3.so.0";
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    candidates[n++] = "/usr/local/lib/libsqlite3.so";
    candidates[n++] = "/usr/lib/libsqlite3.so";
#endif
    candidates[n] = NULL;

    /* 逐个尝试加载 */
    for (int i = 0; i < n; i++) {
        sqlite_handle = load_lib(candidates[i]);
        if (sqlite_handle) break;
    }
    if (!sqlite_handle) {
        fprintf(stderr, "sqlite3_dyn: cannot load libsqlite3 (%s)\n", lib_error());
        return -1;
    }

    /* 验证基础符号，确保加载的是真正的 SQLite */
    if (!get_sym(sqlite_handle, "sqlite3_libversion")) {
        fprintf(stderr, "sqlite3_dyn: loaded library does not export sqlite3_libversion\n");
        free_lib(sqlite_handle);
        sqlite_handle = NULL;
        return -1;
    }
    return 0;
}

/* ============================================================
 * 一次性初始化：保证加载逻辑只执行一次
 * ============================================================ */
int sqlite3_libok(void) {
    if (init_state == 0) {
        init_state = (do_load(NULL) == 0) ? 1 : -1;
    }
    return (init_state == 1 && sqlite_handle != NULL) ? 1 : 0;
}

/* 可选的显式初始化接口：允许提前加载或指定路径 */
int sqlite3_dyn_init(const char *library_path) {
    if (init_state == 1) return 0;
    if (init_state == -1) return -1;
    if (do_load(library_path) == 0) {
        init_state = 1;
        return 0;
    }
    init_state = -1;
    return -1;
}

void sqlite3_dyn_cleanup(void) {
    if (sqlite_handle) {
        free_lib(sqlite_handle);
        sqlite_handle = NULL;
    }
    init_state = 0;
}

/* ============================================================
 * 按需解析宏：每个封装函数首次调用时才 dlsym 自己的符号
 * 失败时返回 fallback（对 int 函数通常是 SQLITE_ERROR）
 * ============================================================ */
#define RESOLVE(var, name, fallback) do {                      \
    if (!(var)) {                                              \
        if (!sqlite3_libok()) return fallback;                 \
        *(void **)(&(var)) = get_sym(sqlite_handle, name);     \
        if (!(var)) {                                          \
            fprintf(stderr, "dlsym failed: %s\n", name);       \
            return fallback;                                   \
        }                                                      \
    }                                                          \
} while (0)

/* ============================================================
 * 封装函数：与原版 SQLite 同名，首次调用时解析并缓存
 * ============================================================ */
int sqlite3_open(const char *filename, sqlite3 **ppDb) {
    static int (*fn)(const char*, sqlite3**) = NULL;
    RESOLVE(fn, "sqlite3_open", SQLITE_ERROR);
    return fn(filename, ppDb);
}

int sqlite3_close(sqlite3 *db) {
    static int (*fn)(sqlite3*) = NULL;
    RESOLVE(fn, "sqlite3_close", SQLITE_ERROR);
    return fn(db);
}

int sqlite3_exec(sqlite3 *db, const char *sql,
                 int (*callback)(void*,int,char**,char**), void *arg,
                 char **errmsg) {
    static int (*fn)(sqlite3*, const char*,
                     int (*)(void*,int,char**,char**), void*, char**) = NULL;
    RESOLVE(fn, "sqlite3_exec", SQLITE_ERROR);
    return fn(db, sql, callback, arg, errmsg);
}

int sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nByte,
                       sqlite3_stmt **ppStmt, const char **pzTail) {
    static int (*fn)(sqlite3*, const char*, int, sqlite3_stmt**, const char**) = NULL;
    RESOLVE(fn, "sqlite3_prepare_v2", SQLITE_ERROR);
    return fn(db, zSql, nByte, ppStmt, pzTail);
}

int sqlite3_step(sqlite3_stmt *pStmt) {
    static int (*fn)(sqlite3_stmt*) = NULL;
    RESOLVE(fn, "sqlite3_step", SQLITE_ERROR);
    return fn(pStmt);
}

int sqlite3_finalize(sqlite3_stmt *pStmt) {
    static int (*fn)(sqlite3_stmt*) = NULL;
    RESOLVE(fn, "sqlite3_finalize", SQLITE_ERROR);
    return fn(pStmt);
}

int sqlite3_reset(sqlite3_stmt *pStmt) {
    static int (*fn)(sqlite3_stmt*) = NULL;
    RESOLVE(fn, "sqlite3_reset", SQLITE_ERROR);
    return fn(pStmt);
}

const char *sqlite3_errmsg(sqlite3 *db) {
    static const char *(*fn)(sqlite3*) = NULL;
    if (!fn) {
        if (!sqlite3_libok()) return "sqlite3 not initialized";
        *(void **)(&fn) = get_sym(sqlite_handle, "sqlite3_errmsg");
        if (!fn) return "sqlite3_errmsg not resolved";
    }
    return fn(db);
}

int sqlite3_column_count(sqlite3_stmt *pStmt) {
    static int (*fn)(sqlite3_stmt*) = NULL;
    RESOLVE(fn, "sqlite3_column_count", 0);
    return fn(pStmt);
}

const char *sqlite3_column_name(sqlite3_stmt *pStmt, int N) {
    static const char *(*fn)(sqlite3_stmt*, int) = NULL;
    if (!fn) {
        if (!sqlite3_libok()) return NULL;
        *(void **)(&fn) = get_sym(sqlite_handle, "sqlite3_column_name");
        if (!fn) return NULL;
    }
    return fn(pStmt, N);
}

const unsigned char *sqlite3_column_text(sqlite3_stmt *pStmt, int iCol) {
    static const unsigned char *(*fn)(sqlite3_stmt*, int) = NULL;
    if (!fn) {
        if (!sqlite3_libok()) return NULL;
        *(void **)(&fn) = get_sym(sqlite_handle, "sqlite3_column_text");
        if (!fn) return NULL;
    }
    return fn(pStmt, iCol);
}

int sqlite3_column_int(sqlite3_stmt *pStmt, int iCol) {
    static int (*fn)(sqlite3_stmt*, int) = NULL;
    RESOLVE(fn, "sqlite3_column_int", 0);
    return fn(pStmt, iCol);
}

double sqlite3_column_double(sqlite3_stmt *pStmt, int iCol) {
    static double (*fn)(sqlite3_stmt*, int) = NULL;
    if (!fn) {
        if (!sqlite3_libok()) return 0.0;
        *(void **)(&fn) = get_sym(sqlite_handle, "sqlite3_column_double");
        if (!fn) return 0.0;
    }
    return fn(pStmt, iCol);
}

int sqlite3_column_type(sqlite3_stmt *pStmt, int iCol) {
    static int (*fn)(sqlite3_stmt*, int) = NULL;
    RESOLVE(fn, "sqlite3_column_type", SQLITE_NULL);
    return fn(pStmt, iCol);
}

int sqlite3_bind_int(sqlite3_stmt *pStmt, int idx, int val) {
    static int (*fn)(sqlite3_stmt*, int, int) = NULL;
    RESOLVE(fn, "sqlite3_bind_int", SQLITE_ERROR);
    return fn(pStmt, idx, val);
}

int sqlite3_bind_text(sqlite3_stmt *pStmt, int idx, const char *val,
                      int n, void (*destructor)(void*)) {
    static int (*fn)(sqlite3_stmt*, int, const char*, int, void(*)(void*)) = NULL;
    RESOLVE(fn, "sqlite3_bind_text", SQLITE_ERROR);
    return fn(pStmt, idx, val, n, destructor);
}

int sqlite3_bind_double(sqlite3_stmt *pStmt, int idx, double val) {
    static int (*fn)(sqlite3_stmt*, int, double) = NULL;
    RESOLVE(fn, "sqlite3_bind_double", SQLITE_ERROR);
    return fn(pStmt, idx, val);
}

int sqlite3_bind_null(sqlite3_stmt *pStmt, int idx) {
    static int (*fn)(sqlite3_stmt*, int) = NULL;
    RESOLVE(fn, "sqlite3_bind_null", SQLITE_ERROR);
    return fn(pStmt, idx);
}

const char *sqlite3_libversion(void) {
    static const char *(*fn)(void) = NULL;
    if (!fn) {
        if (!sqlite3_libok()) return "unknown";
        *(void **)(&fn) = get_sym(sqlite_handle, "sqlite3_libversion");
        if (!fn) return "unknown";
    }
    return fn();
}

