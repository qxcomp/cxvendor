/* ============================================================
 * lexbor_dyn.c - dlopen 模拟链接：Lexbor HTML5 解析器（链接预览子集）
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照 lexbor 3.0.1 的公开头文件（vcpkg port 同版本）。
 * 只覆盖链接预览所需的最小 API：文档创建/销毁/解析、title、DOM 遍历、
 * 标签名/文本/属性提取，共 14 个函数。
 *
 * Lexbor 的结构体庞大且相互嵌套（mraw/hash/arena 等），这里不复制其布局，
 * 而是用同名不透明指针类型 + 与上游完全一致的基础类型/枚举（ABI 等价）：
 *   lxb_char_t = unsigned char, lxb_status_t = unsigned int,
 *   lxb_tag_id_t = uintptr_t。
 * 内联函数（lxb_dom_node_next/first_child/type 等）在动态库中对应 *_noi
 * 版本，封装直接调用 ABI 符号。
 *
 * 单文件、无 .h、无 wrapper；链接期不需要 liblexbor，仅需 -ldl。
 * 加载顺序：显式路径 > LEXBOR_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > Nix > 平台默认；成功后不卸载。
 *
 * 用法：
 *   lexbor_dyn_init(NULL);
 *   lxb_html_document_t *doc = lxb_html_document_create();
 *   lxb_html_document_parse(doc, html, len);
 *   lxb_html_document_destroy(doc);
 *   lexbor_dyn_cleanup();
 * ============================================================ */

/* 函数指针转换场景下抑制 GCC 警告 */
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
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

