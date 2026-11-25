/* rtdb/tests/test_mir.c - MIR JIT C API 动态封装定向测试 */
#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* ---------- 类型（不引入目标库头文件） ---------- */
typedef struct MIR_context *MIR_context_t;
typedef struct MIR_module *MIR_module_t;
typedef struct MIR_item *MIR_item_t;

/* ---------- 动态层接口 ---------- */
extern int mir_dyn_init(const char *);
extern int mir_dyn_libok(void);
extern void mir_dyn_cleanup(void);
extern int mir_dyn_has_symbol(const char *);

/* ---------- 本测试直接调用的原生声明 ---------- */
extern double _MIR_get_api_version(void);

int main(int argc, char **argv) {
    int errs = 0;
    unsigned int i, found = 0;
    const char *names[] = {
        "MIR_alias", "MIR_alias_name", "MIR_append_insn", "MIR_change_module_ctx", "MIR_copy_insn",
        "MIR_finish_func", "MIR_finish_module", "MIR_gen", "MIR_gen_finish", "MIR_gen_set_debug_file",
        "MIR_gen_set_debug_level", "MIR_gen_set_optimize_level", "MIR_get_alloc", "MIR_get_error_func", "MIR_get_func_redef_permission_p",
        "MIR_get_item_func", "MIR_get_module_item", "MIR_insert_insn_after", "MIR_insert_insn_before", "MIR_insn_name",
        "MIR_insn_nops", "MIR_insn_op_mode", "MIR_interp", "MIR_interp_arr", "MIR_interp_arr_varg",
        "MIR_item_name", "MIR_link", "MIR_load_external", "MIR_load_module", "MIR_new_alias_mem_op",
        "MIR_new_bss", "MIR_new_call_insn", "MIR_new_data", "MIR_new_double_op", "MIR_new_export",
        "MIR_new_expr_data", "MIR_new_float_op", "MIR_new_forward", "MIR_new_func", "MIR_new_func_arr",
        "MIR_new_func_reg", "MIR_new_global_func_reg", "MIR_new_import", "MIR_new_insn", "MIR_new_insn_arr",
        "MIR_new_int_op", "MIR_new_jcall_insn", "MIR_new_label", "MIR_new_label_op", "MIR_new_ldouble_op",
        "MIR_new_lref_data", "MIR_new_mem_op", "MIR_new_module", "MIR_new_proto", "MIR_new_proto_arr",
        "MIR_new_ref_data", "MIR_new_ref_op", "MIR_new_reg_op", "MIR_new_ret_insn", "MIR_new_str_op",
        "MIR_new_string_data", "MIR_new_uint_op", "MIR_new_vararg_func", "MIR_new_vararg_func_arr", "MIR_new_vararg_proto",
        "MIR_new_vararg_proto_arr", "MIR_op_eq_p", "MIR_op_hash_step", "MIR_output", "MIR_output_insn",
        "MIR_output_item", "MIR_output_module", "MIR_output_op", "MIR_output_str", "MIR_prepend_insn",
        "MIR_read", "MIR_read_with_func", "MIR_reg", "MIR_reg_hard_reg_name", "MIR_reg_name",
        "MIR_reg_type", "MIR_remove_insn", "MIR_reverse_branch_code", "MIR_set_error_func", "MIR_set_func_redef_permission",
        "MIR_set_gen_interface", "MIR_set_interp_interface", "MIR_set_lazy_bb_gen_interface", "MIR_set_lazy_gen_interface", "MIR_type_str",
        "MIR_write_module", "MIR_write_module_with_func", "MIR_write_with_func", "_MIR_addr_offset", "_MIR_builtin_func",
        "_MIR_builtin_proto", "_MIR_change_code", "_MIR_dump_code", "_MIR_duplicate_func_insns", "_MIR_flush_code_cache",
        "_MIR_free_insn", "_MIR_get_api_version", "_MIR_get_bb_thunk", "_MIR_get_bb_wrapper", "_MIR_get_bend_builtin",
        "_MIR_get_bstart_builtin", "_MIR_get_ff_call", "_MIR_get_hard_reg", "_MIR_get_interp_shim", "_MIR_get_jmpi_thunk",
        "_MIR_get_module_global_var_hard_regs", "_MIR_get_new_code_addr", "_MIR_get_temp_item_name", "_MIR_get_thunk", "_MIR_get_thunk_addr",
        "_MIR_get_wrapper", "_MIR_get_wrapper_end", "_MIR_init", "_MIR_insn_code_op_mode", "_MIR_name_char_p",
        "_MIR_new_alias_var_mem_op", "_MIR_new_temp_reg", "_MIR_new_unspec_insn", "_MIR_new_var_mem_op", "_MIR_new_var_op",
        "_MIR_output_data_item_els", "_MIR_publish_code", "_MIR_publish_code_by_addr", "_MIR_redirect_thunk", "_MIR_register_unspec_insn",
        "_MIR_replace_bb_thunk", "_MIR_reserved_name_p", "_MIR_reserved_ref_name_p", "_MIR_restore_func_insns", "_MIR_set_code",
        "_MIR_type_size", "_MIR_uniq_string", "_MIR_update_code", "_MIR_update_code_arr", "va_arg_builtin",
        "va_block_arg_builtin", "va_end_interp_builtin", "va_start_interp_builtin"
    };

    if (mir_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("libmir not installed, skipping mir_dyn test\n");

    errs += CHECK(mir_dyn_libok());
    errs += CHECK(!mir_dyn_has_symbol("MIR_no_such_symbol_xyz"));
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (mir_dyn_has_symbol(names[i])) found++;
    }
    printf("info: MIR public exports found = %u/%u\n",
           found, (unsigned int)(sizeof names / sizeof names[0]));
    errs += CHECK(found == sizeof names / sizeof names[0]);

    {
        double api = _MIR_get_api_version();
        printf("info: MIR API version %.1f\n", api);
        errs += CHECK(api > 0.0);
    }

    mir_dyn_cleanup();
    return errs ? 1 : 0;
}
