/* ============================================================
 * gumbo_dyn.c - dlopen 模拟链接：Gumbo HTML5 解析器
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照 vendor/gumbo/gumbo.h（与系统 gumbo 0.14.1
 * 头文件一致）。覆盖全部 13 个公开 API；4 个 const 数据符号不定义
 * 同名实体，改由 gumbo_dyn_default_options() 等 accessor 暴露。
 *
 * 上游头存在时包含真头做原型校验；否则内嵌 gumbo.h 的类型与
 * tag_enum.h 的全部标签常量（字段顺序即 ABI）。无 .h、无 wrapper。
 *
 * 单文件；链接期不需要 libgumbo，仅需 -ldl。
 * 加载顺序：显式路径 > GUMBO_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > Nix > 平台默认；成功后不卸载。
 *
 * 用法：
 *   gumbo_dyn_init(NULL);
 *   GumboOutput *out = gumbo_parse("<h1>Hi</h1>");
 *   gumbo_destroy_output(gumbo_dyn_default_options(), out);
 *   gumbo_dyn_cleanup();
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
  typedef HMODULE gumbo_lib_handle_t;
  static gumbo_lib_handle_t gumbo_load_lib(const char *path) { return LoadLibraryA(path); }
  static void *gumbo_get_sym(gumbo_lib_handle_t h, const char *name) { return (void *)GetProcAddress(h, name); }
  static void gumbo_free_lib(gumbo_lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *gumbo_lib_error(void) { return "LoadLibrary failed"; }
  #define GUMBO_PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *gumbo_lib_handle_t;
  static gumbo_lib_handle_t gumbo_load_lib(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
  static void *gumbo_get_sym(gumbo_lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void gumbo_free_lib(gumbo_lib_handle_t h) { if (h) dlclose(h); }
  static const char *gumbo_lib_error(void) { return dlerror(); }
  #define GUMBO_PATH_SEP ':'
#endif

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<gumbo.h>)
#    include <gumbo.h>
#    define GUMBO_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef GUMBO_DYN_HAVE_HEADER
/* ---------- 类型（gumbo.h 原样内嵌，字段顺序即 ABI） ---------- */
typedef struct {
  unsigned int line;
  unsigned int column;
  unsigned int offset;
} GumboSourcePosition;

typedef struct {
  const char *data;
  size_t length;
} GumboStringPiece;

typedef struct {
  void **data;
  unsigned int length;
  unsigned int capacity;
} GumboVector;

