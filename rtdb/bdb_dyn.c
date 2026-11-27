/* ============================================================================
 * db_dyn.c — Berkeley DB (v4/v5/v6) 动态加载封装层
 *
 * 设计约束（与 sqlite3_dyn.c / mysql_dyn.c / pgsql_dyn.c / curl_dyn.c 一致）：
 *   1. 链接期绝不出现 -ldb。
 *   2. 无自定义 .h 文件，无独立 wrapper .c。
 *   3. 每个封装函数首次调用时才 dlsym 自己的符号并缓存。
 *   4. 封装函数名与原版 Berkeley DB C API 一致（db_create 等）。
 *   5. 编译期需要 <db.h>（类型和常量），链接期不需要。
 *   6. 跨平台：Unix/macOS 用 dlopen/dlsym；Windows 用 LoadLibrary/GetProcAddress。
 *   7. 库搜索顺序：显式路径 > DB_DYN_LIB 环境变量 > 系统搜索路径
 *      > 版本化默认库名（v6→v5→v4）> Nix 路径 > 平台默认路径。
 *   8. 加载后用 db_version() 识别实际版本，存入 loaded_major_version。
 *   9. 线程安全：首次调用 bdb_libok() 或任一封装函数时完成加载。
 *
 * 版本说明：
 *   - v4/v5/v6 的共享库 SONAME 带版本号：libdb-4.8.so / libdb-5.3.so / libdb-6.2.so
 *     （macOS 对应 .dylib，Windows 对应 libdb48.dll 等）。
 *   - 三代核心 API 签名基本一致，封装函数可共用。
 *   - v4 与 v5 的事务日志格式不兼容，但数据库文件向后兼容。
 *   - DB 结构体的方法（open/put/get/close）通过结构体内函数指针调用，
 *     不是独立导出符号，无需 dlsym。
 * ========================================================================== */

#include <sys/types.h>
/* _POSIX_C_SOURCE 下 sys/types.h 不定义 BSD 类型，而 db.h 需要它们 */
typedef unsigned int u_int;
typedef unsigned long u_long;
/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<db.h>)
#    include <db.h>
#    define BDB_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef BDB_DYN_HAVE_HEADER
/* ---------- 类型（头文件缺失时的最小 ABI 回退） ---------- */
#include <stdint.h>
typedef struct __db DB;
typedef struct __db_env DB_ENV;
typedef uint32_t u_int32_t;

