/* ============================================================================
 * python_dyn.c — libpython 动态加载封装层（Python 2.7 / 3.x 双版本）
 *
 * 设计约束（与 mpv_dyn.c / rclone_dyn.c / sqlite3_dyn.c 等一致）：
 *   1. 链接期绝不出现 -lpython。
 *   2. 无自定义 .h 文件，无独立 wrapper .c。
 *   3. 每个封装函数首次调用时才 dlsym 自己的符号并缓存。
 *   4. 封装函数名与原版 Python C API 一致（版本专属函数按版本命名）。
 *   5. 编译期不需要 <Python.h>；PyObject 等类型在本文件内不透明定义。
 *   6. 跨平台：Unix/macOS 用 dlopen/dlsym；Windows 用 LoadLibrary/GetProcAddress。
 *   7. 搜索顺序：显式路径 > PYTHON_DYN_LIB > 系统搜索路径 > 默认库名
 *      > Nix / 平台默认路径 / Framework / Termux。
 *   8. 线程安全：首次调用 python_libok() 或任一封装函数时完成加载。
 *      一次性加载本身未加锁，建议主线程在启动阶段先调用一次
 *      python_libok()/python_dyn_init()；加载完成后的并发调用安全。
 *
 * 版本策略：
 *   - 运行时通过 Py_GetVersion() 解析主/次版本号。
 *   - 默认优先 Python 3（3.15 向下到 3.6），找不到再回退 Python 2.7。
 *   - 可用 PYTHON_DYN_VER=2.7 / 3 / 3.12 指定版本，
 *     或调用 python_dyn_init_version(major, minor)。
 *   - Python 2.7 的 Unicode API 在头文件里被宏映射为
 *     PyUnicodeUCS2_* 或 PyUnicodeUCS4_*；本文件加载后探测实际前缀。
 *
 * 重要限制：
 *   - 只能封装“导出函数”，不能封装宏。Py_INCREF / Py_DECREF / Py_TYPE /
 *     Py_REFCNT / PyList_GET_ITEM 等没有符号；请使用 Py_IncRef / Py_DecRef /
 *     PyObject_Type / PyList_GetItem 等等价函数。
 *   - C 可变参数函数无法在封装层透明转发（没有 va_list 变体），因此
 *     PyObject_CallMethod、Py_BuildValue、PyTuple_Pack 等不在此文件封装。
 *   - PyObject 在本文件中是不透明类型，禁止解引用。
 *   - 同一个编译单元里不要同时包含 <Python.h> 和本文件的声明：
 *     Py_ssize_t / PyObject 等 typedef 以及 Py_INCREF 等宏会冲突。
 *     业务侧只需声明自己用到的原型，或把调用代码放到独立 TU。
 *   - Unix/macOS 使用 RTLD_GLOBAL 加载 libpython：Python 的共享扩展
 *     （_ssl.so、zlib.so 等）通常不链接 libpython，依赖全局符号解析
 *     Py* API。这一点与 mpv_dyn.c 的 RTLD_LOCAL 不同。
 *   - 引用计数语义完全遵循 CPython：新引用/借用引用/偷引用不额外改变。
 *   - 除 PyGILState_* 外，绝大多数 API 要求调用方持有 GIL。
 *     Python 2.7 必须先调用 PyEval_InitThreads() 才能释放/跨线程使用 GIL。
 *   - Py_Initialize 成功后不要卸载 libpython（dlclose/FreeLibrary 不安全）；
 *     python_dyn_cleanup() 只允许在加载失败后复位以便重试。
 *   - 2.7 的 Py_SetProgramName/PySys_SetArgv 使用 char*，3.x 使用 wchar_t*，
 *     因此不做同名封装；建议用 PYTHONHOME/PYTHONPATH 环境变量。
 *   - Windows 上 python3.dll 只暴露 Stable ABI；FILE* 相关函数存在跨 CRT 风险。
 *   - 3.13+ 自由线程构建库名带 t（libpython3.13t / python313t.dll），
 *     封装层只原样转发，不承诺其线程模型。
 *
 * 典型用法：
 *
 *   if (python_dyn_init(NULL) != 0) { ... }
 *   Py_Initialize();
 *   PyRun_SimpleString("print('hello')");
 *   Py_Finalize();
 *   python_dyn_cleanup();
 *
 * 编译：链接期只需要 -ldl（Linux/BSD），不要链接 -lpython。
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>

/* ============================================================
 * 不透明类型与常量（不包含 Python.h）
 * ============================================================ */
typedef struct _object PyObject;
typedef struct _ts PyThreadState;
typedef ptrdiff_t Py_ssize_t;
typedef Py_ssize_t Py_hash_t;
typedef int PyGILState_STATE;

#define PyGILState_LOCKED   0
#define PyGILState_UNLOCKED 1

/* PyRun_String 的 start 参数 */
#define Py_single_input 256
#define Py_file_input   257
#define Py_eval_input   258

