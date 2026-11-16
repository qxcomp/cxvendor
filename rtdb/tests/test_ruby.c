/* ruby_dyn 测试：只链 -ldl，不链 libruby
 * 注意 rb_eval_string 出错会 longjmp；安全路径用 rb_eval_string_protect
 */
#include "test_common.h"
#include <stdint.h>

typedef uintptr_t VALUE;

int ruby_dyn_init(const char *library_path);
void ruby_dyn_cleanup(void);
int ruby_dyn_major(void);
int ruby_dyn_minor(void);

void ruby_init(void);
VALUE rb_eval_string_protect(const char *str, int *state);
long rb_num2long(VALUE v);
int ruby_cleanup(int ex);

int main(int argc, char **argv) {
    int failures = 0;
    int state = 0;

    if (ruby_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        printf("SKIP: libruby not available on this system\n");
        ruby_dyn_cleanup();
        return 0;
    }
    printf("loaded libruby %d.%d\n", ruby_dyn_major(), ruby_dyn_minor());

    ruby_init();

    /* 表达式求值：返回 VALUE，rb_num2long 取出整数 */
    {
        VALUE v = rb_eval_string_protect("20 + 22", &state);
        failures += CHECK(state == 0);
        failures += CHECK(rb_num2long(v) == 42);
        printf("20+22 = %ld\n", rb_num2long(v));
    }

    /* 字符串求值 */
    {
        VALUE v = rb_eval_string_protect("'ruby'.upcase", &state);
        failures += CHECK(state == 0);
        failures += CHECK(rb_num2long(rb_eval_string_protect(
                          "'ruby'.length", &state)) == 4);
        (void)v;
    }

    /* 错误路径：未定义常量 → state 非 0（不 longjmp 出去） */
    {
        state = 0;
        rb_eval_string_protect("NoSuchConstantAnywhere", &state);
        failures += CHECK(state != 0);
        printf("undefined constant -> state=%d\n", state);
    }

    failures += CHECK(ruby_cleanup(0) == 0);
    ruby_dyn_cleanup();

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
