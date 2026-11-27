/* ============================================================================
 * reproc_dyn.c — reproc 动态加载封装层
 *
 * 设计目标与约束（与 curl_dyn.c / md4c_dyn.c 一致）：
 *   1. 链接期绝不出现 -lreproc；测试程序只链 -ldl。
 *   2. 无自定义 .h 文件，无独立 wrapper .c。
 *   3. 每个封装函数首次调用时才 dlsym 自己的符号并缓存。
 *   4. 封装函数名与上游 reproc 一致（reproc_new / reproc_start 等）。
 *   5. __has_include 命中时 include 上游头文件，由编译器校验函数原型与类型；
 *      未安装头文件时回退到 v14.2.7 内嵌类型（字段顺序即 ABI）。
 *   6. 跨平台：Unix/macOS 用 dlopen/dlsym；Windows 用 LoadLibrary/GetProcAddress。
 *   7. 库搜索顺序：显式路径 > REPROC_DYN_LIB > 系统搜索路径
 *      > 默认库名 > Nix 路径 > 平台默认路径。
 *   8. 线程安全：首次调用 reproc_libok() 或任一封装函数时完成加载，
 *      多线程并发首次调用需外部保证或由主线程提前触发。
 *
 * 与 curl 方案的差异：
 *   - reproc 错误码是 extern const int 数据符号，不作为函数封装；
 *     加载失败时本封装用本地 REPROC_DYN_EINVAL 作为错误返回值，
 *     避免链接期产生对目标库数据符号的引用。
 * ========================================================================== */

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---------- 平台适配层 ---------- */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE reproc_lib_handle_t;
  static reproc_lib_handle_t reproc_load_lib(const char *path) { return LoadLibraryA(path); }
  static void *reproc_get_sym(reproc_lib_handle_t h, const char *name) {
      return (void *)GetProcAddress(h, name);
  }
  static void reproc_free_lib(reproc_lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *reproc_lib_error(void) { return "LoadLibrary failed"; }
  #define REPROC_DEFAULT_LIB_NAME "reproc.dll"
  #define REPROC_PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *reproc_lib_handle_t;
  static reproc_lib_handle_t reproc_load_lib(const char *path) {
      int flags = RTLD_LAZY | RTLD_LOCAL;
  #ifdef RTLD_DEEPBIND
      flags |= RTLD_DEEPBIND;
  #endif
      return dlopen(path, flags);
  }
  static void *reproc_get_sym(reproc_lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void reproc_free_lib(reproc_lib_handle_t h) { if (h) dlclose(h); }
  static const char *reproc_lib_error(void) { return dlerror(); }
  #ifdef __APPLE__
    #define REPROC_DEFAULT_LIB_NAME "libreproc.dylib"
  #else
    #define REPROC_DEFAULT_LIB_NAME "libreproc.so"
  #endif
  #define REPROC_PATH_SEP ':'
#endif

/* ---------- 上游头文件：存在时引入，用于原型/类型匹配检查 ----------
 * 本封装自己实现同名函数并全程 dlsym，不需要 dllimport/dllexport，
 * 所以先置空 REPROC_EXPORT，再让头文件参与声明检查。
 * 不引用 REPROC_EINVAL 等 extern const int 数据符号，链接期不依赖目标库。
 */
#ifndef REPROC_EXPORT
#define REPROC_EXPORT
#endif

#if defined(__has_include)
#  if __has_include(<reproc/reproc.h>)
#    include <reproc/reproc.h>
#    include <reproc/run.h>
#    include <reproc/drain.h>
#    define REPROC_DYN_HAVE_HEADERS 1
#  endif
#endif

/* ---------- 无上游头文件时：reproc v14.2.7 公开类型（原样内嵌） ---------- */
#ifndef REPROC_DYN_HAVE_HEADERS
typedef struct reproc_t reproc_t;

typedef enum {
  REPROC_STREAM_IN,
  REPROC_STREAM_OUT,
  REPROC_STREAM_ERR,
} REPROC_STREAM;

typedef enum {
  REPROC_REDIRECT_DEFAULT,
  REPROC_REDIRECT_PIPE,
  REPROC_REDIRECT_PARENT,
  REPROC_REDIRECT_DISCARD,
  REPROC_REDIRECT_STDOUT,
  REPROC_REDIRECT_HANDLE,
  REPROC_REDIRECT_FILE,
  REPROC_REDIRECT_PATH,
} REPROC_REDIRECT;

typedef enum {
  REPROC_STOP_NOOP,
  REPROC_STOP_WAIT,
  REPROC_STOP_TERMINATE,
  REPROC_STOP_KILL,
} REPROC_STOP;

typedef struct reproc_stop_action {
  REPROC_STOP action;
  int timeout;
} reproc_stop_action;

typedef struct reproc_stop_actions {
  reproc_stop_action first;
  reproc_stop_action second;
  reproc_stop_action third;
} reproc_stop_actions;

#if defined(_WIN32)
typedef void *reproc_handle;
#else
typedef int reproc_handle;
#endif

typedef struct reproc_redirect {
  REPROC_REDIRECT type;
  reproc_handle handle;
  FILE *file;
  const char *path;
} reproc_redirect;

typedef enum {
  REPROC_ENV_EXTEND,
  REPROC_ENV_EMPTY,
} REPROC_ENV;

typedef struct reproc_options {
  const char *working_directory;
  struct {
    REPROC_ENV behavior;
    const char *const *extra;
  } env;
  struct {
    reproc_redirect in;
    reproc_redirect out;
    reproc_redirect err;
    bool parent;
    bool discard;
    FILE *file;
    const char *path;
  } redirect;
  reproc_stop_actions stop;
  int deadline;
  struct {
    const uint8_t *data;
    size_t size;
  } input;
  bool fork;
  bool nonblocking;
} reproc_options;

enum {
  REPROC_EVENT_IN       = 1 << 0,
  REPROC_EVENT_OUT      = 1 << 1,
  REPROC_EVENT_ERR      = 1 << 2,
  REPROC_EVENT_EXIT     = 1 << 3,
  REPROC_EVENT_DEADLINE = 1 << 4,
};

typedef struct reproc_event_source {
  reproc_t *process;
  int interests;
  int events;
} reproc_event_source;

typedef struct reproc_sink {
  int (*function)(REPROC_STREAM stream, const uint8_t *buffer,
                  size_t size, void *context);
  void *context;
} reproc_sink;
#endif /* REPROC_DYN_HAVE_HEADERS */

/* 上游错误码是 extern const int 数据符号；这里只保留失败回退值，
 * 不声明也不引用那些符号，确保链接期不依赖 libreproc。 */
enum { REPROC_DYN_EINVAL = -22 };

/* ---------- 全局状态 ---------- */
static reproc_lib_handle_t reproc_lib = NULL;
static int reproc_init_state = 0;   /* 0=未加载, 1=成功, -1=失败 */

#define REPROC_MAX_CANDIDATES 64
#define REPROC_PATH_BUF_LEN   1024

/* ---------- 加载：按优先级构造候选路径 ---------- */
static int reproc_do_load(const char *library_path) {
    if (reproc_lib) return 0;

    static char bufs[REPROC_MAX_CANDIDATES][REPROC_PATH_BUF_LEN];
    const char *candidates[REPROC_MAX_CANDIDATES];
    int n = 0;

    if (library_path && *library_path && n < REPROC_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], REPROC_PATH_BUF_LEN, "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }

    const char *explicit_lib = getenv("REPROC_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < REPROC_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], REPROC_PATH_BUF_LEN, "%s", explicit_lib);
        candidates[n] = bufs[n]; n++;
    }

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
            while (p && *p && n < REPROC_MAX_CANDIDATES - 2) {
                char *sep = strchr(p, REPROC_PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
                    snprintf(bufs[n], REPROC_PATH_BUF_LEN, "%s/%s", p, REPROC_DEFAULT_LIB_NAME);
                    candidates[n] = bufs[n]; n++;
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    candidates[n++] = REPROC_DEFAULT_LIB_NAME;
#if defined(__linux__) || defined(__ANDROID__)
    candidates[n++] = "libreproc.so.14";   /* versioned soname */
#endif

    const char *home = getenv("HOME");
    if (home && *home && n < REPROC_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], REPROC_PATH_BUF_LEN, "%s/.nix-profile/lib/%s",
                 home, REPROC_DEFAULT_LIB_NAME);
        candidates[n] = bufs[n]; n++;
    }
    if (n < REPROC_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], REPROC_PATH_BUF_LEN, "/nix/var/nix/profiles/default/lib/%s",
                 REPROC_DEFAULT_LIB_NAME);
        candidates[n] = bufs[n]; n++;
    }

#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libreproc.dylib";
    candidates[n++] = "/usr/local/lib/libreproc.dylib";
    candidates[n++] = "/opt/local/lib/libreproc.dylib";
#elif defined(__linux__) || defined(__ANDROID__)
    candidates[n++] = "/usr/local/lib/libreproc.so";
    candidates[n++] = "/usr/lib/x86_64-linux-gnu/libreproc.so";
    candidates[n++] = "/usr/lib/libreproc.so";
#endif
    candidates[n] = NULL;

    int i;
    for (i = 0; i < n; i++) {
        reproc_lib = reproc_load_lib(candidates[i]);
        if (reproc_lib) break;
    }
    if (!reproc_lib) {
        fprintf(stderr, "reproc_dyn: cannot load libreproc (%s)\n", reproc_lib_error());
        return -1;
    }
    if (!reproc_get_sym(reproc_lib, "reproc_new")) {
        fprintf(stderr, "reproc_dyn: loaded library does not export reproc_new\n");
        reproc_free_lib(reproc_lib);
        reproc_lib = NULL;
        return -1;
    }
    fprintf(stderr, "reproc_dyn: loaded %d %s\n", i, candidates[i]);
    return 0;
}

