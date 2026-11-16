/* ============================================================================
 * mpv_dyn.c — libmpv 动态加载封装层
 *
 * 设计约束（与 sqlite3_dyn.c / ffmpeg_dyn.c 等一致）：
 *   1. 链接期绝不出现 -lmpv。
 *   2. 无自定义 .h 文件，无独立 wrapper .c。
 *   3. 每个封装函数首次调用时才 dlsym 自己的符号并缓存。
 *   4. 封装函数名与原版 libmpv C API 一致。
 *   5. 编译期需要 <mpv/client.h>（类型和常量），链接期不需要。
 *   6. 跨平台：Unix/macOS 用 dlopen/dlsym；Windows 用 LoadLibrary/GetProcAddress。
 *   7. 搜索顺序：显式路径 > MPV_DYN_LIB > 系统搜索路径 > 默认库名
 *      > Nix 路径 > 平台默认路径。
 *   8. 线程安全：首次调用 mpv_libok() 或任一封装函数时完成加载。
 *
 * 版本说明：
 *   - libmpv 的 C API 主版本号当前为 1，API 设计上保持向后兼容。
 *   - 无需像 FFmpeg 那样做运行时版本分支；用 mpv_client_api_version()
 *     获取版本号供参考即可。
 *   - 基础符号验证：mpv_client_api_version 是必然导出的稳定符号。
 * ========================================================================== */

#include <mpv/client.h>
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
  #define PATH_SEP ':'
#endif

/* ============================================================
 * 全局状态
 * ============================================================ */
static lib_handle_t mpv_handle_lib = NULL;
static int init_state = 0;
static unsigned long loaded_api_version = 0;

#define MAX_CANDIDATES 64
#define PATH_BUF_LEN   1024

/* ============================================================
 * 库加载
 * ============================================================ */
static int do_load(const char *library_path) {
    if (mpv_handle_lib) return 0;

    static char bufs[MAX_CANDIDATES][PATH_BUF_LEN];
    const char *candidates[MAX_CANDIDATES];
    int n = 0;

    /* 1. 显式传入的路径 */
    if (library_path && *library_path && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", library_path);
        { candidates[n] = bufs[n]; n++; }
    }

    /* 2. 项目专属环境变量 */
    const char *explicit_lib = getenv("MPV_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", explicit_lib);
        { candidates[n] = bufs[n]; n++; }
    }

    /* 3. 系统搜索路径环境变量 */
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
            while (p && *p && n < MAX_CANDIDATES - 3) {
                char *sep = strchr(p, PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
#ifdef _WIN32
                    snprintf(bufs[n], PATH_BUF_LEN, "%s/mpv-2.dll", p); { candidates[n] = bufs[n]; n++; }
#elif defined(__APPLE__)
                    snprintf(bufs[n], PATH_BUF_LEN, "%s/libmpv.2.dylib", p); { candidates[n] = bufs[n]; n++; }
                    snprintf(bufs[n], PATH_BUF_LEN, "%s/libmpv.dylib", p); { candidates[n] = bufs[n]; n++; }
#else
                    snprintf(bufs[n], PATH_BUF_LEN, "%s/libmpv.so.2", p); { candidates[n] = bufs[n]; n++; }
                    snprintf(bufs[n], PATH_BUF_LEN, "%s/libmpv.so", p); { candidates[n] = bufs[n]; n++; }
#endif
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 4. 默认库名 */
#ifdef _WIN32
    candidates[n++] = "mpv-2.dll";
#elif defined(__APPLE__)
    candidates[n++] = "libmpv.2.dylib";
    candidates[n++] = "libmpv.dylib";
#else
    candidates[n++] = "libmpv.so.2";
    candidates[n++] = "libmpv.so";
#endif

    /* 5. Nix 路径 */
#ifndef _WIN32
    const char *home = getenv("HOME");
    if (home && *home && n < MAX_CANDIDATES - 2) {
#ifdef __APPLE__
        snprintf(bufs[n], PATH_BUF_LEN, "%s/.nix-profile/lib/libmpv.2.dylib", home); { candidates[n] = bufs[n]; n++; }
#else
        snprintf(bufs[n], PATH_BUF_LEN, "%s/.nix-profile/lib/libmpv.so.2", home); { candidates[n] = bufs[n]; n++; }
#endif
    }
    if (n < MAX_CANDIDATES - 2) {
#ifdef __APPLE__
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libmpv.2.dylib";
#else
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libmpv.so.2";
#endif
    }
#endif

    /* 6. 平台默认路径 */
#ifdef __APPLE__
    candidates[n++] = "/opt/local/lib/libmpv.2.dylib";
    candidates[n++] = "/usr/local/lib/libmpv.2.dylib";
    candidates[n++] = "/opt/homebrew/lib/libmpv.2.dylib";
#elif defined(__linux__) || defined(__ANDROID__)
    candidates[n++] = "/usr/local/lib/libmpv.so.2";
    candidates[n++] = "/usr/lib/x86_64-linux-gnu/libmpv.so.2";
    candidates[n++] = "/usr/lib/libmpv.so.2";
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    candidates[n++] = "/usr/local/lib/libmpv.so.2";
    candidates[n++] = "/usr/lib/libmpv.so.2";
#endif
    candidates[n] = NULL;

    for (int i = 0; i < n; i++) {
        mpv_handle_lib = load_lib(candidates[i]);
        if (mpv_handle_lib) break;
    }
    if (!mpv_handle_lib) {
        fprintf(stderr, "mpv_dyn: cannot load libmpv (%s)\n", lib_error());
        return -1;
    }

    /* 验证基础符号 */
    unsigned long (*fn_api_version)(void) = NULL;
    *(void **)(&fn_api_version) = get_sym(mpv_handle_lib, "mpv_client_api_version");
    if (!fn_api_version) {
        fprintf(stderr, "mpv_dyn: loaded library does not export mpv_client_api_version\n");
        free_lib(mpv_handle_lib);
        mpv_handle_lib = NULL;
        return -1;
    }
    loaded_api_version = fn_api_version();
    fprintf(stderr, "mpv_dyn: loaded libmpv API version %lu.%lu\n",
            (loaded_api_version >> 16) & 0xFFFF, loaded_api_version & 0xFFFF);
    return 0;
}

/* ============================================================
 * 一次性初始化
 * ============================================================ */
int mpv_libok(void) {
    if (init_state == 0) {
        init_state = (do_load(NULL) == 0) ? 1 : -1;
    }
    return (init_state == 1 && mpv_handle_lib != NULL) ? 1 : 0;
}

int mpv_dyn_init(const char *library_path) {
    if (init_state == 1) return 0;
    if (init_state == -1) return -1;
    if (do_load(library_path) == 0) {
        init_state = 1;
        return 0;
    }
    init_state = -1;
    return -1;
}

void mpv_dyn_cleanup(void) {
    if (mpv_handle_lib) {
        free_lib(mpv_handle_lib);
        mpv_handle_lib = NULL;
    }
    init_state = 0;
    loaded_api_version = 0;
}

unsigned long mpv_dyn_api_version(void) {
    return loaded_api_version;
}

/* ============================================================
 * 按需解析宏
 * ============================================================ */
#define RESOLVE(var, name, fallback) do {                      \
    if (!(var)) {                                              \
        if (!mpv_libok()) return fallback;                     \
        *(void **)(&(var)) = get_sym(mpv_handle_lib, name);    \
        if (!(var)) {                                          \
            fprintf(stderr, "dlsym failed: %s\n", name);       \
            return fallback;                                   \
        }                                                      \
    }                                                          \
} while (0)