/* PyObject_RichCompare 的比较操作 */
#define Py_LT 0
#define Py_LE 1
#define Py_EQ 2
#define Py_NE 3
#define Py_GT 4
#define Py_GE 5

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
       * libpython 会继续 dlopen 共享扩展模块（_ssl.so、zlib.so 等），
       * 这些模块通常不链接 libpython，而是依赖全局符号解析 Py* API。
       * 因此这里必须用 RTLD_GLOBAL，不能照搬 mpv 的 RTLD_LOCAL。
       * 也不使用 RTLD_DEEPBIND：Python 运行时需要正常的全局符号查找。
       */
      int flags = RTLD_LAZY | RTLD_GLOBAL;
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
static lib_handle_t py_handle_lib = NULL;
static int init_state = 0;          /* 0=未初始化, 1=已加载, -1=加载失败 */
static int py_major = 0;
static int py_minor = 0;
static char py_version_str[256];
static int py_unicode_kind = 0;     /* 2=UCS2, 4=UCS4, 1=普通符号, 0=非2/未知 */
static int want_major = 0;          /* 0=自动（优先3）, 2, 3 */
static int want_minor = -1;         /* -1=最新 */
static int want_loaded = 0;

#define MAX_CANDIDATES 384
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

static char *dup_n(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ============================================================
 * 版本选择
 * ============================================================ */
static void load_want_from_env(void) {
    const char *v;
    int maj = 0, min = -1;
    if (want_loaded) return;
    want_loaded = 1;
    v = getenv("PYTHON_DYN_VER");
    if (!v || !*v) return;
    if (sscanf(v, "%d.%d", &maj, &min) < 1) return;
    if (maj != 2 && maj != 3) return;
    want_major = maj;
    want_minor = (min >= 0) ? min : -1;
}

/* 生成默认的 Python 3 次版本候选（裸库名） */
static int push_py3_bare(const char **candidates,
                         char bufs[][PATH_BUF_LEN], int n) {
    int lo = (want_minor >= 0) ? want_minor : 6;
    int hi = (want_minor >= 0) ? want_minor : 15;
    int v;
    for (v = hi; v >= lo; --v) {
#ifdef _WIN32
        n = push_candidate_fmt(candidates, bufs, n, "python3%d.dll", v);
        if (v >= 13)
            n = push_candidate_fmt(candidates, bufs, n, "python3%dt.dll", v);
#elif defined(__APPLE__)
        n = push_candidate_fmt(candidates, bufs, n, "libpython3.%d.dylib", v);
        if (v <= 7)
            n = push_candidate_fmt(candidates, bufs, n,
                                   "libpython3.%dm.dylib", v);
        if (v >= 13)
            n = push_candidate_fmt(candidates, bufs, n, "libpython3.%dt.dylib", v);
#else
        n = push_candidate_fmt(candidates, bufs, n, "libpython3.%d.so.1.0", v);
        n = push_candidate_fmt(candidates, bufs, n, "libpython3.%d.so", v);
        if (v <= 7) {
            n = push_candidate_fmt(candidates, bufs, n,
                                   "libpython3.%dm.so.1.0", v);
            n = push_candidate_fmt(candidates, bufs, n,
                                   "libpython3.%dm.so", v);
        }
        if (v >= 13)
            n = push_candidate_fmt(candidates, bufs, n, "libpython3.%dt.so.1.0", v);
#endif
    }
    return n;
}

static int push_py2_bare(const char **candidates,
                         char bufs[][PATH_BUF_LEN], int n) {
    if (want_minor >= 0 && want_minor != 7) return n;
#ifdef _WIN32
    n = push_candidate(candidates, bufs, n, "python27.dll");
#elif defined(__APPLE__)
    n = push_candidate(candidates, bufs, n, "libpython2.7.dylib");
#else
    n = push_candidate(candidates, bufs, n, "libpython2.7.so.1.0");
    n = push_candidate(candidates, bufs, n, "libpython2.7.so");
#endif
    return n;
}

/* 某个目录下的通用库名（与版本无关） */
static int push_dir_generic(const char **candidates,
                            char bufs[][PATH_BUF_LEN], int n,
                            const char *dir) {
#ifdef _WIN32
    if (want_major == 0 || want_major == 3)
        n = push_candidate_fmt(candidates, bufs, n, "%s/python3.dll", dir);
    if (want_major == 0 || want_major == 2)
        n = push_candidate_fmt(candidates, bufs, n, "%s/python27.dll", dir);
#elif defined(__APPLE__)
    if (want_major == 0 || want_major == 3)
        n = push_candidate_fmt(candidates, bufs, n, "%s/libpython3.dylib", dir);
    if (want_major == 0 || want_major == 2)
        n = push_candidate_fmt(candidates, bufs, n, "%s/libpython2.7.dylib", dir);
#else
    if (want_major == 0 || want_major == 3)
        n = push_candidate_fmt(candidates, bufs, n, "%s/libpython3.so", dir);
    if (want_major == 0 || want_major == 2)
        n = push_candidate_fmt(candidates, bufs, n, "%s/libpython2.7.so", dir);
#endif
    return n;
}

/* 某个目录下的版本化库名 */
static int push_dir_versions(const char **candidates,
                             char bufs[][PATH_BUF_LEN], int n,
                             const char *dir) {
    int lo = (want_minor >= 0) ? want_minor : 6;
    int hi = (want_minor >= 0) ? want_minor : 15;
    int v;
    if (want_major == 0 || want_major == 3) {
        for (v = hi; v >= lo; --v) {
#ifdef _WIN32
            n = push_candidate_fmt(candidates, bufs, n,
                                   "%s/python3%d.dll", dir, v);
#elif defined(__APPLE__)
            n = push_candidate_fmt(candidates, bufs, n,
                                   "%s/libpython3.%d.dylib", dir, v);
            if (v <= 7)
                n = push_candidate_fmt(candidates, bufs, n,
                                       "%s/libpython3.%dm.dylib", dir, v);
#else
            n = push_candidate_fmt(candidates, bufs, n,
                                   "%s/libpython3.%d.so.1.0", dir, v);
            n = push_candidate_fmt(candidates, bufs, n,
                                   "%s/libpython3.%d.so", dir, v);
            if (v <= 7) {
                n = push_candidate_fmt(candidates, bufs, n,
                                       "%s/libpython3.%dm.so.1.0", dir, v);
                n = push_candidate_fmt(candidates, bufs, n,
                                       "%s/libpython3.%dm.so", dir, v);
            }
#endif
        }
    }
    return n;
}

/* ============================================================
 * 版本探测
 * ============================================================ */
static void detect_unicode_kind(void) {
    py_unicode_kind = 0;
    if (py_major != 2) return;
    if (get_sym(py_handle_lib, "PyUnicodeUCS4_DecodeUTF8"))
        py_unicode_kind = 4;
    else if (get_sym(py_handle_lib, "PyUnicodeUCS2_DecodeUTF8"))
        py_unicode_kind = 2;
    else if (get_sym(py_handle_lib, "PyUnicode_DecodeUTF8"))
        py_unicode_kind = 1;
}

static int try_candidate(const char *path, int idx) {
    lib_handle_t h;
    void *sym_init;
    void *sym_version;
    const char *(*fn_version)(void) = NULL;
    const char *ver;
    int maj = 0, min = 0;

    h = load_lib(path);
    if (!h) return -1;

    sym_init = get_sym(h, "Py_Initialize");
    sym_version = get_sym(h, "Py_GetVersion");
    if (!sym_init || !sym_version) {
        free_lib(h);
        return -1;
    }

    *(void **)(&fn_version) = sym_version;
    ver = fn_version();
    if (!ver || sscanf(ver, "%d.%d", &maj, &min) < 2) {
        free_lib(h);
        return -1;
    }
    if (maj != 2 && maj != 3) {
        free_lib(h);
        return -1;
    }
    if (want_major != 0 && maj != want_major) {
        free_lib(h);
        return -1;
    }
    if (want_minor >= 0 && min != want_minor) {
        free_lib(h);
        return -1;
    }

    py_handle_lib = h;
    py_major = maj;
    py_minor = min;
    snprintf(py_version_str, sizeof(py_version_str), "%s", ver);
    detect_unicode_kind();
    fprintf(stderr, "python_dyn: loaded %d %s (Python %s)\n", idx, path, py_version_str);
    return 0;
}

/* ============================================================
 * 库加载
 * ============================================================ */
static int do_load(const char *library_path) {
    static char bufs[MAX_CANDIDATES][PATH_BUF_LEN];
    const char *candidates[MAX_CANDIDATES];
    int n = 0;
    int i;
    const char *env_path;

    if (py_handle_lib) return 0;
    load_want_from_env();

    /* 1. 显式传入的路径 */
    if (library_path && *library_path)
        n = push_candidate(candidates, bufs, n, library_path);

    /* 2. 项目专属环境变量 */
    {
        const char *explicit_lib = getenv("PYTHON_DYN_LIB");
        if (explicit_lib && *explicit_lib)
            n = push_candidate(candidates, bufs, n, explicit_lib);
    }

    /* 3. 系统搜索路径环境变量中的目录 */
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
                if (*p) {
                    n = push_dir_generic(candidates, bufs, n, p);
                    if (want_minor >= 0 && (want_major == 0 || want_major == 3))
                        n = push_dir_versions(candidates, bufs, n, p);
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 4. 裸库名（交给系统链接器搜索） */
    if (want_major == 0 || want_major == 3) {
        n = push_py3_bare(candidates, bufs, n);
        if (want_minor < 0) {
#ifdef _WIN32
            n = push_candidate(candidates, bufs, n, "python3.dll");
#elif defined(__APPLE__)
            n = push_candidate(candidates, bufs, n, "libpython3.dylib");
#else
            n = push_candidate(candidates, bufs, n, "libpython3.so");
#endif
        }
    }
    if (want_major == 0 || want_major == 2)
        n = push_py2_bare(candidates, bufs, n);

    /* 5. Nix profile */
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        char dir[PATH_BUF_LEN];
        if (home && *home) {
            snprintf(dir, sizeof(dir), "%s/.nix-profile/lib", home);
            n = push_dir_generic(candidates, bufs, n, dir);
            n = push_dir_versions(candidates, bufs, n, dir);
        }
        n = push_dir_generic(candidates, bufs, n,
                             "/nix/var/nix/profiles/default/lib");
        n = push_dir_versions(candidates, bufs, n,
                              "/nix/var/nix/profiles/default/lib");
    }
#endif

    /* 6. 平台默认路径 */
#ifdef __APPLE__
    {
        static const char *libdirs[] = {
            "/opt/local/lib",
            "/usr/local/lib",
            "/opt/homebrew/lib"
        };
        static const char *fwroots[] = {
            "/Library/Frameworks/Python.framework/Versions",
            "/opt/homebrew/Frameworks/Python.framework/Versions"
        };
        int lo = (want_minor >= 0) ? want_minor : 6;
        int hi = (want_minor >= 0) ? want_minor : 15;
        int v;

        for (i = 0; i < (int)(sizeof(libdirs) / sizeof(libdirs[0])); i++) {
            n = push_dir_generic(candidates, bufs, n, libdirs[i]);
            n = push_dir_versions(candidates, bufs, n, libdirs[i]);
        }
        if (want_major == 0 || want_major == 3) {
            for (v = hi; v >= lo; --v)
                n = push_candidate_fmt(candidates, bufs, n,
                                       "/opt/homebrew/opt/python@3.%d/lib/"
                                       "libpython3.%d.dylib", v, v);
        }
        for (i = 0; i < (int)(sizeof(fwroots) / sizeof(fwroots[0])); i++) {
            if (want_major == 0 || want_major == 3) {
                for (v = hi; v >= lo; --v)
                    n = push_candidate_fmt(candidates, bufs, n,
                                           "%s/3.%d/Python", fwroots[i], v);
                n = push_candidate_fmt(candidates, bufs, n,
                                       "%s/Current/Python", fwroots[i]);
            }
            if (want_major == 0 || want_major == 2)
                n = push_candidate_fmt(candidates, bufs, n,
                                       "%s/2.7/Python", fwroots[i]);
        }
        if (want_major == 0 || want_major == 2) {
            n = push_candidate(candidates, bufs, n,
                               "/System/Library/Frameworks/Python.framework/"
                               "Versions/2.7/Python");
            n = push_candidate(candidates, bufs, n,
                               "/System/Library/Frameworks/Python.framework/"
                               "Versions/Current/Python");
        }
    }
#elif defined(__linux__) || defined(__ANDROID__)
    {
        static const char *dirs[] = {
            "/usr/local/lib",
            "/usr/lib",
            "/usr/lib/x86_64-linux-gnu",
            "/usr/lib/aarch64-linux-gnu",
            "/usr/lib/arm-linux-gnueabihf"
        };
        for (i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); i++) {
            n = push_dir_generic(candidates, bufs, n, dirs[i]);
            n = push_dir_versions(candidates, bufs, n, dirs[i]);
        }
        {
            const char *prefix = getenv("PREFIX"); /* Termux */
            if (prefix && *prefix) {
                char dir[PATH_BUF_LEN];
                snprintf(dir, sizeof(dir), "%s/lib", prefix);
                n = push_dir_generic(candidates, bufs, n, dir);
                n = push_dir_versions(candidates, bufs, n, dir);
            }
        }
    }
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    {
        static const char *dirs[] = {
            "/usr/local/lib",
            "/usr/lib"
        };
        for (i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); i++) {
            n = push_dir_generic(candidates, bufs, n, dirs[i]);
            n = push_dir_versions(candidates, bufs, n, dirs[i]);
        }
    }
