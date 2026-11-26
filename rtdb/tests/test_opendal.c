/* opendal_dyn 测试：只链 -ldl，不链 libopendal_c
 * 覆盖：动态加载、控制函数、161 个符号抽样探测，
 *       以及真实创建/释放 options 与内存后端 operator。
 * 库缺失时按 TEST_SKIP 正常退出。
 */
#include "test_common.h"
#include <stdint.h>
#include <stdio.h>

struct opendal_bytes { uint8_t *data; uintptr_t len; uintptr_t capacity; };
struct opendal_error { int code; struct opendal_bytes message; };
struct opendal_operator { void *inner; };
struct opendal_operator_options { void *inner; };
struct opendal_result_operator_new {
    struct opendal_operator *op;
    struct opendal_error *error;
};

extern int  opendal_dyn_init(const char *);
extern int  opendal_libok(void);
extern void opendal_dyn_cleanup(void);
extern int  opendal_dyn_has_symbol(const char *);

extern struct opendal_operator_options *opendal_operator_options_new(void);
extern void opendal_operator_options_set(struct opendal_operator_options *, const char *, const char *);
extern void opendal_operator_options_free(struct opendal_operator_options *);
extern struct opendal_result_operator_new opendal_operator_new(const char *, const struct opendal_operator_options *);
extern void opendal_operator_free(const struct opendal_operator *);
extern void opendal_error_free(struct opendal_error *);
extern void opendal_string_free(char *);

int main(void) {
    int errs = 0;
    struct opendal_operator_options *opts;
    struct opendal_result_operator_new r;

    if (opendal_dyn_init(NULL) != 0)
        TEST_SKIP("libopendal_c not installed, skipping opendal_dyn test\n");

    errs += CHECK(opendal_libok());

    errs += CHECK(opendal_dyn_has_symbol("opendal_operator_new"));
    errs += CHECK(opendal_dyn_has_symbol("opendal_operator_read"));
    errs += CHECK(opendal_dyn_has_symbol("opendal_operator_write"));
    errs += CHECK(opendal_dyn_has_symbol("opendal_metadata_mode"));
    errs += CHECK(opendal_dyn_has_symbol("opendal_operator_info_get_capability"));
    errs += CHECK(opendal_dyn_has_symbol("opendal_operator_options_new"));
    errs += CHECK(opendal_dyn_has_symbol("opendal_reader_read"));
    errs += CHECK(!opendal_dyn_has_symbol("opendal_no_such_symbol_xyz"));

    opts = opendal_operator_options_new();
    errs += CHECK(opts != NULL);
    if (opts) {
        opendal_operator_options_set(opts, "root", "/tmp");
        opendal_operator_options_free(opts);
    }

    r = opendal_operator_new("memory", NULL);
    errs += CHECK(r.op != NULL);
    errs += CHECK(r.error == NULL);
    if (r.op) opendal_operator_free(r.op);
    if (r.error) opendal_error_free(r.error);

    opendal_dyn_cleanup();
    if (errs) { fprintf(stderr, "opendal_dyn: %d failure(s)\n", errs); return 1; }
    printf("opendal_dyn: all checks passed\n");
    return 0;
}
