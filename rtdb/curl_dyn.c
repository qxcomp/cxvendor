/* ============================================================================
 * curl_dyn.c — libcurl 动态加载封装层
 *
 * 设计目标与约束（与 sqlite3_dyn.c / mysql_dyn.c / pgsql_dyn.c 一致）：
 *   1. 链接期绝不出现 -lcurl。
 *   2. 无自定义 .h 文件，无独立 wrapper .c。
 *   3. 每个封装函数首次调用时才 dlsym 自己的符号并缓存。
 *   4. 封装函数名与原版 libcurl 一致（curl_easy_init 等）。
 *   5. 编译期需要 <curl/curl.h>（类型和常量），链接期不需要。
 *   6. 跨平台：Unix/macOS 用 dlopen/dlsym；Windows 用 LoadLibrary/GetProcAddress。
 *   7. 库搜索顺序：显式路径 > CURL_DYN_LIB 环境变量 > 系统搜索路径
 *      > 默认库名 > Nix 路径 > 平台默认路径。
 *   8. 线程安全：首次调用 curl_libok() 或任一封装函数时完成加载，
 *      多线程并发首次调用需外部保证或由主线程提前触发。
 *
 * 与 MySQL 方案的关键差异：
 *   - libcurl 要求在使用任何其他函数前调用 curl_global_init()。
 *     该函数本身不是线程安全的，因此 curl_libok() 在加载库后
 *     会以 CURL_GLOBAL_DEFAULT 参数调用一次，并保证只调用一次。
 *   - curl_global_init 内部会初始化 SSL 和 Winsock（Windows），
 *     因此无需额外处理平台网络初始化。
 * ========================================================================== */

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<curl/curl.h>)
#    include <curl/curl.h>
#    define CURL_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef CURL_DYN_HAVE_HEADER
/* ---------- 类型（头文件缺失时的最小 ABI 回退） ---------- */
#include <stdarg.h>
typedef void CURL;
typedef enum { CURLE_OK = 0, CURLE_FAILED_INIT = 2 } CURLcode;
typedef enum { CURLOPT_URL = 10002, CURLOPT_WRITEDATA = 10001, CURLOPT_ERRORBUFFER = 10010 } CURLoption;
typedef enum { CURLINFO_NONE = 0 } CURLINFO;
typedef enum { CURLVERSION_NOW = 4 } CURLversion;
#define CURL_GLOBAL_DEFAULT 3
struct curl_slist { char *data; struct curl_slist *next; };
typedef struct curl_version_info_data curl_version_info_data;

#endif /* CURL_DYN_HAVE_HEADER */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 新版 curl 头文件把 curl_easy_setopt/getinfo 定义为类型检查宏，
 * 会与本封装的同名函数定义冲突，取消宏定义使用真正的 API */
#ifdef curl_easy_setopt
#undef curl_easy_setopt
#endif
#ifdef curl_easy_getinfo
#undef curl_easy_getinfo
#endif

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
  #define DEFAULT_LIB_NAME "libcurl.dll"
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
    #define DEFAULT_LIB_NAME "libcurl.dylib"
  #else
    #define DEFAULT_LIB_NAME "libcurl.so"
  #endif
  #define PATH_SEP ':'
#endif

/* ============================================================
 * 全局状态
 * ============================================================ */
static lib_handle_t curl_handle = NULL;
static int init_state = 0;   /* 0=未初始化, 1=成功, -1=失败 */

#define MAX_CANDIDATES 64
#define PATH_BUF_LEN   1024

/* ============================================================
 * 库加载：按优先级构造候选路径
 * ============================================================ */