#endif
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        if (try_candidate(candidates[i], i) == 0)
            return 0;
    }

    {
        const char *err = lib_error();
        fprintf(stderr, "python_dyn: cannot load libpython (%s)\n",
                err ? err : "unknown error");
    }
    return -1;
}

/* ============================================================
 * 控制函数
 * ============================================================ */
int python_libok(void) {
    if (init_state == 0)
        init_state = (do_load(NULL) == 0) ? 1 : -1;
    return (init_state == 1 && py_handle_lib != NULL) ? 1 : 0;
}

int python_dyn_init(const char *library_path) {
    if (init_state == 1) return 0;
    if (init_state == -1) return -1;
    if (do_load(library_path) == 0) {
        init_state = 1;
        return 0;
    }
    init_state = -1;
    return -1;
}

int python_dyn_init_version(int major, int minor) {
    if (major != 2 && major != 3) return -1;
    if (major == 2 && minor >= 0 && minor != 7) return -1; /* 仅支持 2.7 */
    if (init_state == 1) {
        if (py_major == major && (minor < 0 || py_minor == minor))
            return 0;
        return -1;
    }
    if (init_state == -1) init_state = 0;
    want_major = major;
    want_minor = minor;
    want_loaded = 1;
    if (do_load(NULL) == 0) {
        init_state = 1;
        return 0;
    }
    init_state = -1;
    return -1;
}

