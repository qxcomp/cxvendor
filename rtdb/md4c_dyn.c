/* ============================================================
 * md4c_dyn.c - dlopen 模拟链接：MD4C Markdown 解析器（核心 + HTML 渲染）
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照本机 /usr/include/md4c.h 与 md4c-html.h。
 * 覆盖两个库的全部公开 API：核心库 md_parse、html 库 md_html。
 * 内部导出符号 entity_lookup 无公开声明，按约定不封装。
 * 类型定义按头文件原样内嵌（字段顺序即 ABI）；无 .h、无 wrapper。
 *
 * 单文件；链接期不需要 libmd4c / libmd4c-html，仅需 -ldl。
 * 加载顺序：显式路径 > MD4C_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > Nix > 平台默认；成功后不卸载。
 *
 * 用法：
 *   md4c_dyn_init(NULL);
 *   md_html("# Hi\n", 5, output_cb, ctx, 0, 0);
 *   md_parse(text, size, &parser, userdata);
 *   md4c_dyn_cleanup();
 * ============================================================ */

/* 双 ABI / 函数指针转换场景下抑制 GCC 警告 */
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
 typedef HMODULE md4c_lib_handle_t;
 static md4c_lib_handle_t md4c_load_lib(const char *path) { return LoadLibraryA(path); }
 static void *md4c_get_sym(md4c_lib_handle_t h, const char *name) { return (void *)GetProcAddress(h, name); }
 static void md4c_free_lib(md4c_lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *md4c_lib_error(void) { return "LoadLibrary failed"; }
 #define MD4C_PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *md4c_lib_handle_t;
 static md4c_lib_handle_t md4c_load_lib(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
 static void *md4c_get_sym(md4c_lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void md4c_free_lib(md4c_lib_handle_t h) { if (h) dlclose(h); }
 static const char *md4c_lib_error(void) { return dlerror(); }
 #define MD4C_PATH_SEP ':'
#endif

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<md4c.h>) && __has_include(<md4c-html.h>)
#    include <md4c.h>
#    include <md4c-html.h>
#    define MD4C_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef MD4C_DYN_HAVE_HEADER
/* ---------- 类型（md4c.h 原样内嵌，字段顺序即 ABI） ---------- */
typedef char          MD_CHAR;
typedef unsigned      MD_SIZE;
typedef unsigned      MD_OFFSET;

typedef enum MD_BLOCKTYPE {
    MD_BLOCK_DOC = 0,
    MD_BLOCK_QUOTE,
    MD_BLOCK_UL,
    MD_BLOCK_OL,
    MD_BLOCK_LI,
    MD_BLOCK_HR,
    MD_BLOCK_H,
    MD_BLOCK_CODE,
    MD_BLOCK_HTML,
    MD_BLOCK_P,
    MD_BLOCK_TABLE,
    MD_BLOCK_THEAD,
    MD_BLOCK_TBODY,
    MD_BLOCK_TR,
    MD_BLOCK_TH,
    MD_BLOCK_TD,
    MD_BLOCK_FOOTNOTE_DEF_SECTION,
    MD_BLOCK_FOOTNOTE_DEF,
    MD_BLOCK_ADMONITION,
    MD_BLOCK_BLANK
} MD_BLOCKTYPE;

typedef enum MD_SPANTYPE {
    MD_SPAN_EM,
    MD_SPAN_STRONG,
    MD_SPAN_A,
    MD_SPAN_IMG,
    MD_SPAN_CODE,
    MD_SPAN_INS,
    MD_SPAN_DEL,
    MD_SPAN_LATEXMATH,
    MD_SPAN_LATEXMATH_DISPLAY,
    MD_SPAN_WIKILINK,
    MD_SPAN_U,
    MD_SPAN_SPOILER,
    MD_SPAN_SUPERSCRIPT,
    MD_SPAN_SUBSCRIPT,
    MD_SPAN_FOOTNOTE_REF,
    MD_SPAN_MARK
} MD_SPANTYPE;

typedef enum MD_TEXTTYPE {
    MD_TEXT_NORMAL = 0,
    MD_TEXT_NULLCHAR,
    MD_TEXT_BR,
    MD_TEXT_SOFTBR,
    MD_TEXT_ENTITY,
    MD_TEXT_CODE,
    MD_TEXT_HTML,
    MD_TEXT_LATEXMATH
} MD_TEXTTYPE;

typedef enum MD_ALIGN {
    MD_ALIGN_DEFAULT = 0,
    MD_ALIGN_LEFT,
    MD_ALIGN_CENTER,
    MD_ALIGN_RIGHT
} MD_ALIGN;

typedef struct MD_ATTRIBUTE {
    const MD_CHAR *text;
    MD_SIZE size;
    const MD_TEXTTYPE *substr_types;
    const MD_OFFSET *substr_offsets;
} MD_ATTRIBUTE;

typedef struct MD_BLOCK_UL_DETAIL {
    int is_tight;
    MD_CHAR mark;
} MD_BLOCK_UL_DETAIL;

typedef struct MD_BLOCK_OL_DETAIL {
    unsigned start;
    int is_tight;
    MD_CHAR mark_delimiter;
} MD_BLOCK_OL_DETAIL;

typedef struct MD_BLOCK_LI_DETAIL {
    int is_task;
    MD_CHAR task_mark;
    MD_OFFSET task_mark_offset;
} MD_BLOCK_LI_DETAIL;

typedef struct MD_BLOCK_H_DETAIL {
    unsigned level;
} MD_BLOCK_H_DETAIL;

typedef struct MD_BLOCK_CODE_DETAIL {
    MD_ATTRIBUTE info;
    MD_ATTRIBUTE lang;
    MD_CHAR fence_char;
} MD_BLOCK_CODE_DETAIL;

typedef struct MD_BLOCK_TABLE_DETAIL {
    unsigned col_count;
    unsigned head_row_count;
    unsigned body_row_count;
} MD_BLOCK_TABLE_DETAIL;

typedef struct MD_BLOCK_TD_DETAIL {
    MD_ALIGN align;
} MD_BLOCK_TD_DETAIL;

typedef struct MD_BLOCK_ADMONITION_DETAIL {
    MD_ATTRIBUTE type;
} MD_BLOCK_ADMONITION_DETAIL;

typedef struct MD_SPAN_A_DETAIL {
    MD_ATTRIBUTE href;
    MD_ATTRIBUTE title;
    int is_autolink;
} MD_SPAN_A_DETAIL;

typedef struct MD_SPAN_IMG_DETAIL {
    MD_ATTRIBUTE src;
    MD_ATTRIBUTE title;
} MD_SPAN_IMG_DETAIL;

typedef struct MD_SPAN_WIKILINK {
    MD_ATTRIBUTE target;
} MD_SPAN_WIKILINK_DETAIL;

typedef struct MD_SPAN_FOOTNOTE_REF_DETAIL {
    unsigned int id;
    unsigned int ref_id;
    MD_ATTRIBUTE label;
} MD_SPAN_FOOTNOTE_REF_DETAIL;

typedef struct MD_BLOCK_FOOTNOTE_DEF_DETAIL {
    unsigned int id;
    unsigned int ref_count;
    MD_ATTRIBUTE label;
} MD_BLOCK_FOOTNOTE_DEF_DETAIL;

typedef struct MD_BLOCK_BLANK_DETAIL {
    unsigned line_count;
} MD_BLOCK_BLANK_DETAIL;

#define MD_FLAG_COLLAPSEWHITESPACE          0x1
#define MD_FLAG_PERMISSIVEATXHEADERS        0x2
#define MD_FLAG_PERMISSIVEURLAUTOLINKS      0x4
#define MD_FLAG_PERMISSIVEEMAILAUTOLINKS    0x8
#define MD_FLAG_NOINDENTEDCODEBLOCKS        0x10
#define MD_FLAG_NOHTMLBLOCKS                0x20
#define MD_FLAG_NOHTMLSPANS                 0x40
#define MD_FLAG_TABLES                      0x100
#define MD_FLAG_STRIKETHROUGH               0x200
#define MD_FLAG_PERMISSIVEWWWAUTOLINKS      0x400
#define MD_FLAG_TASKLISTS                   0x800
#define MD_FLAG_LATEXMATHSPANS              0x1000
#define MD_FLAG_WIKILINKS                   0x2000
#define MD_FLAG_UNDERLINE                   0x4000
#define MD_FLAG_HARD_SOFT_BREAKS            0x8000
#define MD_FLAG_SPOILERS                    0x10000
#define MD_FLAG_SUPERSCRIPTS                0x20000
#define MD_FLAG_SUBSCRIPTS                  0x40000
#define MD_FLAG_ADMONITIONS                 0x80000
#define MD_FLAG_FOOTNOTES                   0x100000
#define MD_FLAG_HIGHLIGHT                   0x200000
#define MD_FLAG_PRESERVEBLANKLINES          0x400000
#define MD_FLAG_INSERT                      0x800000

#define MD_FLAG_PERMISSIVEAUTOLINKS (MD_FLAG_PERMISSIVEEMAILAUTOLINKS | \
                                     MD_FLAG_PERMISSIVEURLAUTOLINKS | \
                                     MD_FLAG_PERMISSIVEWWWAUTOLINKS)
#define MD_FLAG_NOHTML (MD_FLAG_NOHTMLBLOCKS | MD_FLAG_NOHTMLSPANS)

#define MD_DIALECT_COMMONMARK 0
#define MD_DIALECT_GITHUB (MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_TABLES | \
                           MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | \
                           MD_FLAG_ADMONITIONS | MD_FLAG_FOOTNOTES)