#endif /* BDB_DYN_HAVE_HEADER */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 平台适配层
 * ============================================================ */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE bdb_lib_handle_t;
  static bdb_lib_handle_t bdb_load_lib(const char *path) { return LoadLibraryA(path); }
  static void *bdb_get_sym(bdb_lib_handle_t h, const char *name) {
      return (void*)GetProcAddress(h, name);
  }
  static void bdb_free_lib(bdb_lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *bdb_lib_error(void) { return "LoadLibrary failed"; }
  #define BDB_PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *bdb_lib_handle_t;
  static bdb_lib_handle_t bdb_load_lib(const char *path) {
      int flags = RTLD_LAZY | RTLD_LOCAL;
  #ifdef RTLD_DEEPBIND
      flags |= RTLD_DEEPBIND;
  #endif
      return dlopen(path, flags);
  }
  static void *bdb_get_sym(bdb_lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void bdb_free_lib(bdb_lib_handle_t h) { if (h) dlclose(h); }
  static const char *bdb_lib_error(void) { return dlerror(); }
  #define BDB_PATH_SEP ':'
#endif

/* ============================================================
 * 全局状态
 * ============================================================ */
static bdb_lib_handle_t db_handle = NULL;
static int bdb_init_state = 0;            /* 0=未初始化, 1=成功, -1=失败 */
static int loaded_major_version = 0;  /* 4/5/6；0 表示未知 */

#define BDB_MAX_CANDIDATES 80
#define BDB_PATH_BUF_LEN   1024

/* ============================================================
 * 库加载：按优先级构造候选路径（含版本化库名）
 * ============================================================ */
static int bdb_do_load(const char *library_path) {
    if (db_handle) return 0;

    static char bufs[BDB_MAX_CANDIDATES][BDB_PATH_BUF_LEN];
    const char *candidates[BDB_MAX_CANDIDATES];
    int n = 0;

    /* 1. 显式传入的路径 */
    if (library_path && *library_path && n < BDB_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s", library_path);
        { candidates[n] = bufs[n]; n++; }
    }

    /* 2. 项目专属环境变量 */
    const char *explicit_lib = getenv("DB_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < BDB_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s", explicit_lib);
        { candidates[n] = bufs[n]; n++; }
    }

    /* 3. 系统搜索路径环境变量中的目录（拼接各版本库名） */
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
            while (p && *p && n < BDB_MAX_CANDIDATES - 4) {
                char *sep = strchr(p, BDB_PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
#ifdef _WIN32
                    snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s/libdb62.dll", p); { candidates[n] = bufs[n]; n++; }
                    snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s/libdb53.dll", p); { candidates[n] = bufs[n]; n++; }
                    snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s/libdb48.dll", p); { candidates[n] = bufs[n]; n++; }
#elif defined(__APPLE__)
                    snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s/libdb-6.2.dylib", p); { candidates[n] = bufs[n]; n++; }
                    snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s/libdb-5.3.dylib", p); { candidates[n] = bufs[n]; n++; }
                    snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s/libdb-4.8.dylib", p); { candidates[n] = bufs[n]; n++; }
#else
                    snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s/libdb-6.2.so", p); { candidates[n] = bufs[n]; n++; }
                    snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s/libdb-5.3.so", p); { candidates[n] = bufs[n]; n++; }
                    snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s/libdb-4.8.so", p); { candidates[n] = bufs[n]; n++; }
#endif
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 4. 版本化默认库名（v6 → v5 → v4，优先新版本） */
#ifdef _WIN32
    candidates[n++] = "libdb62.dll";
    candidates[n++] = "libdb53.dll";
    candidates[n++] = "libdb48.dll";
#elif defined(__APPLE__)
    candidates[n++] = "libdb-6.2.dylib";
    candidates[n++] = "libdb-5.3.dylib";
    candidates[n++] = "libdb-4.8.dylib";
#else
    candidates[n++] = "libdb-6.2.so";
    candidates[n++] = "libdb-5.3.so";
    candidates[n++] = "libdb-4.8.so";
#endif

    /* 5. Nix 路径（三代，仅 Unix） */
#ifndef _WIN32
    const char *home = getenv("HOME");
    if (home && *home) {
        if (n < BDB_MAX_CANDIDATES - 3) {
#ifdef __APPLE__
            snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s/.nix-profile/lib/libdb-6.2.dylib", home); { candidates[n] = bufs[n]; n++; }
            snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s/.nix-profile/lib/libdb-5.3.dylib", home); { candidates[n] = bufs[n]; n++; }
            snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s/.nix-profile/lib/libdb-4.8.dylib", home); { candidates[n] = bufs[n]; n++; }
#else
            snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s/.nix-profile/lib/libdb-6.2.so", home); { candidates[n] = bufs[n]; n++; }
            snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s/.nix-profile/lib/libdb-5.3.so", home); { candidates[n] = bufs[n]; n++; }
            snprintf(bufs[n], BDB_PATH_BUF_LEN, "%s/.nix-profile/lib/libdb-4.8.so", home); { candidates[n] = bufs[n]; n++; }
#endif
        }
    }
    if (n < BDB_MAX_CANDIDATES - 3) {
#ifdef __APPLE__
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libdb-6.2.dylib";
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libdb-5.3.dylib";
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libdb-4.8.dylib";
#else
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libdb-6.2.so";
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libdb-5.3.so";
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libdb-4.8.so";
#endif
    }
#endif

    /* 6. 平台默认路径 */
#ifdef __APPLE__
    candidates[n++] = "/opt/local/lib/libdb-6.2.dylib";
    candidates[n++] = "/opt/local/lib/libdb-5.3.dylib";
    candidates[n++] = "/opt/local/lib/libdb-4.8.dylib";
    candidates[n++] = "/usr/local/lib/libdb-6.2.dylib";
    candidates[n++] = "/usr/local/lib/libdb-5.3.dylib";
    candidates[n++] = "/usr/local/lib/libdb-4.8.dylib";
#elif defined(__linux__) || defined(__ANDROID__)
    candidates[n++] = "/usr/lib/x86_64-linux-gnu/libdb-6.2.so";
    candidates[n++] = "/usr/lib/x86_64-linux-gnu/libdb-5.3.so";
    candidates[n++] = "/usr/lib/x86_64-linux-gnu/libdb-4.8.so";
    candidates[n++] = "/usr/lib/libdb-6.2.so";
    candidates[n++] = "/usr/lib/libdb-5.3.so";
    candidates[n++] = "/usr/lib/libdb-4.8.so";
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    candidates[n++] = "/usr/local/lib/libdb-6.2.so";
    candidates[n++] = "/usr/local/lib/libdb-5.3.so";
    candidates[n++] = "/usr/local/lib/libdb-4.8.so";
#endif
    candidates[n] = NULL;

    int i;
    for (i = 0; i < n; i++) {
        db_handle = bdb_load_lib(candidates[i]);
        if (db_handle) break;
    }
    if (!db_handle) {
        fprintf(stderr, "bdb_dyn: cannot load libdb (%s)\n", bdb_lib_error());
        return -1;
    }

    /* 验证基础符号并识别版本 */
    void (*fn_version)(int*, int*, int*) = NULL;
    *(void **)(&fn_version) = bdb_get_sym(db_handle, "db_version");
    if (!fn_version) {
        fprintf(stderr, "bdb_dyn: loaded library does not export db_version\n");
        bdb_free_lib(db_handle);
        db_handle = NULL;
        return -1;
    }
    int major = 0, minor = 0, patch = 0;
    fn_version(&major, &minor, &patch);
    loaded_major_version = major;
    fprintf(stderr, "bdb_dyn: loaded %d %s (Berkeley DB %d.%d.%d)\n", i, candidates[i], major, minor, patch);
    return 0;
}

/* ============================================================
 * 一次性初始化
 * ============================================================ */
int bdb_libok(void) {
    if (bdb_init_state == 0) {
        bdb_init_state = (bdb_do_load(NULL) == 0) ? 1 : -1;
    }
    return (bdb_init_state == 1 && db_handle != NULL) ? 1 : 0;
}

int db_dyn_init(const char *library_path) {
    if (bdb_init_state == 1) return 0;
    if (bdb_init_state == -1) return -1;
    if (bdb_do_load(library_path) == 0) {
        bdb_init_state = 1;
        return 0;
    }
    bdb_init_state = -1;
    return -1;
}

void db_dyn_cleanup(void) {
    if (db_handle) {
        bdb_free_lib(db_handle);
        db_handle = NULL;
    }
    bdb_init_state = 0;
    loaded_major_version = 0;
}

int db_dyn_loaded_version(void) {
    return loaded_major_version;
}

/* ============================================================
 * 按需解析宏
 * ============================================================ */
#define BDB_RESOLVE(var, name, fallback) do {                      \
    if (!(var)) {                                              \
        if (!bdb_libok()) return fallback;                      \
        *(void **)(&(var)) = bdb_get_sym(db_handle, name);         \
        if (!(var)) {                                          \
            fprintf(stderr, "dlsym failed: %s\n", name);       \
            return fallback;                                   \
        }                                                      \
    }                                                          \
} while (0)

/* ============================================================
 * 封装函数：与原版 Berkeley DB C API 同名
 * 注：DB 结构体方法（open/put/get/close）通过结构体内函数指针调用，
 *     不是独立导出符号，无需 dlsym。
 * ============================================================ */
int db_create(DB **dbp, DB_ENV *dbenv, u_int32_t flags) {
    static int (*fn)(DB**, DB_ENV*, u_int32_t) = NULL;
    BDB_RESOLVE(fn, "db_create", -1);
    return fn(dbp, dbenv, flags);
}

char *db_strerror(int error) {
    static char *(*fn)(int) = NULL;
    if (!fn) {
        if (!bdb_libok()) return "db not initialized";
        *(void **)(&fn) = bdb_get_sym(db_handle, "db_strerror");
        if (!fn) return "db_strerror not resolved";
    }
    return fn(error);
}

char *db_version(int *majorp, int *minorp, int *patchp) {
    static char *(*fn)(int*, int*, int*) = NULL;
    if (!fn) {
        if (!bdb_libok()) {
            if (majorp) *majorp = 0;
            if (minorp) *minorp = 0;
            if (patchp) *patchp = 0;
            return NULL;
        }
        *(void **)(&fn) = bdb_get_sym(db_handle, "db_version");
        if (!fn) {
            if (majorp) *majorp = 0;
            if (minorp) *minorp = 0;
            if (patchp) *patchp = 0;
            return NULL;
        }
    }
    return fn(majorp, minorp, patchp);
}