/* ============================================================
 * 封装函数：与原版 libmpv C API 同名
 * ============================================================ */
mpv_handle *mpv_create(void) {
    static mpv_handle *(*fn)(void) = NULL;
    if (!fn) {
        if (!mpv_libok()) return NULL;
        *(void **)(&fn) = get_sym(mpv_handle_lib, "mpv_create");
        if (!fn) return NULL;
    }
    return fn();
}

int mpv_initialize(mpv_handle *ctx) {
    static int (*fn)(mpv_handle*) = NULL;
    RESOLVE(fn, "mpv_initialize", -1);
    return fn(ctx);
}

void mpv_terminate_destroy(mpv_handle *ctx) {
    static void (*fn)(mpv_handle*) = NULL;
    if (!fn) {
        if (!mpv_libok()) return;
        *(void **)(&fn) = get_sym(mpv_handle_lib, "mpv_terminate_destroy");
        if (!fn) return;
    }
    fn(ctx);
}

int mpv_command(mpv_handle *ctx, const char **args) {
    static int (*fn)(mpv_handle*, const char**) = NULL;
    RESOLVE(fn, "mpv_command", -1);
    return fn(ctx, args);
}

int mpv_set_option_string(mpv_handle *ctx, const char *name, const char *data) {
    static int (*fn)(mpv_handle*, const char*, const char*) = NULL;
    RESOLVE(fn, "mpv_set_option_string", -1);
    return fn(ctx, name, data);
}

int mpv_set_property_string(mpv_handle *ctx, const char *name, const char *data) {
    static int (*fn)(mpv_handle*, const char*, const char*) = NULL;
    RESOLVE(fn, "mpv_set_property_string", -1);
    return fn(ctx, name, data);
}

char *mpv_get_property_string(mpv_handle *ctx, const char *name) {
    static char *(*fn)(mpv_handle*, const char*) = NULL;
    if (!fn) {
        if (!mpv_libok()) return NULL;
        *(void **)(&fn) = get_sym(mpv_handle_lib, "mpv_get_property_string");
        if (!fn) return NULL;
    }
    return fn(ctx, name);
}

void mpv_free(void *data) {
    static void (*fn)(void*) = NULL;
    if (!fn) {
        if (!mpv_libok()) return;
        *(void **)(&fn) = get_sym(mpv_handle_lib, "mpv_free");
        if (!fn) return;
    }
    fn(data);
}

mpv_event *mpv_wait_event(mpv_handle *ctx, double timeout) {
    static mpv_event *(*fn)(mpv_handle*, double) = NULL;
    if (!fn) {
        if (!mpv_libok()) return NULL;
        *(void **)(&fn) = get_sym(mpv_handle_lib, "mpv_wait_event");
        if (!fn) return NULL;
    }
    return fn(ctx, timeout);
}

const char *mpv_error_string(int error) {
    static const char *(*fn)(int) = NULL;
    if (!fn) {
        if (!mpv_libok()) return "mpv not initialized";
        *(void **)(&fn) = get_sym(mpv_handle_lib, "mpv_error_string");
        if (!fn) return "mpv_error_string not resolved";
    }
    return fn(error);
}
