/* ============================================================
 * mxml_dyn.c - dlopen 模拟链接：Mini-XML（v3 / v4 双 ABI 自动探测）
 * ------------------------------------------------------------
 * 对外原型以 Mini-XML v4.0 为准（95 个导出），另提供 v3 独有 API 16 个，
 * 合计 111 个同名函数。init 后按符号自动探测当前加载库的 ABI：
 *   v4: 命中 mxmlOptionsNew + mxmlLoadIO
 *   v3: 命中 mxmlSetWrapMargin / mxmlSAXLoadFile / mxmlEntityAddCallback
 * 对 26 个签名冲突函数，在封装内部把 v4 调用转换成 v3 调用。
 *
 * 已知降级（v3 下）：
 *   - options 携带的回调配置无法还原，load/save 走库默认回调；
 *   - 自定义节点析构回调在 v3 下传 NULL（mxmlNewCustom/mxmlSetCustom）；
 *   - v4 独有函数在 v3 下、v3 独有函数在 v4 下返回零值/no-op。
 *
 * 单文件、无 .h、无 wrapper；链接期不需要 libmxml。
 * 加载顺序：显式路径 > MXML_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > Nix > 平台默认；成功后不卸载。
 *
 * 用法：
 *   mxml_dyn_init(NULL);
 *   int abi = mxml_dyn_abi();   (返回 3 / 4 / -1)
 *   原版 mxml_* 直接调用（v4 风格）
 *   mxml_dyn_cleanup();
 * ============================================================ */

/* 双 ABI 适配必须在不同函数签名间转换函数指针，抑制 GCC 的对应警告 */
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
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

/* 前置声明（mxml_dyn_abi 会用到） */
int mxml_libok(void);

/* ---------- 平台适配层 ---------- */
#ifdef _WIN32
 #include <windows.h>
 typedef HMODULE lib_handle_t;
 static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
 static void *get_sym(lib_handle_t h, const char *name) { return (void *)GetProcAddress(h, name); }
 static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *lib_error(void) { return "LoadLibrary failed"; }
 #define PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *lib_handle_t;
 static lib_handle_t load_lib(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
 static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
 static const char *lib_error(void) { return dlerror(); }
 #define PATH_SEP ':'
#endif

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<mxml.h>)
#    include <mxml.h>
#    define MXML_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef MXML_DYN_HAVE_HEADER
/* ---------- 类型（mxml.h v4 原样内嵌 + v3 兼容 typedef） ---------- */
typedef enum mxml_add_e { MXML_ADD_BEFORE, MXML_ADD_AFTER } mxml_add_t;
typedef enum mxml_descend_e {
    MXML_DESCEND_FIRST = -1, MXML_DESCEND_NONE = 0, MXML_DESCEND_ALL = 1
} mxml_descend_t;
typedef enum mxml_sax_event_e {
    MXML_SAX_EVENT_CDATA, MXML_SAX_EVENT_COMMENT, MXML_SAX_EVENT_DATA,
    MXML_SAX_EVENT_DECLARATION, MXML_SAX_EVENT_DIRECTIVE,
    MXML_SAX_EVENT_ELEMENT_CLOSE, MXML_SAX_EVENT_ELEMENT_OPEN
} mxml_sax_event_t;
typedef enum mxml_type_e {
    MXML_TYPE_IGNORE = -1, MXML_TYPE_CDATA, MXML_TYPE_COMMENT,
    MXML_TYPE_DECLARATION, MXML_TYPE_DIRECTIVE, MXML_TYPE_ELEMENT,
    MXML_TYPE_INTEGER, MXML_TYPE_OPAQUE, MXML_TYPE_REAL, MXML_TYPE_TEXT,
    MXML_TYPE_CUSTOM
} mxml_type_t;
typedef enum mxml_ws_e {
    MXML_WS_BEFORE_OPEN, MXML_WS_AFTER_OPEN, MXML_WS_BEFORE_CLOSE, MXML_WS_AFTER_CLOSE
} mxml_ws_t;

typedef struct _mxml_node_s mxml_node_t;
typedef struct _mxml_index_s mxml_index_t;
typedef struct _mxml_options_s mxml_options_t;

typedef void (*mxml_error_cb_t)(void *cbdata, const char *message);
typedef void (*mxml_custfree_cb_t)(void *cbdata, void *custdata);
typedef bool (*mxml_custload_cb_t)(void *cbdata, mxml_node_t *node, const char *s);
typedef char *(*mxml_custsave_cb_t)(void *cbdata, mxml_node_t *node);
typedef int (*mxml_entity_cb_t)(void *cbdata, const char *name);
typedef size_t (*mxml_io_cb_t)(void *cbdata, void *buffer, size_t bytes);
typedef bool (*mxml_sax_cb_t)(void *cbdata, mxml_node_t *node, mxml_sax_event_t event);
typedef char *(*mxml_strcopy_cb_t)(void *cbdata, const char *s);
typedef void (*mxml_strfree_cb_t)(void *cbdata, char *s);
typedef mxml_type_t (*mxml_type_cb_t)(void *cbdata, mxml_node_t *node);
typedef const char *(*mxml_ws_cb_t)(void *cbdata, mxml_node_t *node, mxml_ws_t when);

#endif /* MXML_DYN_HAVE_HEADER */

