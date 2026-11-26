/* rtdb/tests/test_libgccjit.c - libgccjit C API 动态封装定向测试 */
#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* ---------- 类型（不引入目标库头文件） ---------- */
typedef struct gcc_jit_context gcc_jit_context;
typedef struct gcc_jit_result gcc_jit_result;
typedef struct gcc_jit_type gcc_jit_type;
typedef struct gcc_jit_function gcc_jit_function;
typedef struct gcc_jit_param gcc_jit_param;
typedef struct gcc_jit_block gcc_jit_block;
typedef struct gcc_jit_rvalue gcc_jit_rvalue;
typedef struct gcc_jit_object gcc_jit_object;

/* /usr/include/libgccjit.h 中相关枚举值（GCC 16.2.1 实测） */
#define GCC_JIT_TYPE_INT 8
#define GCC_JIT_FUNCTION_EXPORTED 0
#define GCC_JIT_BINARY_OP_PLUS 0

/* ---------- 动态层接口 ---------- */
extern int gccjit_dyn_init(const char *);
extern int libgccjit_libok(void);
extern void gccjit_dyn_cleanup(void);
extern int gccjit_dyn_has_symbol(const char *);

/* ---------- 本测试直接调用的原生声明 ---------- */
extern int gcc_jit_version_major(void);
extern int gcc_jit_version_minor(void);
extern int gcc_jit_version_patchlevel(void);
extern gcc_jit_context *gcc_jit_context_acquire(void);
extern void gcc_jit_context_release(gcc_jit_context *);
extern const char *gcc_jit_context_get_first_error(gcc_jit_context *);
extern gcc_jit_type *gcc_jit_context_get_type(gcc_jit_context *, int);
extern gcc_jit_param *gcc_jit_context_new_param(gcc_jit_context *, void *, gcc_jit_type *, const char *);
extern gcc_jit_rvalue *gcc_jit_param_as_rvalue(gcc_jit_param *);
extern gcc_jit_object *gcc_jit_function_as_object(gcc_jit_function *);
extern gcc_jit_function *gcc_jit_context_new_function(gcc_jit_context *, void *, int,
                                                      gcc_jit_type *, const char *, int,
                                                      gcc_jit_param **, int);
extern gcc_jit_block *gcc_jit_function_new_block(gcc_jit_function *, const char *);
extern gcc_jit_rvalue *gcc_jit_context_new_binary_op(gcc_jit_context *, void *, int,
                                                     gcc_jit_type *, gcc_jit_rvalue *, gcc_jit_rvalue *);
extern void gcc_jit_block_end_with_return(gcc_jit_block *, void *, gcc_jit_rvalue *);
extern gcc_jit_result *gcc_jit_context_compile(gcc_jit_context *);
extern void *gcc_jit_result_get_code(gcc_jit_result *, const char *);
extern void gcc_jit_result_release(gcc_jit_result *);
extern size_t gcc_jit_function_get_param_count(gcc_jit_function *);
extern gcc_jit_type *gcc_jit_function_get_return_type(gcc_jit_function *);

