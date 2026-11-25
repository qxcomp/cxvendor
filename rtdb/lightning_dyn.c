/* ============================================================
 * lightning_dyn.c - dlopen 模拟链接：GNU Lightning 2.1 C API
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照 lightning.h.in + jit_x86.h 的 104 个 extern
 * 函数声明；按 ABI 等价内嵌 jit_* typedef 与 jit_code_t 枚举，不引
 * 入目标库头文件。函数名保持原生原样。jit_cpu 全局变量不封装。
 * 链接期不需要 liblightning，仅需 -ldl。
 * 加载顺序：显式路径 > LIGHTNING_DYN_LIB > LD_LIBRARY_PATH > soname
 *           > .so > 平台默认；成功后不卸载。
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
typedef signed char jit_int8_t;
typedef unsigned char jit_uint8_t;
typedef signed short jit_int16_t;
typedef unsigned short jit_uint16_t;
typedef signed int jit_int32_t;
typedef unsigned int jit_uint32_t;
typedef signed long jit_int64_t;
typedef unsigned long jit_uint64_t;
typedef jit_int64_t jit_word_t;
typedef jit_uint64_t jit_uword_t;
typedef float jit_float32_t;
typedef double jit_float64_t;
typedef void *jit_pointer_t;
typedef jit_int32_t jit_bool_t;
typedef jit_int32_t jit_gpr_t;
typedef jit_int32_t jit_fpr_t;
typedef struct jit_node jit_node_t;
typedef struct jit_state jit_state_t;
typedef int jit_code_t; /* enum，在 C ABI 中等价于 int */
typedef void *(*jit_alloc_func_ptr)(size_t);
typedef void *(*jit_realloc_func_ptr)(void *, size_t);
typedef void (*jit_free_func_ptr)(void *);

/* ---------- 动态层状态 ---------- */
static lib_handle_t lightning_handle = NULL;
static int lightning_init_state = 0;

static void *lightning_sym(const char *name) {
    return lightning_handle ? get_sym(lightning_handle, name) : NULL;
}

static int lightning_do_load(const char *library_path) {
    static char bufs[64][1024];
    const char *candidates[64];
    const char *so_soname = "liblightning.so.1";
    const char *so_ver = "liblightning.so.1";
    const char *so_plain = "liblightning.so";
    int n = 0, i;
    int res = -1;

    if (lightning_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("LIGHTNING_DYN_LIB");
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
        if (h) { lightning_handle = h; res = i; break; }
    }
    if (res < 0) {
        fprintf(stderr, "lightning_dyn: cannot load lightning (%s)\n", lib_error());
        return -1;
    }
    fprintf(stderr, "lightning_dyn: loaded lightning %d %s\n", res, candidates[res]);
    return 0;
}

int lightning_dyn_init(const char *library_path) {
    if (lightning_do_load(library_path) == 0) {
        lightning_init_state = 1;
        return 0;
    }
    if (lightning_init_state == 0) lightning_init_state = -1;
    return -1;
}

void lightning_dyn_cleanup(void) {
    if (lightning_handle) { free_lib(lightning_handle); lightning_handle = NULL; }
    lightning_init_state = 0;
}

int lightning_dyn_libok(void) { return lightning_init_state == 1 && lightning_handle != NULL; }

int lightning_dyn_has_symbol(const char *name) {
    if (!lightning_dyn_libok()) return 0;
    return lightning_sym(name) != NULL;
}