/*
 * 注意：libpython 在 Py_Initialize 之后不适合卸载（扩展模块、线程、
 * atexit 等状态无法安全回收）。因此：
 *   - 加载成功后本函数不卸载库，句柄保留到进程退出；
 *   - 只有加载失败状态才复位为 0，允许修正版本/路径后重试。
 */
void python_dyn_cleanup(void) {
    if (init_state == -1)
        init_state = 0;
}

int python_dyn_major(void) {
    if (init_state == 0) python_libok();
    return py_major;
}

int python_dyn_minor(void) {
    if (init_state == 0) python_libok();
    return py_minor;
}

const char *python_dyn_version(void) {
    if (init_state == 0) python_libok();
    return py_version_str;
}

int python_dyn_unicode_kind(void) {
    if (init_state == 0) python_libok();
    return py_unicode_kind;
}

/* ============================================================
 * 通用按需解析宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                \
ret name params {                                                  \
    static ret (*fn) params = NULL;                                \
    if (!fn) {                                                     \
        if (!python_libok()) return fallback;                      \
        *(void **)(&fn) = get_sym(py_handle_lib, #name);           \
        if (!fn) {                                                 \
            fprintf(stderr, "python_dyn: dlsym failed: %s\n",      \
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
        if (!python_libok()) return;                               \
        *(void **)(&fn) = get_sym(py_handle_lib, #name);           \
        if (!fn) {                                                 \
            fprintf(stderr, "python_dyn: dlsym failed: %s\n",      \
                    #name);                                        \
            return;                                                \
        }                                                          \
    }                                                              \
    fn args;                                                       \
}

/* ============================================================
 * 初始化、执行、导入
 * ============================================================ */