/* ---------- 平台适配层 ---------- */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE lexbor_lib_handle_t;
  static lexbor_lib_handle_t lexbor_load_lib(const char *path) { return LoadLibraryA(path); }
  static void *lexbor_get_sym(lexbor_lib_handle_t h, const char *name) { return (void *)GetProcAddress(h, name); }
  static void lexbor_free_lib(lexbor_lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *lexbor_lib_error(void) { return "LoadLibrary failed"; }
  #define LEXBOR_PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *lexbor_lib_handle_t;
  static lexbor_lib_handle_t lexbor_load_lib(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
  static void *lexbor_get_sym(lexbor_lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void lexbor_free_lib(lexbor_lib_handle_t h) { if (h) dlclose(h); }
  static const char *lexbor_lib_error(void) { return dlerror(); }
  #define LEXBOR_PATH_SEP ':'
#endif

/* ---------- 与 lexbor 上游 ABI 等价的最小类型 ---------- */
typedef unsigned char lxb_char_t;
typedef unsigned int  lxb_status_t;
typedef uintptr_t     lxb_tag_id_t;

typedef struct lxb_html_document     lxb_html_document_t;
typedef struct lxb_html_body_element lxb_html_body_element_t;
typedef struct lxb_dom_node          lxb_dom_node_t;
typedef struct lxb_dom_element       lxb_dom_element_t;

typedef enum {
    LXB_DOM_NODE_TYPE_UNDEF      = 0x00,
    LXB_DOM_NODE_TYPE_ELEMENT    = 0x01,
    LXB_DOM_NODE_TYPE_ATTRIBUTE  = 0x02,
    LXB_DOM_NODE_TYPE_TEXT       = 0x03,
    LXB_DOM_NODE_TYPE_CDATA_SECTION = 0x04,
    LXB_DOM_NODE_TYPE_COMMENT    = 0x08,
    LXB_DOM_NODE_TYPE_DOCUMENT   = 0x09,
    LXB_DOM_NODE_TYPE_DOCUMENT_TYPE = 0x0A,
    LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT = 0x0B,
} lxb_dom_node_type_t;

/* 上游 LXB_STATUS_OK == 0；失败返回 1（LXB_STATUS_ERROR） */
#define LEXBOR_DYN_STATUS_OK    0u
#define LEXBOR_DYN_STATUS_ERROR 1u

/* ============================================================
 * 动态加载（单库，多候选路径）
 * ============================================================ */
static lexbor_lib_handle_t lexbor_handle = NULL;
static int lexbor_init_state = 0;

static void *lexbor_sym(const char *name) {
    return lexbor_handle ? lexbor_get_sym(lexbor_handle, name) : NULL;
}

/* 尝试加载单个库：显式路径、环境变量、LD 目录、soname、.so、Nix、默认路径 */
static lexbor_lib_handle_t lexbor_load_one(const char *library_path,
                                    const char *so_soname,
                                    const char *so_plain,
                                    const char *probe_symbol,
                                    const char *env_dir_name,
                                    int *out_idx, const char **out_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    int n = 0, i;
    lexbor_lib_handle_t h = NULL;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv(env_dir_name);
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
                    char *sep = strchr(q, LEXBOR_PATH_SEP);
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
    snprintf(bufs[n], sizeof bufs[0], "/opt/homebrew/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
#elif defined(_WIN32)
    snprintf(bufs[n], sizeof bufs[0], "%s.dll", so_plain);
    candidates[n] = bufs[n]; n++;
#else
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
#endif

    for (i = 0; i < n; i++) {
        h = lexbor_load_lib(candidates[i]);
        if (h && lexbor_get_sym(h, probe_symbol)) {
            if (out_idx) *out_idx = i;
            if (out_path) *out_path = candidates[i];
            break;
        }
        if (h) { lexbor_free_lib(h); h = NULL; }
    }
    return h;
}

static int lexbor_do_load(const char *library_path) {
    int idx = -1;
    char path_buf[1024];
    const char *path = NULL;

    if (lexbor_handle) return 0;

#ifdef _WIN32
    lexbor_handle = lexbor_load_one(library_path, "liblexbor.dll", "lexbor",
                                    "lxb_html_document_create", "LEXBOR_DYN_LIB",
                                    &idx, &path);
#elif defined(__APPLE__)
    lexbor_handle = lexbor_load_one(library_path, "liblexbor.dylib", "liblexbor.dylib",
                                    "lxb_html_document_create", "LEXBOR_DYN_LIB",
                                    &idx, &path);
#else
    lexbor_handle = lexbor_load_one(library_path, "liblexbor.so.3", "liblexbor.so",
                                    "lxb_html_document_create", "LEXBOR_DYN_LIB",
                                    &idx, &path);
#endif
    if (!lexbor_handle) {
        fprintf(stderr, "lexbor_dyn: cannot load liblexbor (%s)\n", lexbor_lib_error());
        return -1;
    }
    if (path) { snprintf(path_buf, sizeof path_buf, "%s", path); path = path_buf; }
    fprintf(stderr, "lexbor_dyn: loaded candidate %d: %s\n", idx, path ? path : "(default)");
    return 0;
}

int lexbor_libok(void) {
    if (lexbor_init_state == 0)
        lexbor_init_state = (lexbor_do_load(NULL) == 0) ? 1 : -1;
    return (lexbor_init_state == 1 && lexbor_handle != NULL) ? 1 : 0;
}

int lexbor_dyn_init(const char *library_path) {
    if (lexbor_do_load(library_path) == 0) {
        lexbor_init_state = 1;
        return 0;
    }
    if (lexbor_init_state == 0) lexbor_init_state = -1;
    return -1;
}

void lexbor_dyn_cleanup(void) {
    if (lexbor_handle) { lexbor_free_lib(lexbor_handle); lexbor_handle = NULL; }
    lexbor_init_state = 0;
}

int lexbor_dyn_has_symbol(const char *name) {
    if (!name || !*name) return 0;
    if (!lexbor_libok()) return 0;
    return lexbor_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define LEXBOR_WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                          \
    static ret (*fn) params = NULL;                                        \
    if (!fn) {                                                             \
        if (!lexbor_libok()) return fallback;                              \
        *(void **)(&fn) = lexbor_sym(#name);                               \
        if (!fn) {                                                         \
            fprintf(stderr, "lexbor_dyn: dlsym failed: %s\n", #name);      \
            return fallback;                                               \
        }                                                                  \
    }                                                                      \
    return fn args;                                                        \
}

/* ============================================================
 * 导出函数封装（链接预览子集：14 个）
 * ============================================================ */

/* --- 文档 --- */
LEXBOR_WRAP_RET(lxb_html_document_t *, lxb_html_document_create, (void), (), NULL)

LEXBOR_WRAP_RET(lxb_html_document_t *, lxb_html_document_destroy,
         (lxb_html_document_t *document), (document), NULL)

LEXBOR_WRAP_RET(lxb_status_t, lxb_html_document_parse,
         (lxb_html_document_t *document, const lxb_char_t *html, size_t size),
         (document, html, size), LEXBOR_DYN_STATUS_ERROR)

LEXBOR_WRAP_RET(const lxb_char_t *, lxb_html_document_title,
         (lxb_html_document_t *document, size_t *len), (document, len), NULL)

LEXBOR_WRAP_RET(lxb_html_body_element_t *, lxb_html_document_body_element_noi,
         (lxb_html_document_t *document), (document), NULL)

/* --- DOM 遍历（ABI 版本，非内联） --- */
LEXBOR_WRAP_RET(lxb_dom_node_t *, lxb_dom_node_first_child_noi,
         (lxb_dom_node_t *node), (node), NULL)

LEXBOR_WRAP_RET(lxb_dom_node_t *, lxb_dom_node_next_noi,
         (lxb_dom_node_t *node), (node), NULL)

LEXBOR_WRAP_RET(lxb_dom_node_t *, lxb_dom_node_parent_noi,
         (lxb_dom_node_t *node), (node), NULL)

LEXBOR_WRAP_RET(lxb_dom_node_type_t, lxb_dom_node_type_noi,
         (lxb_dom_node_t *node), (node), LXB_DOM_NODE_TYPE_UNDEF)

LEXBOR_WRAP_RET(const lxb_char_t *, lxb_dom_node_name,
         (lxb_dom_node_t *node, size_t *len), (node, len), NULL)

LEXBOR_WRAP_RET(lxb_char_t *, lxb_dom_node_text_content,
         (lxb_dom_node_t *node, size_t *len), (node, len), NULL)

/* --- 元素属性与标签名 --- */
LEXBOR_WRAP_RET(const lxb_char_t *, lxb_dom_element_get_attribute,
         (lxb_dom_element_t *element, const lxb_char_t *qualified_name,
          size_t qn_len, size_t *value_len),
         (element, qualified_name, qn_len, value_len), NULL)

LEXBOR_WRAP_RET(const lxb_char_t *, lxb_dom_element_tag_name,
         (lxb_dom_element_t *element, size_t *len), (element, len), NULL)

LEXBOR_WRAP_RET(const lxb_char_t *, lxb_dom_element_local_name,
         (lxb_dom_element_t *element, size_t *len), (element, len), NULL)