static int do_load(const char *library_path) {
    if (curl_handle) return 0;

    static char bufs[MAX_CANDIDATES][PATH_BUF_LEN];
    const char *candidates[MAX_CANDIDATES];
    int n = 0;

    /* 1. 显式传入的路径 */
    if (library_path && *library_path && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", library_path);
        { candidates[n] = bufs[n]; n++; }
    }

    /* 2. 项目专属环境变量 */
    const char *explicit_lib = getenv("CURL_DYN_LIB");
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
    candidates[n++] = "/opt/local/lib/libcurl.dylib";
    candidates[n++] = "/usr/local/lib/libcurl.dylib";
    candidates[n++] = "/opt/homebrew/lib/libcurl.dylib";
    candidates[n++] = "/usr/local/opt/curl/lib/libcurl.dylib";  /* Homebrew keg-only */
#elif defined(__linux__) || defined(__ANDROID__)
    candidates[n++] = "/usr/local/lib/libcurl.so";
    candidates[n++] = "/usr/lib/x86_64-linux-gnu/libcurl.so";
    candidates[n++] = "/usr/lib/libcurl.so";
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    candidates[n++] = "/usr/local/lib/libcurl.so";
    candidates[n++] = "/usr/lib/libcurl.so";
#endif
    candidates[n] = NULL;

    for (int i = 0; i < n; i++) {
        curl_handle = load_lib(candidates[i]);
        if (curl_handle) break;
    }
    if (!curl_handle) {
        fprintf(stderr, "curl_dyn: cannot load libcurl (%s)\n", lib_error());
        return -1;
    }

    /* 验证基础符号 */
    if (!get_sym(curl_handle, "curl_version_info")) {
        fprintf(stderr, "curl_dyn: loaded library does not export curl_version_info\n");
        free_lib(curl_handle);
        curl_handle = NULL;
        return -1;
    }
    return 0;
}

/* ============================================================
 * 一次性初始化：加载库 + curl_global_init
 * ============================================================ */
int curl_libok(void) {
    if (init_state == 0) {
        if (do_load(NULL) != 0) {
            init_state = -1;
            return 0;
        }
        /* libcurl 要求在使用任何函数前初始化全局状态 */
        CURLcode (*fn_global_init)(long) = NULL;
        *(void **)(&fn_global_init) = get_sym(curl_handle, "curl_global_init");
        if (!fn_global_init) {
            fprintf(stderr, "curl_dyn: curl_global_init not found\n");
            free_lib(curl_handle);
            curl_handle = NULL;
            init_state = -1;
            return 0;
        }
        if (fn_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            fprintf(stderr, "curl_dyn: curl_global_init failed\n");
            free_lib(curl_handle);
            curl_handle = NULL;
            init_state = -1;
            return 0;
        }
        init_state = 1;
    }
    return (init_state == 1 && curl_handle != NULL) ? 1 : 0;
}

int curl_dyn_init(const char *library_path) {
    if (init_state == 1) return 0;
    if (init_state == -1) return -1;
    if (do_load(library_path) != 0) {
        init_state = -1;
        return -1;
    }
    CURLcode (*fn_global_init)(long) = NULL;
    *(void **)(&fn_global_init) = get_sym(curl_handle, "curl_global_init");
    if (!fn_global_init || fn_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        free_lib(curl_handle);
        curl_handle = NULL;
        init_state = -1;
        return -1;
    }
    init_state = 1;
    return 0;
}

void curl_dyn_cleanup(void) {
    if (curl_handle) {
        void (*fn_global_cleanup)(void) = NULL;
        *(void **)(&fn_global_cleanup) = get_sym(curl_handle, "curl_global_cleanup");
        if (fn_global_cleanup) fn_global_cleanup();
        free_lib(curl_handle);
        curl_handle = NULL;
    }
    init_state = 0;
}

/* ============================================================
 * 按需解析宏
 * ============================================================ */
#define RESOLVE(var, name, fallback) do {                      \
    if (!(var)) {                                              \
        if (!curl_libok()) return fallback;                    \
        *(void **)(&(var)) = get_sym(curl_handle, name);       \
        if (!(var)) {                                          \
            fprintf(stderr, "dlsym failed: %s\n", name);       \
            return fallback;                                   \
        }                                                      \
    }                                                          \
} while (0)

/* ============================================================
 * 封装函数：与原版 libcurl 同名
 * ============================================================ */
CURL *curl_easy_init(void) {
    static CURL *(*fn)(void) = NULL;
    if (!fn) {
        if (!curl_libok()) return NULL;
        *(void **)(&fn) = get_sym(curl_handle, "curl_easy_init");
        if (!fn) return NULL;
    }
    return fn();
}