WRAP_VOID(Py_Initialize, (void), ())
WRAP_VOID(Py_InitializeEx, (int initsigs), (initsigs))
WRAP_VOID(Py_Finalize, (void), ())
WRAP_RET(int, Py_FinalizeEx, (void), (), -1)
WRAP_RET(int, Py_IsInitialized, (void), (), 0)
WRAP_RET(const char *, Py_GetVersion, (void), (), NULL)
WRAP_RET(const char *, Py_GetPlatform, (void), (), NULL)

WRAP_RET(int, PyRun_SimpleString, (const char *s), (s), -1)
WRAP_RET(PyObject *, PyRun_String,
         (const char *s, int start, PyObject *globals, PyObject *locals),
         (s, start, globals, locals), NULL)
WRAP_RET(PyObject *, PyImport_ImportModule, (const char *name), (name), NULL)
WRAP_RET(PyObject *, PyImport_AddModule, (const char *name), (name), NULL)
WRAP_RET(PyObject *, PyImport_GetModuleDict, (void), (), NULL)

WRAP_RET(PyObject *, PyEval_GetBuiltins, (void), (), NULL)
WRAP_RET(PyObject *, PyEval_GetGlobals, (void), (), NULL)
WRAP_RET(PyObject *, PyEval_GetLocals, (void), (), NULL)

/* ============================================================
 * 对象、属性、调用
 * ============================================================ */
WRAP_RET(PyObject *, PyObject_GetAttrString,
         (PyObject *o, const char *name), (o, name), NULL)
WRAP_RET(int, PyObject_SetAttrString,
         (PyObject *o, const char *name, PyObject *v), (o, name, v), -1)
WRAP_RET(int, PyObject_HasAttrString,
         (PyObject *o, const char *name), (o, name), 0)

WRAP_RET(PyObject *, PyObject_Call,
         (PyObject *callable, PyObject *args, PyObject *kwargs),
         (callable, args, kwargs), NULL)
WRAP_RET(PyObject *, PyObject_CallObject,
         (PyObject *callable, PyObject *args), (callable, args), NULL)
WRAP_RET(PyObject *, PyObject_Str, (PyObject *o), (o), NULL)
WRAP_RET(PyObject *, PyObject_Repr, (PyObject *o), (o), NULL)
WRAP_RET(PyObject *, PyObject_GetIter, (PyObject *o), (o), NULL)
WRAP_RET(PyObject *, PyIter_Next, (PyObject *iter), (iter), NULL)

WRAP_RET(int, PyObject_IsTrue, (PyObject *o), (o), -1)
WRAP_RET(int, PyObject_Not, (PyObject *o), (o), -1)
WRAP_RET(Py_ssize_t, PyObject_Length, (PyObject *o), (o), -1)
WRAP_RET(PyObject *, PyObject_GetItem, (PyObject *o, PyObject *key),
         (o, key), NULL)
WRAP_RET(int, PyObject_SetItem,
         (PyObject *o, PyObject *key, PyObject *v), (o, key, v), -1)
WRAP_RET(int, PyObject_DelItem, (PyObject *o, PyObject *key), (o, key), -1)

WRAP_RET(PyObject *, PyObject_Type, (PyObject *o), (o), NULL)
WRAP_RET(int, PyObject_IsInstance, (PyObject *o, PyObject *cls), (o, cls), -1)
WRAP_RET(int, PyObject_IsSubclass, (PyObject *o, PyObject *cls), (o, cls), -1)
WRAP_RET(PyObject *, PyObject_RichCompare,
         (PyObject *a, PyObject *b, int op), (a, b, op), NULL)
WRAP_RET(int, PyObject_RichCompareBool,
         (PyObject *a, PyObject *b, int op), (a, b, op), -1)
WRAP_RET(PyObject *, PyObject_Dir, (PyObject *o), (o), NULL)

/* PyObject_DelAttrString 在 Python 2.7 是宏，没有导出符号 */
int PyObject_DelAttrString(PyObject *o, const char *name) {
    static int (*fn)(PyObject *, const char *) = NULL;
    if (!fn) {
        if (!python_libok()) return -1;
        *(void **)(&fn) = get_sym(py_handle_lib, "PyObject_DelAttrString");
    }
    if (fn) return fn(o, name);
    return PyObject_SetAttrString(o, name, NULL);
}

