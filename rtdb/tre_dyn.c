/* ============================================================
 * tre_dyn.c — TRE 动态加载封装层（公开头文件 API 全量封装）
 *
 * 统计：导出函数 47 / 封装 25 / 跳过 22 / 数据符号 0 / 内联类型 7
 * 设计要点（与 rtdb/libxml2_dyn.c 一致）：
 *   1. 单文件、无 .h、无 wrapper 层；链接期不链接目标库（只 -ldl）。
 *   2. 每个封装函数首次调用时才 dlsym 并缓存；未调用的 API 不解析。
 *   3. 编译期内联官方头类型（由 gcc -aux-info 核对签名），无目标库依赖。
 *   4. 加载顺序：显式路径 > TRE_DYN_LIB > LD_LIBRARY_PATH > soname > Nix > 平台默认。
 *   5. 数据符号从目标库同步值；const 符号以可写副本承载。
 *   6. 未封装符号（头文件未声明 / 宏或内联 / 未导出）见下方清单。
 * ============================================================ */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
/* ---------- 官方头文件内联类型 ---------- */
typedef int regoff_t;
typedef struct {
  size_t re_nsub;
  void *value;
} regex_t;
typedef struct {
  regoff_t rm_so;
  regoff_t rm_eo;
} regmatch_t;
typedef struct {
  int cost_ins;
  int cost_del;
  int cost_subst;
  int max_cost;
  int max_ins;
  int max_del;
  int max_subst;
  int max_err;
} regaparams_t;
typedef struct {
  size_t nmatch;
  regmatch_t *pmatch;
  int cost;
  int num_ins;
  int num_del;
  int num_subst;
} regamatch_t;
typedef wchar_t tre_char_t;
typedef struct {
  int (*get_next_char)(tre_char_t *c, unsigned int *pos_add, void *context);
  void (*rewind)(size_t pos, void *context);
  int (*compare)(size_t pos1, size_t pos2, size_t len, void *context);
  void *context;
} tre_str_source;

/* ============================================================
 * 平台适配层：统一动态库 API 与默认库名
 * ============================================================ */
#ifdef _WIN32
 #include <windows.h>
 typedef HMODULE lib_handle_t;
 static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
 static void *get_sym(lib_handle_t h, const char *name) {
     return (void *)GetProcAddress(h, name);
 }
 static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *lib_error(void) { return "LoadLibrary failed"; }
 #define PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *lib_handle_t;
 static lib_handle_t load_lib(const char *path) {
     return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
 }
 static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
 static const char *lib_error(void) { return dlerror(); }
 #define PATH_SEP ':'
#endif
/* ---------- 数据符号副本 ---------- */

/* 前向声明（LOADER 在下方定义 LIB_sym） */
void *tre_sym(const char *name);

void tre_dyn_sync_globals(void) {
    void *p;
    (void)p;
}

/* ============================================================
 * 全局状态与库加载
 * ============================================================ */
static lib_handle_t tre_handle = NULL;
static int tre_init_state = 0;

#define MAX_CANDIDATES 64
#define PATH_BUF_LEN   1024

static const char *tre_defaults[] = {
"libtre.so.5", "libtre.so", "libtre.dylib", "tre.dll"
};

