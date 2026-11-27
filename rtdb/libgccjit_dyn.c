/* ============================================================
 * libgccjit_dyn.c - dlopen 模拟链接：libgccjit C API（GCC 16.2.1）
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照 /usr/include/libgccjit.h 的 166 个公开
 * 函数声明；opaque handle、枚举按 ABI 等价的 int/指针内嵌，不
 * 引入目标库头文件。函数名保持原生原样。
 * 链接期不需要 libgccjit，仅需 -ldl。
 * 加载顺序：显式路径 > LIBGCCJIT_DYN_LIB > LD_LIBRARY_PATH >
 *           soname > .so > Nix > 平台默认；成功后不卸载。
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
#include <sys/types.h>

/* ---------- 平台适配层 ---------- */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE libgccjit_lib_handle_t;
  static libgccjit_lib_handle_t libgccjit_load_lib(const char *path) { return LoadLibraryA(path); }
  static void *libgccjit_get_sym(libgccjit_lib_handle_t h, const char *name) { return (void *)GetProcAddress(h, name); }
  static void libgccjit_free_lib(libgccjit_lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *libgccjit_lib_error(void) { return "LoadLibrary failed"; }
  #define LIBGCCJIT_PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *libgccjit_lib_handle_t;
  static libgccjit_lib_handle_t libgccjit_load_lib(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
  static void *libgccjit_get_sym(libgccjit_lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void libgccjit_free_lib(libgccjit_lib_handle_t h) { if (h) dlclose(h); }
  static const char *libgccjit_lib_error(void) { return dlerror(); }
  #define LIBGCCJIT_PATH_SEP ':'
#endif

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<libgccjit.h>)
#    include <libgccjit.h>
#    define LIBGCCJIT_DYN_HAVE_HEADER 1
    typedef enum gcc_jit_str_option gcc_jit_str_option;
    typedef enum gcc_jit_int_option gcc_jit_int_option;
    typedef enum gcc_jit_bool_option gcc_jit_bool_option;
    typedef enum gcc_jit_output_kind gcc_jit_output_kind;
    typedef enum gcc_jit_types gcc_jit_types;
    typedef enum gcc_jit_function_kind gcc_jit_function_kind;
    typedef enum gcc_jit_global_kind gcc_jit_global_kind;
    typedef enum gcc_jit_tls_model gcc_jit_tls_model;
    typedef enum gcc_jit_unary_op gcc_jit_unary_op;
    typedef enum gcc_jit_binary_op gcc_jit_binary_op;
    typedef enum gcc_jit_comparison gcc_jit_comparison;
    typedef enum gcc_jit_fn_attribute gcc_jit_fn_attribute;
    typedef enum gcc_jit_variable_attribute gcc_jit_variable_attribute;
#  endif
#endif

#ifndef LIBGCCJIT_DYN_HAVE_HEADER
/* ---------- 类型（头文件原样内嵌，字段顺序即 ABI） ---------- */
typedef struct gcc_jit_context gcc_jit_context;
typedef struct gcc_jit_result gcc_jit_result;
typedef struct gcc_jit_target_info gcc_jit_target_info;
typedef struct gcc_jit_timer gcc_jit_timer;
typedef struct gcc_jit_object gcc_jit_object;
typedef struct gcc_jit_location gcc_jit_location;
typedef struct gcc_jit_type gcc_jit_type;
typedef struct gcc_jit_function_type gcc_jit_function_type;
typedef struct gcc_jit_vector_type gcc_jit_vector_type;
typedef struct gcc_jit_field gcc_jit_field;
typedef struct gcc_jit_struct gcc_jit_struct;
typedef struct gcc_jit_function gcc_jit_function;
typedef struct gcc_jit_block gcc_jit_block;
typedef struct gcc_jit_rvalue gcc_jit_rvalue;
typedef struct gcc_jit_lvalue gcc_jit_lvalue;
typedef struct gcc_jit_param gcc_jit_param;
typedef struct gcc_jit_case gcc_jit_case;
typedef struct gcc_jit_extended_asm gcc_jit_extended_asm;

/* 枚举在 C ABI 中等价于 int；只保留类型名，取值由调用方按头文件使用。 */
typedef int gcc_jit_str_option;
typedef int gcc_jit_int_option;
typedef int gcc_jit_bool_option;
typedef int gcc_jit_output_kind;
typedef int gcc_jit_types;
typedef int gcc_jit_function_kind;
typedef int gcc_jit_global_kind;
typedef int gcc_jit_tls_model;
typedef int gcc_jit_unary_op;
typedef int gcc_jit_binary_op;
typedef int gcc_jit_comparison;
typedef int gcc_jit_fn_attribute;
typedef int gcc_jit_variable_attribute;

#endif /* LIBGCCJIT_DYN_HAVE_HEADER */

/* ---------- 动态层状态 ---------- */
static libgccjit_lib_handle_t gccjit_handle = NULL;
static int gccjit_init_state = 0;

static void *gccjit_sym(const char *name) {
    return gccjit_handle ? libgccjit_get_sym(gccjit_handle, name) : NULL;
}

static int gccjit_do_load(const char *library_path) {
    static char bufs[64][1024];
    const char *candidates[64];
    const char *so_soname = "libgccjit.so.0";
    const char *so_ver = "libgccjit.so.0";
    const char *so_plain = "libgccjit.so";
    int n = 0, i;
    int res = -1;

    if (gccjit_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("LIBGCCJIT_DYN_LIB");
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
                    char *sep = strchr(p, LIBGCCJIT_PATH_SEP);
                    if (sep) *sep = '\0';
                    if (*p) {
                        snprintf(bufs[n], sizeof bufs[0], "%s%c%s", p, LIBGCCJIT_PATH_SEP, so_soname);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s%c%s", p, LIBGCCJIT_PATH_SEP, so_plain);
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
        libgccjit_lib_handle_t h = libgccjit_load_lib(candidates[i]);
        if (h) { gccjit_handle = h; res = i; break; }
    }
    if (res < 0) {
        fprintf(stderr, "libgccjit_dyn: cannot load gccjit (%s)\n", libgccjit_lib_error());
        return -1;
    }
    fprintf(stderr, "libgccjit_dyn: loaded %d %s\n", res, candidates[res]);
    return 0;
}

int gccjit_dyn_init(const char *library_path) {
    if (gccjit_do_load(library_path) == 0) {
        gccjit_init_state = 1;
        return 0;
    }
    if (gccjit_init_state == 0) gccjit_init_state = -1;
    return -1;
}

void gccjit_dyn_cleanup(void) {
    if (gccjit_handle) { libgccjit_free_lib(gccjit_handle); gccjit_handle = NULL; }
    gccjit_init_state = 0;
}

int libgccjit_libok(void) { return gccjit_init_state == 1 && gccjit_handle != NULL; }

int gccjit_dyn_has_symbol(const char *name) {
    if (!libgccjit_libok()) return 0;
    return gccjit_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define LIBGCCJIT_WRAP_RET(ret, name, params, args, fallback)                            \
ret name params {                                                              \
    static ret (*fn) params = NULL;                                            \
    if (!fn) {                                                                 \
        if (!libgccjit_libok()) return fallback;                               \
        *(void **)(&fn) = gccjit_sym(#name);                                   \
        if (!fn) {                                                             \
            fprintf(stderr, "libgccjit_dyn: dlsym failed: %s\n", #name);          \
            return fallback;                                                   \
        }                                                                      \
    }                                                                          \
    return fn args;                                                            \
}
#define LIBGCCJIT_WRAP_VOID(name, params, args)                                          \
void name params {                                                             \
    static void (*fn) params = NULL;                                           \
    if (!fn) {                                                                 \
        if (!libgccjit_libok()) return;                                        \
        *(void **)(&fn) = gccjit_sym(#name);                                   \
        if (!fn) {                                                             \
            fprintf(stderr, "libgccjit_dyn: dlsym failed: %s\n", #name);          \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn args;                                                                   \
}

/* ---------- 166 个公开函数（函数名原样） ---------- */
LIBGCCJIT_WRAP_RET(gcc_jit_context *, gcc_jit_context_acquire, (void), (), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_context_release, (gcc_jit_context *ctxt), (ctxt))
LIBGCCJIT_WRAP_VOID(gcc_jit_context_set_str_option, (gcc_jit_context *ctxt, gcc_jit_str_option opt, const char *value), (ctxt, opt, value))
LIBGCCJIT_WRAP_VOID(gcc_jit_context_set_int_option, (gcc_jit_context *ctxt, gcc_jit_int_option opt, int value), (ctxt, opt, value))
LIBGCCJIT_WRAP_VOID(gcc_jit_context_set_bool_option, (gcc_jit_context *ctxt, gcc_jit_bool_option opt, int value), (ctxt, opt, value))
LIBGCCJIT_WRAP_VOID(gcc_jit_context_set_bool_allow_unreachable_blocks, (gcc_jit_context *ctxt, int bool_value), (ctxt, bool_value))
LIBGCCJIT_WRAP_VOID(gcc_jit_context_set_bool_print_errors_to_stderr, (gcc_jit_context *ctxt, int enabled), (ctxt, enabled))
LIBGCCJIT_WRAP_VOID(gcc_jit_context_set_bool_use_external_driver, (gcc_jit_context *ctxt, int bool_value), (ctxt, bool_value))
LIBGCCJIT_WRAP_VOID(gcc_jit_context_add_command_line_option, (gcc_jit_context *ctxt, const char *optname), (ctxt, optname))
LIBGCCJIT_WRAP_VOID(gcc_jit_context_add_driver_option, (gcc_jit_context *ctxt, const char *optname), (ctxt, optname))
LIBGCCJIT_WRAP_RET(gcc_jit_result *, gcc_jit_context_compile, (gcc_jit_context *ctxt), (ctxt), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_context_compile_to_file, (gcc_jit_context *ctxt, gcc_jit_output_kind output_kind, const char *output_path), (ctxt, output_kind, output_path))
LIBGCCJIT_WRAP_VOID(gcc_jit_context_dump_to_file, (gcc_jit_context *ctxt, const char *path, int update_locations), (ctxt, path, update_locations))
LIBGCCJIT_WRAP_VOID(gcc_jit_context_set_logfile, (gcc_jit_context *ctxt, FILE *logfile, int flags, int verbosity), (ctxt, logfile, flags, verbosity))
LIBGCCJIT_WRAP_RET(const char *, gcc_jit_context_get_first_error, (gcc_jit_context *ctxt), (ctxt), NULL)
LIBGCCJIT_WRAP_RET(const char *, gcc_jit_context_get_last_error, (gcc_jit_context *ctxt), (ctxt), NULL)
LIBGCCJIT_WRAP_RET(void *, gcc_jit_result_get_code, (gcc_jit_result *result, const char *funcname), (result, funcname), NULL)
LIBGCCJIT_WRAP_RET(void *, gcc_jit_result_get_global, (gcc_jit_result *result, const char *name), (result, name), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_result_release, (gcc_jit_result *result), (result))
LIBGCCJIT_WRAP_RET(gcc_jit_context *, gcc_jit_object_get_context, (gcc_jit_object *obj), (obj), NULL)
LIBGCCJIT_WRAP_RET(const char *, gcc_jit_object_get_debug_string, (gcc_jit_object *obj), (obj), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_location *, gcc_jit_context_new_location, (gcc_jit_context *ctxt, const char *filename, int line, int column), (ctxt, filename, line, column), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_object *, gcc_jit_location_as_object, (gcc_jit_location *loc), (loc), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_object *, gcc_jit_type_as_object, (gcc_jit_type *type), (type), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_context_get_type, (gcc_jit_context *ctxt, gcc_jit_types type_), (ctxt, type_), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_context_get_int_type, (gcc_jit_context *ctxt, int num_bytes, int is_signed), (ctxt, num_bytes, is_signed), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_type_get_pointer, (gcc_jit_type *type), (type), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_type_get_const, (gcc_jit_type *type), (type), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_type_get_volatile, (gcc_jit_type *type), (type), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_type_get_restrict, (gcc_jit_type *type), (type), NULL)
LIBGCCJIT_WRAP_RET(int, gcc_jit_compatible_types, (gcc_jit_type *ltype, gcc_jit_type *rtype), (ltype, rtype), 0)
LIBGCCJIT_WRAP_RET(ssize_t, gcc_jit_type_get_size, (gcc_jit_type *type), (type), 0)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_context_new_array_type, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_type *element_type, int num_elements), (ctxt, loc, element_type, num_elements), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_context_new_array_type_u64, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_type *element_type, uint64_t num_elements), (ctxt, loc, element_type, num_elements), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_field *, gcc_jit_context_new_field, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_type *type, const char *name), (ctxt, loc, type, name), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_field *, gcc_jit_context_new_bitfield, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_type *type, int width, const char *name), (ctxt, loc, type, width, name), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_object *, gcc_jit_field_as_object, (gcc_jit_field *field), (field), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_struct *, gcc_jit_context_new_struct_type, (gcc_jit_context *ctxt, gcc_jit_location *loc, const char *name, int num_fields, gcc_jit_field **fields), (ctxt, loc, name, num_fields, fields), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_struct *, gcc_jit_context_new_opaque_struct, (gcc_jit_context *ctxt, gcc_jit_location *loc, const char *name), (ctxt, loc, name), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_struct_as_type, (gcc_jit_struct *struct_type), (struct_type), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_struct_set_fields, (gcc_jit_struct *struct_type, gcc_jit_location *loc, int num_fields, gcc_jit_field **fields), (struct_type, loc, num_fields, fields))
LIBGCCJIT_WRAP_RET(gcc_jit_field *, gcc_jit_struct_get_field, (gcc_jit_struct *struct_type, size_t index), (struct_type, index), NULL)
LIBGCCJIT_WRAP_RET(size_t, gcc_jit_struct_get_field_count, (gcc_jit_struct *struct_type), (struct_type), 0)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_context_new_union_type, (gcc_jit_context *ctxt, gcc_jit_location *loc, const char *name, int num_fields, gcc_jit_field **fields), (ctxt, loc, name, num_fields, fields), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_context_new_function_ptr_type, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_type *return_type, int num_params, gcc_jit_type **param_types, int is_variadic), (ctxt, loc, return_type, num_params, param_types, is_variadic), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_param *, gcc_jit_context_new_param, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_type *type, const char *name), (ctxt, loc, type, name), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_object *, gcc_jit_param_as_object, (gcc_jit_param *param), (param), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_lvalue *, gcc_jit_param_as_lvalue, (gcc_jit_param *param), (param), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_param_as_rvalue, (gcc_jit_param *param), (param), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_function *, gcc_jit_context_new_function, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_function_kind kind, gcc_jit_type *return_type, const char *name, int num_params, gcc_jit_param **params, int is_variadic), (ctxt, loc, kind, return_type, name, num_params, params, is_variadic), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_function *, gcc_jit_context_get_builtin_function, (gcc_jit_context *ctxt, const char *name), (ctxt, name), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_object *, gcc_jit_function_as_object, (gcc_jit_function *func), (func), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_param *, gcc_jit_function_get_param, (gcc_jit_function *func, int index), (func, index), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_function_dump_to_dot, (gcc_jit_function *func, const char *path), (func, path))
LIBGCCJIT_WRAP_RET(gcc_jit_block *, gcc_jit_function_new_block, (gcc_jit_function *func, const char *name), (func, name), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_object *, gcc_jit_block_as_object, (gcc_jit_block *block), (block), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_function *, gcc_jit_block_get_function, (gcc_jit_block *block), (block), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_lvalue *, gcc_jit_context_new_global, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_global_kind kind, gcc_jit_type *type, const char *name), (ctxt, loc, kind, type, name), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_struct_constructor, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_type *type, size_t num_values, gcc_jit_field **fields, gcc_jit_rvalue **values), (ctxt, loc, type, num_values, fields, values), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_union_constructor, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_type *type, gcc_jit_field *field, gcc_jit_rvalue *value), (ctxt, loc, type, field, value), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_array_constructor, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_type *type, size_t num_values, gcc_jit_rvalue **values), (ctxt, loc, type, num_values, values), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_lvalue *, gcc_jit_global_set_initializer_rvalue, (gcc_jit_lvalue *global, gcc_jit_rvalue *init_value), (global, init_value), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_function *, gcc_jit_context_get_target_builtin_function, (gcc_jit_context *ctxt, const char *name), (ctxt, name), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_lvalue *, gcc_jit_global_set_initializer, (gcc_jit_lvalue *global, const void *blob, size_t num_bytes), (global, blob, num_bytes), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_global_set_readonly, (gcc_jit_lvalue *global), (global))
LIBGCCJIT_WRAP_RET(gcc_jit_object *, gcc_jit_lvalue_as_object, (gcc_jit_lvalue *lvalue), (lvalue), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_lvalue_as_rvalue, (gcc_jit_lvalue *lvalue), (lvalue), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_object *, gcc_jit_rvalue_as_object, (gcc_jit_rvalue *rvalue), (rvalue), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_rvalue_get_type, (gcc_jit_rvalue *rvalue), (rvalue), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_rvalue_from_int, (gcc_jit_context *ctxt, gcc_jit_type *numeric_type, int value), (ctxt, numeric_type, value), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_rvalue_from_long, (gcc_jit_context *ctxt, gcc_jit_type *numeric_type, long value), (ctxt, numeric_type, value), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_zero, (gcc_jit_context *ctxt, gcc_jit_type *numeric_type), (ctxt, numeric_type), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_one, (gcc_jit_context *ctxt, gcc_jit_type *numeric_type), (ctxt, numeric_type), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_rvalue_from_double, (gcc_jit_context *ctxt, gcc_jit_type *numeric_type, double value), (ctxt, numeric_type, value), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_rvalue_from_ptr, (gcc_jit_context *ctxt, gcc_jit_type *pointer_type, void *value), (ctxt, pointer_type, value), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_null, (gcc_jit_context *ctxt, gcc_jit_type *pointer_type), (ctxt, pointer_type), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_sizeof, (gcc_jit_context *ctxt, gcc_jit_type *type), (ctxt, type), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_alignof, (gcc_jit_context *ctxt, gcc_jit_type *type), (ctxt, type), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_string_literal, (gcc_jit_context *ctxt, const char *value), (ctxt, value), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_unary_op, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_unary_op op, gcc_jit_type *result_type, gcc_jit_rvalue *rvalue), (ctxt, loc, op, result_type, rvalue), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_binary_op, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_binary_op op, gcc_jit_type *result_type, gcc_jit_rvalue *a, gcc_jit_rvalue *b), (ctxt, loc, op, result_type, a, b), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_comparison, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_comparison op, gcc_jit_rvalue *a, gcc_jit_rvalue *b), (ctxt, loc, op, a, b), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_call, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_function *func, int numargs, gcc_jit_rvalue **args), (ctxt, loc, func, numargs, args), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_call_through_ptr, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_rvalue *fn_ptr, int numargs, gcc_jit_rvalue **args), (ctxt, loc, fn_ptr, numargs, args), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_cast, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_rvalue *rvalue, gcc_jit_type *type), (ctxt, loc, rvalue, type), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_bitcast, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_rvalue *rvalue, gcc_jit_type *type), (ctxt, loc, rvalue, type), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_lvalue_set_alignment, (gcc_jit_lvalue *lvalue, unsigned bytes), (lvalue, bytes))
LIBGCCJIT_WRAP_RET(unsigned, gcc_jit_lvalue_get_alignment, (gcc_jit_lvalue *lvalue), (lvalue), 0)
LIBGCCJIT_WRAP_RET(gcc_jit_lvalue *, gcc_jit_context_new_array_access, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_rvalue *ptr, gcc_jit_rvalue *index), (ctxt, loc, ptr, index), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_convert_vector, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_rvalue *vector, gcc_jit_type *type), (ctxt, loc, vector, type), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_rvalue_vector_perm, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_rvalue *elements1, gcc_jit_rvalue *elements2, gcc_jit_rvalue *mask), (ctxt, loc, elements1, elements2, mask), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_lvalue *, gcc_jit_context_new_vector_access, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_rvalue *vector, gcc_jit_rvalue *index), (ctxt, loc, vector, index), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_lvalue *, gcc_jit_lvalue_access_field, (gcc_jit_lvalue *struct_or_union, gcc_jit_location *loc, gcc_jit_field *field), (struct_or_union, loc, field), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_rvalue_access_field, (gcc_jit_rvalue *struct_or_union, gcc_jit_location *loc, gcc_jit_field *field), (struct_or_union, loc, field), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_lvalue *, gcc_jit_rvalue_dereference_field, (gcc_jit_rvalue *ptr, gcc_jit_location *loc, gcc_jit_field *field), (ptr, loc, field), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_lvalue *, gcc_jit_rvalue_dereference, (gcc_jit_rvalue *rvalue, gcc_jit_location *loc), (rvalue, loc), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_lvalue_get_address, (gcc_jit_lvalue *lvalue, gcc_jit_location *loc), (lvalue, loc), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_lvalue_set_tls_model, (gcc_jit_lvalue *lvalue, gcc_jit_tls_model model), (lvalue, model))
LIBGCCJIT_WRAP_VOID(gcc_jit_lvalue_set_link_section, (gcc_jit_lvalue *lvalue, const char *section_name), (lvalue, section_name))
LIBGCCJIT_WRAP_VOID(gcc_jit_lvalue_set_register_name, (gcc_jit_lvalue *lvalue, const char *reg_name), (lvalue, reg_name))
LIBGCCJIT_WRAP_RET(gcc_jit_lvalue *, gcc_jit_function_new_local, (gcc_jit_function *func, gcc_jit_location *loc, gcc_jit_type *type, const char *name), (func, loc, type, name), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_lvalue *, gcc_jit_function_new_temp, (gcc_jit_function *func, gcc_jit_location *loc, gcc_jit_type *type), (func, loc, type), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_block_add_eval, (gcc_jit_block *block, gcc_jit_location *loc, gcc_jit_rvalue *rvalue), (block, loc, rvalue))
LIBGCCJIT_WRAP_VOID(gcc_jit_block_add_assignment, (gcc_jit_block *block, gcc_jit_location *loc, gcc_jit_lvalue *lvalue, gcc_jit_rvalue *rvalue), (block, loc, lvalue, rvalue))
LIBGCCJIT_WRAP_VOID(gcc_jit_block_add_assignment_op, (gcc_jit_block *block, gcc_jit_location *loc, gcc_jit_lvalue *lvalue, gcc_jit_binary_op op, gcc_jit_rvalue *rvalue), (block, loc, lvalue, op, rvalue))
LIBGCCJIT_WRAP_VOID(gcc_jit_block_add_comment, (gcc_jit_block *block, gcc_jit_location *loc, const char *text), (block, loc, text))
LIBGCCJIT_WRAP_VOID(gcc_jit_block_end_with_conditional, (gcc_jit_block *block, gcc_jit_location *loc, gcc_jit_rvalue *boolval, gcc_jit_block *on_true, gcc_jit_block *on_false), (block, loc, boolval, on_true, on_false))
LIBGCCJIT_WRAP_VOID(gcc_jit_block_end_with_jump, (gcc_jit_block *block, gcc_jit_location *loc, gcc_jit_block *target), (block, loc, target))
LIBGCCJIT_WRAP_VOID(gcc_jit_block_end_with_return, (gcc_jit_block *block, gcc_jit_location *loc, gcc_jit_rvalue *rvalue), (block, loc, rvalue))
LIBGCCJIT_WRAP_VOID(gcc_jit_block_end_with_void_return, (gcc_jit_block *block, gcc_jit_location *loc), (block, loc))
LIBGCCJIT_WRAP_RET(gcc_jit_case *, gcc_jit_context_new_case, (gcc_jit_context *ctxt, gcc_jit_rvalue *min_value, gcc_jit_rvalue *max_value, gcc_jit_block *dest_block), (ctxt, min_value, max_value, dest_block), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_object *, gcc_jit_case_as_object, (gcc_jit_case *case_), (case_), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_block_end_with_switch, (gcc_jit_block *block, gcc_jit_location *loc, gcc_jit_rvalue *expr, gcc_jit_block *default_block, int num_cases, gcc_jit_case **cases), (block, loc, expr, default_block, num_cases, cases))
LIBGCCJIT_WRAP_RET(gcc_jit_context *, gcc_jit_context_new_child_context, (gcc_jit_context *parent_ctxt), (parent_ctxt), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_context_dump_reproducer_to_file, (gcc_jit_context *ctxt, const char *path), (ctxt, path))
LIBGCCJIT_WRAP_VOID(gcc_jit_context_enable_dump, (gcc_jit_context *ctxt, const char *dumpname, char **out_ptr), (ctxt, dumpname, out_ptr))
LIBGCCJIT_WRAP_RET(gcc_jit_timer *, gcc_jit_timer_new, (void), (), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_timer_release, (gcc_jit_timer *timer), (timer))
LIBGCCJIT_WRAP_VOID(gcc_jit_context_set_timer, (gcc_jit_context *ctxt, gcc_jit_timer *timer), (ctxt, timer))
LIBGCCJIT_WRAP_RET(gcc_jit_timer *, gcc_jit_context_get_timer, (gcc_jit_context *ctxt), (ctxt), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_timer_push, (gcc_jit_timer *timer, const char *item_name), (timer, item_name))
LIBGCCJIT_WRAP_VOID(gcc_jit_timer_pop, (gcc_jit_timer *timer, const char *item_name), (timer, item_name))
LIBGCCJIT_WRAP_VOID(gcc_jit_timer_print, (gcc_jit_timer *timer, FILE *f_out), (timer, f_out))
LIBGCCJIT_WRAP_VOID(gcc_jit_rvalue_set_bool_require_tail_call, (gcc_jit_rvalue *call, int require_tail_call), (call, require_tail_call))
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_type_get_aligned, (gcc_jit_type *type, size_t alignment_in_bytes), (type, alignment_in_bytes), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_type_get_vector, (gcc_jit_type *type, size_t num_units), (type, num_units), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_function_get_address, (gcc_jit_function *func, gcc_jit_location *loc), (func, loc), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_rvalue *, gcc_jit_context_new_rvalue_from_vector, (gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_type *vec_type, size_t num_elements, gcc_jit_rvalue **elements), (ctxt, loc, vec_type, num_elements, elements), NULL)
LIBGCCJIT_WRAP_RET(int, gcc_jit_version_major, (void), (), 0)
LIBGCCJIT_WRAP_RET(int, gcc_jit_version_minor, (void), (), 0)
LIBGCCJIT_WRAP_RET(int, gcc_jit_version_patchlevel, (void), (), 0)
LIBGCCJIT_WRAP_RET(gcc_jit_extended_asm *, gcc_jit_block_add_extended_asm, (gcc_jit_block *block, gcc_jit_location *loc, const char *asm_template), (block, loc, asm_template), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_extended_asm *, gcc_jit_block_end_with_extended_asm_goto, (gcc_jit_block *block, gcc_jit_location *loc, const char *asm_template, int num_goto_blocks, gcc_jit_block **goto_blocks, gcc_jit_block *fallthrough_block), (block, loc, asm_template, num_goto_blocks, goto_blocks, fallthrough_block), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_object *, gcc_jit_extended_asm_as_object, (gcc_jit_extended_asm *ext_asm), (ext_asm), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_extended_asm_set_volatile_flag, (gcc_jit_extended_asm *ext_asm, int flag), (ext_asm, flag))
LIBGCCJIT_WRAP_VOID(gcc_jit_extended_asm_set_inline_flag, (gcc_jit_extended_asm *ext_asm, int flag), (ext_asm, flag))
LIBGCCJIT_WRAP_VOID(gcc_jit_extended_asm_add_output_operand, (gcc_jit_extended_asm *ext_asm, const char *asm_symbolic_name, const char *constraint, gcc_jit_lvalue *dest), (ext_asm, asm_symbolic_name, constraint, dest))
LIBGCCJIT_WRAP_VOID(gcc_jit_extended_asm_add_input_operand, (gcc_jit_extended_asm *ext_asm, const char *asm_symbolic_name, const char *constraint, gcc_jit_rvalue *src), (ext_asm, asm_symbolic_name, constraint, src))
LIBGCCJIT_WRAP_VOID(gcc_jit_extended_asm_add_clobber, (gcc_jit_extended_asm *ext_asm, const char *victim), (ext_asm, victim))
LIBGCCJIT_WRAP_VOID(gcc_jit_context_add_top_level_asm, (gcc_jit_context *ctxt, gcc_jit_location *loc, const char *asm_stmts), (ctxt, loc, asm_stmts))
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_function_get_return_type, (gcc_jit_function *func), (func), NULL)
LIBGCCJIT_WRAP_RET(size_t, gcc_jit_function_get_param_count, (gcc_jit_function *func), (func), 0)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_type_dyncast_array, (gcc_jit_type *type), (type), NULL)
LIBGCCJIT_WRAP_RET(int, gcc_jit_type_is_bool, (gcc_jit_type *type), (type), 0)
LIBGCCJIT_WRAP_RET(gcc_jit_function_type *, gcc_jit_type_dyncast_function_ptr_type, (gcc_jit_type *type), (type), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_function_type_get_return_type, (gcc_jit_function_type *function_type), (function_type), NULL)
LIBGCCJIT_WRAP_RET(size_t, gcc_jit_function_type_get_param_count, (gcc_jit_function_type *function_type), (function_type), 0)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_function_type_get_param_type, (gcc_jit_function_type *function_type, size_t index), (function_type, index), NULL)
LIBGCCJIT_WRAP_RET(int, gcc_jit_type_is_integral, (gcc_jit_type *type), (type), 0)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_type_is_pointer, (gcc_jit_type *type), (type), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_vector_type *, gcc_jit_type_dyncast_vector, (gcc_jit_type *type), (type), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_struct *, gcc_jit_type_is_struct, (gcc_jit_type *type), (type), NULL)
LIBGCCJIT_WRAP_RET(size_t, gcc_jit_vector_type_get_num_units, (gcc_jit_vector_type *vector_type), (vector_type), 0)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_vector_type_get_element_type, (gcc_jit_vector_type *vector_type), (vector_type), NULL)
LIBGCCJIT_WRAP_RET(gcc_jit_type *, gcc_jit_type_unqualified, (gcc_jit_type *type), (type), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_function_add_attribute, (gcc_jit_function *func, gcc_jit_fn_attribute attribute), (func, attribute))
LIBGCCJIT_WRAP_VOID(gcc_jit_function_add_string_attribute, (gcc_jit_function *func, gcc_jit_fn_attribute attribute, const char *value), (func, attribute, value))
LIBGCCJIT_WRAP_RET(gcc_jit_target_info *, gcc_jit_context_get_target_info, (gcc_jit_context *ctxt), (ctxt), NULL)
LIBGCCJIT_WRAP_VOID(gcc_jit_target_info_release, (gcc_jit_target_info *info), (info))
LIBGCCJIT_WRAP_RET(int, gcc_jit_target_info_cpu_supports, (gcc_jit_target_info *info, const char *feature), (info, feature), 0)
LIBGCCJIT_WRAP_RET(const char *, gcc_jit_target_info_arch, (gcc_jit_target_info *info), (info), NULL)
LIBGCCJIT_WRAP_RET(int, gcc_jit_target_info_supports_target_dependent_type, (gcc_jit_target_info *info, gcc_jit_types type), (info, type), 0)
LIBGCCJIT_WRAP_VOID(gcc_jit_function_add_integer_array_attribute, (gcc_jit_function *func, gcc_jit_fn_attribute attribute, const int *value, size_t length), (func, attribute, value, length))
LIBGCCJIT_WRAP_VOID(gcc_jit_lvalue_add_string_attribute, (gcc_jit_lvalue *variable, gcc_jit_variable_attribute attribute, const char *value), (variable, attribute, value))
LIBGCCJIT_WRAP_VOID(gcc_jit_context_set_output_ident, (gcc_jit_context *ctxt, const char *output_ident), (ctxt, output_ident))
LIBGCCJIT_WRAP_VOID(gcc_jit_context_set_abort_on_unsupported_target_builtin, (gcc_jit_context *ctxt), (ctxt))