typedef enum {
  GUMBO_TAG_HTML,        GUMBO_TAG_HEAD,       GUMBO_TAG_TITLE,
  GUMBO_TAG_BASE,        GUMBO_TAG_LINK,       GUMBO_TAG_META,
  GUMBO_TAG_STYLE,       GUMBO_TAG_SCRIPT,     GUMBO_TAG_NOSCRIPT,
  GUMBO_TAG_TEMPLATE,    GUMBO_TAG_BODY,       GUMBO_TAG_ARTICLE,
  GUMBO_TAG_SECTION,     GUMBO_TAG_NAV,        GUMBO_TAG_ASIDE,
  GUMBO_TAG_H1,          GUMBO_TAG_H2,         GUMBO_TAG_H3,
  GUMBO_TAG_H4,          GUMBO_TAG_H5,         GUMBO_TAG_H6,
  GUMBO_TAG_HGROUP,      GUMBO_TAG_HEADER,     GUMBO_TAG_FOOTER,
  GUMBO_TAG_ADDRESS,     GUMBO_TAG_P,          GUMBO_TAG_HR,
  GUMBO_TAG_PRE,         GUMBO_TAG_BLOCKQUOTE, GUMBO_TAG_OL,
  GUMBO_TAG_UL,          GUMBO_TAG_LI,         GUMBO_TAG_DL,
  GUMBO_TAG_DT,          GUMBO_TAG_DD,         GUMBO_TAG_FIGURE,
  GUMBO_TAG_FIGCAPTION,  GUMBO_TAG_MAIN,       GUMBO_TAG_DIV,
  GUMBO_TAG_A,           GUMBO_TAG_EM,         GUMBO_TAG_STRONG,
  GUMBO_TAG_SMALL,       GUMBO_TAG_S,          GUMBO_TAG_CITE,
  GUMBO_TAG_Q,           GUMBO_TAG_DFN,        GUMBO_TAG_ABBR,
  GUMBO_TAG_DATA,        GUMBO_TAG_TIME,       GUMBO_TAG_CODE,
  GUMBO_TAG_VAR,         GUMBO_TAG_SAMP,       GUMBO_TAG_KBD,
  GUMBO_TAG_SUB,         GUMBO_TAG_SUP,        GUMBO_TAG_I,
  GUMBO_TAG_B,           GUMBO_TAG_U,          GUMBO_TAG_MARK,
  GUMBO_TAG_RUBY,        GUMBO_TAG_RT,         GUMBO_TAG_RP,
  GUMBO_TAG_BDI,         GUMBO_TAG_BDO,        GUMBO_TAG_SPAN,
  GUMBO_TAG_BR,          GUMBO_TAG_WBR,        GUMBO_TAG_INS,
  GUMBO_TAG_DEL,         GUMBO_TAG_IMAGE,      GUMBO_TAG_IMG,
  GUMBO_TAG_PICTURE,     GUMBO_TAG_IFRAME,     GUMBO_TAG_EMBED,
  GUMBO_TAG_OBJECT,      GUMBO_TAG_PARAM,      GUMBO_TAG_VIDEO,
  GUMBO_TAG_AUDIO,       GUMBO_TAG_SOURCE,     GUMBO_TAG_TRACK,
  GUMBO_TAG_CANVAS,      GUMBO_TAG_MAP,        GUMBO_TAG_AREA,
  GUMBO_TAG_MATH,        GUMBO_TAG_MI,         GUMBO_TAG_MO,
  GUMBO_TAG_MN,          GUMBO_TAG_MS,         GUMBO_TAG_MTEXT,
  GUMBO_TAG_MGLYPH,      GUMBO_TAG_MALIGNMARK, GUMBO_TAG_ANNOTATION_XML,
  GUMBO_TAG_SVG,         GUMBO_TAG_FOREIGNOBJECT, GUMBO_TAG_DESC,
  GUMBO_TAG_TABLE,       GUMBO_TAG_CAPTION,    GUMBO_TAG_COLGROUP,
  GUMBO_TAG_COL,         GUMBO_TAG_TBODY,      GUMBO_TAG_THEAD,
  GUMBO_TAG_TFOOT,       GUMBO_TAG_TR,         GUMBO_TAG_TD,
  GUMBO_TAG_TH,          GUMBO_TAG_FORM,       GUMBO_TAG_FIELDSET,
  GUMBO_TAG_LEGEND,      GUMBO_TAG_LABEL,      GUMBO_TAG_INPUT,
  GUMBO_TAG_BUTTON,      GUMBO_TAG_SELECT,     GUMBO_TAG_SELECTEDCONTENT,
  GUMBO_TAG_DATALIST,    GUMBO_TAG_OPTGROUP,   GUMBO_TAG_OPTION,
  GUMBO_TAG_TEXTAREA,    GUMBO_TAG_KEYGEN,     GUMBO_TAG_OUTPUT,
  GUMBO_TAG_PROGRESS,    GUMBO_TAG_METER,      GUMBO_TAG_DETAILS,
  GUMBO_TAG_SUMMARY,     GUMBO_TAG_MENU,       GUMBO_TAG_MENUITEM,
  GUMBO_TAG_APPLET,      GUMBO_TAG_ACRONYM,    GUMBO_TAG_BGSOUND,
  GUMBO_TAG_DIR,         GUMBO_TAG_FRAME,      GUMBO_TAG_FRAMESET,
  GUMBO_TAG_NOFRAMES,    GUMBO_TAG_ISINDEX,    GUMBO_TAG_LISTING,
  GUMBO_TAG_XMP,         GUMBO_TAG_NEXTID,     GUMBO_TAG_NOEMBED,
  GUMBO_TAG_PLAINTEXT,   GUMBO_TAG_RB,         GUMBO_TAG_STRIKE,
  GUMBO_TAG_BASEFONT,    GUMBO_TAG_BIG,        GUMBO_TAG_BLINK,
  GUMBO_TAG_CENTER,      GUMBO_TAG_FONT,       GUMBO_TAG_MARQUEE,
  GUMBO_TAG_MULTICOL,    GUMBO_TAG_NOBR,       GUMBO_TAG_SPACER,
  GUMBO_TAG_DIALOG,      GUMBO_TAG_SEARCH,     GUMBO_TAG_TT,
  GUMBO_TAG_RTC,
  GUMBO_TAG_UNKNOWN,
  GUMBO_TAG_LAST,
} GumboTag;