int main(int argc, char **argv) {
    int errs = 0;
    unsigned int i, found = 0;
    const char *names[] = {
        "gcc_jit_block_add_assignment", "gcc_jit_block_add_assignment_op", "gcc_jit_block_add_comment", "gcc_jit_block_add_eval", "gcc_jit_block_add_extended_asm",
        "gcc_jit_block_as_object", "gcc_jit_block_end_with_conditional", "gcc_jit_block_end_with_extended_asm_goto", "gcc_jit_block_end_with_jump", "gcc_jit_block_end_with_return",
        "gcc_jit_block_end_with_switch", "gcc_jit_block_end_with_void_return", "gcc_jit_block_get_function", "gcc_jit_case_as_object", "gcc_jit_compatible_types",
        "gcc_jit_context_acquire", "gcc_jit_context_add_command_line_option", "gcc_jit_context_add_driver_option", "gcc_jit_context_add_top_level_asm", "gcc_jit_context_compile",
        "gcc_jit_context_compile_to_file", "gcc_jit_context_convert_vector", "gcc_jit_context_dump_reproducer_to_file", "gcc_jit_context_dump_to_file", "gcc_jit_context_enable_dump",
        "gcc_jit_context_get_builtin_function", "gcc_jit_context_get_first_error", "gcc_jit_context_get_int_type", "gcc_jit_context_get_last_error", "gcc_jit_context_get_target_builtin_function",
        "gcc_jit_context_get_target_info", "gcc_jit_context_get_timer", "gcc_jit_context_get_type", "gcc_jit_context_new_alignof", "gcc_jit_context_new_array_access",
        "gcc_jit_context_new_array_constructor", "gcc_jit_context_new_array_type", "gcc_jit_context_new_array_type_u64", "gcc_jit_context_new_binary_op", "gcc_jit_context_new_bitcast",
        "gcc_jit_context_new_bitfield", "gcc_jit_context_new_call", "gcc_jit_context_new_call_through_ptr", "gcc_jit_context_new_case", "gcc_jit_context_new_cast",
        "gcc_jit_context_new_child_context", "gcc_jit_context_new_comparison", "gcc_jit_context_new_field", "gcc_jit_context_new_function", "gcc_jit_context_new_function_ptr_type",
        "gcc_jit_context_new_global", "gcc_jit_context_new_location", "gcc_jit_context_new_opaque_struct", "gcc_jit_context_new_param", "gcc_jit_context_new_rvalue_from_double",
        "gcc_jit_context_new_rvalue_from_int", "gcc_jit_context_new_rvalue_from_long", "gcc_jit_context_new_rvalue_from_ptr", "gcc_jit_context_new_rvalue_from_vector", "gcc_jit_context_new_rvalue_vector_perm",
        "gcc_jit_context_new_sizeof", "gcc_jit_context_new_string_literal", "gcc_jit_context_new_struct_constructor", "gcc_jit_context_new_struct_type", "gcc_jit_context_new_unary_op",
        "gcc_jit_context_new_union_constructor", "gcc_jit_context_new_union_type", "gcc_jit_context_new_vector_access", "gcc_jit_context_null", "gcc_jit_context_one",
        "gcc_jit_context_release", "gcc_jit_context_set_abort_on_unsupported_target_builtin", "gcc_jit_context_set_bool_allow_unreachable_blocks", "gcc_jit_context_set_bool_option", "gcc_jit_context_set_bool_print_errors_to_stderr",
        "gcc_jit_context_set_bool_use_external_driver", "gcc_jit_context_set_int_option", "gcc_jit_context_set_logfile", "gcc_jit_context_set_output_ident", "gcc_jit_context_set_str_option",
        "gcc_jit_context_set_timer", "gcc_jit_context_zero", "gcc_jit_extended_asm_add_clobber", "gcc_jit_extended_asm_add_input_operand", "gcc_jit_extended_asm_add_output_operand",
        "gcc_jit_extended_asm_as_object", "gcc_jit_extended_asm_set_inline_flag", "gcc_jit_extended_asm_set_volatile_flag", "gcc_jit_field_as_object", "gcc_jit_function_add_attribute",
        "gcc_jit_function_add_integer_array_attribute", "gcc_jit_function_add_string_attribute", "gcc_jit_function_as_object", "gcc_jit_function_dump_to_dot", "gcc_jit_function_get_address",
        "gcc_jit_function_get_param", "gcc_jit_function_get_param_count", "gcc_jit_function_get_return_type", "gcc_jit_function_new_block", "gcc_jit_function_new_local",
        "gcc_jit_function_new_temp", "gcc_jit_function_type_get_param_count", "gcc_jit_function_type_get_param_type", "gcc_jit_function_type_get_return_type", "gcc_jit_global_set_initializer",
        "gcc_jit_global_set_initializer_rvalue", "gcc_jit_global_set_readonly", "gcc_jit_location_as_object", "gcc_jit_lvalue_access_field", "gcc_jit_lvalue_add_string_attribute",
        "gcc_jit_lvalue_as_object", "gcc_jit_lvalue_as_rvalue", "gcc_jit_lvalue_get_address", "gcc_jit_lvalue_get_alignment", "gcc_jit_lvalue_set_alignment",
        "gcc_jit_lvalue_set_link_section", "gcc_jit_lvalue_set_register_name", "gcc_jit_lvalue_set_tls_model", "gcc_jit_object_get_context", "gcc_jit_object_get_debug_string",
        "gcc_jit_param_as_lvalue", "gcc_jit_param_as_object", "gcc_jit_param_as_rvalue", "gcc_jit_result_get_code", "gcc_jit_result_get_global",
        "gcc_jit_result_release", "gcc_jit_rvalue_access_field", "gcc_jit_rvalue_as_object", "gcc_jit_rvalue_dereference", "gcc_jit_rvalue_dereference_field",
        "gcc_jit_rvalue_get_type", "gcc_jit_rvalue_set_bool_require_tail_call", "gcc_jit_struct_as_type", "gcc_jit_struct_get_field", "gcc_jit_struct_get_field_count",
        "gcc_jit_struct_set_fields", "gcc_jit_target_info_arch", "gcc_jit_target_info_cpu_supports", "gcc_jit_target_info_release", "gcc_jit_target_info_supports_target_dependent_type",
        "gcc_jit_timer_new", "gcc_jit_timer_pop", "gcc_jit_timer_print", "gcc_jit_timer_push", "gcc_jit_timer_release",
        "gcc_jit_type_as_object", "gcc_jit_type_dyncast_array", "gcc_jit_type_dyncast_function_ptr_type", "gcc_jit_type_dyncast_vector", "gcc_jit_type_get_aligned",
        "gcc_jit_type_get_const", "gcc_jit_type_get_pointer", "gcc_jit_type_get_restrict", "gcc_jit_type_get_size", "gcc_jit_type_get_vector",
        "gcc_jit_type_get_volatile", "gcc_jit_type_is_bool", "gcc_jit_type_is_integral", "gcc_jit_type_is_pointer", "gcc_jit_type_is_struct",
        "gcc_jit_type_unqualified", "gcc_jit_vector_type_get_element_type", "gcc_jit_vector_type_get_num_units", "gcc_jit_version_major", "gcc_jit_version_minor",
        "gcc_jit_version_patchlevel"
    };

    if (gccjit_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("libgccjit not installed, skipping gccjit_dyn test\n");

    errs += CHECK(libgccjit_libok());
    errs += CHECK(!gccjit_dyn_has_symbol("gcc_jit_no_such_symbol_xyz"));
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (gccjit_dyn_has_symbol(names[i])) found++;
    }
    printf("info: libgccjit public exports found = %u/%u\n",
           found, (unsigned int)(sizeof names / sizeof names[0]));
    errs += CHECK(found == sizeof names / sizeof names[0]);

    printf("info: libgccjit %d.%d.%d\n", gcc_jit_version_major(),
           gcc_jit_version_minor(), gcc_jit_version_patchlevel());
    errs += CHECK(gcc_jit_version_major() > 0);

    {
        gcc_jit_context *ctxt = gcc_jit_context_acquire();
        errs += CHECK(ctxt != NULL);
        if (!ctxt) goto done;

        {
            gcc_jit_type *int_type = gcc_jit_context_get_type(ctxt, GCC_JIT_TYPE_INT);
            gcc_jit_param *params[2];
            gcc_jit_function *func;
            gcc_jit_block *block;
            gcc_jit_rvalue *sum;
            gcc_jit_result *result;
            int (*add2)(int, int);

            errs += CHECK(int_type != NULL);
            if (!int_type) goto release;
            params[0] = gcc_jit_context_new_param(ctxt, NULL, int_type, "a");
            params[1] = gcc_jit_context_new_param(ctxt, NULL, int_type, "b");
            errs += CHECK(params[0] != NULL && params[1] != NULL);
            if (!params[0] || !params[1]) goto release;

            func = gcc_jit_context_new_function(ctxt, NULL, GCC_JIT_FUNCTION_EXPORTED,
                                                int_type, "rtdb_add2", 2, params, 0);
            errs += CHECK(func != NULL);
            if (!func) goto release;
            errs += CHECK(gcc_jit_function_as_object(func) != NULL);
            errs += CHECK(gcc_jit_function_get_param_count(func) == 2);
            errs += CHECK(gcc_jit_function_get_return_type(func) != NULL);

            block = gcc_jit_function_new_block(func, "entry");
            errs += CHECK(block != NULL);
            if (!block) goto release;
            sum = gcc_jit_context_new_binary_op(ctxt, NULL, GCC_JIT_BINARY_OP_PLUS,
                                                int_type, gcc_jit_param_as_rvalue(params[0]),
                                                gcc_jit_param_as_rvalue(params[1]));
            errs += CHECK(sum != NULL);
            if (!sum) goto release;
            gcc_jit_block_end_with_return(block, NULL, sum);

            result = gcc_jit_context_compile(ctxt);
            errs += CHECK(result != NULL);
            if (!result) {
                printf("info: gccjit error: %s\n",
                       gcc_jit_context_get_first_error(ctxt));
                goto release;
            }
            *(void **)(&add2) = gcc_jit_result_get_code(result, "rtdb_add2");
            errs += CHECK(add2 != NULL);
            if (add2) {
                errs += CHECK(add2(2, 3) == 5);
                errs += CHECK(add2(-7, 9) == 2);
            }
            gcc_jit_result_release(result);
        }

release:
        gcc_jit_context_release(ctxt);
    }

done:
    gccjit_dyn_cleanup();
    return errs ? 1 : 0;
}