static int tre_do_load(const char *library_path) {
    if (tre_handle) return 0;
    static char bufs[MAX_CANDIDATES][PATH_BUF_LEN];
    const char *candidates[MAX_CANDIDATES];
    int n = 0, i;

    if (library_path && *library_path && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    const char *explicit_lib = getenv("TRE_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", explicit_lib);
        candidates[n] = bufs[n]; n++;
    }
    const char *env_path = getenv(
#ifdef _WIN32
        "PATH"
#elif defined(__APPLE__)
        "DYLD_LIBRARY_PATH"
#else
        "LD_LIBRARY_PATH"
#endif
    );
    if (env_path && *env_path) {
        char *copy = strdup(env_path);
        if (copy) {
            char *p = copy;
            while (p && *p && n < MAX_CANDIDATES - 2) {
                char *sep = strchr(p, PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
                    for (i = 0; i < (int)(sizeof(tre_defaults)/sizeof(tre_defaults[0])); i++) {
                        if (n >= MAX_CANDIDATES - 2) break;
                        snprintf(bufs[n], PATH_BUF_LEN, "%s/%s", p, tre_defaults[i]);
                        candidates[n] = bufs[n]; n++;
                    }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }
    for (i = 0; i < (int)(sizeof(tre_defaults)/sizeof(tre_defaults[0])) && n < MAX_CANDIDATES - 1; i++)
        candidates[n++] = tre_defaults[i];
    const char *home = getenv("HOME");
    if (home && *home && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s/.nix-profile/lib/%s", home, tre_defaults[0]);
        candidates[n] = bufs[n]; n++;
    }
    if (n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "/nix/var/nix/profiles/default/lib/%s", tre_defaults[0]);
        candidates[n] = bufs[n]; n++;
    }
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        tre_handle = load_lib(candidates[i]);
        if (tre_handle) break;
    }
    if (!tre_handle) {
        fprintf(stderr, "tre_dyn: cannot load library (%s)\n", lib_error());
        return -1;
    }
    if (!get_sym(tre_handle, "tre_ast_new_catenation")) {
        fprintf(stderr, "tre_dyn: loaded library does not export %s\n", "tre_ast_new_catenation");
        free_lib(tre_handle); tre_handle = NULL;
        return -1;
    }
    fprintf(stderr, "tre_dyn: loaded %d %s\n", i, candidates[i]);
    tre_dyn_sync_globals();
    return 0;
}

void *tre_sym(const char *name) { return tre_handle ? get_sym(tre_handle, name) : NULL; }

int tre_libok(void) {
    if (tre_init_state == 0)
        tre_init_state = (tre_do_load(NULL) == 0) ? 1 : -1;
    return (tre_init_state == 1 && tre_handle != NULL) ? 1 : 0;
}

int tre_dyn_init(const char *library_path) {
    if (tre_do_load(library_path) == 0) { tre_init_state = 1; return 0; }
    if (tre_init_state == 0) tre_init_state = -1;
    return -1;
}

void tre_dyn_cleanup(void) {
    /* 不卸载已成功加载的库（dlclose 不安全，且函数指针可能仍被持有） */
    if (!tre_handle) tre_init_state = 0;
    else if (tre_init_state == -1) tre_init_state = 0;
}

int tre_dyn_has_symbol(const char *name) {
    if (!tre_libok()) return 0;
    return tre_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏：首次调用 dlsym 并缓存；失败打印 stderr 并返回安全值
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!tre_libok()) return fallback;                                \
        *(void **)(&fn) = tre_sym(#name);                                 \
        if (!fn) {                                                        \
            fprintf(stderr, "tre_dyn: dlsym failed: %s\n", #name);        \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define WRAP_VOID(name, params, args)                                     \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!tre_libok()) return;                                         \
        *(void **)(&fn) = tre_sym(#name);                                 \
        if (!fn) {                                                        \
            fprintf(stderr, "tre_dyn: dlsym failed: %s\n", #name);        \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}
/* ---------- 全量函数封装 ---------- */
WRAP_RET(int, tre_config, (int a0, void * a1), (a0, a1), 0)
WRAP_RET(int, tre_have_approx, (const regex_t * a0), (a0), 0)
WRAP_RET(int, tre_have_backrefs, (const regex_t * a0), (a0), 0)
WRAP_RET(int, tre_regaexec, (const regex_t * a0, const char * a1, regamatch_t * a2, regaparams_t a3, int a4), (a0, a1, a2, a3, a4), 0)
WRAP_RET(int, tre_regaexecb, (const regex_t * a0, const char * a1, regamatch_t * a2, regaparams_t a3, int a4), (a0, a1, a2, a3, a4), 0)
WRAP_RET(int, tre_reganexec, (const regex_t * a0, const char * a1, size_t a2, regamatch_t * a3, regaparams_t a4, int a5), (a0, a1, a2, a3, a4, a5), 0)
WRAP_VOID(tre_regaparams_default, (regaparams_t * a0), (a0))
WRAP_RET(int, tre_regawexec, (const regex_t * a0, const wchar_t * a1, regamatch_t * a2, regaparams_t a3, int a4), (a0, a1, a2, a3, a4), 0)
WRAP_RET(int, tre_regawnexec, (const regex_t * a0, const wchar_t * a1, size_t a2, regamatch_t * a3, regaparams_t a4, int a5), (a0, a1, a2, a3, a4, a5), 0)
WRAP_RET(int, tre_regcomp, (regex_t * a0, const char * a1, int a2), (a0, a1, a2), 0)
WRAP_RET(int, tre_regcompb, (regex_t * a0, const char * a1, int a2), (a0, a1, a2), 0)
WRAP_RET(size_t, tre_regerror, (int a0, const regex_t * a1, char * a2, size_t a3), (a0, a1, a2, a3), 0)
WRAP_RET(int, tre_regexec, (const regex_t * a0, const char * a1, size_t a2, regmatch_t * a3, int a4), (a0, a1, a2, a3, a4), 0)
WRAP_RET(int, tre_regexecb, (const regex_t * a0, const char * a1, size_t a2, regmatch_t * a3, int a4), (a0, a1, a2, a3, a4), 0)
WRAP_VOID(tre_regfree, (regex_t * a0), (a0))
WRAP_RET(int, tre_regncomp, (regex_t * a0, const char * a1, size_t a2, int a3), (a0, a1, a2, a3), 0)
WRAP_RET(int, tre_regncompb, (regex_t * a0, const char * a1, size_t a2, int a3), (a0, a1, a2, a3), 0)
WRAP_RET(int, tre_regnexec, (const regex_t * a0, const char * a1, size_t a2, size_t a3, regmatch_t * a4, int a5), (a0, a1, a2, a3, a4, a5), 0)
WRAP_RET(int, tre_regnexecb, (const regex_t * a0, const char * a1, size_t a2, size_t a3, regmatch_t * a4, int a5), (a0, a1, a2, a3, a4, a5), 0)
WRAP_RET(int, tre_reguexec, (const regex_t * a0, const tre_str_source * a1, size_t a2, regmatch_t * a3, int a4), (a0, a1, a2, a3, a4), 0)
WRAP_RET(int, tre_regwcomp, (regex_t * a0, const wchar_t * a1, int a2), (a0, a1, a2), 0)
WRAP_RET(int, tre_regwexec, (const regex_t * a0, const wchar_t * a1, size_t a2, regmatch_t * a3, int a4), (a0, a1, a2, a3, a4), 0)
WRAP_RET(int, tre_regwncomp, (regex_t * a0, const wchar_t * a1, size_t a2, int a3), (a0, a1, a2, a3), 0)
WRAP_RET(int, tre_regwnexec, (const regex_t * a0, const wchar_t * a1, size_t a2, size_t a3, regmatch_t * a4, int a5), (a0, a1, a2, a3, a4, a5), 0)
WRAP_RET(char *, tre_version, (), (), NULL)
/* ---------- 未封装（头文件未声明或不可 dlsym，共 22 个） ---------- */
/* tre_ast_new_catenation (头未声明) */
/* tre_ast_new_iter (头未声明) */
/* tre_ast_new_literal (头未声明) */
/* tre_ast_new_node (头未声明) */
/* tre_ast_new_union (头未声明) */
/* tre_compile (头未声明) */
/* tre_fill_pmatch (头未声明) */
/* tre_free (头未声明) */
/* tre_mem_alloc_impl (头未声明) */
/* tre_mem_destroy (头未声明) */
/* tre_mem_new_impl (头未声明) */
/* tre_parse (头未声明) */
/* tre_stack_destroy (头未声明) */
/* tre_stack_new (头未声明) */
/* tre_stack_num_objects (头未声明) */
/* tre_stack_pop_int (头未声明) */
/* tre_stack_pop_voidptr (头未声明) */
/* tre_stack_push_int (头未声明) */
/* tre_stack_push_voidptr (头未声明) */
/* tre_tnfa_run_approx (头未声明) */
/* tre_tnfa_run_backtrack (头未声明) */
/* tre_tnfa_run_parallel (头未声明) */