/* v3 兼容 typedef：真头不定义，两分支共用 */
typedef void (*mxml_v3_custom_destroy_cb_t)(void *);
typedef int (*mxml_v3_custom_load_cb_t)(mxml_node_t *, const char *);
typedef char *(*mxml_v3_custom_save_cb_t)(mxml_node_t *);
typedef void (*mxml_v3_error_cb_t)(const char *);
typedef int (*mxml_v3_entity_cb_t)(const char *);
typedef mxml_type_t (*mxml_v3_load_cb_t)(mxml_node_t *);
typedef const char *(*mxml_v3_save_cb_t)(mxml_node_t *, int);
typedef void (*mxml_v3_sax_cb_t)(mxml_node_t *, mxml_sax_event_t, void *);

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static lib_handle_t mxml_handle = NULL;
static int mxml_init_state = 0;
static int mxml_abi = 0;   /* 0 未探测, 3, 4, -1 未知 */

static void *mxml_sym(const char *name) {
    return mxml_handle ? get_sym(mxml_handle, name) : NULL;
}

static int mxml_detect_abi(void) {
    if (mxml_abi) return mxml_abi;
    if (mxml_sym("mxmlOptionsNew") && mxml_sym("mxmlLoadIO")) mxml_abi = 4;
    else if (mxml_sym("mxmlSetWrapMargin") || mxml_sym("mxmlSAXLoadFile") ||
             mxml_sym("mxmlEntityAddCallback")) mxml_abi = 3;
    else mxml_abi = -1;
    return mxml_abi;
}

int mxml_dyn_abi(void) {
    if (!mxml_libok()) return 0;
    return mxml_detect_abi();
}

/* v3 枚举值 -> v4 枚举值（v3: ELEMENT=0..CUSTOM=5；v4: ELEMENT=4..CUSTOM=9） */
static mxml_type_t mxml_dyn_v3_type_to_v4(mxml_type_t t) {
    switch ((int)t) {
        case -1: return MXML_TYPE_IGNORE;
        case  0: return MXML_TYPE_ELEMENT;
        case  1: return MXML_TYPE_INTEGER;
        case  2: return MXML_TYPE_OPAQUE;
        case  3: return MXML_TYPE_REAL;
        case  4: return MXML_TYPE_TEXT;
        case  5: return MXML_TYPE_CUSTOM;
        default: return t;
    }
}

/* 变参 -> 堆 buffer（vsnprintf 两遍法） */
static char *mxml_dyn_vformat(const char *fmt, va_list ap) {
    va_list ap2;
    int n;
    char *buf;
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) return NULL;
    buf = (char *)malloc((size_t)n + 1);
    if (!buf) return NULL;
    vsnprintf(buf, (size_t)n + 1, fmt, ap);
    return buf;
}