typedef struct MD_PARSER {
    unsigned abi_version;
    unsigned flags;
    int (*enter_block)(MD_BLOCKTYPE, void *, void *);
    int (*leave_block)(MD_BLOCKTYPE, void *, void *);
    int (*enter_span)(MD_SPANTYPE, void *, void *);
    int (*leave_span)(MD_SPANTYPE, void *, void *);
    int (*text)(MD_TEXTTYPE, const MD_CHAR *, MD_SIZE, void *);
    void (*debug_log)(const char *, void *);
    void (*syntax)(void);
} MD_PARSER;

typedef MD_PARSER MD_RENDERER;

/* md4c-html.h 的渲染标志 */
#define MD_HTML_FLAG_DEBUG            0x0001
#define MD_HTML_FLAG_VERBATIM_ENTITIES 0x0002
#define MD_HTML_FLAG_SKIP_UTF8_BOM    0x0004
#define MD_HTML_FLAG_XHTML            0x0008

#endif /* MD4C_DYN_HAVE_HEADER */

/* ============================================================
 * 动态加载（双库：核心 + html；多候选路径）
 * ============================================================ */
static md4c_lib_handle_t md4c_handle_core = NULL;
static md4c_lib_handle_t md4c_handle_html = NULL;
static int md4c_init_state = 0;