typedef enum {
  GUMBO_ATTR_NAMESPACE_NONE,
  GUMBO_ATTR_NAMESPACE_XLINK,
  GUMBO_ATTR_NAMESPACE_XML,
  GUMBO_ATTR_NAMESPACE_XMLNS,
} GumboAttributeNamespaceEnum;

typedef struct {
  GumboAttributeNamespaceEnum attr_namespace;
  const char *name;
  GumboStringPiece original_name;
  const char *value;
  GumboStringPiece original_value;
  GumboSourcePosition name_start;
  GumboSourcePosition name_end;
  GumboSourcePosition value_start;
  GumboSourcePosition value_end;
} GumboAttribute;

typedef enum {
  GUMBO_NODE_DOCUMENT,
  GUMBO_NODE_ELEMENT,
  GUMBO_NODE_TEXT,
  GUMBO_NODE_CDATA,
  GUMBO_NODE_COMMENT,
  GUMBO_NODE_WHITESPACE,
  GUMBO_NODE_TEMPLATE,
  GUMBO_NODE_PROCESSING_INSTRUCTION,
} GumboNodeType;

typedef struct GumboInternalNode GumboNode;

typedef enum {
  GUMBO_DOCTYPE_NO_QUIRKS,
  GUMBO_DOCTYPE_QUIRKS,
  GUMBO_DOCTYPE_LIMITED_QUIRKS,
} GumboQuirksModeEnum;

typedef enum {
  GUMBO_NAMESPACE_HTML,
  GUMBO_NAMESPACE_SVG,
  GUMBO_NAMESPACE_MATHML,
} GumboNamespaceEnum;

typedef enum {
  GUMBO_INSERTION_NORMAL = 0,
  GUMBO_INSERTION_BY_PARSER = 1 << 0,
  GUMBO_INSERTION_IMPLICIT_END_TAG = 1 << 1,
  GUMBO_INSERTION_IMPLIED = 1 << 3,
  GUMBO_INSERTION_CONVERTED_FROM_END_TAG = 1 << 4,
  GUMBO_INSERTION_FROM_ISINDEX = 1 << 5,
  GUMBO_INSERTION_FROM_IMAGE = 1 << 6,
  GUMBO_INSERTION_RECONSTRUCTED_FORMATTING_ELEMENT = 1 << 7,
  GUMBO_INSERTION_ADOPTION_AGENCY_CLONED = 1 << 8,
  GUMBO_INSERTION_ADOPTION_AGENCY_MOVED = 1 << 9,
  GUMBO_INSERTION_FOSTER_PARENTED = 1 << 10,
} GumboParseFlags;

typedef struct {
  GumboVector children;
  bool has_doctype;
  const char *name;
  const char *public_identifier;
  const char *system_identifier;
  GumboQuirksModeEnum doc_type_quirks_mode;
} GumboDocument;

typedef struct {
  const char *text;
  GumboStringPiece original_text;
  GumboSourcePosition start_pos;
} GumboText;

