/* ============================================================
 * cmark_dyn.c - dlopen 模拟链接：CommonMark (libcmark) C API
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照本机 /usr/include/cmark.h（0.31.2）。
 * 封装头文件声明的全部 72 个公开函数。
 * 内部导出符号 cmark_node_check 无公开声明，按约定不封装。
 * 类型定义按头文件原样内嵌（4 个枚举 + opaque 类型 + cmark_mem）。
 *
 * 单文件；链接期不需要 libcmark，仅需 -ldl。
 * 加载顺序：显式路径 > CMARK_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > Nix > 平台默认；成功后不卸载。
 *
 * 用法：
 *   cmark_dyn_init(NULL);
 *   char *html = cmark_markdown_to_html("# Hi\n", 5, 0);
 *   ... 原版 cmark_* 直接调用 ...
 *   cmark_dyn_cleanup();
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

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
#  if __has_include(<cmark.h>)
#    include <cmark.h>
#    define CMARK_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef CMARK_DYN_HAVE_HEADER
/* ---------- 类型（cmark.h 原样内嵌，字段顺序即 ABI） ---------- */
#define CMARK_VERSION ((0 << 16) | (31 << 8) | 2)
#define CMARK_VERSION_STRING "0.31.2"

typedef enum {
    CMARK_NODE_NONE,
    CMARK_NODE_DOCUMENT,
    CMARK_NODE_BLOCK_QUOTE,
    CMARK_NODE_LIST,
    CMARK_NODE_ITEM,
    CMARK_NODE_CODE_BLOCK,
    CMARK_NODE_HTML_BLOCK,
    CMARK_NODE_CUSTOM_BLOCK,
    CMARK_NODE_PARAGRAPH,
    CMARK_NODE_HEADING,
    CMARK_NODE_THEMATIC_BREAK,

    CMARK_NODE_FIRST_BLOCK = CMARK_NODE_DOCUMENT,
    CMARK_NODE_LAST_BLOCK = CMARK_NODE_THEMATIC_BREAK,

    CMARK_NODE_TEXT,
    CMARK_NODE_SOFTBREAK,
    CMARK_NODE_LINEBREAK,
    CMARK_NODE_CODE,
    CMARK_NODE_HTML_INLINE,
    CMARK_NODE_CUSTOM_INLINE,
    CMARK_NODE_EMPH,
    CMARK_NODE_STRONG,
    CMARK_NODE_LINK,
    CMARK_NODE_IMAGE,

    CMARK_NODE_FIRST_INLINE = CMARK_NODE_TEXT,
    CMARK_NODE_LAST_INLINE = CMARK_NODE_IMAGE
} cmark_node_type;

#define CMARK_NODE_HEADER CMARK_NODE_HEADING
#define CMARK_NODE_HRULE CMARK_NODE_THEMATIC_BREAK
#define CMARK_NODE_HTML CMARK_NODE_HTML_BLOCK
#define CMARK_NODE_INLINE_HTML CMARK_NODE_HTML_INLINE

typedef enum {
    CMARK_NO_LIST,
    CMARK_BULLET_LIST,
    CMARK_ORDERED_LIST
} cmark_list_type;

typedef enum {
    CMARK_NO_DELIM,
    CMARK_PERIOD_DELIM,
    CMARK_PAREN_DELIM
} cmark_delim_type;

typedef struct cmark_node cmark_node;
typedef struct cmark_parser cmark_parser;
typedef struct cmark_iter cmark_iter;

typedef struct cmark_mem {
    void *(*calloc)(size_t, size_t);
    void *(*realloc)(void *, size_t);
    void (*free)(void *);
} cmark_mem;

typedef enum {
    CMARK_EVENT_NONE,
    CMARK_EVENT_DONE,
    CMARK_EVENT_ENTER,
    CMARK_EVENT_EXIT
} cmark_event_type;

#define CMARK_OPT_DEFAULT 0
#define CMARK_OPT_SOURCEPOS (1 << 1)
#define CMARK_OPT_HARDBREAKS (1 << 2)
#define CMARK_OPT_SAFE (1 << 3)
#define CMARK_OPT_UNSAFE (1 << 17)
#define CMARK_OPT_NOBREAKS (1 << 4)
#define CMARK_OPT_NORMALIZE (1 << 8)
#define CMARK_OPT_VALIDATE_UTF8 (1 << 9)
#define CMARK_OPT_SMART (1 << 10)