/* ---------- WRAP 宏 ---------- */
#define WRAP_RET(ret, name, params, args, fallback)                            \
ret name params {                                                              \
    static ret (*fn) params = NULL;                                            \
    if (!fn) {                                                                 \
        if (!lightning_dyn_libok()) return fallback;                           \
        *(void **)(&fn) = lightning_sym(#name);                                \
        if (!fn) {                                                             \
            fprintf(stderr, "lightning_dyn: dlsym failed: %s\n", #name);       \
            return fallback;                                                   \
        }                                                                      \
    }                                                                          \
    return fn args;                                                            \
}
#define WRAP_VOID(name, params, args)                                          \
void name params {                                                             \
    static void (*fn) params = NULL;                                           \
    if (!fn) {                                                                 \
        if (!lightning_dyn_libok()) return;                                    \
        *(void **)(&fn) = lightning_sym(#name);                                \
        if (!fn) {                                                             \
            fprintf(stderr, "lightning_dyn: dlsym failed: %s\n", #name);       \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn args;                                                                   \
}

/* ---------- 104 个 extern 函数（函数名原样） ---------- */
WRAP_VOID(init_jit, (const char *p), (p))
WRAP_VOID(finish_jit, (void), ())
WRAP_RET(jit_state_t *, jit_new_state, (void), (), NULL)
WRAP_VOID(_jit_clear_state, (jit_state_t *j), (j))
WRAP_VOID(_jit_destroy_state, (jit_state_t *j), (j))
WRAP_RET(jit_pointer_t, _jit_address, (jit_state_t *j, jit_node_t *node), (j, node), NULL)
WRAP_RET(jit_node_t *, _jit_name, (jit_state_t *j, const char *name), (j, name), NULL)
WRAP_RET(jit_node_t *, _jit_note, (jit_state_t *j, const char *name, int size), (j, name, size), NULL)
WRAP_RET(jit_node_t *, _jit_label, (jit_state_t *j), (j), NULL)
WRAP_RET(jit_node_t *, _jit_forward, (jit_state_t *j), (j), NULL)
WRAP_RET(jit_node_t *, _jit_indirect, (jit_state_t *j), (j), NULL)
WRAP_VOID(_jit_link, (jit_state_t *j, jit_node_t *node), (j, node))
WRAP_RET(jit_bool_t, _jit_forward_p, (jit_state_t *j, jit_node_t *node), (j, node), 0)
WRAP_RET(jit_bool_t, _jit_indirect_p, (jit_state_t *j, jit_node_t *node), (j, node), 0)
WRAP_RET(jit_bool_t, _jit_target_p, (jit_state_t *j, jit_node_t *node), (j, node), 0)
WRAP_VOID(_jit_prolog, (jit_state_t *j), (j))
WRAP_RET(jit_int32_t, _jit_allocai, (jit_state_t *j, jit_int32_t size), (j, size), 0)
WRAP_VOID(_jit_allocar, (jit_state_t *j, jit_int32_t reg, jit_int32_t size), (j, reg, size))
WRAP_VOID(_jit_ellipsis, (jit_state_t *j), (j))
WRAP_RET(jit_node_t *, _jit_arg, (jit_state_t *j), (j), NULL)
WRAP_VOID(_jit_getarg_c, (jit_state_t *j, jit_gpr_t reg, jit_node_t *node), (j, reg, node))
WRAP_VOID(_jit_getarg_uc, (jit_state_t *j, jit_gpr_t reg, jit_node_t *node), (j, reg, node))
WRAP_VOID(_jit_getarg_s, (jit_state_t *j, jit_gpr_t reg, jit_node_t *node), (j, reg, node))
WRAP_VOID(_jit_getarg_us, (jit_state_t *j, jit_gpr_t reg, jit_node_t *node), (j, reg, node))
WRAP_VOID(_jit_getarg_i, (jit_state_t *j, jit_gpr_t reg, jit_node_t *node), (j, reg, node))
WRAP_VOID(_jit_getarg_ui, (jit_state_t *j, jit_gpr_t reg, jit_node_t *node), (j, reg, node))
WRAP_VOID(_jit_getarg_l, (jit_state_t *j, jit_gpr_t reg, jit_node_t *node), (j, reg, node))
WRAP_VOID(_jit_putargr, (jit_state_t *j, jit_gpr_t reg, jit_node_t *node), (j, reg, node))
WRAP_VOID(_jit_putargi, (jit_state_t *j, jit_word_t word, jit_node_t *node), (j, word, node))
WRAP_VOID(_jit_prepare, (jit_state_t *j), (j))
WRAP_VOID(_jit_va_push, (jit_state_t *j, jit_gpr_t reg), (j, reg))
WRAP_VOID(_jit_pushargr, (jit_state_t *j, jit_gpr_t reg), (j, reg))
WRAP_VOID(_jit_pushargi, (jit_state_t *j, jit_word_t word), (j, word))
WRAP_VOID(_jit_finishr, (jit_state_t *j, jit_gpr_t reg), (j, reg))
WRAP_RET(jit_node_t *, _jit_finishi, (jit_state_t *j, jit_pointer_t ptr), (j, ptr), NULL)
WRAP_VOID(_jit_ret, (jit_state_t *j), (j))
WRAP_VOID(_jit_retr, (jit_state_t *j, jit_gpr_t reg), (j, reg))
WRAP_VOID(_jit_reti, (jit_state_t *j, jit_word_t word), (j, word))
WRAP_VOID(_jit_retval_c, (jit_state_t *j, jit_gpr_t reg), (j, reg))
WRAP_VOID(_jit_retval_uc, (jit_state_t *j, jit_gpr_t reg), (j, reg))
WRAP_VOID(_jit_retval_s, (jit_state_t *j, jit_gpr_t reg), (j, reg))
WRAP_VOID(_jit_retval_us, (jit_state_t *j, jit_gpr_t reg), (j, reg))
WRAP_VOID(_jit_retval_i, (jit_state_t *j, jit_gpr_t reg), (j, reg))
WRAP_VOID(_jit_retval_ui, (jit_state_t *j, jit_gpr_t reg), (j, reg))
WRAP_VOID(_jit_retval_l, (jit_state_t *j, jit_gpr_t reg), (j, reg))
WRAP_VOID(_jit_epilog, (jit_state_t *j), (j))
WRAP_VOID(_jit_patch, (jit_state_t *j, jit_node_t *node), (j, node))
WRAP_VOID(_jit_patch_at, (jit_state_t *j, jit_node_t *node, jit_node_t *other), (j, node, other))
WRAP_VOID(_jit_patch_abs, (jit_state_t *j, jit_node_t *node, jit_pointer_t ptr), (j, node, ptr))
WRAP_VOID(_jit_realize, (jit_state_t *j), (j))
WRAP_RET(jit_pointer_t, _jit_get_code, (jit_state_t *j, jit_word_t *size), (j, size), NULL)
WRAP_VOID(_jit_set_code, (jit_state_t *j, jit_pointer_t code, jit_word_t size), (j, code, size))
WRAP_RET(jit_pointer_t, _jit_get_data, (jit_state_t *j, jit_word_t *size, jit_word_t *seg), (j, size, seg), NULL)
WRAP_VOID(_jit_set_data, (jit_state_t *j, jit_pointer_t data, jit_word_t size, jit_word_t seg), (j, data, size, seg))
WRAP_VOID(_jit_frame, (jit_state_t *j, jit_int32_t size), (j, size))
WRAP_VOID(_jit_tramp, (jit_state_t *j, jit_int32_t size), (j, size))
WRAP_RET(jit_pointer_t, _jit_emit, (jit_state_t *j), (j), NULL)
WRAP_VOID(_jit_print, (jit_state_t *j), (j))
WRAP_RET(jit_node_t *, _jit_arg_f, (jit_state_t *j), (j), NULL)
WRAP_VOID(_jit_getarg_f, (jit_state_t *j, jit_fpr_t reg, jit_node_t *node), (j, reg, node))
WRAP_VOID(_jit_putargr_f, (jit_state_t *j, jit_fpr_t reg, jit_node_t *node), (j, reg, node))
WRAP_VOID(_jit_putargi_f, (jit_state_t *j, jit_float32_t value, jit_node_t *node), (j, value, node))
WRAP_VOID(_jit_pushargr_f, (jit_state_t *j, jit_fpr_t reg), (j, reg))
WRAP_VOID(_jit_pushargi_f, (jit_state_t *j, jit_float32_t value), (j, value))
WRAP_VOID(_jit_retr_f, (jit_state_t *j, jit_fpr_t reg), (j, reg))
WRAP_VOID(_jit_reti_f, (jit_state_t *j, jit_float32_t value), (j, value))
WRAP_VOID(_jit_retval_f, (jit_state_t *j, jit_fpr_t reg), (j, reg))
WRAP_RET(jit_node_t *, _jit_arg_d, (jit_state_t *j), (j), NULL)
WRAP_VOID(_jit_getarg_d, (jit_state_t *j, jit_fpr_t reg, jit_node_t *node), (j, reg, node))
WRAP_VOID(_jit_putargr_d, (jit_state_t *j, jit_fpr_t reg, jit_node_t *node), (j, reg, node))
WRAP_VOID(_jit_putargi_d, (jit_state_t *j, jit_float64_t value, jit_node_t *node), (j, value, node))
WRAP_VOID(_jit_pushargr_d, (jit_state_t *j, jit_fpr_t reg), (j, reg))
WRAP_VOID(_jit_pushargi_d, (jit_state_t *j, jit_float64_t value), (j, value))
WRAP_VOID(_jit_retr_d, (jit_state_t *j, jit_fpr_t reg), (j, reg))
WRAP_VOID(_jit_reti_d, (jit_state_t *j, jit_float64_t value), (j, value))
WRAP_VOID(_jit_retval_d, (jit_state_t *j, jit_fpr_t reg), (j, reg))
WRAP_RET(jit_int32_t, _jit_get_reg, (jit_state_t *j, jit_int32_t reg), (j, reg), 0)
WRAP_VOID(_jit_unget_reg, (jit_state_t *j, jit_int32_t reg), (j, reg))
WRAP_RET(jit_node_t *, _jit_new_node, (jit_state_t *j, jit_code_t code), (j, code), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_w, (jit_state_t *j, jit_code_t code, jit_word_t w), (j, code, w), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_f, (jit_state_t *j, jit_code_t code, jit_float32_t f), (j, code, f), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_d, (jit_state_t *j, jit_code_t code, jit_float64_t d), (j, code, d), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_p, (jit_state_t *j, jit_code_t code, jit_pointer_t p), (j, code, p), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_ww, (jit_state_t *j, jit_code_t code, jit_word_t w1, jit_word_t w2), (j, code, w1, w2), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_wp, (jit_state_t *j, jit_code_t code, jit_word_t w, jit_pointer_t p), (j, code, w, p), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_fp, (jit_state_t *j, jit_code_t code, jit_float32_t f, jit_pointer_t p), (j, code, f, p), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_dp, (jit_state_t *j, jit_code_t code, jit_float64_t d, jit_pointer_t p), (j, code, d, p), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_pw, (jit_state_t *j, jit_code_t code, jit_pointer_t p, jit_word_t w), (j, code, p, w), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_wf, (jit_state_t *j, jit_code_t code, jit_word_t w, jit_float32_t f), (j, code, w, f), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_wd, (jit_state_t *j, jit_code_t code, jit_word_t w, jit_float64_t d), (j, code, w, d), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_www, (jit_state_t *j, jit_code_t code, jit_word_t w1, jit_word_t w2, jit_word_t w3), (j, code, w1, w2, w3), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_qww, (jit_state_t *j, jit_code_t code, jit_int32_t q1, jit_int32_t q2, jit_word_t w1, jit_word_t w2), (j, code, q1, q2, w1, w2), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_wwf, (jit_state_t *j, jit_code_t code, jit_word_t w1, jit_word_t w2, jit_float32_t f), (j, code, w1, w2, f), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_wwd, (jit_state_t *j, jit_code_t code, jit_word_t w1, jit_word_t w2, jit_float64_t d), (j, code, w1, w2, d), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_pww, (jit_state_t *j, jit_code_t code, jit_pointer_t p, jit_word_t w1, jit_word_t w2), (j, code, p, w1, w2), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_pwf, (jit_state_t *j, jit_code_t code, jit_pointer_t p, jit_word_t w, jit_float32_t f), (j, code, p, w, f), NULL)
WRAP_RET(jit_node_t *, _jit_new_node_pwd, (jit_state_t *j, jit_code_t code, jit_pointer_t p, jit_word_t w, jit_float64_t d), (j, code, p, w, d), NULL)
WRAP_RET(jit_bool_t, _jit_arg_register_p, (jit_state_t *j, jit_node_t *node), (j, node), 0)
WRAP_RET(jit_bool_t, _jit_callee_save_p, (jit_state_t *j, jit_int32_t reg), (j, reg), 0)
WRAP_RET(jit_bool_t, _jit_pointer_p, (jit_state_t *j, jit_pointer_t ptr), (j, ptr), 0)
WRAP_RET(jit_bool_t, _jit_get_note, (jit_state_t *j, jit_pointer_t ptr, char **kind, char **note, int *size), (j, ptr, kind, note, size), 0)
WRAP_VOID(_jit_disassemble, (jit_state_t *j), (j))
WRAP_VOID(jit_set_memory_functions, (jit_alloc_func_ptr alloc, jit_realloc_func_ptr realloc_fn, jit_free_func_ptr free_fn), (alloc, realloc_fn, free_fn))
WRAP_VOID(jit_get_memory_functions, (jit_alloc_func_ptr *alloc, jit_realloc_func_ptr *realloc_fn, jit_free_func_ptr *free_fn), (alloc, realloc_fn, free_fn))
