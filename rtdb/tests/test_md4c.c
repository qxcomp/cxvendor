/* md4c_dyn 测试：只链 -ldl，不链 libmd4c / libmd4c-html
 * 覆盖：动态加载双库、控制函数、符号探测，
 *       以及真实 md_html 转 HTML 与 md_parse 回调统计。
 * 库缺失时按 TEST_SKIP 正常退出。
 */
#include "test_common.h"
#include <stdio.h>
#include <string.h>

typedef char          MD_CHAR;
typedef unsigned      MD_SIZE;
typedef enum { MD_BLOCK_DOC = 0, MD_BLOCK_P = 9 } md_test_block_t;
typedef enum { MD_TEXT_NORMAL = 0 } md_test_text_t;
typedef struct {
    unsigned abi_version;
    unsigned flags;
    int (*enter_block)(int, void *, void *);
    int (*leave_block)(int, void *, void *);
    int (*enter_span)(int, void *, void *);
    int (*leave_span)(int, void *, void *);
    int (*text)(int, const char *, unsigned, void *);
    void (*debug_log)(const char *, void *);
    void (*syntax)(void);
} md_test_parser_t;

extern int  md4c_dyn_init(const char *);
extern int  md4c_dyn_libok(void);
extern void md4c_dyn_cleanup(void);
extern int  md4c_dyn_has_symbol(const char *);

extern int md_parse(const char *, MD_SIZE, const md_test_parser_t *, void *);
extern int md_html(const char *, MD_SIZE,
                   void (*)(const char *, MD_SIZE, void *),
                   void *, unsigned, unsigned);

struct out_ctx { char buf[512]; size_t len; };

static void collect_output(const char *s, MD_SIZE n, void *ud) {
    struct out_ctx *c = (struct out_ctx *)ud;
    size_t room = sizeof(c->buf) - c->len - 1;
    if (n > room) n = (MD_SIZE)room;
    memcpy(c->buf + c->len, s, n);
    c->len += n;
    c->buf[c->len] = 0;
}

struct parse_stat { int blocks; int texts; };
static int on_block(int type, void *detail, void *ud) {
    (void)type; (void)detail;
    ((struct parse_stat *)ud)->blocks++;
    return 0;
}
static int on_text(int type, const char *s, unsigned n, void *ud) {
    (void)type; (void)s; (void)n;
    ((struct parse_stat *)ud)->texts++;
    return 0;
}

int main(void) {
    int errs = 0;
    struct out_ctx out;
    struct parse_stat st;
    md_test_parser_t parser;
    const char md[] = "# Hello\n\nworld\n";

    if (md4c_dyn_init(NULL) != 0)
        TEST_SKIP("libmd4c not installed, skipping md4c_dyn test\n");

    errs += CHECK(md4c_dyn_libok());
    errs += CHECK(md4c_dyn_has_symbol("md_parse"));
    errs += CHECK(md4c_dyn_has_symbol("md_html"));
    errs += CHECK(!md4c_dyn_has_symbol("md_no_such_symbol_xyz"));

    /* md_html: Markdown -> HTML 输出校验 */
    memset(&out, 0, sizeof out);
    errs += CHECK(md_html(md, (MD_SIZE)(sizeof md - 1), collect_output, &out, 0, 0) == 0);
    printf("info: html = %s\n", out.buf);
    errs += CHECK(strstr(out.buf, "<h1>Hello</h1>") != NULL);
    errs += CHECK(strstr(out.buf, "world") != NULL);

    /* md_parse: 自定义回调统计块与文本事件 */
    memset(&st, 0, sizeof st);
    memset(&parser, 0, sizeof parser);
    parser.enter_block = on_block;
    parser.leave_block = on_block;
    parser.text = on_text;
    errs += CHECK(md_parse(md, (MD_SIZE)(sizeof md - 1), &parser, &st) == 0);
    printf("info: blocks=%d texts=%d\n", st.blocks, st.texts);
    errs += CHECK(st.blocks >= 4);
    errs += CHECK(st.texts >= 2);

    md4c_dyn_cleanup();
    if (errs) { fprintf(stderr, "md4c_dyn: %d failure(s)\n", errs); return 1; }
    printf("md4c_dyn: all checks passed\n");
    return 0;
}