#endif /* CMARK_DYN_HAVE_HEADER */

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static lib_handle_t cmark_handle = NULL;
static int cmark_init_state = 0;

static void *cmark_sym(const char *name) {
    return cmark_handle ? get_sym(cmark_handle, name) : NULL;
}

static int cmark_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    const char *so_soname = "libcmark.so.0.31.2";
    const char *so_plain = "libcmark.so";
    int n = 0, i;

    if (cmark_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("CMARK_DYN_LIB");
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
                while (q && *q && n < 80) {
                    char *sep = strchr(q, PATH_SEP);
                    if (sep) *sep = 0;
                    if (*q) {
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_soname);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_plain);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = so_soname;
    candidates[n++] = "libcmark.so.0";
    candidates[n++] = so_plain;
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/%s", home, so_soname);
            candidates[n] = bufs[n]; n++;
        }
        snprintf(bufs[n], sizeof bufs[0], "/nix/var/nix/profiles/default/lib/%s", so_soname);
        candidates[n] = bufs[n]; n++;
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libcmark.dylib";
    candidates[n++] = "/usr/local/lib/libcmark.dylib";
#elif defined(_WIN32)
    candidates[n++] = "libcmark.dll";
#else
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
#endif

    for (i = 0; i < n; i++) {
        cmark_handle = load_lib(candidates[i]);
        if (cmark_handle && cmark_sym("cmark_version")) break;
        if (cmark_handle) { free_lib(cmark_handle); cmark_handle = NULL; }
    }
    if (!cmark_handle || !cmark_sym("cmark_markdown_to_html")) {
        fprintf(stderr, "cmark_dyn: cannot load cmark (%s)\n", lib_error());
        if (cmark_handle) { free_lib(cmark_handle); cmark_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "cmark_dyn: loaded cmark\n");
    return 0;
}

int cmark_dyn_libok(void) {
    if (cmark_init_state == 0)
        cmark_init_state = (cmark_do_load(NULL) == 0) ? 1 : -1;
    return (cmark_init_state == 1 && cmark_handle != NULL) ? 1 : 0;
}

int cmark_dyn_init(const char *library_path) {
    if (cmark_do_load(library_path) == 0) {
        cmark_init_state = 1;
        return 0;
    }
    if (cmark_init_state == 0) cmark_init_state = -1;
    return -1;
}

void cmark_dyn_cleanup(void) {
    if (!cmark_handle) cmark_init_state = 0;
    else if (cmark_init_state == -1) cmark_init_state = 0;
}