void curl_easy_cleanup(CURL *handle) {
    static void (*fn)(CURL*) = NULL;
    if (!fn) {
        if (!curl_libok()) return;
        *(void **)(&fn) = get_sym(curl_handle, "curl_easy_cleanup");
        if (!fn) return;
    }
    fn(handle);
}

CURLcode curl_easy_setopt(CURL *handle, CURLoption option, ...) {
    /* 可变参数函数无法用统一宏处理，需要单独实现 */
    static CURLcode (*fn)(CURL*, CURLoption, ...) = NULL;
    if (!fn) {
        if (!curl_libok()) return CURLE_FAILED_INIT;
        *(void **)(&fn) = get_sym(curl_handle, "curl_easy_setopt");
        if (!fn) return CURLE_FAILED_INIT;
    }
    /* 注意：可变参数的转发是平台相关的，严格来说不可移植。
     * 实际项目中建议对常用的 option 类型做显式分支封装。 */
    va_list args;
    va_start(args, option);
    /* 简化处理：只支持最常见的字符串和长整数类型 */
    CURLcode result;
    switch (option) {
        case CURLOPT_URL:
        case CURLOPT_WRITEDATA:
        case CURLOPT_ERRORBUFFER: {
            void *arg = va_arg(args, void*);
            result = fn(handle, option, arg);
            break;
        }
        default: {
            long arg = va_arg(args, long);
            result = fn(handle, option, arg);
            break;
        }
    }
    va_end(args);
    return result;
}

CURLcode curl_easy_perform(CURL *handle) {
    static CURLcode (*fn)(CURL*) = NULL;
    RESOLVE(fn, "curl_easy_perform", CURLE_FAILED_INIT);
    return fn(handle);
}

CURLcode curl_easy_getinfo(CURL *handle, CURLINFO info, ...) {
    static CURLcode (*fn)(CURL*, CURLINFO, ...) = NULL;
    if (!fn) {
        if (!curl_libok()) return CURLE_FAILED_INIT;
        *(void **)(&fn) = get_sym(curl_handle, "curl_easy_getinfo");
        if (!fn) return CURLE_FAILED_INIT;
    }
    va_list args;
    va_start(args, info);
    void *arg = va_arg(args, void*);
    CURLcode result = fn(handle, info, arg);
    va_end(args);
    return result;
}

const char *curl_easy_strerror(CURLcode code) {
    static const char *(*fn)(CURLcode) = NULL;
    if (!fn) {
        if (!curl_libok()) return "curl not initialized";
        *(void **)(&fn) = get_sym(curl_handle, "curl_easy_strerror");
        if (!fn) return "curl_easy_strerror not resolved";
    }
    return fn(code);
}

curl_version_info_data *curl_version_info(CURLversion age) {
    static curl_version_info_data *(*fn)(CURLversion) = NULL;
    if (!fn) {
        if (!curl_libok()) return NULL;
        *(void **)(&fn) = get_sym(curl_handle, "curl_version_info");
        if (!fn) return NULL;
    }
    return fn(age);
}

void curl_free(void *p) {
    static void (*fn)(void*) = NULL;
    if (!fn) {
        if (!curl_libok()) return;
        *(void **)(&fn) = get_sym(curl_handle, "curl_free");
        if (!fn) return;
    }
    fn(p);
}

struct curl_slist *curl_slist_append(struct curl_slist *list, const char *string) {
    static struct curl_slist *(*fn)(struct curl_slist*, const char*) = NULL;
    if (!fn) {
        if (!curl_libok()) return NULL;
        *(void **)(&fn) = get_sym(curl_handle, "curl_slist_append");
        if (!fn) return NULL;
    }
    return fn(list, string);
}

void curl_slist_free_all(struct curl_slist *list) {
    static void (*fn)(struct curl_slist*) = NULL;
    if (!fn) {
        if (!curl_libok()) return;
        *(void **)(&fn) = get_sym(curl_handle, "curl_slist_free_all");
        if (!fn) return;
    }
    fn(list);
}