typedef struct {
  GumboVector children;
  GumboTag tag;
  GumboNamespaceEnum tag_namespace;
  GumboStringPiece original_tag;
  GumboStringPiece original_end_tag;
  GumboSourcePosition start_pos;
  GumboSourcePosition end_pos;
  GumboVector attributes;
} GumboElement;

struct GumboInternalNode {
  GumboNodeType type;
  GumboNode *parent;
  size_t index_within_parent;
  GumboParseFlags parse_flags;
  union {
    GumboDocument document;
    GumboElement element;
    GumboText text;
  } v;
};

typedef void *(*GumboAllocatorFunction)(void *userdata, size_t size);
typedef void (*GumboDeallocatorFunction)(void *userdata, void *ptr);

typedef struct GumboInternalOptions {
  GumboAllocatorFunction allocator;
  GumboDeallocatorFunction deallocator;
  void *userdata;
  int tab_stop;
  bool stop_on_first_error;
  int max_errors;
  GumboTag fragment_context;
  GumboNamespaceEnum fragment_namespace;
} GumboOptions;

typedef struct GumboInternalOutput {
  GumboNode *document;
  GumboNode *root;
  GumboVector errors;
} GumboOutput;

#endif /* !GUMBO_DYN_HAVE_HEADER */

/* ============================================================
 * 动态加载（单库，多候选路径）
 * ============================================================ */
static gumbo_lib_handle_t gumbo_handle = NULL;
static int gumbo_init_state = 0;

static void *gumbo_sym(const char *name) {
    return gumbo_handle ? gumbo_get_sym(gumbo_handle, name) : NULL;
}

/* 尝试加载单个库：显式路径、环境变量、LD 目录、soname、.so、Nix、默认路径 */
static gumbo_lib_handle_t gumbo_load_one(const char *library_path,
                                   const char *so_soname,
                                   const char *so_plain,
                                   const char *probe_symbol,
                                   const char *env_dir_name,
                                   int *out_idx, const char **out_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    int n = 0, i;
    gumbo_lib_handle_t h = NULL;

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
                    char *sep = strchr(q, GUMBO_PATH_SEP);
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
        h = gumbo_load_lib(candidates[i]);
        if (h && gumbo_get_sym(h, probe_symbol)) {
            if (out_idx) *out_idx = i;
            if (out_path) *out_path = candidates[i];
            break;
        }
        if (h) { gumbo_free_lib(h); h = NULL; }
    }
    return h;
}

static int gumbo_do_load(const char *library_path) {
    int idx = -1;
    char path_buf[1024];
    const char *path = NULL;

    if (gumbo_handle) return 0;

#ifdef _WIN32
    gumbo_handle = gumbo_load_one(library_path, "libgumbo.dll", "gumbo",
                                  "gumbo_parse", "GUMBO_DYN_LIB", &idx, &path);
#elif defined(__APPLE__)
    gumbo_handle = gumbo_load_one(library_path, "libgumbo.dylib", "libgumbo.dylib",
                                  "gumbo_parse", "GUMBO_DYN_LIB", &idx, &path);
#else
    gumbo_handle = gumbo_load_one(library_path, "libgumbo.so.4", "libgumbo.so",
                                  "gumbo_parse", "GUMBO_DYN_LIB", &idx, &path);
#endif
    if (!gumbo_handle) {
        fprintf(stderr, "gumbo_dyn: cannot load libgumbo (%s)\n", gumbo_lib_error());
        return -1;
    }
    if (path) { snprintf(path_buf, sizeof path_buf, "%s", path); path = path_buf; }
    fprintf(stderr, "gumbo_dyn: loaded candidate %d: %s\n", idx, path ? path : "(default)");
    return 0;
}

int gumbo_libok(void) {
    if (gumbo_init_state == 0)
        gumbo_init_state = (gumbo_do_load(NULL) == 0) ? 1 : -1;
    return (gumbo_init_state == 1 && gumbo_handle != NULL) ? 1 : 0;
}