/* PyObject_Hash 在 2.7 返回 long，3.x 返回 Py_hash_t */
Py_ssize_t PyObject_Hash(PyObject *o) {
    if (!python_libok()) return -1;
    if (py_major == 2) {
        static long (*fn2)(PyObject *) = NULL;
        if (!fn2) {
            if (!python_libok()) return -1;
            *(void **)(&fn2) = get_sym(py_handle_lib, "PyObject_Hash");
            if (!fn2) {
                fprintf(stderr, "python_dyn: dlsym failed: PyObject_Hash\n");
                return -1;
            }
        }
        return (Py_ssize_t)fn2(o);
    } else {
        static Py_ssize_t (*fn3)(PyObject *) = NULL;
        if (!fn3) {
            if (!python_libok()) return -1;
            *(void **)(&fn3) = get_sym(py_handle_lib, "PyObject_Hash");
            if (!fn3) {
                fprintf(stderr, "python_dyn: dlsym failed: PyObject_Hash\n");
                return -1;
            }
        }
        return fn3(o);
    }
}

/* ============================================================
 * 数值
 * ============================================================ */
WRAP_RET(PyObject *, PyLong_FromLong, (long v), (v), NULL)
WRAP_RET(long, PyLong_AsLong, (PyObject *o), (o), -1)
WRAP_RET(PyObject *, PyLong_FromLongLong, (long long v), (v), NULL)
WRAP_RET(long long, PyLong_AsLongLong, (PyObject *o), (o), -1)
WRAP_RET(PyObject *, PyLong_FromString,
         (const char *s, char **end, int base), (s, end, base), NULL)
WRAP_RET(PyObject *, PyFloat_FromDouble, (double v), (v), NULL)
WRAP_RET(double, PyFloat_AsDouble, (PyObject *o), (o), -1.0)
WRAP_RET(PyObject *, PyBool_FromLong, (long v), (v), NULL)

/* ============================================================
 * 字符串：Python 2.7 专属
 * ============================================================ */
WRAP_RET(PyObject *, PyString_FromString, (const char *s), (s), NULL)
WRAP_RET(PyObject *, PyString_FromStringAndSize,
         (const char *s, Py_ssize_t n), (s, n), NULL)
WRAP_RET(char *, PyString_AsString, (PyObject *o), (o), NULL)
WRAP_RET(int, PyString_AsStringAndSize,
         (PyObject *o, char **buf, Py_ssize_t *len), (o, buf, len), -1)
WRAP_RET(Py_ssize_t, PyString_Size, (PyObject *o), (o), -1)
WRAP_RET(PyObject *, PyInt_FromLong, (long v), (v), NULL)
WRAP_RET(long, PyInt_AsLong, (PyObject *o), (o), -1)

/* ============================================================
 * 字符串：Python 3.x 专属
 * ============================================================ */
WRAP_RET(PyObject *, PyBytes_FromString, (const char *s), (s), NULL)
WRAP_RET(PyObject *, PyBytes_FromStringAndSize,
         (const char *s, Py_ssize_t n), (s, n), NULL)
WRAP_RET(char *, PyBytes_AsString, (PyObject *o), (o), NULL)
WRAP_RET(int, PyBytes_AsStringAndSize,
         (PyObject *o, char **buf, Py_ssize_t *len), (o, buf, len), -1)
WRAP_RET(Py_ssize_t, PyBytes_Size, (PyObject *o), (o), -1)

/* ============================================================
 * Unicode：2.7 需要 UCS2_/UCS4_ 前缀
 * ============================================================ */
static void *get_unicode_sym(const char *base) {
    char name[128];
    if (py_major == 2) {
        if (py_unicode_kind == 4)
            snprintf(name, sizeof(name), "PyUnicodeUCS4_%s", base);
        else if (py_unicode_kind == 2)
            snprintf(name, sizeof(name), "PyUnicodeUCS2_%s", base);
        else
            snprintf(name, sizeof(name), "PyUnicode_%s", base);
    } else {
        snprintf(name, sizeof(name), "PyUnicode_%s", base);
    }
    return get_sym(py_handle_lib, name);
}