static int mxml_do_load(const char *library_path) {
    static char bufs[128][1024];
    const char *candidates[128];
    int n = 0, i;

    if (mxml_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("MXML_DYN_LIB");
        if (e && *e) {
            snprintf(bufs[n], sizeof bufs[0], "%s", e);
            candidates[n] = bufs[n]; n++;
        }
    }
    {
#ifdef _WIN32
        const char *env_path = getenv("PATH");
#elif defined(__APPLE__)
        const char *env_path = getenv("DYLD_LIBRARY_PATH");
#else
        const char *env_path = getenv("LD_LIBRARY_PATH");
#endif
        if (env_path && *env_path) {
            char *copy = strdup(env_path);
            if (copy) {
                char *q = copy;
                while (q && *q && n < 100) {
                    char *sep = strchr(q, PATH_SEP);
                    if (sep) *sep = 0;
                    if (*q) {
                        const char *names[] = {
                            "libmxml4.so.4", "libmxml4.so", "libmxml.so.4",
                            "libmxml.so.1", "libmxml.so", "libmxml4.dylib",
                            "libmxml.dylib"
                        };
                        size_t k;
                        for (k = 0; k < sizeof(names)/sizeof(names[0]); k++) {
                            snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, names[k]);
                            candidates[n] = bufs[n]; n++;
                        }
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = "libmxml4.so.4";
    candidates[n++] = "libmxml4.so";
    candidates[n++] = "libmxml.so.4";
    candidates[n++] = "libmxml.so.1";
    candidates[n++] = "libmxml.so";
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libmxml4.so.4", home);
            candidates[n] = bufs[n]; n++;
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libmxml.so.4", home);
            candidates[n] = bufs[n]; n++;
        }
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libmxml4.so.4";
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libmxml.so.4";
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libmxml4.dylib";
    candidates[n++] = "/usr/local/lib/libmxml4.dylib";
#elif defined(_WIN32)
    candidates[n++] = "mxml4.dll";
    candidates[n++] = "mxml.dll";
#else
    candidates[n++] = "/usr/local/lib/libmxml4.so.4";
    candidates[n++] = "/usr/lib/libmxml4.so.4";
    candidates[n++] = "/usr/local/lib/libmxml.so.4";
    candidates[n++] = "/usr/lib/libmxml.so.4";
#endif

    for (i = 0; i < n; i++) {
        mxml_handle = load_lib(candidates[i]);
        if (mxml_handle) break;
    }
    if (!mxml_handle || !mxml_sym("mxmlNewXML")) {
        fprintf(stderr, "mxml_dyn: cannot load mxml (%s)\n", lib_error());
        if (mxml_handle) { free_lib(mxml_handle); mxml_handle = NULL; }
        return -1;
    }
    mxml_abi = 0;
    mxml_detect_abi();
    fprintf(stderr, "mxml_dyn: loaded %d %s (abi=%d)\n", i, candidates[i], mxml_abi);
    return 0;
}

int mxml_libok(void) {
    if (mxml_init_state == 0)
        mxml_init_state = (mxml_do_load(NULL) == 0) ? 1 : -1;
    return (mxml_init_state == 1 && mxml_handle != NULL) ? 1 : 0;
}

int mxml_dyn_init(const char *library_path) {
    if (mxml_do_load(library_path) == 0) {
        mxml_init_state = 1;
        return 0;
    }
    if (mxml_init_state == 0) mxml_init_state = -1;
    return -1;
}

void mxml_dyn_cleanup(void) {
    if (!mxml_handle) { mxml_init_state = 0; mxml_abi = 0; }
    else if (mxml_init_state == -1) { mxml_init_state = 0; mxml_abi = 0; }
}

int mxml_dyn_has_symbol(const char *name) {
    if (!mxml_libok()) return 0;
    return mxml_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                          \
    static ret (*fn) params = NULL;                                        \
    if (!fn) {                                                             \
        if (!mxml_libok()) return fallback;                                \
        *(void **)(&fn) = mxml_sym(#name);                                 \
        if (!fn) {                                                         \
            fprintf(stderr, "mxml_dyn: dlsym failed: %s\n", #name);        \
            return fallback;                                               \
        }                                                                  \
    }                                                                      \
    return fn args;                                                        \
}

#define WRAP_VOID(name, params, args)                                      \
void name params {                                                         \
    static void (*fn) params = NULL;                                       \
    if (!fn) {                                                             \
        if (!mxml_libok()) return;                                         \
        *(void **)(&fn) = mxml_sym(#name);                                 \
        if (!fn) {                                                         \
            fprintf(stderr, "mxml_dyn: dlsym failed: %s\n", #name);        \
            return;                                                        \
        }                                                                  \
    }                                                                      \
    fn args;                                                               \
}

/* ============================================================
 * 一、直接共用（56 个，v3/v4 ABI 兼容）
 * ============================================================ */
WRAP_VOID(mxmlDelete, (mxml_node_t *node), (node))
WRAP_VOID(mxmlElementClearAttr, (mxml_node_t *node, const char *name), (node, name))
WRAP_RET(const char *, mxmlElementGetAttr, (mxml_node_t *node, const char *name), (node, name), NULL)
WRAP_VOID(mxmlElementSetAttr, (mxml_node_t *node, const char *name, const char *value), (node, name, value))
WRAP_RET(mxml_node_t *, mxmlFindElement, (mxml_node_t *node, mxml_node_t *top, const char *element, const char *attr, const char *value, mxml_descend_t descend), (node, top, element, attr, value, descend), NULL)
WRAP_RET(mxml_node_t *, mxmlFindPath, (mxml_node_t *node, const char *path), (node, path), NULL)
WRAP_RET(const char *, mxmlGetCDATA, (mxml_node_t *node), (node), NULL)
WRAP_RET(const char *, mxmlGetComment, (mxml_node_t *node), (node), NULL)
WRAP_RET(const void *, mxmlGetCustom, (mxml_node_t *node), (node), NULL)
WRAP_RET(const char *, mxmlGetDeclaration, (mxml_node_t *node), (node), NULL)
WRAP_RET(const char *, mxmlGetDirective, (mxml_node_t *node), (node), NULL)
WRAP_RET(const char *, mxmlGetElement, (mxml_node_t *node), (node), NULL)
WRAP_RET(mxml_node_t *, mxmlGetFirstChild, (mxml_node_t *node), (node), NULL)
WRAP_RET(mxml_node_t *, mxmlGetLastChild, (mxml_node_t *node), (node), NULL)
WRAP_RET(mxml_node_t *, mxmlGetNextSibling, (mxml_node_t *node), (node), NULL)
WRAP_RET(const char *, mxmlGetOpaque, (mxml_node_t *node), (node), NULL)
WRAP_RET(mxml_node_t *, mxmlGetParent, (mxml_node_t *node), (node), NULL)
WRAP_RET(mxml_node_t *, mxmlGetPrevSibling, (mxml_node_t *node), (node), NULL)
WRAP_RET(double, mxmlGetReal, (mxml_node_t *node), (node), 0)
WRAP_RET(void *, mxmlGetUserData, (mxml_node_t *node), (node), NULL)
WRAP_VOID(mxmlIndexDelete, (mxml_index_t *ind), (ind))
WRAP_RET(mxml_node_t *, mxmlIndexEnum, (mxml_index_t *ind), (ind), NULL)
WRAP_RET(mxml_node_t *, mxmlIndexFind, (mxml_index_t *ind, const char *element, const char *value), (ind, element, value), NULL)
WRAP_RET(mxml_index_t *, mxmlIndexNew, (mxml_node_t *node, const char *element, const char *attr), (node, element, attr), NULL)
WRAP_RET(mxml_node_t *, mxmlIndexReset, (mxml_index_t *ind), (ind), NULL)
WRAP_RET(mxml_node_t *, mxmlLoadFilename, (mxml_node_t *top, mxml_options_t *options, const char *filename), (top, options, filename), NULL)
WRAP_RET(mxml_node_t *, mxmlLoadIO, (mxml_node_t *top, mxml_options_t *options, mxml_io_cb_t io_cb, void *io_cbdata), (top, options, io_cb, io_cbdata), NULL)
WRAP_VOID(mxmlOptionsDelete, (mxml_options_t *options), (options))
WRAP_RET(mxml_options_t *, mxmlOptionsNew, (void), (), NULL)
WRAP_VOID(mxmlOptionsSetCustomCallbacks, (mxml_options_t *options, mxml_custload_cb_t load_cb, mxml_custsave_cb_t save_cb, void *cbdata), (options, load_cb, save_cb, cbdata))
WRAP_VOID(mxmlOptionsSetEntityCallback, (mxml_options_t *options, mxml_entity_cb_t cb, void *cbdata), (options, cb, cbdata))
WRAP_VOID(mxmlOptionsSetErrorCallback, (mxml_options_t *options, mxml_error_cb_t cb, void *cbdata), (options, cb, cbdata))
WRAP_VOID(mxmlOptionsSetSAXCallback, (mxml_options_t *options, mxml_sax_cb_t cb, void *cbdata), (options, cb, cbdata))
WRAP_VOID(mxmlOptionsSetTypeCallback, (mxml_options_t *options, mxml_type_cb_t cb, void *cbdata), (options, cb, cbdata))
WRAP_VOID(mxmlOptionsSetTypeValue, (mxml_options_t *options, mxml_type_t type), (options, type))
WRAP_VOID(mxmlOptionsSetWhitespaceCallback, (mxml_options_t *options, mxml_ws_cb_t cb, void *cbdata), (options, cb, cbdata))
WRAP_VOID(mxmlOptionsSetWrapMargin, (mxml_options_t *options, int column), (options, column))
WRAP_RET(mxml_node_t *, mxmlNewCDATA, (mxml_node_t *parent, const char *string), (parent, string), NULL)
WRAP_RET(mxml_node_t *, mxmlNewComment, (mxml_node_t *parent, const char *comment), (parent, comment), NULL)
WRAP_RET(mxml_node_t *, mxmlNewDeclaration, (mxml_node_t *parent, const char *declaration), (parent, declaration), NULL)
WRAP_RET(mxml_node_t *, mxmlNewDirective, (mxml_node_t *parent, const char *directive), (parent, directive), NULL)
WRAP_RET(mxml_node_t *, mxmlNewElement, (mxml_node_t *parent, const char *name), (parent, name), NULL)
WRAP_RET(mxml_node_t *, mxmlNewOpaque, (mxml_node_t *parent, const char *opaque), (parent, opaque), NULL)
WRAP_RET(mxml_node_t *, mxmlNewReal, (mxml_node_t *parent, double real), (parent, real), NULL)
WRAP_RET(mxml_node_t *, mxmlNewXML, (const char *version), (version), NULL)
WRAP_RET(int, mxmlRelease, (mxml_node_t *node), (node), 0)
WRAP_VOID(mxmlRemove, (mxml_node_t *node), (node))
WRAP_RET(int, mxmlRetain, (mxml_node_t *node), (node), 0)
WRAP_RET(bool, mxmlSaveFilename, (mxml_node_t *node, mxml_options_t *options, const char *filename), (node, options, filename), false)
WRAP_RET(bool, mxmlSaveIO, (mxml_node_t *node, mxml_options_t *options, mxml_io_cb_t io_cb, void *io_cbdata), (node, options, io_cb, io_cbdata), false)
WRAP_RET(bool, mxmlSetComment, (mxml_node_t *node, const char *comment), (node, comment), false)
WRAP_RET(bool, mxmlSetDeclaration, (mxml_node_t *node, const char *declaration), (node, declaration), false)
WRAP_RET(bool, mxmlSetDirective, (mxml_node_t *node, const char *directive), (node, directive), false)
WRAP_VOID(mxmlSetStringCallbacks, (mxml_strcopy_cb_t strcopy_cb, mxml_strfree_cb_t strfree_cb, void *str_cbdata), (strcopy_cb, strfree_cb, str_cbdata))
WRAP_RET(mxml_node_t *, mxmlWalkNext, (mxml_node_t *node, mxml_node_t *top, mxml_descend_t descend), (node, top, descend), NULL)
WRAP_RET(mxml_node_t *, mxmlWalkPrev, (mxml_node_t *node, mxml_node_t *top, mxml_descend_t descend), (node, top, descend), NULL)

/* ============================================================
 * 二、签名冲突（26 个，v4 原型 + v3 运行时适配）
 * ============================================================ */
void mxmlAdd(mxml_node_t *parent, mxml_add_t add, mxml_node_t *child, mxml_node_t *node) {
    static void (*fn)(mxml_node_t *, mxml_add_t, mxml_node_t *, mxml_node_t *) = NULL;
    if (!fn) { if (!mxml_libok()) return;
        *(void **)(&fn) = mxml_sym("mxmlAdd");
        if (!fn) { fprintf(stderr, "mxml_dyn: dlsym failed: mxmlAdd\n"); return; } }
    if (mxml_dyn_abi() == 3)
        ((void (*)(mxml_node_t *, int, mxml_node_t *, mxml_node_t *))fn)(parent, (int)add, child, node);
    else
        fn(parent, add, child, node);
}

size_t mxmlElementGetAttrCount(mxml_node_t *node) {
    static size_t (*fn)(mxml_node_t *) = NULL;
    if (!fn) { if (!mxml_libok()) return 0;
        *(void **)(&fn) = mxml_sym("mxmlElementGetAttrCount");
        if (!fn) return 0; }
    if (mxml_dyn_abi() == 3) return (size_t)((int (*)(mxml_node_t *))fn)(node);
    return fn(node);
}

const char *mxmlElementGetAttrByIndex(mxml_node_t *node, size_t idx, const char **name) {
    static const char *(*fn)(mxml_node_t *, size_t, const char **) = NULL;
    if (!fn) { if (!mxml_libok()) return NULL;
        *(void **)(&fn) = mxml_sym("mxmlElementGetAttrByIndex");
        if (!fn) return NULL; }
    if (mxml_dyn_abi() == 3)
        return ((const char *(*)(mxml_node_t *, int, const char **))fn)(node, (int)idx, name);
    return fn(node, idx, name);
}

long mxmlGetInteger(mxml_node_t *node) {
    static long (*fn)(mxml_node_t *) = NULL;
    if (!fn) { if (!mxml_libok()) return 0;
        *(void **)(&fn) = mxml_sym("mxmlGetInteger");
        if (!fn) return 0; }
    if (mxml_dyn_abi() == 3) return (long)((int (*)(mxml_node_t *))fn)(node);
    return fn(node);
}

size_t mxmlGetRefCount(mxml_node_t *node) {
    static size_t (*fn)(mxml_node_t *) = NULL;
    if (!fn) { if (!mxml_libok()) return 0;
        *(void **)(&fn) = mxml_sym("mxmlGetRefCount");
        if (!fn) return 0; }
    if (mxml_dyn_abi() == 3) return (size_t)((int (*)(mxml_node_t *))fn)(node);
    return fn(node);
}

mxml_type_t mxmlGetType(mxml_node_t *node) {
    static mxml_type_t (*fn)(mxml_node_t *) = NULL;
    if (!fn) { if (!mxml_libok()) return MXML_TYPE_IGNORE;
        *(void **)(&fn) = mxml_sym("mxmlGetType");
        if (!fn) return MXML_TYPE_IGNORE; }
    if (mxml_dyn_abi() == 3)
        return mxml_dyn_v3_type_to_v4(((mxml_type_t (*)(mxml_node_t *))fn)(node));
    return fn(node);
}

const char *mxmlGetText(mxml_node_t *node, bool *whitespace) {
    static const char *(*fn)(mxml_node_t *, bool *) = NULL;
    if (!fn) { if (!mxml_libok()) return NULL;
        *(void **)(&fn) = mxml_sym("mxmlGetText");
        if (!fn) return NULL; }
    if (mxml_dyn_abi() == 3) {
        int w = 0;
        const char *r = ((const char *(*)(mxml_node_t *, int *))fn)(node, &w);
        if (whitespace) *whitespace = (w != 0);
        return r;
    }
    return fn(node, whitespace);
}

size_t mxmlIndexGetCount(mxml_index_t *ind) {
    static size_t (*fn)(mxml_index_t *) = NULL;
    if (!fn) { if (!mxml_libok()) return 0;
        *(void **)(&fn) = mxml_sym("mxmlIndexGetCount");
        if (!fn) return 0; }
    if (mxml_dyn_abi() == 3) return (size_t)((int (*)(mxml_index_t *))fn)(ind);
    return fn(ind);
}

mxml_node_t *mxmlLoadFd(mxml_node_t *top, mxml_options_t *options, int fd) {
    static mxml_node_t *(*fn)(mxml_node_t *, mxml_options_t *, int) = NULL;
    if (!fn) { if (!mxml_libok()) return NULL;
        *(void **)(&fn) = mxml_sym("mxmlLoadFd");
        if (!fn) return NULL; }
    if (mxml_dyn_abi() == 3)
        return ((mxml_node_t *(*)(mxml_node_t *, int, mxml_v3_load_cb_t))fn)(top, fd, NULL);
    (void)options;
    return fn(top, options, fd);
}

mxml_node_t *mxmlLoadFile(mxml_node_t *top, mxml_options_t *options, FILE *fp) {
    static mxml_node_t *(*fn)(mxml_node_t *, mxml_options_t *, FILE *) = NULL;
    if (!fn) { if (!mxml_libok()) return NULL;
        *(void **)(&fn) = mxml_sym("mxmlLoadFile");
        if (!fn) return NULL; }
    if (mxml_dyn_abi() == 3)
        return ((mxml_node_t *(*)(mxml_node_t *, FILE *, mxml_v3_load_cb_t))fn)(top, fp, NULL);
    (void)options;
    return fn(top, options, fp);
}

mxml_node_t *mxmlLoadString(mxml_node_t *top, mxml_options_t *options, const char *s) {
    static mxml_node_t *(*fn)(mxml_node_t *, mxml_options_t *, const char *) = NULL;
    if (!fn) { if (!mxml_libok()) return NULL;
        *(void **)(&fn) = mxml_sym("mxmlLoadString");
        if (!fn) return NULL; }
    if (mxml_dyn_abi() == 3)
        return ((mxml_node_t *(*)(mxml_node_t *, const char *, mxml_v3_load_cb_t))fn)(top, s, NULL);
    (void)options;
    return fn(top, options, s);
}

mxml_node_t *mxmlNewCustom(mxml_node_t *parent, void *data, mxml_custfree_cb_t free_cb, void *free_cbdata) {
    static mxml_node_t *(*fn)(mxml_node_t *, void *, mxml_custfree_cb_t, void *) = NULL;
    if (!fn) { if (!mxml_libok()) return NULL;
        *(void **)(&fn) = mxml_sym("mxmlNewCustom");
        if (!fn) return NULL; }
    if (mxml_dyn_abi() == 3) {
        (void)free_cb; (void)free_cbdata;
        return ((mxml_node_t *(*)(mxml_node_t *, void *, mxml_v3_custom_destroy_cb_t))fn)(parent, data, NULL);
    }
    return fn(parent, data, free_cb, free_cbdata);
}

mxml_node_t *mxmlNewInteger(mxml_node_t *parent, long integer) {
    static mxml_node_t *(*fn)(mxml_node_t *, long) = NULL;
    if (!fn) { if (!mxml_libok()) return NULL;
        *(void **)(&fn) = mxml_sym("mxmlNewInteger");
        if (!fn) return NULL; }
    if (mxml_dyn_abi() == 3)
        return ((mxml_node_t *(*)(mxml_node_t *, int))fn)(parent, (int)integer);
    return fn(parent, integer);
}

mxml_node_t *mxmlNewText(mxml_node_t *parent, bool whitespace, const char *string) {
    static mxml_node_t *(*fn)(mxml_node_t *, bool, const char *) = NULL;
    if (!fn) { if (!mxml_libok()) return NULL;
        *(void **)(&fn) = mxml_sym("mxmlNewText");
        if (!fn) return NULL; }
    if (mxml_dyn_abi() == 3)
        return ((mxml_node_t *(*)(mxml_node_t *, int, const char *))fn)(parent, (int)(whitespace ? 1 : 0), string);
    return fn(parent, whitespace, string);
}

char *mxmlSaveAllocString(mxml_node_t *node, mxml_options_t *options) {
    static char *(*fn)(mxml_node_t *, mxml_options_t *) = NULL;
    if (!fn) { if (!mxml_libok()) return NULL;
        *(void **)(&fn) = mxml_sym("mxmlSaveAllocString");
        if (!fn) return NULL; }
    if (mxml_dyn_abi() == 3) {
        (void)options;
        return ((char *(*)(mxml_node_t *, mxml_v3_save_cb_t))fn)(node, NULL);
    }
    return fn(node, options);
}

bool mxmlSaveFd(mxml_node_t *node, mxml_options_t *options, int fd) {
    static bool (*fn)(mxml_node_t *, mxml_options_t *, int) = NULL;
    if (!fn) { if (!mxml_libok()) return false;
        *(void **)(&fn) = mxml_sym("mxmlSaveFd");
        if (!fn) return false; }
    if (mxml_dyn_abi() == 3) {
        (void)options;
        return ((int (*)(mxml_node_t *, int, mxml_v3_save_cb_t))fn)(node, fd, NULL) != 0;
    }
    return fn(node, options, fd);
}

bool mxmlSaveFile(mxml_node_t *node, mxml_options_t *options, FILE *fp) {
    static bool (*fn)(mxml_node_t *, mxml_options_t *, FILE *) = NULL;
    if (!fn) { if (!mxml_libok()) return false;
        *(void **)(&fn) = mxml_sym("mxmlSaveFile");
        if (!fn) return false; }
    if (mxml_dyn_abi() == 3) {
        (void)options;
        return ((int (*)(mxml_node_t *, FILE *, mxml_v3_save_cb_t))fn)(node, fp, NULL) != 0;
    }
    return fn(node, options, fp);
}

size_t mxmlSaveString(mxml_node_t *node, mxml_options_t *options, char *buffer, size_t bufsize) {
    static size_t (*fn)(mxml_node_t *, mxml_options_t *, char *, size_t) = NULL;
    if (!fn) { if (!mxml_libok()) return 0;
        *(void **)(&fn) = mxml_sym("mxmlSaveString");
        if (!fn) return 0; }
    if (mxml_dyn_abi() == 3) {
        (void)options;
        return (size_t)((int (*)(mxml_node_t *, char *, int, mxml_v3_save_cb_t))fn)(node, buffer, (int)bufsize, NULL);
    }
    return fn(node, options, buffer, bufsize);
}

bool mxmlSetCDATA(mxml_node_t *node, const char *data) {
    static bool (*fn)(mxml_node_t *, const char *) = NULL;
    if (!fn) { if (!mxml_libok()) return false;
        *(void **)(&fn) = mxml_sym("mxmlSetCDATA");
        if (!fn) return false; }
    if (mxml_dyn_abi() == 3)
        return ((int (*)(mxml_node_t *, const char *))fn)(node, data) != 0;
    return fn(node, data);
}

bool mxmlSetCustom(mxml_node_t *node, void *data, mxml_custfree_cb_t free_cb, void *cbdata) {
    static bool (*fn)(mxml_node_t *, void *, mxml_custfree_cb_t, void *) = NULL;
    if (!fn) { if (!mxml_libok()) return false;
        *(void **)(&fn) = mxml_sym("mxmlSetCustom");
        if (!fn) return false; }
    if (mxml_dyn_abi() == 3) {
        (void)free_cb; (void)cbdata;
        return ((int (*)(mxml_node_t *, void *, mxml_v3_custom_destroy_cb_t))fn)(node, data, NULL) != 0;
    }
    return fn(node, data, free_cb, cbdata);
}

bool mxmlSetElement(mxml_node_t *node, const char *name) {
    static bool (*fn)(mxml_node_t *, const char *) = NULL;
    if (!fn) { if (!mxml_libok()) return false;
        *(void **)(&fn) = mxml_sym("mxmlSetElement");
        if (!fn) return false; }
    if (mxml_dyn_abi() == 3)
        return ((int (*)(mxml_node_t *, const char *))fn)(node, name) != 0;
    return fn(node, name);
}

bool mxmlSetInteger(mxml_node_t *node, long integer) {
    static bool (*fn)(mxml_node_t *, long) = NULL;
    if (!fn) { if (!mxml_libok()) return false;
        *(void **)(&fn) = mxml_sym("mxmlSetInteger");
        if (!fn) return false; }
    if (mxml_dyn_abi() == 3)
        return ((int (*)(mxml_node_t *, int))fn)(node, (int)integer) != 0;
    return fn(node, integer);
}

bool mxmlSetOpaque(mxml_node_t *node, const char *opaque) {
    static bool (*fn)(mxml_node_t *, const char *) = NULL;
    if (!fn) { if (!mxml_libok()) return false;
        *(void **)(&fn) = mxml_sym("mxmlSetOpaque");
        if (!fn) return false; }
    if (mxml_dyn_abi() == 3)
        return ((int (*)(mxml_node_t *, const char *))fn)(node, opaque) != 0;
    return fn(node, opaque);
}

bool mxmlSetReal(mxml_node_t *node, double real) {
    static bool (*fn)(mxml_node_t *, double) = NULL;
    if (!fn) { if (!mxml_libok()) return false;
        *(void **)(&fn) = mxml_sym("mxmlSetReal");
        if (!fn) return false; }
    if (mxml_dyn_abi() == 3)
        return ((int (*)(mxml_node_t *, double))fn)(node, real) != 0;
    return fn(node, real);
}

bool mxmlSetText(mxml_node_t *node, bool whitespace, const char *string) {
    static bool (*fn)(mxml_node_t *, bool, const char *) = NULL;
    if (!fn) { if (!mxml_libok()) return false;
        *(void **)(&fn) = mxml_sym("mxmlSetText");
        if (!fn) return false; }
    if (mxml_dyn_abi() == 3)
        return ((int (*)(mxml_node_t *, int, const char *))fn)(node, (int)(whitespace ? 1 : 0), string) != 0;
    return fn(node, whitespace, string);
}

bool mxmlSetUserData(mxml_node_t *node, void *data) {
    static bool (*fn)(mxml_node_t *, void *) = NULL;
    if (!fn) { if (!mxml_libok()) return false;
        *(void **)(&fn) = mxml_sym("mxmlSetUserData");
        if (!fn) return false; }
    if (mxml_dyn_abi() == 3) {
        ((void (*)(mxml_node_t *, void *))fn)(node, data);
        return true;
    }
    return fn(node, data);
}

/* ============================================================
 * 三、变参 *f 函数（13 个，vsnprintf -> 对应非 f 版本）
 * ============================================================ */
void mxmlElementSetAttrf(mxml_node_t *node, const char *name, const char *format, ...) {
    char *buf; va_list ap;
    va_start(ap, format); buf = mxml_dyn_vformat(format, ap); va_end(ap);
    if (!buf) return;
    mxmlElementSetAttr(node, name, buf);
    free(buf);
}

mxml_node_t *mxmlNewCDATAf(mxml_node_t *parent, const char *format, ...) {
    char *buf; va_list ap; mxml_node_t *r;
    va_start(ap, format); buf = mxml_dyn_vformat(format, ap); va_end(ap);
    if (!buf) return NULL;
    r = mxmlNewCDATA(parent, buf);
    free(buf);
    return r;
}

mxml_node_t *mxmlNewCommentf(mxml_node_t *parent, const char *format, ...) {
    char *buf; va_list ap; mxml_node_t *r;
    va_start(ap, format); buf = mxml_dyn_vformat(format, ap); va_end(ap);
    if (!buf) return NULL;
    r = mxmlNewComment(parent, buf);
    free(buf);
    return r;
}

mxml_node_t *mxmlNewDeclarationf(mxml_node_t *parent, const char *format, ...) {
    char *buf; va_list ap; mxml_node_t *r;
    va_start(ap, format); buf = mxml_dyn_vformat(format, ap); va_end(ap);
    if (!buf) return NULL;
    r = mxmlNewDeclaration(parent, buf);
    free(buf);
    return r;
}

mxml_node_t *mxmlNewDirectivef(mxml_node_t *parent, const char *format, ...) {
    char *buf; va_list ap; mxml_node_t *r;
    va_start(ap, format); buf = mxml_dyn_vformat(format, ap); va_end(ap);
    if (!buf) return NULL;
    r = mxmlNewDirective(parent, buf);
    free(buf);
    return r;
}

mxml_node_t *mxmlNewOpaquef(mxml_node_t *parent, const char *format, ...) {
    char *buf; va_list ap; mxml_node_t *r;
    va_start(ap, format); buf = mxml_dyn_vformat(format, ap); va_end(ap);
    if (!buf) return NULL;
    r = mxmlNewOpaque(parent, buf);
    free(buf);
    return r;
}

mxml_node_t *mxmlNewTextf(mxml_node_t *parent, bool whitespace, const char *format, ...) {
    char *buf; va_list ap; mxml_node_t *r;
    va_start(ap, format); buf = mxml_dyn_vformat(format, ap); va_end(ap);
    if (!buf) return NULL;
    r = mxmlNewText(parent, whitespace, buf);
    free(buf);
    return r;
}

bool mxmlSetCDATAf(mxml_node_t *node, const char *format, ...) {
    char *buf; va_list ap; bool r;
    va_start(ap, format); buf = mxml_dyn_vformat(format, ap); va_end(ap);
    if (!buf) return false;
    r = mxmlSetCDATA(node, buf);
    free(buf);
    return r;
}

bool mxmlSetCommentf(mxml_node_t *node, const char *format, ...) {
    char *buf; va_list ap; bool r;
    va_start(ap, format); buf = mxml_dyn_vformat(format, ap); va_end(ap);
    if (!buf) return false;
    r = mxmlSetComment(node, buf);
    free(buf);
    return r;
}

bool mxmlSetDeclarationf(mxml_node_t *node, const char *format, ...) {
    char *buf; va_list ap; bool r;
    va_start(ap, format); buf = mxml_dyn_vformat(format, ap); va_end(ap);
    if (!buf) return false;
    r = mxmlSetDeclaration(node, buf);
    free(buf);
    return r;
}

bool mxmlSetDirectivef(mxml_node_t *node, const char *format, ...) {
    char *buf; va_list ap; bool r;
    va_start(ap, format); buf = mxml_dyn_vformat(format, ap); va_end(ap);
    if (!buf) return false;
    r = mxmlSetDirective(node, buf);
    free(buf);
    return r;
}

bool mxmlSetOpaquef(mxml_node_t *node, const char *format, ...) {
    char *buf; va_list ap; bool r;
    va_start(ap, format); buf = mxml_dyn_vformat(format, ap); va_end(ap);
    if (!buf) return false;
    r = mxmlSetOpaque(node, buf);
    free(buf);
    return r;
}

bool mxmlSetTextf(mxml_node_t *node, bool whitespace, const char *format, ...) {
    char *buf; va_list ap; bool r;
    va_start(ap, format); buf = mxml_dyn_vformat(format, ap); va_end(ap);
    if (!buf) return false;
    r = mxmlSetText(node, whitespace, buf);
    free(buf);
    return r;
}

/* ============================================================
 * 四、v3 独有 API（16 个，v4 下符号缺失 -> 零值/no-op）
 * ============================================================ */
WRAP_VOID(mxmlElementDeleteAttr, (mxml_node_t *node, const char *name), (node, name))
WRAP_RET(int, mxmlEntityAddCallback, (mxml_v3_entity_cb_t cb), (cb), 0)
WRAP_RET(const char *, mxmlEntityGetName, (int val), (val), NULL)
WRAP_RET(int, mxmlEntityGetValue, (const char *name), (name), 0)
WRAP_VOID(mxmlEntityRemoveCallback, (mxml_v3_entity_cb_t cb), (cb))
WRAP_RET(mxml_node_t *, mxmlSAXLoadFd, (mxml_node_t *top, int fd, mxml_v3_load_cb_t cb, mxml_v3_sax_cb_t sax, void *sax_data), (top, fd, cb, sax, sax_data), NULL)
WRAP_RET(mxml_node_t *, mxmlSAXLoadFile, (mxml_node_t *top, FILE *fp, mxml_v3_load_cb_t cb, mxml_v3_sax_cb_t sax, void *sax_data), (top, fp, cb, sax, sax_data), NULL)
WRAP_RET(mxml_node_t *, mxmlSAXLoadString, (mxml_node_t *top, const char *s, mxml_v3_load_cb_t cb, mxml_v3_sax_cb_t sax, void *sax_data), (top, s, cb, sax, sax_data), NULL)
WRAP_VOID(mxmlSetCustomHandlers, (mxml_v3_custom_load_cb_t load, mxml_v3_custom_save_cb_t save), (load, save))
WRAP_VOID(mxmlSetErrorCallback, (mxml_v3_error_cb_t cb), (cb))
WRAP_VOID(mxmlSetWrapMargin, (int column), (column))
WRAP_RET(mxml_type_t, mxml_ignore_cb, (mxml_node_t *node), (node), MXML_TYPE_IGNORE)
WRAP_RET(mxml_type_t, mxml_integer_cb, (mxml_node_t *node), (node), MXML_TYPE_INTEGER)
WRAP_RET(mxml_type_t, mxml_opaque_cb, (mxml_node_t *node), (node), MXML_TYPE_OPAQUE)
WRAP_RET(mxml_type_t, mxml_real_cb, (mxml_node_t *node), (node), MXML_TYPE_REAL)

/* mxml_error（v3 独有，变参） */
void mxml_error(const char *format, ...) {
    char *buf;
    va_list ap;
    static void (*fn)(const char *) = NULL;
    if (!fn) {
        if (!mxml_libok()) return;
        *(void **)(&fn) = mxml_sym("mxml_error");
        if (!fn) return;
    }
    va_start(ap, format);
    buf = mxml_dyn_vformat(format, ap);
    va_end(ap);
    if (!buf) return;
    fn(buf);
    free(buf);
}
