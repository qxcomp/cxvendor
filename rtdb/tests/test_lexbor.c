/* lexbor_dyn 测试：只链 -ldl，不链 liblexbor
 * 覆盖：动态加载、控制函数、符号探测（含负例）、HTML 解析、title 提取、
 *       DOM 遍历（*_noi ABI 符号）、元素属性与文本提取、文档销毁。
 * 本机默认无 liblexbor 时按 TEST_SKIP 退出；验证时用
 *   LEXBOR_DYN_LIB=/path/to/liblexbor.so.3 ./tests/build/test_lexbor
 */
#include "test_common.h"
#include <stdio.h>
#include <string.h>

/* 与 lexbor 上游 ABI 等价的最小类型声明，不 include 目标库头 */
typedef unsigned char lxb_char_t;
typedef unsigned int  lxb_status_t;
typedef struct lxb_html_document     lxb_html_document_t;
typedef struct lxb_html_body_element lxb_html_body_element_t;
typedef struct lxb_dom_node          lxb_dom_node_t;
typedef struct lxb_dom_element       lxb_dom_element_t;

typedef enum {
    LXB_DOM_NODE_TYPE_UNDEF   = 0x00,
    LXB_DOM_NODE_TYPE_ELEMENT = 0x01,
    LXB_DOM_NODE_TYPE_TEXT    = 0x03,
    LXB_DOM_NODE_TYPE_COMMENT = 0x08,
    LXB_DOM_NODE_TYPE_DOCUMENT = 0x09,
} lxb_dom_node_type_t;

extern int  lexbor_dyn_init(const char *);
extern int  lexbor_libok(void);
extern void lexbor_dyn_cleanup(void);
extern int  lexbor_dyn_has_symbol(const char *);

extern lxb_html_document_t *lxb_html_document_create(void);
extern lxb_html_document_t *lxb_html_document_destroy(lxb_html_document_t *);
extern lxb_status_t lxb_html_document_parse(lxb_html_document_t *, const lxb_char_t *, size_t);
extern const lxb_char_t *lxb_html_document_title(lxb_html_document_t *, size_t *);
extern lxb_html_body_element_t *lxb_html_document_body_element_noi(lxb_html_document_t *);
extern lxb_dom_node_t *lxb_dom_node_first_child_noi(lxb_dom_node_t *);
extern lxb_dom_node_t *lxb_dom_node_next_noi(lxb_dom_node_t *);
extern lxb_dom_node_t *lxb_dom_node_parent_noi(lxb_dom_node_t *);
extern lxb_dom_node_type_t lxb_dom_node_type_noi(lxb_dom_node_t *);
extern const lxb_char_t *lxb_dom_node_name(lxb_dom_node_t *, size_t *);
extern lxb_char_t *lxb_dom_node_text_content(lxb_dom_node_t *, size_t *);
extern const lxb_char_t *lxb_dom_element_get_attribute(lxb_dom_element_t *,
        const lxb_char_t *, size_t, size_t *);
extern const lxb_char_t *lxb_dom_element_tag_name(lxb_dom_element_t *, size_t *);
extern const lxb_char_t *lxb_dom_element_local_name(lxb_dom_element_t *, size_t *);

static int eq(const lxb_char_t *p, size_t len, const char *s) {
    size_t n = strlen(s);
    return p && len == n && memcmp(p, s, n) == 0;
}

static int el_count, txt_count, meta_ok, div_ok, text_ok;

static void walk(lxb_dom_node_t *n) {
    while (n) {
        lxb_dom_node_type_t t = lxb_dom_node_type_noi(n);
        if (t == LXB_DOM_NODE_TYPE_ELEMENT) {
            size_t len = 0, vlen = 0;
            const lxb_char_t *name = lxb_dom_element_local_name((lxb_dom_element_t *)n, &len);
            el_count++;
            if (eq(name, len, "meta")) {
                const lxb_char_t *v = lxb_dom_element_get_attribute(
                    (lxb_dom_element_t *)n, (const lxb_char_t *)"content", 7, &vlen);
                if (eq(v, vlen, "lexbor dyn test")) meta_ok = 1;
            }
            if (eq(name, len, "div")) {
                lxb_char_t *txt = lxb_dom_node_text_content(n, &vlen);
                div_ok = 1;
                if (eq(txt, vlen, "body text")) text_ok = 1;
            }
            walk(lxb_dom_node_first_child_noi(n));
        } else if (t == LXB_DOM_NODE_TYPE_TEXT) {
            txt_count++;
        }
        n = lxb_dom_node_next_noi(n);
    }
}

int main(void) {
    static const lxb_char_t html[] =
        "<!doctype html><html><head><title>Hello Lexbor</title>"
        "<meta property=\"og:title\" content=\"lexbor dyn test\">"
        "</head><body><div id=\"main\">body text</div></body></html>";
    const size_t html_len = sizeof(html) - 1;
    int rc = 0;
    lxb_html_document_t *doc;
    lxb_html_body_element_t *body;
    size_t len = 0;
    const lxb_char_t *title;

    if (lexbor_dyn_init(NULL) != 0 || !lexbor_libok())
        TEST_SKIP("liblexbor not available (set LEXBOR_DYN_LIB to test)\n");

    rc += CHECK(lexbor_libok());
    rc += CHECK(lexbor_dyn_has_symbol("lxb_html_document_parse"));
    rc += CHECK(lexbor_dyn_has_symbol("lxb_dom_node_next_noi"));
    rc += CHECK(!lexbor_dyn_has_symbol("lxb_no_such_symbol_xyz"));

    doc = lxb_html_document_create();
    if (!doc) {
        fprintf(stderr, "FAIL: lxb_html_document_create returned NULL\n");
        lexbor_dyn_cleanup();
        return 1;
    }
    rc += CHECK(lxb_html_document_parse(doc, html, html_len) == 0);

    title = lxb_html_document_title(doc, &len);
    rc += CHECK(eq(title, len, "Hello Lexbor"));

    body = lxb_html_document_body_element_noi(doc);
    rc += CHECK(body != NULL);
    if (body) {
        lxb_dom_node_t *body_node = (lxb_dom_node_t *)body;
        rc += CHECK(lxb_dom_node_parent_noi(body_node) != NULL);
        rc += CHECK(lxb_dom_node_type_noi(body_node) == LXB_DOM_NODE_TYPE_ELEMENT);
        el_count = txt_count = meta_ok = div_ok = text_ok = 0;
        /* body 的父节点是 <html>；从它的首子节点开始可覆盖 head + body */
        {
            lxb_dom_node_t *html_node = lxb_dom_node_parent_noi(body_node);
            walk(lxb_dom_node_first_child_noi(html_node));
        }
        rc += CHECK(el_count >= 3);
        rc += CHECK(txt_count >= 1);
        rc += CHECK(meta_ok);
        rc += CHECK(div_ok);
        rc += CHECK(text_ok);
    }

    rc += CHECK(lxb_html_document_destroy(doc) == NULL);

    lexbor_dyn_cleanup();
    return rc ? 1 : 0;
}