/* ---------- 一次性初始化 ---------- */
int reproc_libok(void) {
    if (reproc_init_state == 0) {
        if (reproc_do_load(NULL) != 0) {
            reproc_init_state = -1;
            return 0;
        }
        reproc_init_state = 1;
    }
    return (reproc_init_state == 1 && reproc_lib != NULL) ? 1 : 0;
}

int reproc_dyn_init(const char *library_path) {
    if (reproc_init_state == 1) return 0;
    if (reproc_init_state == -1) return -1;
    if (reproc_do_load(library_path) != 0) {
        reproc_init_state = -1;
        return -1;
    }
    reproc_init_state = 1;
    return 0;
}

void reproc_dyn_cleanup(void) {
    if (reproc_lib) {
        reproc_free_lib(reproc_lib);
        reproc_lib = NULL;
    }
    reproc_init_state = 0;
}

int reproc_dyn_has_symbol(const char *name) {
    if (!reproc_libok()) return 0;
    return reproc_get_sym(reproc_lib, name) != NULL;
}

/* ---------- 按需解析宏 ---------- */
#define REPROC_RESOLVE(var, name, fallback) do {                    \
    if (!(var)) {                                            \
        if (!reproc_libok()) return fallback;                \
        *(void **)(&(var)) = reproc_get_sym(reproc_lib, name);      \
        if (!(var)) {                                        \
            fprintf(stderr, "dlsym failed: %s\n", name);     \
            return fallback;                                 \
        }                                                    \
    }                                                        \
} while (0)

