/* ============================================================
 * mir_dyn.c - dlopen 模拟链接：MIR JIT C API（mir.h + mir-gen.h）
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照公开头文件的 143 个 extern 函数声明（含
 * MIR_* 与 _MIR_* 内部 API）；opaque handle、枚举和按值传递的
 * MIR_op_t/MIR_str_t 按 ABI 等价内嵌，不引入目标库头文件。函数名
 * 保持原生原样。12 个变参函数用 __builtin_apply 转发。
 * 链接期不需要 libmir，仅需 -ldl。
 * 加载顺序：显式路径 > MIR_DYN_LIB > LD_LIBRARY_PATH > .so > 平台
 * 默认；成功后不卸载。若 gen 后端另行打包为 libmir-gen.so，请通过
 * MIR_DYN_LIB 指向该库或预先加载。
 * 加载日志照搬 opendal_dyn.c 当前 diff。
 * ============================================================ */

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/* ---------- 平台适配层 ---------- */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE lib_handle_t;
  static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
  static void *get_sym(lib_handle_t h, const char *name) { return (void *)GetProcAddress(h, name); }
  static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *lib_error(void) { return "LoadLibrary failed"; }
  #define PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *lib_handle_t;
  static lib_handle_t load_lib(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
  static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
  static const char *lib_error(void) { return dlerror(); }
  #define PATH_SEP ':'
#endif

/* ---------- 类型（头文件原样内嵌，字段顺序即 ABI） ---------- */
typedef struct MIR_context *MIR_context_t;
typedef struct MIR_module *MIR_module_t;
typedef struct MIR_item *MIR_item_t;
typedef struct MIR_func *MIR_func_t;
typedef struct MIR_insn *MIR_insn_t;
typedef struct MIR_insn *MIR_label_t;
typedef struct MIR_alloc *MIR_alloc_t;
typedef struct MIR_code_alloc *MIR_code_alloc_t;

/* 枚举在 C ABI 中等价于 int */
typedef int MIR_type_t;
typedef int MIR_insn_code_t;
typedef int MIR_op_mode_t;
typedef int MIR_error_type_t;
typedef uint8_t MIR_scale_t;
typedef int64_t MIR_disp_t;
typedef uint32_t MIR_reg_t;
typedef uint32_t MIR_alias_t;
typedef unsigned htab_hash_t;

typedef void (*MIR_error_func_t)(MIR_error_type_t, const char *, ...);
typedef struct MIR_str { size_t len; const char *s; } MIR_str_t;

typedef struct {
    MIR_type_t type : 8;
    MIR_scale_t scale;
    MIR_alias_t alias;
    MIR_alias_t nonalias;
    uint32_t nloc;
    MIR_reg_t base, index;
    MIR_disp_t disp;
} MIR_mem_t;

typedef struct {
    void *data;
    MIR_op_mode_t mode : 8;
    MIR_op_mode_t value_mode : 8;
    union {
        MIR_reg_t reg;
        MIR_reg_t var;
        int64_t i;
        uint64_t u;
        float f;
        double d;
        long double ld;
        MIR_item_t ref;
        MIR_str_t str;
        MIR_mem_t mem;
        MIR_mem_t var_mem;
        MIR_label_t label;
    } u;
} MIR_op_t;

typedef union {
    MIR_insn_code_t ic;
    void *a;
    int64_t i;
    uint64_t u;
    float f;
    double d;
    long double ld;
} MIR_val_t;

typedef struct MIR_var {
    MIR_type_t type;
    const char *name;
    size_t size;
} MIR_var_t;

struct MIR_code_reloc { size_t offset; const void *value; };
typedef struct MIR_code_reloc MIR_code_reloc_t;

typedef struct {
    MIR_type_t type;
    size_t size;
} _MIR_arg_desc_t;

/* ---------- 动态层状态 ---------- */
static lib_handle_t mir_handle = NULL;
static int mir_init_state = 0;

static void *mir_sym(const char *name) {
    void *p = mir_handle ? get_sym(mir_handle, name) : NULL;
#ifndef _WIN32
    if (!p) p = dlsym(RTLD_DEFAULT, name);
#endif
    return p;
}

static int mir_do_load(const char *library_path) {
    static char bufs[64][1024];
    const char *candidates[64];
    const char *so_soname = "libmir.so";
    const char *so_ver = "libmir.so.1";
    const char *so_plain = "libmir-gen.so";
    int n = 0, i;
    int res = -1;

    if (mir_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("MIR_DYN_LIB");
        if (e && *e) {
            snprintf(bufs[n], sizeof bufs[0], "%s", e);
            candidates[n] = bufs[n]; n++;
        }
    }
    {
#ifdef _WIN32
        const char *env_path = getenv("PATH");
#elif defined(__APPLE__)
        const char *env_path = getenv("DYLD_LIBRARY_PATH");
#else
        const char *env_path = getenv("LD_LIBRARY_PATH");
#endif
        if (env_path && *env_path) {
            char *copy = strdup(env_path);
            if (copy) {
                char *p = copy;
                while (p && n < 60) {
                    char *sep = strchr(p, PATH_SEP);
                    if (sep) *sep = '\0';
                    if (*p) {
                        snprintf(bufs[n], sizeof bufs[0], "%s%c%s", p, PATH_SEP, so_soname);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s%c%s", p, PATH_SEP, so_plain);
                        candidates[n] = bufs[n]; n++;
                    }
                    p = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    {
        snprintf(bufs[n], sizeof bufs[0], "%s", so_soname);
        candidates[n] = bufs[n]; n++;
        snprintf(bufs[n], sizeof bufs[0], "%s", so_ver);
        candidates[n] = bufs[n]; n++;
        snprintf(bufs[n], sizeof bufs[0], "%s", so_plain);
        candidates[n] = bufs[n]; n++;
    }
    {
#ifdef __linux__
        static const char *dirs[] = { "/usr/lib", "/usr/lib64", "/usr/local/lib", "/lib" };
        unsigned int d;
        for (d = 0; d < sizeof dirs / sizeof dirs[0] && n < 60; d++) {
            snprintf(bufs[n], sizeof bufs[0], "%s/%s", dirs[d], so_soname);
            candidates[n] = bufs[n]; n++;
        }
#endif
    }

    candidates[n] = NULL;
    for (i = 0; i < n; i++) {
        lib_handle_t h = load_lib(candidates[i]);
        if (h) { mir_handle = h; res = i; break; }
    }
    if (res < 0) {
        fprintf(stderr, "mir_dyn: cannot load mir (%s)\n", lib_error());
        return -1;
    }
    fprintf(stderr, "mir_dyn: loaded mir %d %s\n", res, candidates[res]);
    return 0;
}

int mir_dyn_init(const char *library_path) {
    if (mir_do_load(library_path) == 0) {
        mir_init_state = 1;
        return 0;
    }
    if (mir_init_state == 0) mir_init_state = -1;
    return -1;
}

void mir_dyn_cleanup(void) {
    if (mir_handle) { free_lib(mir_handle); mir_handle = NULL; }
    mir_init_state = 0;
}

int mir_dyn_libok(void) { return mir_init_state == 1 && mir_handle != NULL; }

int mir_dyn_has_symbol(const char *name) {
    if (!mir_dyn_libok()) return 0;
    return mir_sym(name) != NULL;
}

/* ---------- WRAP 宏 ---------- */
#define WRAP_RET(ret, name, params, args, fallback)                            \
ret name params {                                                              \
    static ret (*fn) params = NULL;                                            \
    if (!fn) {                                                                 \
        if (!mir_dyn_libok()) return fallback;                                 \
        *(void **)(&fn) = mir_sym(#name);                                      \
        if (!fn) {                                                             \
            fprintf(stderr, "mir_dyn: dlsym failed: %s\n", #name);             \
            return fallback;                                                   \
        }                                                                      \
    }                                                                          \
    return fn args;                                                            \
}
#define WRAP_VOID(name, params, args)                                          \
void name params {                                                             \
    static void (*fn) params = NULL;                                           \
    if (!fn) {                                                                 \
        if (!mir_dyn_libok()) return;                                          \
        *(void **)(&fn) = mir_sym(#name);                                      \
        if (!fn) {                                                             \
            fprintf(stderr, "mir_dyn: dlsym failed: %s\n", #name);             \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn args;                                                                   \
}
/* 变参函数：用 __builtin_apply 原样转发寄存器/栈参数 */
#define MIR_VAR_BEGIN(ret, name)                                               \
    static __typeof__(name) *fn = NULL;                                        \
    void *r;                                                                    \
    if (!fn) {                                                                 \
        if (!mir_dyn_libok()) return (ret)0;                                   \
        *(void **)(&fn) = mir_sym(#name);                                      \
        if (!fn) {                                                             \
            fprintf(stderr, "mir_dyn: dlsym failed: %s\n", #name);             \
            return (ret)0;                                                     \
        }                                                                      \
    }
#define MIR_VAR_CALL()                                                         \
    r = __builtin_apply((void (*)(void))fn, __builtin_apply_args(), 4096);     \
    __builtin_return(r);

/* ---------- 143 个 extern 函数（函数名原样） ---------- */
WRAP_RET(double, _MIR_get_api_version, (void), (), 0)
WRAP_RET(MIR_context_t, _MIR_init, (MIR_alloc_t alloc, MIR_code_alloc_t code_alloc), (alloc, code_alloc), NULL)
WRAP_RET(MIR_module_t, MIR_new_module, (MIR_context_t ctx, const char *name), (ctx, name), NULL)
WRAP_RET(MIR_item_t, MIR_new_import, (MIR_context_t ctx, const char *name), (ctx, name), NULL)
WRAP_RET(MIR_item_t, MIR_new_export, (MIR_context_t ctx, const char *name), (ctx, name), NULL)
WRAP_RET(MIR_item_t, MIR_new_forward, (MIR_context_t ctx, const char *name), (ctx, name), NULL)
WRAP_RET(MIR_item_t, MIR_new_bss, (MIR_context_t ctx, const char *name, size_t len), (ctx, name, len), NULL)
WRAP_RET(MIR_item_t, MIR_new_data, (MIR_context_t ctx, const char *name, MIR_type_t el_type, size_t nel, const void *els), (ctx, name, el_type, nel, els), NULL)
WRAP_RET(MIR_item_t, MIR_new_string_data, (MIR_context_t ctx, const char *name, MIR_str_t str), (ctx, name, str), NULL)
WRAP_RET(MIR_item_t, MIR_new_ref_data, (MIR_context_t ctx, const char *name, MIR_item_t item, int64_t disp), (ctx, name, item, disp), NULL)
WRAP_RET(MIR_item_t, MIR_new_lref_data, (MIR_context_t ctx, const char *name, MIR_label_t label, MIR_label_t label2, int64_t disp), (ctx, name, label, label2, disp), NULL)
WRAP_RET(MIR_item_t, MIR_new_expr_data, (MIR_context_t ctx, const char *name, MIR_item_t expr_item), (ctx, name, expr_item), NULL)
WRAP_RET(MIR_item_t, MIR_new_proto_arr, (MIR_context_t ctx, const char *name, size_t nres, MIR_type_t *res_types, size_t nargs, MIR_var_t *vars), (ctx, name, nres, res_types, nargs, vars), NULL)
MIR_item_t MIR_new_proto(MIR_context_t ctx, const char *name, size_t nres, MIR_type_t *res_types, size_t nargs, ...) {
    (void)ctx; (void)name; (void)nres; (void)res_types; (void)nargs;
    MIR_VAR_BEGIN(MIR_item_t, MIR_new_proto)
    MIR_VAR_CALL()
}
WRAP_RET(MIR_item_t, MIR_new_vararg_proto_arr, (MIR_context_t ctx, const char *name, size_t nres, MIR_type_t *res_types, size_t nargs, MIR_var_t *vars), (ctx, name, nres, res_types, nargs, vars), NULL)
MIR_item_t MIR_new_vararg_proto(MIR_context_t ctx, const char *name, size_t nres, MIR_type_t *res_types, size_t nargs, ...) {
    (void)ctx; (void)name; (void)nres; (void)res_types; (void)nargs;
    MIR_VAR_BEGIN(MIR_item_t, MIR_new_vararg_proto)
    MIR_VAR_CALL()
}
WRAP_RET(MIR_item_t, MIR_new_func_arr, (MIR_context_t ctx, const char *name, size_t nres, MIR_type_t *res_types, size_t nargs, MIR_var_t *vars), (ctx, name, nres, res_types, nargs, vars), NULL)
MIR_item_t MIR_new_func(MIR_context_t ctx, const char *name, size_t nres, MIR_type_t *res_types, size_t nargs, ...) {
    (void)ctx; (void)name; (void)nres; (void)res_types; (void)nargs;
    MIR_VAR_BEGIN(MIR_item_t, MIR_new_func)
    MIR_VAR_CALL()
}
WRAP_RET(MIR_item_t, MIR_new_vararg_func_arr, (MIR_context_t ctx, const char *name, size_t nres, MIR_type_t *res_types, size_t nargs, MIR_var_t *vars), (ctx, name, nres, res_types, nargs, vars), NULL)
MIR_item_t MIR_new_vararg_func(MIR_context_t ctx, const char *name, size_t nres, MIR_type_t *res_types, size_t nargs, ...) {
    (void)ctx; (void)name; (void)nres; (void)res_types; (void)nargs;
    MIR_VAR_BEGIN(MIR_item_t, MIR_new_vararg_func)
    MIR_VAR_CALL()
}
WRAP_RET(const char *, MIR_item_name, (MIR_context_t ctx, MIR_item_t item), (ctx, item), NULL)
WRAP_RET(MIR_func_t, MIR_get_item_func, (MIR_context_t ctx, MIR_item_t item), (ctx, item), NULL)
WRAP_RET(MIR_reg_t, MIR_new_func_reg, (MIR_context_t ctx, MIR_func_t func, MIR_type_t type, const char *name), (ctx, func, type, name), 0)
WRAP_RET(MIR_reg_t, MIR_new_global_func_reg, (MIR_context_t ctx, MIR_func_t func, MIR_type_t type, const char *name, const char *hard_reg_name), (ctx, func, type, name, hard_reg_name), 0)
WRAP_VOID(MIR_finish_func, (MIR_context_t ctx), (ctx))
WRAP_VOID(MIR_finish_module, (MIR_context_t ctx), (ctx))
WRAP_RET(MIR_error_func_t, MIR_get_error_func, (MIR_context_t ctx), (ctx), NULL)
WRAP_VOID(MIR_set_error_func, (MIR_context_t ctx, MIR_error_func_t func), (ctx, func))
WRAP_RET(MIR_alloc_t, MIR_get_alloc, (MIR_context_t ctx), (ctx), NULL)
WRAP_RET(int, MIR_get_func_redef_permission_p, (MIR_context_t ctx), (ctx), 0)
WRAP_VOID(MIR_set_func_redef_permission, (MIR_context_t ctx, int flag_p), (ctx, flag_p))
WRAP_RET(MIR_insn_t, MIR_new_insn_arr, (MIR_context_t ctx, MIR_insn_code_t code, size_t nops, MIR_op_t *ops), (ctx, code, nops, ops), NULL)
MIR_insn_t MIR_new_insn(MIR_context_t ctx, MIR_insn_code_t code, ...) {
    (void)ctx; (void)code;
    MIR_VAR_BEGIN(MIR_insn_t, MIR_new_insn)
    MIR_VAR_CALL()
}
MIR_insn_t MIR_new_call_insn(MIR_context_t ctx, size_t nops, ...) {
    (void)ctx; (void)nops;
    MIR_VAR_BEGIN(MIR_insn_t, MIR_new_call_insn)
    MIR_VAR_CALL()
}
MIR_insn_t MIR_new_jcall_insn(MIR_context_t ctx, size_t nops, ...) {
    (void)ctx; (void)nops;
    MIR_VAR_BEGIN(MIR_insn_t, MIR_new_jcall_insn)
    MIR_VAR_CALL()
}
MIR_insn_t MIR_new_ret_insn(MIR_context_t ctx, size_t nops, ...) {
    (void)ctx; (void)nops;
    MIR_VAR_BEGIN(MIR_insn_t, MIR_new_ret_insn)
    MIR_VAR_CALL()
}
WRAP_RET(MIR_insn_t, MIR_copy_insn, (MIR_context_t ctx, MIR_insn_t insn), (ctx, insn), NULL)
WRAP_RET(const char *, MIR_insn_name, (MIR_context_t ctx, MIR_insn_code_t code), (ctx, code), NULL)
WRAP_RET(size_t, MIR_insn_nops, (MIR_context_t ctx, MIR_insn_t insn), (ctx, insn), 0)
WRAP_RET(MIR_op_mode_t, MIR_insn_op_mode, (MIR_context_t ctx, MIR_insn_t insn, size_t nop, int *out_p), (ctx, insn, nop, out_p), 0)
WRAP_RET(MIR_insn_t, MIR_new_label, (MIR_context_t ctx), (ctx), NULL)
WRAP_RET(MIR_reg_t, MIR_reg, (MIR_context_t ctx, const char *reg_name, MIR_func_t func), (ctx, reg_name, func), 0)
WRAP_RET(MIR_type_t, MIR_reg_type, (MIR_context_t ctx, MIR_reg_t reg, MIR_func_t func), (ctx, reg, func), 0)
WRAP_RET(const char *, MIR_reg_name, (MIR_context_t ctx, MIR_reg_t reg, MIR_func_t func), (ctx, reg, func), NULL)
WRAP_RET(const char *, MIR_reg_hard_reg_name, (MIR_context_t ctx, MIR_reg_t reg, MIR_func_t func), (ctx, reg, func), NULL)
WRAP_RET(const char *, MIR_alias_name, (MIR_context_t ctx, MIR_alias_t alias), (ctx, alias), NULL)
WRAP_RET(MIR_alias_t, MIR_alias, (MIR_context_t ctx, const char *name), (ctx, name), 0)
WRAP_RET(MIR_op_t, MIR_new_reg_op, (MIR_context_t ctx, MIR_reg_t reg), (ctx, reg), (MIR_op_t){0})
WRAP_RET(MIR_op_t, MIR_new_int_op, (MIR_context_t ctx, int64_t v), (ctx, v), (MIR_op_t){0})
WRAP_RET(MIR_op_t, MIR_new_uint_op, (MIR_context_t ctx, uint64_t v), (ctx, v), (MIR_op_t){0})
WRAP_RET(MIR_op_t, MIR_new_float_op, (MIR_context_t ctx, float v), (ctx, v), (MIR_op_t){0})
WRAP_RET(MIR_op_t, MIR_new_double_op, (MIR_context_t ctx, double v), (ctx, v), (MIR_op_t){0})
WRAP_RET(MIR_op_t, MIR_new_ldouble_op, (MIR_context_t ctx, long double v), (ctx, v), (MIR_op_t){0})
WRAP_RET(MIR_op_t, MIR_new_ref_op, (MIR_context_t ctx, MIR_item_t item), (ctx, item), (MIR_op_t){0})
WRAP_RET(MIR_op_t, MIR_new_str_op, (MIR_context_t ctx, MIR_str_t str), (ctx, str), (MIR_op_t){0})
WRAP_RET(MIR_op_t, MIR_new_mem_op, (MIR_context_t ctx, MIR_type_t type, MIR_disp_t disp, MIR_reg_t base, MIR_reg_t index, MIR_scale_t scale), (ctx, type, disp, base, index, scale), (MIR_op_t){0})
WRAP_RET(MIR_op_t, MIR_new_alias_mem_op, (MIR_context_t ctx, MIR_type_t type, MIR_disp_t disp, MIR_reg_t base, MIR_reg_t index, MIR_scale_t scale, MIR_alias_t alias, MIR_alias_t noalias), (ctx, type, disp, base, index, scale, alias, noalias), (MIR_op_t){0})
WRAP_RET(MIR_op_t, MIR_new_label_op, (MIR_context_t ctx, MIR_label_t label), (ctx, label), (MIR_op_t){0})
WRAP_RET(int, MIR_op_eq_p, (MIR_context_t ctx, MIR_op_t op1, MIR_op_t op2), (ctx, op1, op2), 0)
WRAP_RET(htab_hash_t, MIR_op_hash_step, (MIR_context_t ctx, htab_hash_t h, MIR_op_t op), (ctx, h, op), 0)
WRAP_VOID(MIR_append_insn, (MIR_context_t ctx, MIR_item_t func, MIR_insn_t insn), (ctx, func, insn))
WRAP_VOID(MIR_prepend_insn, (MIR_context_t ctx, MIR_item_t func, MIR_insn_t insn), (ctx, func, insn))
WRAP_VOID(MIR_insert_insn_after, (MIR_context_t ctx, MIR_item_t func, MIR_insn_t after, MIR_insn_t insn), (ctx, func, after, insn))
WRAP_VOID(MIR_insert_insn_before, (MIR_context_t ctx, MIR_item_t func, MIR_insn_t before, MIR_insn_t insn), (ctx, func, before, insn))
WRAP_VOID(MIR_remove_insn, (MIR_context_t ctx, MIR_item_t func, MIR_insn_t insn), (ctx, func, insn))
WRAP_VOID(MIR_change_module_ctx, (MIR_context_t old_ctx, MIR_module_t m, MIR_context_t new_ctx), (old_ctx, m, new_ctx))
WRAP_RET(MIR_insn_code_t, MIR_reverse_branch_code, (MIR_insn_code_t code), (code), 0)
WRAP_RET(const char *, MIR_type_str, (MIR_context_t ctx, MIR_type_t tp), (ctx, tp), NULL)
WRAP_VOID(MIR_output_str, (MIR_context_t ctx, FILE *f, MIR_str_t str), (ctx, f, str))
WRAP_VOID(MIR_output_op, (MIR_context_t ctx, FILE *f, MIR_op_t op, MIR_func_t func), (ctx, f, op, func))
WRAP_VOID(MIR_output_insn, (MIR_context_t ctx, FILE *f, MIR_insn_t insn, MIR_func_t func, int newline_p), (ctx, f, insn, func, newline_p))
WRAP_VOID(MIR_output_item, (MIR_context_t ctx, FILE *f, MIR_item_t item), (ctx, f, item))
WRAP_VOID(MIR_output_module, (MIR_context_t ctx, FILE *f, MIR_module_t module), (ctx, f, module))
WRAP_VOID(MIR_output, (MIR_context_t ctx, FILE *f), (ctx, f))
WRAP_VOID(MIR_write_module, (MIR_context_t ctx, FILE *f, MIR_module_t module), (ctx, f, module))
WRAP_VOID(MIR_read, (MIR_context_t ctx, FILE *f), (ctx, f))
WRAP_VOID(MIR_write_with_func, (MIR_context_t ctx, int (*const writer_func)(MIR_context_t, uint8_t)), (ctx, writer_func))
WRAP_VOID(MIR_write_module_with_func, (MIR_context_t ctx, int (*const writer_func)(MIR_context_t, uint8_t), MIR_module_t module), (ctx, writer_func, module))
WRAP_VOID(MIR_read_with_func, (MIR_context_t ctx, int (*const reader_func)(MIR_context_t)), (ctx, reader_func))
WRAP_RET(MIR_item_t, MIR_get_module_item, (MIR_context_t ctx, MIR_module_t module, const char *name), (ctx, module, name), NULL)
WRAP_VOID(MIR_load_module, (MIR_context_t ctx, MIR_module_t m), (ctx, m))
WRAP_VOID(MIR_load_external, (MIR_context_t ctx, const char *name, void *addr), (ctx, name, addr))
WRAP_VOID(MIR_link, (MIR_context_t ctx, void (*set_interface)(MIR_context_t, MIR_item_t), void *(*import_resolver)(const char *)), (ctx, set_interface, import_resolver))
void MIR_interp(MIR_context_t ctx, MIR_item_t func_item, MIR_val_t *results, size_t nargs, ...) {
    (void)ctx; (void)func_item; (void)results; (void)nargs;
    MIR_VAR_BEGIN(void, MIR_interp)
    MIR_VAR_CALL()
}
WRAP_VOID(MIR_interp_arr, (MIR_context_t ctx, MIR_item_t func_item, MIR_val_t *results, size_t nargs, MIR_val_t *vals), (ctx, func_item, results, nargs, vals))
WRAP_VOID(MIR_interp_arr_varg, (MIR_context_t ctx, MIR_item_t func_item, MIR_val_t *results, size_t nargs, MIR_val_t *vals, va_list va), (ctx, func_item, results, nargs, vals, va))
WRAP_VOID(MIR_set_interp_interface, (MIR_context_t ctx, MIR_item_t func_item), (ctx, func_item))
WRAP_RET(const char *, _MIR_uniq_string, (MIR_context_t ctx, const char *str), (ctx, str), NULL)
WRAP_RET(int, _MIR_reserved_ref_name_p, (MIR_context_t ctx, const char *name), (ctx, name), 0)
WRAP_RET(int, _MIR_reserved_name_p, (MIR_context_t ctx, const char *name), (ctx, name), 0)
WRAP_RET(int64_t, _MIR_addr_offset, (MIR_context_t ctx, MIR_insn_code_t code), (ctx, code), 0)
WRAP_VOID(_MIR_free_insn, (MIR_context_t ctx, MIR_insn_t insn), (ctx, insn))
WRAP_RET(MIR_reg_t, _MIR_new_temp_reg, (MIR_context_t ctx, MIR_type_t type, MIR_func_t func), (ctx, type, func), 0)
WRAP_RET(size_t, _MIR_type_size, (MIR_context_t ctx, MIR_type_t type), (ctx, type), 0)
WRAP_RET(MIR_op_mode_t, _MIR_insn_code_op_mode, (MIR_context_t ctx, MIR_insn_code_t code, size_t nop, int *out_p), (ctx, code, nop, out_p), 0)
MIR_insn_t _MIR_new_unspec_insn(MIR_context_t ctx, size_t nops, ...) {
    (void)ctx; (void)nops;
    MIR_VAR_BEGIN(MIR_insn_t, _MIR_new_unspec_insn)
    MIR_VAR_CALL()
}
WRAP_VOID(_MIR_register_unspec_insn, (MIR_context_t ctx, uint64_t code, const char *name, size_t nres, MIR_type_t *res_types, size_t nargs, int vararg_p, MIR_var_t *args), (ctx, code, name, nres, res_types, nargs, vararg_p, args))
WRAP_VOID(_MIR_duplicate_func_insns, (MIR_context_t ctx, MIR_item_t func_item), (ctx, func_item))
WRAP_VOID(_MIR_restore_func_insns, (MIR_context_t ctx, MIR_item_t func_item), (ctx, func_item))
WRAP_VOID(_MIR_output_data_item_els, (MIR_context_t ctx, FILE *f, MIR_item_t item, int c_p), (ctx, f, item, c_p))
WRAP_VOID(_MIR_get_temp_item_name, (MIR_context_t ctx, MIR_module_t module, char *buff, size_t buff_len), (ctx, module, buff, buff_len))
WRAP_RET(MIR_op_t, _MIR_new_var_op, (MIR_context_t ctx, MIR_reg_t var), (ctx, var), (MIR_op_t){0})
WRAP_RET(MIR_op_t, _MIR_new_var_mem_op, (MIR_context_t ctx, MIR_type_t type, MIR_disp_t disp, MIR_reg_t base, MIR_reg_t index, MIR_scale_t scale), (ctx, type, disp, base, index, scale), (MIR_op_t){0})
WRAP_RET(MIR_op_t, _MIR_new_alias_var_mem_op, (MIR_context_t ctx, MIR_type_t type, MIR_disp_t disp, MIR_reg_t base, MIR_reg_t index, MIR_scale_t scale, MIR_alias_t alias, MIR_alias_t no_alias), (ctx, type, disp, base, index, scale, alias, no_alias), (MIR_op_t){0})
MIR_item_t _MIR_builtin_proto(MIR_context_t ctx, MIR_module_t module, const char *name, size_t nres, MIR_type_t *res_types, size_t nargs, ...) {
    (void)ctx; (void)module; (void)name; (void)nres; (void)res_types; (void)nargs;
    MIR_VAR_BEGIN(MIR_item_t, _MIR_builtin_proto)
    MIR_VAR_CALL()
}
WRAP_RET(MIR_item_t, _MIR_builtin_func, (MIR_context_t ctx, MIR_module_t module, const char *name, void *addr), (ctx, module, name, addr), NULL)
WRAP_VOID(_MIR_flush_code_cache, (void *start, void *bound), (start, bound))
WRAP_RET(uint8_t *, _MIR_publish_code, (MIR_context_t ctx, const uint8_t *code, size_t code_len), (ctx, code, code_len), NULL)
WRAP_RET(uint8_t *, _MIR_get_new_code_addr, (MIR_context_t ctx, size_t size), (ctx, size), NULL)
WRAP_RET(uint8_t *, _MIR_publish_code_by_addr, (MIR_context_t ctx, void *addr, const uint8_t *code, size_t code_len), (ctx, addr, code, code_len), NULL)
WRAP_VOID(_MIR_set_code, (MIR_code_alloc_t alloc, size_t prot_start, size_t prot_len, uint8_t *base, size_t nloc, const MIR_code_reloc_t *relocs, size_t reloc_size), (alloc, prot_start, prot_len, base, nloc, relocs, reloc_size))
WRAP_VOID(_MIR_change_code, (MIR_context_t ctx, uint8_t *addr, const uint8_t *code, size_t code_len), (ctx, addr, code, code_len))
WRAP_VOID(_MIR_update_code_arr, (MIR_context_t ctx, uint8_t *base, size_t nloc, const MIR_code_reloc_t *relocs), (ctx, base, nloc, relocs))
void _MIR_update_code(MIR_context_t ctx, uint8_t *base, size_t nloc, ...) {
    (void)ctx; (void)base; (void)nloc;
    MIR_VAR_BEGIN(void, _MIR_update_code)
    MIR_VAR_CALL()
}
WRAP_RET(void *, va_arg_builtin, (void *p, uint64_t t), (p, t), NULL)
WRAP_VOID(va_block_arg_builtin, (void *res, void *p, size_t s, uint64_t t), (res, p, s, t))
WRAP_VOID(va_start_interp_builtin, (MIR_context_t ctx, void *p, void *a), (ctx, p, a))
WRAP_VOID(va_end_interp_builtin, (MIR_context_t ctx, void *p), (ctx, p))
WRAP_RET(void *, _MIR_get_bstart_builtin, (MIR_context_t ctx), (ctx), NULL)
WRAP_RET(void *, _MIR_get_bend_builtin, (MIR_context_t ctx), (ctx), NULL)
WRAP_RET(void *, _MIR_get_ff_call, (MIR_context_t ctx, size_t nres, MIR_type_t *res_types, size_t nargs, _MIR_arg_desc_t *arg_descs, size_t arg_vars_num), (ctx, nres, res_types, nargs, arg_descs, arg_vars_num), NULL)
WRAP_RET(void *, _MIR_get_interp_shim, (MIR_context_t ctx, MIR_item_t func_item, void *handler), (ctx, func_item, handler), NULL)
WRAP_RET(void *, _MIR_get_thunk, (MIR_context_t ctx), (ctx), NULL)
WRAP_RET(void *, _MIR_get_thunk_addr, (MIR_context_t ctx, void *thunk), (ctx, thunk), NULL)
WRAP_VOID(_MIR_redirect_thunk, (MIR_context_t ctx, void *thunk, void *to), (ctx, thunk, to))
WRAP_RET(void *, _MIR_get_jmpi_thunk, (MIR_context_t ctx, void **res_loc, void *res, void *cont), (ctx, res_loc, res, cont), NULL)
WRAP_RET(void *, _MIR_get_wrapper, (MIR_context_t ctx, MIR_item_t called_func, void *hook_address), (ctx, called_func, hook_address), NULL)
WRAP_RET(void *, _MIR_get_wrapper_end, (MIR_context_t ctx), (ctx), NULL)
WRAP_RET(void *, _MIR_get_bb_thunk, (MIR_context_t ctx, void *bb_version, void *handler), (ctx, bb_version, handler), NULL)
WRAP_VOID(_MIR_replace_bb_thunk, (MIR_context_t ctx, void *thunk, void *to), (ctx, thunk, to))
WRAP_RET(void *, _MIR_get_bb_wrapper, (MIR_context_t ctx, void *data, void *hook_address), (ctx, data, hook_address), NULL)
WRAP_RET(int, _MIR_name_char_p, (MIR_context_t ctx, int ch, int first_p), (ctx, ch, first_p), 0)
WRAP_VOID(_MIR_dump_code, (const char *name, uint8_t *code, size_t code_len), (name, code, code_len))
WRAP_RET(int, _MIR_get_hard_reg, (MIR_context_t ctx, const char *hard_reg_name), (ctx, hard_reg_name), 0)
WRAP_RET(void *, _MIR_get_module_global_var_hard_regs, (MIR_context_t ctx, MIR_module_t module), (ctx, module), NULL)
WRAP_VOID(MIR_gen_set_debug_file, (MIR_context_t ctx, FILE *f), (ctx, f))
WRAP_VOID(MIR_gen_set_debug_level, (MIR_context_t ctx, int debug_level), (ctx, debug_level))
WRAP_VOID(MIR_gen_set_optimize_level, (MIR_context_t ctx, unsigned int level), (ctx, level))
WRAP_RET(void *, MIR_gen, (MIR_context_t ctx, MIR_item_t func_item), (ctx, func_item), NULL)
WRAP_VOID(MIR_set_gen_interface, (MIR_context_t ctx, MIR_item_t func_item), (ctx, func_item))
WRAP_VOID(MIR_set_lazy_gen_interface, (MIR_context_t ctx, MIR_item_t func_item), (ctx, func_item))
WRAP_VOID(MIR_set_lazy_bb_gen_interface, (MIR_context_t ctx, MIR_item_t func_item), (ctx, func_item))
WRAP_VOID(MIR_gen_finish, (MIR_context_t ctx), (ctx))
