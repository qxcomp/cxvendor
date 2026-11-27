/* gumbo_dyn 测试：只链 -ldl，不链 libgumbo
 * 覆盖：动态加载、控制函数、符号探测（含负例）、纯函数、解析树遍历、
 *       属性提取、默认选项 accessor、parse_with_options 与 destroy_output。
 * 库缺失时按 TEST_SKIP 正常退出。
 */
#include "test_common.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* 最小 ABI 类型声明：字段顺序/偏移与 gumbo.h 一致，不 include 目标库头 */
typedef struct {
    unsigned int line, column, offset;
} GumboSourcePosition;

typedef struct {
    const char *data;
    size_t length;
} GumboStringPiece;

typedef struct {
    void **data;
    unsigned int length, capacity;
} GumboVector;

typedef enum {
    GUMBO_TAG_HTML  = 0,
    GUMBO_TAG_HEAD  = 1,
    GUMBO_TAG_TITLE = 2,
    GUMBO_TAG_META  = 5,
    GUMBO_TAG_DIV   = 38,
    GUMBO_TAG_BR    = 66,
    GUMBO_TAG_UNKNOWN = 154
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
    GumboSourcePosition name_start, name_end, value_start, value_end;
} GumboAttribute;

typedef enum {
    GUMBO_NODE_DOCUMENT,
    GUMBO_NODE_ELEMENT,
    GUMBO_NODE_TEXT,
    GUMBO_NODE_CDATA,
    GUMBO_NODE_COMMENT,
    GUMBO_NODE_WHITESPACE,
    GUMBO_NODE_TEMPLATE,
    GUMBO_NODE_PROCESSING_INSTRUCTION
} GumboNodeType;

typedef enum {
    GUMBO_DOCTYPE_NO_QUIRKS,
    GUMBO_DOCTYPE_QUIRKS,
    GUMBO_DOCTYPE_LIMITED_QUIRKS
} GumboQuirksModeEnum;

typedef enum {
    GUMBO_NAMESPACE_HTML,
    GUMBO_NAMESPACE_SVG,
    GUMBO_NAMESPACE_MATHML
} GumboNamespaceEnum;

typedef enum { GUMBO_INSERTION_NORMAL = 0 } GumboParseFlags;

typedef struct {
    GumboVector children;
    bool has_doctype;
    const char *name, *public_identifier, *system_identifier;
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
    GumboStringPiece original_tag, original_end_tag;
    GumboSourcePosition start_pos, end_pos;
    GumboVector attributes;
} GumboElement;

typedef struct GumboInternalNode GumboNode;
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

typedef struct GumboInternalOptions GumboOptions;
typedef struct GumboInternalOutput {
    GumboNode *document;
    GumboNode *root;
    GumboVector errors;
} GumboOutput;

extern int  gumbo_dyn_init(const char *);
extern int  gumbo_libok(void);
extern void gumbo_dyn_cleanup(void);
extern int  gumbo_dyn_has_symbol(const char *);
extern const GumboOptions *gumbo_dyn_default_options(void);

extern bool gumbo_string_equals(const GumboStringPiece *, const GumboStringPiece *);
extern bool gumbo_string_equals_ignore_case(const GumboStringPiece *, const GumboStringPiece *);
extern int  gumbo_vector_index_of(GumboVector *, const void *);
extern bool gumbo_tag_is_void(GumboTag);
extern const char *gumbo_normalized_tagname(GumboTag);
extern void gumbo_tag_from_original_text(GumboStringPiece *);
extern const char *gumbo_normalize_svg_tagname(const GumboStringPiece *);
extern GumboTag gumbo_tag_enum(const char *);
extern GumboTag gumbo_tagn_enum(const char *, unsigned int);
extern GumboAttribute *gumbo_get_attribute(const GumboVector *, const char *);
extern GumboOutput *gumbo_parse(const char *);
extern GumboOutput *gumbo_parse_with_options(const GumboOptions *, const char *, size_t);
extern void gumbo_destroy_output(const GumboOptions *, GumboOutput *);

