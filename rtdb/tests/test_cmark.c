/* cmark_dyn 测试：只链 -ldl，不链 libcmark
 * 覆盖：动态加载、控制函数、符号探测，
 *       以及真实 markdown->html、AST 解析/渲染、节点与迭代器操作。
 * 库缺失时按 TEST_SKIP 正常退出。
 */
#include "test_common.h"
#include <stdio.h>
#include <string.h>

typedef struct cmark_node cmark_node;
typedef struct cmark_parser cmark_parser;
typedef struct cmark_iter cmark_iter;
typedef enum { CMARK_NODE_NONE = 0, CMARK_NODE_DOCUMENT = 1,
               CMARK_NODE_PARAGRAPH = 8, CMARK_NODE_TEXT = 11 } cmark_node_type;
typedef enum { CMARK_EVENT_DONE = 1 } cmark_event_type;

extern int  cmark_dyn_init(const char *);
extern int  cmark_libok(void);
extern void cmark_dyn_cleanup(void);
extern int  cmark_dyn_has_symbol(const char *);

extern char        *cmark_markdown_to_html(const char *, size_t, int);
extern cmark_node  *cmark_parse_document(const char *, size_t, int);
extern char        *cmark_render_html(cmark_node *, int);
extern void         cmark_node_free(cmark_node *);
extern cmark_node  *cmark_node_new(cmark_node_type);
extern int          cmark_node_append_child(cmark_node *, cmark_node *);
extern cmark_node_type cmark_node_get_type(cmark_node *);
extern const char  *cmark_node_get_type_string(cmark_node *);
extern int          cmark_node_set_literal(cmark_node *, const char *);
extern const char  *cmark_node_get_literal(cmark_node *);
extern cmark_iter  *cmark_iter_new(cmark_node *);
extern void         cmark_iter_free(cmark_iter *);
extern cmark_event_type cmark_iter_next(cmark_iter *);
extern cmark_node  *cmark_iter_get_node(cmark_iter *);
extern int          cmark_version(void);
extern const char  *cmark_version_string(void);

int main(void) {
    int errs = 0;
    const char md[] = "# Hello\n\nworld\n";
    char *html;
    cmark_node *root, *para, *text;
    cmark_iter *it;
    int events = 0;

    if (cmark_dyn_init(NULL) != 0)
        TEST_SKIP("libcmark not installed, skipping cmark_dyn test\n");

    errs += CHECK(cmark_libok());
    errs += CHECK(cmark_dyn_has_symbol("cmark_markdown_to_html"));
    errs += CHECK(cmark_dyn_has_symbol("cmark_parse_document"));
    errs += CHECK(cmark_dyn_has_symbol("cmark_render_html"));
    errs += CHECK(cmark_dyn_has_symbol("cmark_node_new"));
    errs += CHECK(cmark_dyn_has_symbol("cmark_iter_new"));
    errs += CHECK(cmark_dyn_has_symbol("cmark_version_string"));
    errs += CHECK(!cmark_dyn_has_symbol("cmark_no_such_symbol_xyz"));

    errs += CHECK(cmark_version() > 0);
    errs += CHECK(cmark_version_string() != NULL);
    printf("info: cmark version = %s\n", cmark_version_string());

    /* simple interface */
    html = cmark_markdown_to_html(md, (size_t)(sizeof md - 1), 0);
    errs += CHECK(html != NULL);
    if (html) {
        printf("info: html = %s\n", html);
        errs += CHECK(strstr(html, "<h1>Hello</h1>") != NULL);
        errs += CHECK(strstr(html, "world") != NULL);
    }

    /* AST 解析 + 渲染 */
    root = cmark_parse_document(md, (size_t)(sizeof md - 1), 0);
    errs += CHECK(root != NULL);
    if (root) {
        errs += CHECK(cmark_node_get_type(root) == CMARK_NODE_DOCUMENT);
        html = cmark_render_html(root, 0);
        errs += CHECK(html != NULL);
        if (html) {
            errs += CHECK(strstr(html, "<h1>Hello</h1>") != NULL);
        }

        it = cmark_iter_new(root);
        errs += CHECK(it != NULL);
        if (it) {
            while (cmark_iter_next(it) != CMARK_EVENT_DONE) {
                events++;
                (void)cmark_iter_get_node(it);
            }
            cmark_iter_free(it);
        }
        errs += CHECK(events >= 4);
        printf("info: iter events = %d\n", events);
        cmark_node_free(root);
    }

    /* 手工构建节点树 */
    para = cmark_node_new(CMARK_NODE_PARAGRAPH);
    text = cmark_node_new(CMARK_NODE_TEXT);
    errs += CHECK(para != NULL);
    errs += CHECK(text != NULL);
    if (para && text) {
        errs += CHECK(cmark_node_append_child(para, text) == 1);
        errs += CHECK(cmark_node_set_literal(text, "hi") == 1);
        errs += CHECK(strcmp(cmark_node_get_literal(text), "hi") == 0);
        errs += CHECK(strcmp(cmark_node_get_type_string(para), "paragraph") == 0);
    }
    if (text) cmark_node_free(text);
    if (para) cmark_node_free(para);

    cmark_dyn_cleanup();
    if (errs) { fprintf(stderr, "cmark_dyn: %d failure(s)\n", errs); return 1; }
    printf("cmark_dyn: all checks passed\n");
    return 0;
}