int gumbo_dyn_init(const char *library_path) {
    if (gumbo_do_load(library_path) == 0) {
        gumbo_init_state = 1;
        return 0;
    }
    if (gumbo_init_state == 0) gumbo_init_state = -1;
    return -1;
}

void gumbo_dyn_cleanup(void) {
    if (gumbo_handle) { gumbo_free_lib(gumbo_handle); gumbo_handle = NULL; }
    gumbo_init_state = 0;
}

int gumbo_dyn_has_symbol(const char *name) {
    if (!name || !*name) return 0;
    if (!gumbo_libok()) return 0;
    return gumbo_sym(name) != NULL;
}

/* 数据符号 accessor：不定义同名实体，避免与真头 extern const 冲突 */
const GumboOptions *gumbo_dyn_default_options(void) {
    return (const GumboOptions *)gumbo_sym("kGumboDefaultOptions");
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define GUMBO_WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                          \
    static ret (*fn) params = NULL;                                        \
    if (!fn) {                                                             \
        if (!gumbo_libok()) return fallback;                               \
        *(void **)(&fn) = gumbo_sym(#name);                                \
        if (!fn) {                                                         \
            fprintf(stderr, "gumbo_dyn: dlsym failed: %s\n", #name);       \
            return fallback;                                               \
        }                                                                  \
    }                                                                      \
    return fn args;                                                        \
}

#define GUMBO_WRAP_VOID(name, params, args)                                      \
void name params {                                                         \
    static void (*fn) params = NULL;                                       \
    if (!fn) {                                                             \
        if (!gumbo_libok()) return;                                        \
        *(void **)(&fn) = gumbo_sym(#name);                                \
        if (!fn) {                                                         \
            fprintf(stderr, "gumbo_dyn: dlsym failed: %s\n", #name);       \
            return;                                                        \
        }                                                                  \
    }                                                                      \
    fn args;                                                               \
}

/* ============================================================
 * 导出函数封装（公开 API：13 个）
 * ============================================================ */

GUMBO_WRAP_RET(bool, gumbo_string_equals,
         (const GumboStringPiece *str1, const GumboStringPiece *str2),
         (str1, str2), false)

GUMBO_WRAP_RET(bool, gumbo_string_equals_ignore_case,
         (const GumboStringPiece *str1, const GumboStringPiece *str2),
         (str1, str2), false)

GUMBO_WRAP_RET(int, gumbo_vector_index_of,
         (GumboVector *vector, const void *element),
         (vector, element), -1)

GUMBO_WRAP_RET(bool, gumbo_tag_is_void, (GumboTag tag), (tag), false)

GUMBO_WRAP_RET(const char *, gumbo_normalized_tagname, (GumboTag tag), (tag), NULL)

GUMBO_WRAP_VOID(gumbo_tag_from_original_text, (GumboStringPiece *text), (text))

GUMBO_WRAP_RET(const char *, gumbo_normalize_svg_tagname,
         (const GumboStringPiece *tagname), (tagname), NULL)

GUMBO_WRAP_RET(GumboTag, gumbo_tag_enum, (const char *tagname), (tagname), GUMBO_TAG_UNKNOWN)

GUMBO_WRAP_RET(GumboTag, gumbo_tagn_enum,
         (const char *tagname, unsigned int length), (tagname, length), GUMBO_TAG_UNKNOWN)

GUMBO_WRAP_RET(GumboAttribute *, gumbo_get_attribute,
         (const GumboVector *attrs, const char *name), (attrs, name), NULL)

GUMBO_WRAP_RET(GumboOutput *, gumbo_parse, (const char *buffer), (buffer), NULL)

GUMBO_WRAP_RET(GumboOutput *, gumbo_parse_with_options,
         (const GumboOptions *options, const char *buffer, size_t buffer_length),
         (options, buffer, buffer_length), NULL)

GUMBO_WRAP_VOID(gumbo_destroy_output,
          (const GumboOptions *options, GumboOutput *output), (options, output))