/* 递归统计元素/文本，并寻找 meta description 与 title 文本 */
static int el_count, txt_count, meta_ok, title_ok;
static void walk(GumboNode *n) {
    unsigned int i;
    if (!n) return;
    if (n->type == GUMBO_NODE_ELEMENT || n->type == GUMBO_NODE_TEMPLATE) {
        GumboElement *e = &n->v.element;
        el_count++;
        if (e->tag == GUMBO_TAG_META) {
            GumboAttribute *a = gumbo_get_attribute(&e->attributes, "content");
            if (a && a->value && strcmp(a->value, "gumbo dyn test") == 0) meta_ok = 1;
        }
        for (i = 0; i < e->children.length; i++)
            walk((GumboNode *)e->children.data[i]);
    } else if (n->type == GUMBO_NODE_TEXT) {
        txt_count++;
        if (n->v.text.text && strstr(n->v.text.text, "Hello Gumbo") != NULL)
            title_ok = 1;
    }
}

int main(void) {
    static const char html[] =
        "<!doctype html><html><head><title>Hello Gumbo</title>"
        "<meta name=\"description\" content=\"gumbo dyn test\">"
        "</head><body><div id=\"main\">text</div></body></html>";
    int rc = 0;
    GumboOutput *out;
    const GumboOptions *opts;
    GumboStringPiece s1, s2, orig;
    void *items[2];
    int sentinel;
    GumboVector vec;

    if (gumbo_dyn_init(NULL) != 0 || !gumbo_libok())
        TEST_SKIP("libgumbo not available\n");

    rc += CHECK(gumbo_libok());
    rc += CHECK(gumbo_dyn_has_symbol("gumbo_parse"));
    rc += CHECK(gumbo_dyn_has_symbol("kGumboDefaultOptions"));
    rc += CHECK(!gumbo_dyn_has_symbol("gumbo_no_such_symbol_xyz"));

    /* 纯函数 */
    rc += CHECK(gumbo_tag_enum("DIV") == GUMBO_TAG_DIV);
    rc += CHECK(gumbo_tagn_enum("METAX", 4) == GUMBO_TAG_META);
    rc += CHECK(strcmp(gumbo_normalized_tagname(GUMBO_TAG_DIV), "div") == 0);
    rc += CHECK(gumbo_tag_is_void(GUMBO_TAG_BR));
    rc += CHECK(!gumbo_tag_is_void(GUMBO_TAG_DIV));

    s1.data = "Hello"; s1.length = 5;
    s2.data = "hello"; s2.length = 5;
    rc += CHECK(!gumbo_string_equals(&s1, &s2));
    rc += CHECK(gumbo_string_equals_ignore_case(&s1, &s2));

    orig.data = "<DiV class=x>"; orig.length = 13;
    gumbo_tag_from_original_text(&orig);
    rc += CHECK(orig.length == 3 && memcmp(orig.data, "DiV", 3) == 0);

    items[0] = &sentinel; items[1] = &rc;
    vec.data = items; vec.length = 2; vec.capacity = 2;
    rc += CHECK(gumbo_vector_index_of(&vec, &rc) == 1);
    rc += CHECK(gumbo_vector_index_of(&vec, &items[0]) == -1);

    /* 解析与遍历 */
    out = gumbo_parse(html);
    if (!out) {
        fprintf(stderr, "FAIL: gumbo_parse returned NULL\n");
        gumbo_dyn_cleanup();
        return 1;
    }
    rc += CHECK(out->document != NULL);
    rc += CHECK(out->root != NULL);
    rc += CHECK(out->root->type == GUMBO_NODE_ELEMENT);
    rc += CHECK(out->root->v.element.tag == GUMBO_TAG_HTML);

    el_count = txt_count = meta_ok = title_ok = 0;
    walk(out->root);
    rc += CHECK(el_count >= 5);
    rc += CHECK(txt_count >= 2);
    rc += CHECK(meta_ok);
    rc += CHECK(title_ok);

    opts = gumbo_dyn_default_options();
    rc += CHECK(opts != NULL);
    if (opts) {
        GumboOutput *out2 = gumbo_parse_with_options(opts, html, strlen(html));
        rc += CHECK(out2 != NULL);
        gumbo_destroy_output(opts, out2);
    }
    gumbo_destroy_output(gumbo_dyn_default_options(), out);

    gumbo_dyn_cleanup();
    return rc ? 1 : 0;
}