#define WRAP_UNICODE_RET(ret, name, base, params, args, fallback)  \
ret name params {                                                  \
    static ret (*fn) params = NULL;                                \
    if (!fn) {                                                     \
        if (!python_libok()) return fallback;                      \
        *(void **)(&fn) = get_unicode_sym(base);                   \
        if (!fn) {                                                 \
            fprintf(stderr, "python_dyn: dlsym failed: %s\n",      \
                    #name);                                        \
            return fallback;                                       \
        }                                                          \
    }                                                              \
    return fn args;                                                \
}

WRAP_UNICODE_RET(PyObject *, PyUnicode_FromString, "FromString",
                 (const char *s), (s), NULL)
WRAP_UNICODE_RET(PyObject *, PyUnicode_FromStringAndSize, "FromStringAndSize",
                 (const char *s, Py_ssize_t n), (s, n), NULL)
WRAP_UNICODE_RET(PyObject *, PyUnicode_DecodeUTF8, "DecodeUTF8",
                 (const char *s, Py_ssize_t n, const char *errors),
                 (s, n, errors), NULL)
WRAP_UNICODE_RET(PyObject *, PyUnicode_AsUTF8String, "AsUTF8String",
                 (PyObject *o), (o), NULL)
WRAP_UNICODE_RET(PyObject *, PyUnicode_AsEncodedString, "AsEncodedString",
                 (PyObject *o, const char *enc, const char *errors),
                 (o, enc, errors), NULL)
WRAP_UNICODE_RET(Py_ssize_t, PyUnicode_GetSize, "GetSize",
                 (PyObject *o), (o), -1)

/* 3.3+ 才有；2.7 调用会安全返回 NULL */
WRAP_RET(const char *, PyUnicode_AsUTF8AndSize,
         (PyObject *o, Py_ssize_t *size), (o, size), NULL)

/* ============================================================
 * 容器与模块
 * ============================================================ */
WRAP_RET(PyObject *, PyTuple_New, (Py_ssize_t n), (n), NULL)
WRAP_RET(Py_ssize_t, PyTuple_Size, (PyObject *o), (o), -1)
WRAP_RET(PyObject *, PyTuple_GetItem, (PyObject *o, Py_ssize_t i), (o, i), NULL)
WRAP_RET(int, PyTuple_SetItem,
         (PyObject *o, Py_ssize_t i, PyObject *v), (o, i, v), -1)

WRAP_RET(PyObject *, PyList_New, (Py_ssize_t n), (n), NULL)
WRAP_RET(Py_ssize_t, PyList_Size, (PyObject *o), (o), -1)
WRAP_RET(PyObject *, PyList_GetItem, (PyObject *o, Py_ssize_t i), (o, i), NULL)
WRAP_RET(int, PyList_SetItem,
         (PyObject *o, Py_ssize_t i, PyObject *v), (o, i, v), -1)
WRAP_RET(int, PyList_Append, (PyObject *o, PyObject *v), (o, v), -1)
WRAP_RET(int, PyList_Insert,
         (PyObject *o, Py_ssize_t i, PyObject *v), (o, i, v), -1)
WRAP_RET(PyObject *, PyList_GetSlice,
         (PyObject *o, Py_ssize_t low, Py_ssize_t high), (o, low, high), NULL)
WRAP_RET(int, PyList_SetSlice,
         (PyObject *o, Py_ssize_t low, Py_ssize_t high, PyObject *v),
         (o, low, high, v), -1)
WRAP_RET(int, PyList_Sort, (PyObject *o), (o), -1)
WRAP_RET(int, PyList_Reverse, (PyObject *o), (o), -1)
WRAP_RET(PyObject *, PyList_AsTuple, (PyObject *o), (o), NULL)

WRAP_RET(PyObject *, PyDict_New, (void), (), NULL)
WRAP_RET(Py_ssize_t, PyDict_Size, (PyObject *o), (o), -1)
WRAP_RET(PyObject *, PyDict_GetItem, (PyObject *o, PyObject *key),
         (o, key), NULL)
WRAP_RET(PyObject *, PyDict_GetItemString, (PyObject *o, const char *key),
         (o, key), NULL)
WRAP_RET(int, PyDict_SetItem,
         (PyObject *o, PyObject *key, PyObject *v), (o, key, v), -1)
WRAP_RET(int, PyDict_SetItemString,
         (PyObject *o, const char *key, PyObject *v), (o, key, v), -1)
WRAP_RET(int, PyDict_DelItem, (PyObject *o, PyObject *key), (o, key), -1)
WRAP_RET(int, PyDict_DelItemString, (PyObject *o, const char *key),
         (o, key), -1)
WRAP_RET(int, PyDict_Contains, (PyObject *o, PyObject *key), (o, key), -1)
WRAP_RET(int, PyDict_Next,
         (PyObject *o, Py_ssize_t *pos, PyObject **key, PyObject **value),
         (o, pos, key, value), 0)
WRAP_RET(PyObject *, PyDict_Keys, (PyObject *o), (o), NULL)
WRAP_RET(PyObject *, PyDict_Values, (PyObject *o), (o), NULL)
WRAP_RET(PyObject *, PyDict_Items, (PyObject *o), (o), NULL)

WRAP_RET(PyObject *, PySet_New, (PyObject *iterable), (iterable), NULL)
WRAP_RET(int, PySet_Add, (PyObject *set, PyObject *key), (set, key), -1)
WRAP_RET(int, PySet_Contains, (PyObject *set, PyObject *key), (set, key), -1)
WRAP_RET(Py_ssize_t, PySet_Size, (PyObject *set), (set), -1)
WRAP_RET(int, PySet_Discard, (PyObject *set, PyObject *key), (set, key), -1)
WRAP_RET(PyObject *, PySet_Pop, (PyObject *set), (set), NULL)
WRAP_RET(int, PySet_Clear, (PyObject *set), (set), -1)

WRAP_RET(PyObject *, PySlice_New,
         (PyObject *start, PyObject *stop, PyObject *step),
         (start, stop, step), NULL)
WRAP_RET(int, PySlice_GetIndicesEx,
         (PyObject *slice, Py_ssize_t length,
          Py_ssize_t *start, Py_ssize_t *stop,
          Py_ssize_t *step, Py_ssize_t *slicelength),
         (slice, length, start, stop, step, slicelength), -1)

WRAP_RET(PyObject *, PyModule_New, (const char *name), (name), NULL)
WRAP_RET(PyObject *, PyModule_GetDict, (PyObject *module), (module), NULL)
WRAP_RET(const char *, PyModule_GetName, (PyObject *module), (module), NULL)
WRAP_RET(int, PyModule_AddObject,
         (PyObject *module, const char *name, PyObject *value),
         (module, name, value), -1)
WRAP_RET(int, PyModule_AddStringConstant,
         (PyObject *module, const char *name, const char *value),
         (module, name, value), -1)
WRAP_RET(int, PyModule_AddIntConstant,
         (PyObject *module, const char *name, long value),
         (module, name, value), -1)

/* ============================================================
 * 错误处理
 * ============================================================ */
WRAP_RET(PyObject *, PyErr_Occurred, (void), (), NULL)
WRAP_VOID(PyErr_Print, (void), ())
WRAP_VOID(PyErr_Clear, (void), ())
WRAP_VOID(PyErr_SetString, (PyObject *type, const char *msg), (type, msg))
WRAP_VOID(PyErr_Fetch,
          (PyObject **type, PyObject **value, PyObject **tb),
          (type, value, tb))
WRAP_VOID(PyErr_Restore,
          (PyObject *type, PyObject *value, PyObject *tb),
          (type, value, tb))
WRAP_VOID(PyErr_NormalizeException,
          (PyObject **type, PyObject **value, PyObject **tb),
          (type, value, tb))

/* ============================================================
 * GIL、线程、引用计数
 * ============================================================ */
/*
 * 库未加载时返回 PyGILState_UNLOCKED 只是安全默认值；调用方应先检查
 * python_libok()，不要在加载失败后继续执行 Python API。
 */
WRAP_RET(PyGILState_STATE, PyGILState_Ensure, (void), (),
         PyGILState_UNLOCKED)
WRAP_VOID(PyGILState_Release, (PyGILState_STATE state), (state))
WRAP_RET(PyThreadState *, PyEval_SaveThread, (void), (), NULL)
WRAP_VOID(PyEval_RestoreThread, (PyThreadState *ts), (ts))
WRAP_VOID(PyEval_InitThreads, (void), ())

WRAP_VOID(Py_IncRef, (PyObject *o), (o))
WRAP_VOID(Py_DecRef, (PyObject *o), (o))

void python_dyn_xincref(PyObject *o) {
    if (o) Py_IncRef(o);
}

void python_dyn_xdecref(PyObject *o) {
    if (o) Py_DecRef(o);
}

/* ============================================================
 * 跨版本 UTF-8 辅助
 * ============================================================ */

/* 新建 Python 字符串对象；Python 2.7 走 PyUnicode_DecodeUTF8 */
PyObject *python_dyn_from_utf8(const char *s) {
    if (!s) return NULL;
    if (!python_libok()) return NULL;
    if (py_major == 2)
        return PyUnicode_DecodeUTF8(s, (Py_ssize_t)strlen(s), "strict");
    return PyUnicode_FromString(s);
}

/*
 * 把 Unicode 对象转成新分配的 UTF-8 C 字符串。
 * 成功返回 malloc 分配的缓冲区，调用 python_dyn_free() 释放；
 * 失败返回 NULL（可能设置 Python 异常）。
 */
char *python_dyn_to_utf8(PyObject *obj) {
    if (!obj) return NULL;
    if (!python_libok()) return NULL;

    if (py_major == 3) {
        Py_ssize_t len = 0;
        const char *p = PyUnicode_AsUTF8AndSize(obj, &len);
        if (p) return dup_n(p, (size_t)len);

        /* 老版本 3.x 的兜底路径 */
        PyErr_Clear();
        {
            PyObject *bytes = PyUnicode_AsUTF8String(obj);
            char *buf = NULL;
            Py_ssize_t blen = 0;
            char *out;
            if (!bytes) return NULL;
            if (PyBytes_AsStringAndSize(bytes, &buf, &blen) != 0) {
                Py_DecRef(bytes);
                return NULL;
            }
            out = dup_n(buf, (size_t)blen);
            Py_DecRef(bytes);
            return out;
        }
    } else {
        /* Python 2：先按 str 处理，失败再按 unicode 处理 */
        char *buf = NULL;
        Py_ssize_t len = 0;
        if (PyString_AsStringAndSize(obj, &buf, &len) == 0 && buf)
            return dup_n(buf, (size_t)len);

        PyErr_Clear();
        {
            PyObject *bytes = PyUnicode_AsEncodedString(obj, "utf-8", "strict");
            char *out;
            if (!bytes) {
                PyErr_Clear();
                return NULL;
            }
            if (PyString_AsStringAndSize(bytes, &buf, &len) != 0 || !buf) {
                Py_DecRef(bytes);
                return NULL;
            }
            out = dup_n(buf, (size_t)len);
            Py_DecRef(bytes);
            return out;
        }
    }
}

/* 把 bytes（3.x）或 str（2.7）转成新分配的 C 字符串 */
char *python_dyn_bytes_to_cstr(PyObject *obj) {
    char *buf = NULL;
    Py_ssize_t len = 0;
    if (!obj) return NULL;
    if (!python_libok()) return NULL;
    if (py_major == 3) {
        if (PyBytes_AsStringAndSize(obj, &buf, &len) != 0 || !buf)
            return NULL;
    } else {
        if (PyString_AsStringAndSize(obj, &buf, &len) != 0 || !buf)
            return NULL;
    }
    return dup_n(buf, (size_t)len);
}

void python_dyn_free(void *p) {
    free(p);
}
