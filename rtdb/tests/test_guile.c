/* guile_dyn 测试：只链 -ldl，不链 libguile
 * 注意：SCM 在封装层定义为 uintptr_t（与 Guile 3.x ABI 兼容的最小声明）
 */
#include "test_common.h"
#include <stdint.h>

typedef uintptr_t SCM;

int guile_dyn_init(const char *library_path);
void guile_dyn_cleanup(void);
int guile_dyn_is_initialized(void);

void scm_init_guile(void);
SCM scm_c_eval_string(const char *str);
SCM scm_from_int32(int32_t v);
int32_t scm_to_int32(SCM v);
char *scm_to_utf8_string(SCM v);
SCM scm_c_catch(SCM tag, SCM (*body)(void *), void *body_data,
                SCM (*handler)(void *, SCM, SCM), void *handler_data,
                SCM (*pre_unwind)(void *, SCM, SCM), void *pre_unwind_data);

static SCM catch_body(void *data) {
    (void)data;
    return scm_c_eval_string("(+ 1 'a)"); /* 抛出 Wrong type 异常 */
}

static SCM catch_handler(void *data, SCM tag, SCM throw_args) {
    (void)data; (void)tag; (void)throw_args;
    printf("caught exception (expected)\n");
    return scm_from_int32(0);
}

int main(int argc, char **argv) {
    int failures = 0;

    if (guile_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        fprintf(stderr, "guile_dyn_init failed\n");
        return 1;
    }

    failures += CHECK(guile_dyn_is_initialized() == 0);
    scm_init_guile();
    failures += CHECK(guile_dyn_is_initialized() != 0);

    /* scm_from_int32 / scm_to_int32 往返 */
    failures += CHECK(scm_to_int32(scm_from_int32(42)) == 42);

    /* 表达式求值 */
    {
        SCM v = scm_c_eval_string("(+ 1 2)");
        failures += CHECK(scm_to_int32(v) == 3);
        printf("(+ 1 2) = %d\n", scm_to_int32(v));
    }

    /* 字符串求值：scm_to_utf8_string 返回值用 free() 释放 */
    {
        SCM v = scm_c_eval_string("(string-append \"gu\" \"ile\")");
        char *s = scm_to_utf8_string(v);
        failures += CHECK(s && strcmp(s, "guile") == 0);
        printf("string result=%s\n", s ? s : "(null)");
        free(s);
    }

    /* 错误路径：未绑定变量求值会 throw，用 scm_c_catch 捕获不应崩溃 */
    {
        SCM tag = scm_c_eval_string("#t");
        SCM v = scm_c_catch(tag, catch_body, NULL,
                            catch_handler, NULL, NULL, NULL);
        (void)v;
        failures += CHECK(1); /* 走到这里即未崩溃 */
    }

    guile_dyn_cleanup();

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