/* ---------- 封装函数：与上游 reproc 同名（真实头文件存在时编译器校验签名） ---------- */
reproc_t *reproc_new(void) {
    static reproc_t *(*fn)(void) = NULL;
    REPROC_RESOLVE(fn, "reproc_new", NULL);
    return fn();
}

int reproc_start(reproc_t *process, const char *const *argv,
                 reproc_options options) {
    static int (*fn)(reproc_t *, const char *const *, reproc_options) = NULL;
    REPROC_RESOLVE(fn, "reproc_start", REPROC_DYN_EINVAL);
    return fn(process, argv, options);
}

int reproc_pid(reproc_t *process) {
    static int (*fn)(reproc_t *) = NULL;
    REPROC_RESOLVE(fn, "reproc_pid", REPROC_DYN_EINVAL);
    return fn(process);
}

int reproc_poll(reproc_event_source *sources, size_t num_sources, int timeout) {
    static int (*fn)(reproc_event_source *, size_t, int) = NULL;
    REPROC_RESOLVE(fn, "reproc_poll", REPROC_DYN_EINVAL);
    return fn(sources, num_sources, timeout);
}

int reproc_read(reproc_t *process, REPROC_STREAM stream,
                uint8_t *buffer, size_t size) {
    static int (*fn)(reproc_t *, REPROC_STREAM, uint8_t *, size_t) = NULL;
    REPROC_RESOLVE(fn, "reproc_read", REPROC_DYN_EINVAL);
    return fn(process, stream, buffer, size);
}