int cmark_dyn_has_symbol(const char *name) {
    if (!cmark_dyn_libok()) return 0;
    return cmark_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                          \
    static ret (*fn) params = NULL;                                        \
    if (!fn) {                                                             \
        if (!cmark_dyn_libok()) return fallback;                           \
        *(void **)(&fn) = cmark_sym(#name);                                \
        if (!fn) {                                                         \
            fprintf(stderr, "cmark_dyn: dlsym failed: %s\n", #name);       \
            return fallback;                                               \
        }                                                                  \
    }                                                                      \
    return fn args;                                                        \
}

#define WRAP_VOID(name, params, args)                                      \
void name params {                                                         \
    static void (*fn) params = NULL;                                       \
    if (!fn) {                                                             \
        if (!cmark_dyn_libok()) return;                                    \
        *(void **)(&fn) = cmark_sym(#name);                                \
        if (!fn) {                                                         \
            fprintf(stderr, "cmark_dyn: dlsym failed: %s\n", #name);       \
            return;                                                        \
        }                                                                  \
    }                                                                      \
    fn args;                                                               \
}

/* ============================================================
 * 导出函数封装（cmark.h 全部 72 个，顺序与头文件一致）
 * ============================================================ */

/* --- simple interface --- */
WRAP_RET(char *, cmark_markdown_to_html, (const char *text, size_t len, int options), (text, len, options), NULL)

/* --- allocator --- */
WRAP_RET(cmark_mem *, cmark_get_default_mem_allocator, (void), (), NULL)

/* --- 节点分类 --- */
WRAP_RET(bool, cmark_node_is_block, (cmark_node *node), (node), false)
WRAP_RET(bool, cmark_node_is_inline, (cmark_node *node), (node), false)
WRAP_RET(bool, cmark_node_is_leaf, (cmark_node *node), (node), false)

/* --- 创建与销毁 --- */
WRAP_RET(cmark_node *, cmark_node_new, (cmark_node_type type), (type), NULL)
WRAP_RET(cmark_node *, cmark_node_new_with_mem, (cmark_node_type type, cmark_mem *mem), (type, mem), NULL)
WRAP_VOID(cmark_node_free, (cmark_node *node), (node))

/* --- 树遍历 --- */
WRAP_RET(cmark_node *, cmark_node_next, (cmark_node *node), (node), NULL)
WRAP_RET(cmark_node *, cmark_node_previous, (cmark_node *node), (node), NULL)
WRAP_RET(cmark_node *, cmark_node_parent, (cmark_node *node), (node), NULL)
WRAP_RET(cmark_node *, cmark_node_first_child, (cmark_node *node), (node), NULL)
WRAP_RET(cmark_node *, cmark_node_last_child, (cmark_node *node), (node), NULL)

/* --- 迭代器 --- */
WRAP_RET(cmark_iter *, cmark_iter_new, (cmark_node *root), (root), NULL)
WRAP_VOID(cmark_iter_free, (cmark_iter *iter), (iter))
WRAP_RET(cmark_event_type, cmark_iter_next, (cmark_iter *iter), (iter), CMARK_EVENT_DONE)
WRAP_RET(cmark_node *, cmark_iter_get_node, (cmark_iter *iter), (iter), NULL)
WRAP_RET(cmark_event_type, cmark_iter_get_event_type, (cmark_iter *iter), (iter), CMARK_EVENT_NONE)
WRAP_RET(cmark_node *, cmark_iter_get_root, (cmark_iter *iter), (iter), NULL)
WRAP_VOID(cmark_iter_reset, (cmark_iter *iter, cmark_node *current, cmark_event_type event_type), (iter, current, event_type))

/* --- 属性访问 --- */
WRAP_RET(void *, cmark_node_get_user_data, (cmark_node *node), (node), NULL)
WRAP_RET(int, cmark_node_set_user_data, (cmark_node *node, void *user_data), (node, user_data), 0)
WRAP_RET(cmark_node_type, cmark_node_get_type, (cmark_node *node), (node), CMARK_NODE_NONE)
WRAP_RET(const char *, cmark_node_get_type_string, (cmark_node *node), (node), NULL)
WRAP_RET(const char *, cmark_node_get_literal, (cmark_node *node), (node), NULL)
WRAP_RET(int, cmark_node_set_literal, (cmark_node *node, const char *content), (node, content), 0)
WRAP_RET(int, cmark_node_get_heading_level, (cmark_node *node), (node), 0)
WRAP_RET(int, cmark_node_set_heading_level, (cmark_node *node, int level), (node, level), 0)
WRAP_RET(cmark_list_type, cmark_node_get_list_type, (cmark_node *node), (node), CMARK_NO_LIST)
WRAP_RET(int, cmark_node_set_list_type, (cmark_node *node, cmark_list_type type), (node, type), 0)
WRAP_RET(cmark_delim_type, cmark_node_get_list_delim, (cmark_node *node), (node), CMARK_NO_DELIM)
WRAP_RET(int, cmark_node_set_list_delim, (cmark_node *node, cmark_delim_type delim), (node, delim), 0)
WRAP_RET(int, cmark_node_get_list_start, (cmark_node *node), (node), 0)
WRAP_RET(int, cmark_node_set_list_start, (cmark_node *node, int start), (node, start), 0)
WRAP_RET(int, cmark_node_get_list_tight, (cmark_node *node), (node), 0)
WRAP_RET(int, cmark_node_set_list_tight, (cmark_node *node, int tight), (node, tight), 0)
WRAP_RET(const char *, cmark_node_get_fence_info, (cmark_node *node), (node), NULL)
WRAP_RET(int, cmark_node_set_fence_info, (cmark_node *node, const char *info), (node, info), 0)
WRAP_RET(const char *, cmark_node_get_url, (cmark_node *node), (node), NULL)
WRAP_RET(int, cmark_node_set_url, (cmark_node *node, const char *url), (node, url), 0)
WRAP_RET(const char *, cmark_node_get_title, (cmark_node *node), (node), NULL)
WRAP_RET(int, cmark_node_set_title, (cmark_node *node, const char *title), (node, title), 0)
WRAP_RET(const char *, cmark_node_get_on_enter, (cmark_node *node), (node), NULL)
WRAP_RET(int, cmark_node_set_on_enter, (cmark_node *node, const char *on_enter), (node, on_enter), 0)
WRAP_RET(const char *, cmark_node_get_on_exit, (cmark_node *node), (node), NULL)
WRAP_RET(int, cmark_node_set_on_exit, (cmark_node *node, const char *on_exit), (node, on_exit), 0)
WRAP_RET(int, cmark_node_get_start_line, (cmark_node *node), (node), 0)
WRAP_RET(int, cmark_node_get_start_column, (cmark_node *node), (node), 0)
WRAP_RET(int, cmark_node_get_end_line, (cmark_node *node), (node), 0)
WRAP_RET(int, cmark_node_get_end_column, (cmark_node *node), (node), 0)

/* --- 树操作 --- */
WRAP_VOID(cmark_node_unlink, (cmark_node *node), (node))
WRAP_RET(int, cmark_node_insert_before, (cmark_node *node, cmark_node *sibling), (node, sibling), 0)
WRAP_RET(int, cmark_node_insert_after, (cmark_node *node, cmark_node *sibling), (node, sibling), 0)
WRAP_RET(int, cmark_node_replace, (cmark_node *oldnode, cmark_node *newnode), (oldnode, newnode), 0)
WRAP_RET(int, cmark_node_prepend_child, (cmark_node *node, cmark_node *child), (node, child), 0)
WRAP_RET(int, cmark_node_append_child, (cmark_node *node, cmark_node *child), (node, child), 0)
WRAP_VOID(cmark_consolidate_text_nodes, (cmark_node *root), (root))

/* --- 解析器 --- */
WRAP_RET(cmark_parser *, cmark_parser_new, (int options), (options), NULL)
WRAP_RET(cmark_parser *, cmark_parser_new_with_mem, (int options, cmark_mem *mem), (options, mem), NULL)
WRAP_RET(cmark_parser *, cmark_parser_new_with_mem_into_root, (int options, cmark_mem *mem, cmark_node *root), (options, mem, root), NULL)
WRAP_VOID(cmark_parser_free, (cmark_parser *parser), (parser))
WRAP_VOID(cmark_parser_feed, (cmark_parser *parser, const char *buffer, size_t len), (parser, buffer, len))
WRAP_RET(cmark_node *, cmark_parser_finish, (cmark_parser *parser), (parser), NULL)
WRAP_RET(cmark_node *, cmark_parse_document, (const char *buffer, size_t len, int options), (buffer, len, options), NULL)
WRAP_RET(cmark_node *, cmark_parse_file, (FILE *f, int options), (f, options), NULL)

/* --- 渲染 --- */
WRAP_RET(char *, cmark_render_xml, (cmark_node *root, int options), (root, options), NULL)
WRAP_RET(char *, cmark_render_html, (cmark_node *root, int options), (root, options), NULL)
WRAP_RET(char *, cmark_render_man, (cmark_node *root, int options, int width), (root, options, width), NULL)
WRAP_RET(char *, cmark_render_commonmark, (cmark_node *root, int options, int width), (root, options, width), NULL)
WRAP_RET(char *, cmark_render_latex, (cmark_node *root, int options, int width), (root, options, width), NULL)

/* --- 版本 --- */
WRAP_RET(int, cmark_version, (void), (), 0)
WRAP_RET(const char *, cmark_version_string, (void), (), NULL)
