/* rtdb/tests/test_lightning.c - GNU Lightning 2.1 C API 动态封装定向测试 */
#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* ---------- 类型（不引入目标库头文件） ---------- */
typedef struct jit_state jit_state_t;

/* ---------- 动态层接口 ---------- */
extern int lightning_dyn_init(const char *);
extern int lightning_libok(void);
extern void lightning_dyn_cleanup(void);
extern int lightning_dyn_has_symbol(const char *);

/* ---------- 本测试直接调用的原生声明 ---------- */
extern void init_jit(const char *);
extern void finish_jit(void);
extern jit_state_t *jit_new_state(void);
extern void _jit_destroy_state(jit_state_t *);

int main(int argc, char **argv) {
    int errs = 0;
    unsigned int i, found = 0;
    const char *names[] = {
        "_jit_address", "_jit_allocai", "_jit_allocar", "_jit_arg", "_jit_arg_d",
        "_jit_arg_f", "_jit_arg_register_p", "_jit_callee_save_p", "_jit_clear_state", "_jit_destroy_state",
        "_jit_disassemble", "_jit_ellipsis", "_jit_emit", "_jit_epilog", "_jit_finishi",
        "_jit_finishr", "_jit_forward", "_jit_forward_p", "_jit_frame", "_jit_get_code",
        "_jit_get_data", "_jit_get_note", "_jit_get_reg", "_jit_getarg_c", "_jit_getarg_d",
        "_jit_getarg_f", "_jit_getarg_i", "_jit_getarg_l", "_jit_getarg_s", "_jit_getarg_uc",
        "_jit_getarg_ui", "_jit_getarg_us", "_jit_indirect", "_jit_indirect_p", "_jit_label",
        "_jit_link", "_jit_name", "_jit_new_node", "_jit_new_node_d", "_jit_new_node_dp",
        "_jit_new_node_f", "_jit_new_node_fp", "_jit_new_node_p", "_jit_new_node_pw", "_jit_new_node_pwd",
        "_jit_new_node_pwf", "_jit_new_node_pww", "_jit_new_node_qww", "_jit_new_node_w", "_jit_new_node_wd",
        "_jit_new_node_wf", "_jit_new_node_wp", "_jit_new_node_ww", "_jit_new_node_wwd", "_jit_new_node_wwf",
        "_jit_new_node_www", "_jit_note", "_jit_patch", "_jit_patch_abs", "_jit_patch_at",
        "_jit_pointer_p", "_jit_prepare", "_jit_print", "_jit_prolog", "_jit_pushargi",
        "_jit_pushargi_d", "_jit_pushargi_f", "_jit_pushargr", "_jit_pushargr_d", "_jit_pushargr_f",
        "_jit_putargi", "_jit_putargi_d", "_jit_putargi_f", "_jit_putargr", "_jit_putargr_d",
        "_jit_putargr_f", "_jit_realize", "_jit_ret", "_jit_reti", "_jit_reti_d",
        "_jit_reti_f", "_jit_retr", "_jit_retr_d", "_jit_retr_f", "_jit_retval_c",
        "_jit_retval_d", "_jit_retval_f", "_jit_retval_i", "_jit_retval_l", "_jit_retval_s",
        "_jit_retval_uc", "_jit_retval_ui", "_jit_retval_us", "_jit_set_code", "_jit_set_data",
        "_jit_target_p", "_jit_tramp", "_jit_unget_reg", "_jit_va_push", "finish_jit",
        "init_jit", "jit_get_memory_functions", "jit_new_state", "jit_set_memory_functions"
    };

    if (lightning_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("GNU Lightning not installed, skipping lightning_dyn test\n");

    errs += CHECK(lightning_libok());
    errs += CHECK(!lightning_dyn_has_symbol("jit_no_such_symbol_xyz"));
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (lightning_dyn_has_symbol(names[i])) found++;
    }
    printf("info: GNU Lightning public exports found = %u/%u\n",
           found, (unsigned int)(sizeof names / sizeof names[0]));
    errs += CHECK(found == sizeof names / sizeof names[0]);

    init_jit(NULL);
    {
        jit_state_t *j = jit_new_state();
        errs += CHECK(j != NULL);
        if (j) _jit_destroy_state(j);
    }
    finish_jit();

    lightning_dyn_cleanup();
    return errs ? 1 : 0;
}