int reproc_write(reproc_t *process, const uint8_t *buffer, size_t size) {
    static int (*fn)(reproc_t *, const uint8_t *, size_t) = NULL;
    REPROC_RESOLVE(fn, "reproc_write", REPROC_DYN_EINVAL);
    return fn(process, buffer, size);
}

int reproc_close(reproc_t *process, REPROC_STREAM stream) {
    static int (*fn)(reproc_t *, REPROC_STREAM) = NULL;
    REPROC_RESOLVE(fn, "reproc_close", REPROC_DYN_EINVAL);
    return fn(process, stream);
}

int reproc_wait(reproc_t *process, int timeout) {
    static int (*fn)(reproc_t *, int) = NULL;
    REPROC_RESOLVE(fn, "reproc_wait", REPROC_DYN_EINVAL);
    return fn(process, timeout);
}

int reproc_terminate(reproc_t *process) {
    static int (*fn)(reproc_t *) = NULL;
    REPROC_RESOLVE(fn, "reproc_terminate", REPROC_DYN_EINVAL);
    return fn(process);
}

int reproc_kill(reproc_t *process) {
    static int (*fn)(reproc_t *) = NULL;
    REPROC_RESOLVE(fn, "reproc_kill", REPROC_DYN_EINVAL);
    return fn(process);
}

int reproc_stop(reproc_t *process, reproc_stop_actions stop) {
    static int (*fn)(reproc_t *, reproc_stop_actions) = NULL;
    REPROC_RESOLVE(fn, "reproc_stop", REPROC_DYN_EINVAL);
    return fn(process, stop);
}

reproc_t *reproc_destroy(reproc_t *process) {
    static reproc_t *(*fn)(reproc_t *) = NULL;
    REPROC_RESOLVE(fn, "reproc_destroy", NULL);
    return fn(process);
}

const char *reproc_strerror(int error) {
    static const char *(*fn)(int) = NULL;
    REPROC_RESOLVE(fn, "reproc_strerror", "reproc not initialized");
    return fn(error);
}

int reproc_run(const char *const *argv, reproc_options options) {
    static int (*fn)(const char *const *, reproc_options) = NULL;
    REPROC_RESOLVE(fn, "reproc_run", REPROC_DYN_EINVAL);
    return fn(argv, options);
}

int reproc_run_ex(const char *const *argv, reproc_options options,
                  reproc_sink out, reproc_sink err) {
    static int (*fn)(const char *const *, reproc_options,
                     reproc_sink, reproc_sink) = NULL;
    REPROC_RESOLVE(fn, "reproc_run_ex", REPROC_DYN_EINVAL);
    return fn(argv, options, out, err);
}

int reproc_drain(reproc_t *process, reproc_sink out, reproc_sink err) {
    static int (*fn)(reproc_t *, reproc_sink, reproc_sink) = NULL;
    REPROC_RESOLVE(fn, "reproc_drain", REPROC_DYN_EINVAL);
    return fn(process, out, err);
}

reproc_sink reproc_sink_string(char **output) {
    static reproc_sink (*fn)(char **) = NULL;
    reproc_sink empty = { NULL, NULL };
    REPROC_RESOLVE(fn, "reproc_sink_string", empty);
    return fn(output);
}

reproc_sink reproc_sink_discard(void) {
    static reproc_sink (*fn)(void) = NULL;
    reproc_sink empty = { NULL, NULL };
    REPROC_RESOLVE(fn, "reproc_sink_discard", empty);
    return fn();
}

void *reproc_free(void *ptr) {
    static void *(*fn)(void *) = NULL;
    if (!fn) {
        if (!reproc_libok()) return NULL;
        *(void **)(&fn) = reproc_get_sym(reproc_lib, "reproc_free");
        if (!fn) return NULL;
    }
    return fn(ptr);
}