static void *md4c_sym(const char *name) {
    void *p = md4c_handle_core ? md4c_get_sym(md4c_handle_core, name) : NULL;
    if (p) return p;
    return md4c_handle_html ? md4c_get_sym(md4c_handle_html, name) : NULL;
}

/* 尝试加载单个库：显式路径、环境变量、LD 目录、soname、.so、Nix、默认路径 */
static md4c_lib_handle_t md4c_load_one(const char *library_path,
                                  const char *so_soname,
                                  const char *so_plain,
                                  const char *probe_symbol,
                                  const char *env_dir_name,
                                  int *out_idx, const char **out_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    int n = 0, i;
    md4c_lib_handle_t h = NULL;

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
                    char *sep = strchr(q, MD4C_PATH_SEP);
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
        h = md4c_load_lib(candidates[i]);
        if (h && md4c_get_sym(h, probe_symbol)) {
            if (out_idx) *out_idx = i;
            if (out_path) *out_path = candidates[i];
            break;
        }
        if (h) { md4c_free_lib(h); h = NULL; }
    }
    return h;
}

static int md4c_do_load(const char *library_path) {
    if (md4c_handle_core && md4c_handle_html) return 0;

    int core_idx = -1;
    char core_path_buf[1024];
    const char *core_path = NULL;
    if (!md4c_handle_core)
        md4c_handle_core = md4c_load_one(library_path, "libmd4c.so.0", "libmd4c.so",
                                         "md_parse", "MD4C_DYN_LIB", &core_idx, &core_path);
    if (core_path) {
        snprintf(core_path_buf, sizeof core_path_buf, "%s", core_path);
        core_path = core_path_buf;
    }
    if (!md4c_handle_html)
        md4c_handle_html = md4c_load_one(library_path, "libmd4c-html.so.0", "libmd4c-html.so",
                                         "md_html", "MD4C_DYN_LIB", NULL, NULL);

    if (!md4c_handle_core) {
        fprintf(stderr, "md4c_dyn: cannot load md4c core (%s)\n", md4c_lib_error());
        return -1;
    }
    if (!md4c_handle_html)
        fprintf(stderr, "md4c_dyn: warning: md4c-html not loaded, md_html unavailable (%s)\n", md4c_lib_error());
    fprintf(stderr, "md4c_dyn: loaded %d %s (core=%s html=%s)\n", core_idx, core_path, md4c_handle_core ? "yes" : "no", md4c_handle_html ? "yes" : "no");
    return 0;
}

int md4c_libok(void) {
    if (md4c_init_state == 0)
        md4c_init_state = (md4c_do_load(NULL) == 0) ? 1 : -1;
    return (md4c_init_state == 1 && md4c_handle_core != NULL) ? 1 : 0;
}

int md4c_dyn_init(const char *library_path) {
    if (md4c_do_load(library_path) == 0) {
        md4c_init_state = 1;
        return 0;
    }
    if (md4c_init_state == 0) md4c_init_state = -1;
    return -1;
}

void md4c_dyn_cleanup(void) {
    if (md4c_handle_core) { md4c_free_lib(md4c_handle_core); md4c_handle_core = NULL; }
    if (md4c_handle_html) { md4c_free_lib(md4c_handle_html); md4c_handle_html = NULL; }
    md4c_init_state = 0;
}

int md4c_dyn_has_symbol(const char *name) {
    if (!md4c_libok()) return 0;
    return md4c_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define MD4C_WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                          \
    static ret (*fn) params = NULL;                                        \
    if (!fn) {                                                             \
        if (!md4c_libok()) return fallback;                                \
        *(void **)(&fn) = md4c_sym(#name);                                 \
        if (!fn) {                                                         \
            fprintf(stderr, "md4c_dyn: dlsym failed: %s\n", #name);        \
            return fallback;                                               \
        }                                                                  \
    }                                                                      \
    return fn args;                                                        \
}

/* ============================================================
 * 导出函数封装（公开 API：md_parse + md_html）
 * ============================================================ */

/* --- 核心库：md4c.h --- */
MD4C_WRAP_RET(int, md_parse,
         (const MD_CHAR *text, MD_SIZE size, const MD_PARSER *parser, void *userdata),
         (text, size, parser, userdata), -1)

/* --- html 库：md4c-html.h --- */
MD4C_WRAP_RET(int, md_html,
         (const MD_CHAR *input, MD_SIZE input_size,
          void (*process_output)(const MD_CHAR *, MD_SIZE, void *),
          void *userdata, unsigned parser_flags, unsigned renderer_flags),
         (input, input_size, process_output, userdata, parser_flags, renderer_flags), -1)
