/* ============================================================
 * pcre2_dyn.c — PCRE2 动态加载封装层（公开头文件 API 全量封装）
 *
 * 统计：导出函数 241 / 封装 241 / 跳过 0 / 数据符号 0 / 内联类型 63
 * 设计要点（与 rtdb/libxml2_dyn.c 一致）：
 *   1. 单文件、无 .h、无 wrapper 层；链接期不链接目标库（只 -ldl）。
 *   2. 8/16/32/POSIX 四个库各自懒加载；每个函数首次调用才 dlsym。
 *   3. 编译期内联官方头类型（由 gcc -aux-info 核对签名），无目标库依赖。
 *   4. 加载顺序：显式路径 > *_DYN_LIB > LD_LIBRARY_PATH > soname > Nix > 平台默认。
 *   5. 未封装符号（头文件未声明 / 宏或内联 / 未导出）见下方清单。
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
typedef uint8_t PCRE2_UCHAR8;
typedef uint16_t PCRE2_UCHAR16;
typedef uint32_t PCRE2_UCHAR32;
typedef const PCRE2_UCHAR8 *PCRE2_SPTR8;
typedef const PCRE2_UCHAR16 *PCRE2_SPTR16;
typedef const PCRE2_UCHAR32 *PCRE2_SPTR32;
struct pcre2_real_general_context_8;
typedef struct pcre2_real_general_context_8 pcre2_general_context_8;
struct pcre2_real_compile_context_8;
typedef struct pcre2_real_compile_context_8 pcre2_compile_context_8;
struct pcre2_real_match_context_8;
typedef struct pcre2_real_match_context_8 pcre2_match_context_8;
struct pcre2_real_convert_context_8;
typedef struct pcre2_real_convert_context_8 pcre2_convert_context_8;
struct pcre2_real_code_8;
typedef struct pcre2_real_code_8 pcre2_code_8;
struct pcre2_real_match_data_8;
typedef struct pcre2_real_match_data_8 pcre2_match_data_8;
struct pcre2_real_jit_stack_8;
typedef struct pcre2_real_jit_stack_8 pcre2_jit_stack_8;
typedef pcre2_jit_stack_8 *(*pcre2_jit_callback_8)(void *);
typedef struct pcre2_callout_block_8 { uint32_t version; uint32_t callout_number; uint32_t capture_top; uint32_t capture_last; size_t *offset_vector; PCRE2_SPTR8 mark; PCRE2_SPTR8 subject; size_t subject_length; size_t start_match; size_t current_position; size_t pattern_position; size_t next_item_length; size_t callout_string_offset; size_t callout_string_length; PCRE2_SPTR8 callout_string; uint32_t callout_flags; } pcre2_callout_block_8;
typedef struct pcre2_callout_enumerate_block_8 { uint32_t version; size_t pattern_position; size_t next_item_length; uint32_t callout_number; size_t callout_string_offset; size_t callout_string_length; PCRE2_SPTR8 callout_string; } pcre2_callout_enumerate_block_8;
typedef struct pcre2_substitute_callout_block_8 { uint32_t version; PCRE2_SPTR8 input; PCRE2_SPTR8 output; size_t output_offsets[2]; size_t *ovector; uint32_t oveccount; uint32_t subscount; } pcre2_substitute_callout_block_8;
struct pcre2_real_general_context_16;
typedef struct pcre2_real_general_context_16 pcre2_general_context_16;
struct pcre2_real_compile_context_16;
typedef struct pcre2_real_compile_context_16 pcre2_compile_context_16;
struct pcre2_real_match_context_16;
typedef struct pcre2_real_match_context_16 pcre2_match_context_16;
struct pcre2_real_convert_context_16;
typedef struct pcre2_real_convert_context_16 pcre2_convert_context_16;
struct pcre2_real_code_16;
typedef struct pcre2_real_code_16 pcre2_code_16;
struct pcre2_real_match_data_16;
typedef struct pcre2_real_match_data_16 pcre2_match_data_16;
struct pcre2_real_jit_stack_16;
typedef struct pcre2_real_jit_stack_16 pcre2_jit_stack_16;
typedef pcre2_jit_stack_16 *(*pcre2_jit_callback_16)(void *);
typedef struct pcre2_callout_block_16 { uint32_t version; uint32_t callout_number; uint32_t capture_top; uint32_t capture_last; size_t *offset_vector; PCRE2_SPTR16 mark; PCRE2_SPTR16 subject; size_t subject_length; size_t start_match; size_t current_position; size_t pattern_position; size_t next_item_length; size_t callout_string_offset; size_t callout_string_length; PCRE2_SPTR16 callout_string; uint32_t callout_flags; } pcre2_callout_block_16;
typedef struct pcre2_callout_enumerate_block_16 { uint32_t version; size_t pattern_position; size_t next_item_length; uint32_t callout_number; size_t callout_string_offset; size_t callout_string_length; PCRE2_SPTR16 callout_string; } pcre2_callout_enumerate_block_16;
typedef struct pcre2_substitute_callout_block_16 { uint32_t version; PCRE2_SPTR16 input; PCRE2_SPTR16 output; size_t output_offsets[2]; size_t *ovector; uint32_t oveccount; uint32_t subscount; } pcre2_substitute_callout_block_16;
struct pcre2_real_general_context_32;
typedef struct pcre2_real_general_context_32 pcre2_general_context_32;
struct pcre2_real_compile_context_32;
typedef struct pcre2_real_compile_context_32 pcre2_compile_context_32;
struct pcre2_real_match_context_32;
typedef struct pcre2_real_match_context_32 pcre2_match_context_32;
struct pcre2_real_convert_context_32;
typedef struct pcre2_real_convert_context_32 pcre2_convert_context_32;
struct pcre2_real_code_32;
typedef struct pcre2_real_code_32 pcre2_code_32;
struct pcre2_real_match_data_32;
typedef struct pcre2_real_match_data_32 pcre2_match_data_32;
struct pcre2_real_jit_stack_32;
typedef struct pcre2_real_jit_stack_32 pcre2_jit_stack_32;
typedef pcre2_jit_stack_32 *(*pcre2_jit_callback_32)(void *);
typedef struct pcre2_callout_block_32 { uint32_t version; uint32_t callout_number; uint32_t capture_top; uint32_t capture_last; size_t *offset_vector; PCRE2_SPTR32 mark; PCRE2_SPTR32 subject; size_t subject_length; size_t start_match; size_t current_position; size_t pattern_position; size_t next_item_length; size_t callout_string_offset; size_t callout_string_length; PCRE2_SPTR32 callout_string; uint32_t callout_flags; } pcre2_callout_block_32;
typedef struct pcre2_callout_enumerate_block_32 { uint32_t version; size_t pattern_position; size_t next_item_length; uint32_t callout_number; size_t callout_string_offset; size_t callout_string_length; PCRE2_SPTR32 callout_string; } pcre2_callout_enumerate_block_32;
typedef struct pcre2_substitute_callout_block_32 { uint32_t version; PCRE2_SPTR32 input; PCRE2_SPTR32 output; size_t output_offsets[2]; size_t *ovector; uint32_t oveccount; uint32_t subscount; } pcre2_substitute_callout_block_32;
typedef struct {
  void *re_pcre2_code;
  void *re_match_data;
  const char *re_endp;
  size_t re_nsub;
  size_t re_erroffset;
  int re_cflags;
} regex_t;
typedef int regoff_t;
typedef struct {
  regoff_t rm_so;
  regoff_t rm_eo;
} regmatch_t;

/* ============================================================
 * 平台适配层：统一动态库 API 与默认库名
 * ============================================================ */
#ifdef _WIN32
 #include <windows.h>
 typedef HMODULE pcre2_lib_handle_t;
 static pcre2_lib_handle_t pcre2_load_lib(const char *path) { return LoadLibraryA(path); }
 static void *pcre2_get_sym(pcre2_lib_handle_t h, const char *name) {
     return (void *)GetProcAddress(h, name);
 }
 static void pcre2_free_lib(pcre2_lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *pcre2_lib_error(void) { return "LoadLibrary failed"; }
 #define PCRE2_PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *pcre2_lib_handle_t;
 static pcre2_lib_handle_t pcre2_load_lib(const char *path) {
     return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
 }
 static void *pcre2_get_sym(pcre2_lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void pcre2_free_lib(pcre2_lib_handle_t h) { if (h) dlclose(h); }
 static const char *pcre2_lib_error(void) { return dlerror(); }
 #define PCRE2_PATH_SEP ':'
#endif
/* ============================================================
 * 多动态库加载器
 * ============================================================ */
#define PCRE2_MAX_CANDIDATES 64
#define PCRE2_PATH_BUF_LEN   1024

static pcre2_lib_handle_t pcre2_handle_g8 = NULL;
static int pcre2_init_state_g8 = 0;

static const char *pcre2_defaults_g8[] = {
"libpcre2-8.so.0", "libpcre2-8.so"
};

static int pcre2_do_load_g8(const char *library_path) {
    if (pcre2_handle_g8) return 0;
    static char bufs[PCRE2_MAX_CANDIDATES][PCRE2_PATH_BUF_LEN];
    const char *candidates[PCRE2_MAX_CANDIDATES];
    int n = 0, i;

    if (library_path && *library_path && n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    const char *explicit_lib = getenv("PCRE2_8_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s", explicit_lib);
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
            while (p && *p && n < PCRE2_MAX_CANDIDATES - 2) {
                char *sep = strchr(p, PCRE2_PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
                    for (i = 0; i < (int)(sizeof(pcre2_defaults_g8)/sizeof(pcre2_defaults_g8[0])); i++) {
                        if (n >= PCRE2_MAX_CANDIDATES - 2) break;
                        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s/%s", p, pcre2_defaults_g8[i]);
                        candidates[n] = bufs[n]; n++;
                    }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }
    for (i = 0; i < (int)(sizeof(pcre2_defaults_g8)/sizeof(pcre2_defaults_g8[0])) && n < PCRE2_MAX_CANDIDATES - 1; i++)
        candidates[n++] = pcre2_defaults_g8[i];
    const char *home = getenv("HOME");
    if (home && *home && n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s/.nix-profile/lib/%s", home, pcre2_defaults_g8[0]);
        candidates[n] = bufs[n]; n++;
    }
    if (n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "/nix/var/nix/profiles/default/lib/%s", pcre2_defaults_g8[0]);
        candidates[n] = bufs[n]; n++;
    }
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        pcre2_handle_g8 = pcre2_load_lib(candidates[i]);
        if (pcre2_handle_g8) break;
    }
    if (!pcre2_handle_g8) {
        fprintf(stderr, "pcre2_dyn: cannot load %s library (%s)\n", "g8", pcre2_lib_error());
        return -1;
    }
    if (!pcre2_get_sym(pcre2_handle_g8, "pcre2_callout_enumerate_8")) {
        fprintf(stderr, "pcre2_dyn: %s library does not export %s\n", "g8", "pcre2_callout_enumerate_8");
        pcre2_free_lib(pcre2_handle_g8); pcre2_handle_g8 = NULL;
        return -1;
    }
    fprintf(stderr, "pcre2_dyn: loaded %s %d %s\n", "g8", i, candidates[i]);
    return 0;
}

void *pcre2_sym_g8(const char *name) {
    return pcre2_handle_g8 ? pcre2_get_sym(pcre2_handle_g8, name) : NULL;
}

int pcre2_libok_g8(void) {
    if (pcre2_init_state_g8 == 0)
        pcre2_init_state_g8 = (pcre2_do_load_g8(NULL) == 0) ? 1 : -1;
    return (pcre2_init_state_g8 == 1 && pcre2_handle_g8 != NULL) ? 1 : 0;
}

static pcre2_lib_handle_t pcre2_handle_g16 = NULL;
static int pcre2_init_state_g16 = 0;

static const char *pcre2_defaults_g16[] = {
"libpcre2-16.so.0", "libpcre2-16.so"
};

static int pcre2_do_load_g16(const char *library_path) {
    if (pcre2_handle_g16) return 0;
    static char bufs[PCRE2_MAX_CANDIDATES][PCRE2_PATH_BUF_LEN];
    const char *candidates[PCRE2_MAX_CANDIDATES];
    int n = 0, i;

    if (library_path && *library_path && n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    const char *explicit_lib = getenv("PCRE2_16_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s", explicit_lib);
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
            while (p && *p && n < PCRE2_MAX_CANDIDATES - 2) {
                char *sep = strchr(p, PCRE2_PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
                    for (i = 0; i < (int)(sizeof(pcre2_defaults_g16)/sizeof(pcre2_defaults_g16[0])); i++) {
                        if (n >= PCRE2_MAX_CANDIDATES - 2) break;
                        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s/%s", p, pcre2_defaults_g16[i]);
                        candidates[n] = bufs[n]; n++;
                    }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }
    for (i = 0; i < (int)(sizeof(pcre2_defaults_g16)/sizeof(pcre2_defaults_g16[0])) && n < PCRE2_MAX_CANDIDATES - 1; i++)
        candidates[n++] = pcre2_defaults_g16[i];
    const char *home = getenv("HOME");
    if (home && *home && n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s/.nix-profile/lib/%s", home, pcre2_defaults_g16[0]);
        candidates[n] = bufs[n]; n++;
    }
    if (n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "/nix/var/nix/profiles/default/lib/%s", pcre2_defaults_g16[0]);
        candidates[n] = bufs[n]; n++;
    }
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        pcre2_handle_g16 = pcre2_load_lib(candidates[i]);
        if (pcre2_handle_g16) break;
    }
    if (!pcre2_handle_g16) {
        fprintf(stderr, "pcre2_dyn: cannot load %s library (%s)\n", "g16", pcre2_lib_error());
        return -1;
    }
    if (!pcre2_get_sym(pcre2_handle_g16, "pcre2_callout_enumerate_16")) {
        fprintf(stderr, "pcre2_dyn: %s library does not export %s\n", "g16", "pcre2_callout_enumerate_16");
        pcre2_free_lib(pcre2_handle_g16); pcre2_handle_g16 = NULL;
        return -1;
    }
    fprintf(stderr, "pcre2_dyn: loaded %s %d %s\n", "g16", i, candidates[i]);
    return 0;
}

void *pcre2_sym_g16(const char *name) {
    return pcre2_handle_g16 ? pcre2_get_sym(pcre2_handle_g16, name) : NULL;
}

int pcre2_libok_g16(void) {
    if (pcre2_init_state_g16 == 0)
        pcre2_init_state_g16 = (pcre2_do_load_g16(NULL) == 0) ? 1 : -1;
    return (pcre2_init_state_g16 == 1 && pcre2_handle_g16 != NULL) ? 1 : 0;
}

static pcre2_lib_handle_t pcre2_handle_g32 = NULL;
static int pcre2_init_state_g32 = 0;

static const char *pcre2_defaults_g32[] = {
"libpcre2-32.so.0", "libpcre2-32.so"
};

static int pcre2_do_load_g32(const char *library_path) {
    if (pcre2_handle_g32) return 0;
    static char bufs[PCRE2_MAX_CANDIDATES][PCRE2_PATH_BUF_LEN];
    const char *candidates[PCRE2_MAX_CANDIDATES];
    int n = 0, i;

    if (library_path && *library_path && n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    const char *explicit_lib = getenv("PCRE2_32_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s", explicit_lib);
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
            while (p && *p && n < PCRE2_MAX_CANDIDATES - 2) {
                char *sep = strchr(p, PCRE2_PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
                    for (i = 0; i < (int)(sizeof(pcre2_defaults_g32)/sizeof(pcre2_defaults_g32[0])); i++) {
                        if (n >= PCRE2_MAX_CANDIDATES - 2) break;
                        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s/%s", p, pcre2_defaults_g32[i]);
                        candidates[n] = bufs[n]; n++;
                    }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }
    for (i = 0; i < (int)(sizeof(pcre2_defaults_g32)/sizeof(pcre2_defaults_g32[0])) && n < PCRE2_MAX_CANDIDATES - 1; i++)
        candidates[n++] = pcre2_defaults_g32[i];
    const char *home = getenv("HOME");
    if (home && *home && n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s/.nix-profile/lib/%s", home, pcre2_defaults_g32[0]);
        candidates[n] = bufs[n]; n++;
    }
    if (n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "/nix/var/nix/profiles/default/lib/%s", pcre2_defaults_g32[0]);
        candidates[n] = bufs[n]; n++;
    }
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        pcre2_handle_g32 = pcre2_load_lib(candidates[i]);
        if (pcre2_handle_g32) break;
    }
    if (!pcre2_handle_g32) {
        fprintf(stderr, "pcre2_dyn: cannot load %s library (%s)\n", "g32", pcre2_lib_error());
        return -1;
    }
    if (!pcre2_get_sym(pcre2_handle_g32, "pcre2_callout_enumerate_32")) {
        fprintf(stderr, "pcre2_dyn: %s library does not export %s\n", "g32", "pcre2_callout_enumerate_32");
        pcre2_free_lib(pcre2_handle_g32); pcre2_handle_g32 = NULL;
        return -1;
    }
    fprintf(stderr, "pcre2_dyn: loaded %s %d %s\n", "g32", i, candidates[i]);
    return 0;
}

void *pcre2_sym_g32(const char *name) {
    return pcre2_handle_g32 ? pcre2_get_sym(pcre2_handle_g32, name) : NULL;
}

int pcre2_libok_g32(void) {
    if (pcre2_init_state_g32 == 0)
        pcre2_init_state_g32 = (pcre2_do_load_g32(NULL) == 0) ? 1 : -1;
    return (pcre2_init_state_g32 == 1 && pcre2_handle_g32 != NULL) ? 1 : 0;
}

static pcre2_lib_handle_t pcre2_handle_posix = NULL;
static int pcre2_init_state_posix = 0;

static const char *pcre2_defaults_posix[] = {
"libpcre2-posix.so.3", "libpcre2-posix.so"
};

static int pcre2_do_load_posix(const char *library_path) {
    if (pcre2_handle_posix) return 0;
    static char bufs[PCRE2_MAX_CANDIDATES][PCRE2_PATH_BUF_LEN];
    const char *candidates[PCRE2_MAX_CANDIDATES];
    int n = 0, i;

    if (library_path && *library_path && n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    const char *explicit_lib = getenv("PCRE2_POSIX_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s", explicit_lib);
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
            while (p && *p && n < PCRE2_MAX_CANDIDATES - 2) {
                char *sep = strchr(p, PCRE2_PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
                    for (i = 0; i < (int)(sizeof(pcre2_defaults_posix)/sizeof(pcre2_defaults_posix[0])); i++) {
                        if (n >= PCRE2_MAX_CANDIDATES - 2) break;
                        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s/%s", p, pcre2_defaults_posix[i]);
                        candidates[n] = bufs[n]; n++;
                    }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }
    for (i = 0; i < (int)(sizeof(pcre2_defaults_posix)/sizeof(pcre2_defaults_posix[0])) && n < PCRE2_MAX_CANDIDATES - 1; i++)
        candidates[n++] = pcre2_defaults_posix[i];
    const char *home = getenv("HOME");
    if (home && *home && n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "%s/.nix-profile/lib/%s", home, pcre2_defaults_posix[0]);
        candidates[n] = bufs[n]; n++;
    }
    if (n < PCRE2_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PCRE2_PATH_BUF_LEN, "/nix/var/nix/profiles/default/lib/%s", pcre2_defaults_posix[0]);
        candidates[n] = bufs[n]; n++;
    }
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        pcre2_handle_posix = pcre2_load_lib(candidates[i]);
        if (pcre2_handle_posix) break;
    }
    if (!pcre2_handle_posix) {
        fprintf(stderr, "pcre2_dyn: cannot load %s library (%s)\n", "posix", pcre2_lib_error());
        return -1;
    }
    if (!pcre2_get_sym(pcre2_handle_posix, "pcre2_regcomp")) {
        fprintf(stderr, "pcre2_dyn: %s library does not export %s\n", "posix", "pcre2_regcomp");
        pcre2_free_lib(pcre2_handle_posix); pcre2_handle_posix = NULL;
        return -1;
    }
    fprintf(stderr, "pcre2_dyn: loaded %s %d %s\n", "posix", i, candidates[i]);
    return 0;
}

void *pcre2_sym_posix(const char *name) {
    return pcre2_handle_posix ? pcre2_get_sym(pcre2_handle_posix, name) : NULL;
}

int pcre2_libok_posix(void) {
    if (pcre2_init_state_posix == 0)
        pcre2_init_state_posix = (pcre2_do_load_posix(NULL) == 0) ? 1 : -1;
    return (pcre2_init_state_posix == 1 && pcre2_handle_posix != NULL) ? 1 : 0;
}

int pcre2_libok(void) {
    return (pcre2_libok_g8() && pcre2_libok_g16() &&
            pcre2_libok_g32() && pcre2_libok_posix()) ? 1 : 0;
}

int pcre2_dyn_init(const char *library_path) {
    int rc[4];
    rc[0] = pcre2_do_load_g8(library_path);
    pcre2_init_state_g8 = (rc[0] == 0) ? 1 : -1;
    rc[1] = pcre2_do_load_g16(NULL);
    pcre2_init_state_g16 = (rc[1] == 0) ? 1 : -1;
    rc[2] = pcre2_do_load_g32(NULL);
    pcre2_init_state_g32 = (rc[2] == 0) ? 1 : -1;
    rc[3] = pcre2_do_load_posix(NULL);
    pcre2_init_state_posix = (rc[3] == 0) ? 1 : -1;
    return (rc[0] == 0 && rc[1] == 0 && rc[2] == 0 && rc[3] == 0) ? 0 : -1;
}

void pcre2_dyn_cleanup(void) {
    if (!pcre2_handle_g8) pcre2_init_state_g8 = 0;
    else if (pcre2_init_state_g8 == -1) pcre2_init_state_g8 = 0;
    if (!pcre2_handle_g16) pcre2_init_state_g16 = 0;
    else if (pcre2_init_state_g16 == -1) pcre2_init_state_g16 = 0;
    if (!pcre2_handle_g32) pcre2_init_state_g32 = 0;
    else if (pcre2_init_state_g32 == -1) pcre2_init_state_g32 = 0;
    if (!pcre2_handle_posix) pcre2_init_state_posix = 0;
    else if (pcre2_init_state_posix == -1) pcre2_init_state_posix = 0;
}

int pcre2_dyn_has_symbol(const char *name) {
    if (!pcre2_libok_g8()) return 0;
    return (pcre2_sym_g8(name) != NULL || pcre2_sym_g16(name) != NULL ||
            pcre2_sym_g32(name) != NULL || pcre2_sym_posix(name) != NULL);
}
/* ============================================================
 * WRAP 宏：首次调用 dlsym 并缓存；失败打印 stderr 并返回安全值
 * ============================================================ */

#define PCRE2_WRAP_RET_G8(ret, name, params, args, fallback)                 \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!pcre2_libok_g8()) return fallback;                           \
        *(void **)(&fn) = pcre2_sym_g8(#name);                            \
        if (!fn) {                                                        \
            fprintf(stderr, "pcre2_dyn: dlsym failed: %s\n", #name);      \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define PCRE2_WRAP_VOID_G8(name, params, args)                              \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!pcre2_libok_g8()) return;                                    \
        *(void **)(&fn) = pcre2_sym_g8(#name);                            \
        if (!fn) {                                                        \
            fprintf(stderr, "pcre2_dyn: dlsym failed: %s\n", #name);      \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}

#define PCRE2_WRAP_RET_G16(ret, name, params, args, fallback)                 \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!pcre2_libok_g16()) return fallback;                           \
        *(void **)(&fn) = pcre2_sym_g16(#name);                            \
        if (!fn) {                                                        \
            fprintf(stderr, "pcre2_dyn: dlsym failed: %s\n", #name);      \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define PCRE2_WRAP_VOID_G16(name, params, args)                              \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!pcre2_libok_g16()) return;                                    \
        *(void **)(&fn) = pcre2_sym_g16(#name);                            \
        if (!fn) {                                                        \
            fprintf(stderr, "pcre2_dyn: dlsym failed: %s\n", #name);      \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}

#define PCRE2_WRAP_RET_G32(ret, name, params, args, fallback)                 \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!pcre2_libok_g32()) return fallback;                           \
        *(void **)(&fn) = pcre2_sym_g32(#name);                            \
        if (!fn) {                                                        \
            fprintf(stderr, "pcre2_dyn: dlsym failed: %s\n", #name);      \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define PCRE2_WRAP_VOID_G32(name, params, args)                              \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!pcre2_libok_g32()) return;                                    \
        *(void **)(&fn) = pcre2_sym_g32(#name);                            \
        if (!fn) {                                                        \
            fprintf(stderr, "pcre2_dyn: dlsym failed: %s\n", #name);      \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}

#define PCRE2_WRAP_RET_POSIX(ret, name, params, args, fallback)                 \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!pcre2_libok_posix()) return fallback;                           \
        *(void **)(&fn) = pcre2_sym_posix(#name);                            \
        if (!fn) {                                                        \
            fprintf(stderr, "pcre2_dyn: dlsym failed: %s\n", #name);      \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define PCRE2_WRAP_VOID_POSIX(name, params, args)                              \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!pcre2_libok_posix()) return;                                    \
        *(void **)(&fn) = pcre2_sym_posix(#name);                            \
        if (!fn) {                                                        \
            fprintf(stderr, "pcre2_dyn: dlsym failed: %s\n", #name);      \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}
/* ---------- 全量函数封装：8 位 ---------- */
PCRE2_WRAP_RET_G8(int, pcre2_callout_enumerate_8, (const pcre2_code_8 * a0, int (*a1 ) (pcre2_callout_enumerate_block_8 *, void *), void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G8(pcre2_code_8 *, pcre2_code_copy_8, (const pcre2_code_8 * a0), (a0), NULL)
PCRE2_WRAP_RET_G8(pcre2_code_8 *, pcre2_code_copy_with_tables_8, (const pcre2_code_8 * a0), (a0), NULL)
PCRE2_WRAP_VOID_G8(pcre2_code_free_8, (pcre2_code_8 * a0), (a0))
PCRE2_WRAP_RET_G8(pcre2_code_8 *, pcre2_compile_8, (PCRE2_SPTR8 a0, size_t a1, uint32_t a2, int * a3, size_t * a4, pcre2_compile_context_8 * a5), (a0, a1, a2, a3, a4, a5), NULL)
PCRE2_WRAP_RET_G8(pcre2_compile_context_8 *, pcre2_compile_context_copy_8, (pcre2_compile_context_8 * a0), (a0), NULL)
PCRE2_WRAP_RET_G8(pcre2_compile_context_8 *, pcre2_compile_context_create_8, (pcre2_general_context_8 * a0), (a0), NULL)
PCRE2_WRAP_VOID_G8(pcre2_compile_context_free_8, (pcre2_compile_context_8 * a0), (a0))
PCRE2_WRAP_RET_G8(int, pcre2_config_8, (uint32_t a0, void * a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(pcre2_convert_context_8 *, pcre2_convert_context_copy_8, (pcre2_convert_context_8 * a0), (a0), NULL)
PCRE2_WRAP_RET_G8(pcre2_convert_context_8 *, pcre2_convert_context_create_8, (pcre2_general_context_8 * a0), (a0), NULL)
PCRE2_WRAP_VOID_G8(pcre2_convert_context_free_8, (pcre2_convert_context_8 * a0), (a0))
PCRE2_WRAP_VOID_G8(pcre2_converted_pattern_free_8, (PCRE2_UCHAR8 * a0), (a0))
PCRE2_WRAP_RET_G8(int, pcre2_dfa_match_8, (const pcre2_code_8 * a0, PCRE2_SPTR8 a1, size_t a2, size_t a3, uint32_t a4, pcre2_match_data_8 * a5, pcre2_match_context_8 * a6, int * a7, size_t a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
PCRE2_WRAP_RET_G8(pcre2_general_context_8 *, pcre2_general_context_copy_8, (pcre2_general_context_8 * a0), (a0), NULL)
PCRE2_WRAP_RET_G8(pcre2_general_context_8 *, pcre2_general_context_create_8, (void *(*a0 ) (size_t, void *), void (*a1 ) (void *, void *), void * a2), (a0, a1, a2), NULL)
PCRE2_WRAP_VOID_G8(pcre2_general_context_free_8, (pcre2_general_context_8 * a0), (a0))
PCRE2_WRAP_RET_G8(int, pcre2_get_error_message_8, (int a0, PCRE2_UCHAR8 * a1, size_t a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G8(PCRE2_SPTR8, pcre2_get_mark_8, (pcre2_match_data_8 * a0), (a0), ((PCRE2_SPTR8)0))
PCRE2_WRAP_RET_G8(size_t, pcre2_get_match_data_heapframes_size_8, (pcre2_match_data_8 * a0), (a0), 0)
PCRE2_WRAP_RET_G8(size_t, pcre2_get_match_data_size_8, (pcre2_match_data_8 * a0), (a0), 0)
PCRE2_WRAP_RET_G8(uint32_t, pcre2_get_ovector_count_8, (pcre2_match_data_8 * a0), (a0), 0)
PCRE2_WRAP_RET_G8(size_t *, pcre2_get_ovector_pointer_8, (pcre2_match_data_8 * a0), (a0), NULL)
PCRE2_WRAP_RET_G8(size_t, pcre2_get_startchar_8, (pcre2_match_data_8 * a0), (a0), 0)
PCRE2_WRAP_RET_G8(int, pcre2_jit_compile_8, (pcre2_code_8 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_VOID_G8(pcre2_jit_free_unused_memory_8, (pcre2_general_context_8 * a0), (a0))
PCRE2_WRAP_RET_G8(int, pcre2_jit_match_8, (const pcre2_code_8 * a0, PCRE2_SPTR8 a1, size_t a2, size_t a3, uint32_t a4, pcre2_match_data_8 * a5, pcre2_match_context_8 * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
PCRE2_WRAP_VOID_G8(pcre2_jit_stack_assign_8, (pcre2_match_context_8 * a0, pcre2_jit_callback_8 a1, void * a2), (a0, a1, a2))
PCRE2_WRAP_RET_G8(pcre2_jit_stack_8 *, pcre2_jit_stack_create_8, (size_t a0, size_t a1, pcre2_general_context_8 * a2), (a0, a1, a2), NULL)
PCRE2_WRAP_VOID_G8(pcre2_jit_stack_free_8, (pcre2_jit_stack_8 * a0), (a0))
PCRE2_WRAP_RET_G8(const uint8_t *, pcre2_maketables_8, (pcre2_general_context_8 * a0), (a0), NULL)
PCRE2_WRAP_VOID_G8(pcre2_maketables_free_8, (pcre2_general_context_8 * a0, const uint8_t * a1), (a0, a1))
PCRE2_WRAP_RET_G8(int, pcre2_match_8, (const pcre2_code_8 * a0, PCRE2_SPTR8 a1, size_t a2, size_t a3, uint32_t a4, pcre2_match_data_8 * a5, pcre2_match_context_8 * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
PCRE2_WRAP_RET_G8(pcre2_match_context_8 *, pcre2_match_context_copy_8, (pcre2_match_context_8 * a0), (a0), NULL)
PCRE2_WRAP_RET_G8(pcre2_match_context_8 *, pcre2_match_context_create_8, (pcre2_general_context_8 * a0), (a0), NULL)
PCRE2_WRAP_VOID_G8(pcre2_match_context_free_8, (pcre2_match_context_8 * a0), (a0))
PCRE2_WRAP_RET_G8(pcre2_match_data_8 *, pcre2_match_data_create_8, (uint32_t a0, pcre2_general_context_8 * a1), (a0, a1), NULL)
PCRE2_WRAP_RET_G8(pcre2_match_data_8 *, pcre2_match_data_create_from_pattern_8, (const pcre2_code_8 * a0, pcre2_general_context_8 * a1), (a0, a1), NULL)
PCRE2_WRAP_VOID_G8(pcre2_match_data_free_8, (pcre2_match_data_8 * a0), (a0))
PCRE2_WRAP_RET_G8(int, pcre2_next_match_8, (pcre2_match_data_8 * a0, size_t * a1, uint32_t * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G8(int, pcre2_pattern_convert_8, (PCRE2_SPTR8 a0, size_t a1, uint32_t a2, PCRE2_UCHAR8 ** a3, size_t * a4, pcre2_convert_context_8 * a5), (a0, a1, a2, a3, a4, a5), -1)
PCRE2_WRAP_RET_G8(int, pcre2_pattern_info_8, (const pcre2_code_8 * a0, uint32_t a1, void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G8(int32_t, pcre2_serialize_decode_8, (pcre2_code_8 ** a0, int32_t a1, const uint8_t * a2, pcre2_general_context_8 * a3), (a0, a1, a2, a3), ((int32_t)0))
PCRE2_WRAP_RET_G8(int32_t, pcre2_serialize_encode_8, (const pcre2_code_8 ** a0, int32_t a1, uint8_t ** a2, size_t * a3, pcre2_general_context_8 * a4), (a0, a1, a2, a3, a4), ((int32_t)0))
PCRE2_WRAP_VOID_G8(pcre2_serialize_free_8, (uint8_t * a0), (a0))
PCRE2_WRAP_RET_G8(int32_t, pcre2_serialize_get_number_of_codes_8, (const uint8_t * a0), (a0), ((int32_t)0))
PCRE2_WRAP_RET_G8(int, pcre2_set_bsr_8, (pcre2_compile_context_8 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_callout_8, (pcre2_match_context_8 * a0, int (*a1 ) (pcre2_callout_block_8 *, void *), void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_character_tables_8, (pcre2_compile_context_8 * a0, const uint8_t * a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_compile_extra_options_8, (pcre2_compile_context_8 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_compile_recursion_guard_8, (pcre2_compile_context_8 * a0, int (*a1 ) (uint32_t, void *), void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_depth_limit_8, (pcre2_match_context_8 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_glob_escape_8, (pcre2_convert_context_8 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_glob_separator_8, (pcre2_convert_context_8 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_heap_limit_8, (pcre2_match_context_8 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_match_limit_8, (pcre2_match_context_8 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_max_pattern_compiled_length_8, (pcre2_compile_context_8 * a0, size_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_max_pattern_length_8, (pcre2_compile_context_8 * a0, size_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_max_varlookbehind_8, (pcre2_compile_context_8 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_newline_8, (pcre2_compile_context_8 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_offset_limit_8, (pcre2_match_context_8 * a0, size_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_optimize_8, (pcre2_compile_context_8 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_parens_nest_limit_8, (pcre2_compile_context_8 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_recursion_limit_8, (pcre2_match_context_8 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_recursion_memory_management_8, (pcre2_match_context_8 * a0, void *(*a1 ) (size_t, void *), void (*a2 ) (void *, void *), void * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_substitute_callout_8, (pcre2_match_context_8 * a0, int (*a1 ) (pcre2_substitute_callout_block_8 *, void *), void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G8(int, pcre2_set_substitute_case_callout_8, (pcre2_match_context_8 * a0, size_t (*a1 ) (PCRE2_SPTR8, size_t, PCRE2_UCHAR8 *, size_t, int, void *), void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G8(int, pcre2_substitute_8, (const pcre2_code_8 * a0, PCRE2_SPTR8 a1, size_t a2, size_t a3, uint32_t a4, pcre2_match_data_8 * a5, pcre2_match_context_8 * a6, PCRE2_SPTR8 a7, size_t a8, PCRE2_UCHAR8 * a9, size_t * a10), (a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10), -1)
PCRE2_WRAP_RET_G8(int, pcre2_substring_copy_byname_8, (pcre2_match_data_8 * a0, PCRE2_SPTR8 a1, PCRE2_UCHAR8 * a2, size_t * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_RET_G8(int, pcre2_substring_copy_bynumber_8, (pcre2_match_data_8 * a0, uint32_t a1, PCRE2_UCHAR8 * a2, size_t * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_VOID_G8(pcre2_substring_free_8, (PCRE2_UCHAR8 * a0), (a0))
PCRE2_WRAP_RET_G8(int, pcre2_substring_get_byname_8, (pcre2_match_data_8 * a0, PCRE2_SPTR8 a1, PCRE2_UCHAR8 ** a2, size_t * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_RET_G8(int, pcre2_substring_get_bynumber_8, (pcre2_match_data_8 * a0, uint32_t a1, PCRE2_UCHAR8 ** a2, size_t * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_RET_G8(int, pcre2_substring_length_byname_8, (pcre2_match_data_8 * a0, PCRE2_SPTR8 a1, size_t * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G8(int, pcre2_substring_length_bynumber_8, (pcre2_match_data_8 * a0, uint32_t a1, size_t * a2), (a0, a1, a2), -1)
PCRE2_WRAP_VOID_G8(pcre2_substring_list_free_8, (PCRE2_UCHAR8 ** a0), (a0))
PCRE2_WRAP_RET_G8(int, pcre2_substring_list_get_8, (pcre2_match_data_8 * a0, PCRE2_UCHAR8 *** a1, size_t ** a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G8(int, pcre2_substring_nametable_scan_8, (const pcre2_code_8 * a0, PCRE2_SPTR8 a1, PCRE2_SPTR8 * a2, PCRE2_SPTR8 * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_RET_G8(int, pcre2_substring_number_from_name_8, (const pcre2_code_8 * a0, PCRE2_SPTR8 a1), (a0, a1), -1)
/* ---------- 全量函数封装：16 位 ---------- */
PCRE2_WRAP_RET_G16(int, pcre2_callout_enumerate_16, (const pcre2_code_16 * a0, int (*a1 ) (pcre2_callout_enumerate_block_16 *, void *), void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G16(pcre2_code_16 *, pcre2_code_copy_16, (const pcre2_code_16 * a0), (a0), NULL)
PCRE2_WRAP_RET_G16(pcre2_code_16 *, pcre2_code_copy_with_tables_16, (const pcre2_code_16 * a0), (a0), NULL)
PCRE2_WRAP_VOID_G16(pcre2_code_free_16, (pcre2_code_16 * a0), (a0))
PCRE2_WRAP_RET_G16(pcre2_code_16 *, pcre2_compile_16, (PCRE2_SPTR16 a0, size_t a1, uint32_t a2, int * a3, size_t * a4, pcre2_compile_context_16 * a5), (a0, a1, a2, a3, a4, a5), NULL)
PCRE2_WRAP_RET_G16(pcre2_compile_context_16 *, pcre2_compile_context_copy_16, (pcre2_compile_context_16 * a0), (a0), NULL)
PCRE2_WRAP_RET_G16(pcre2_compile_context_16 *, pcre2_compile_context_create_16, (pcre2_general_context_16 * a0), (a0), NULL)
PCRE2_WRAP_VOID_G16(pcre2_compile_context_free_16, (pcre2_compile_context_16 * a0), (a0))
PCRE2_WRAP_RET_G16(int, pcre2_config_16, (uint32_t a0, void * a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(pcre2_convert_context_16 *, pcre2_convert_context_copy_16, (pcre2_convert_context_16 * a0), (a0), NULL)
PCRE2_WRAP_RET_G16(pcre2_convert_context_16 *, pcre2_convert_context_create_16, (pcre2_general_context_16 * a0), (a0), NULL)
PCRE2_WRAP_VOID_G16(pcre2_convert_context_free_16, (pcre2_convert_context_16 * a0), (a0))
PCRE2_WRAP_VOID_G16(pcre2_converted_pattern_free_16, (PCRE2_UCHAR16 * a0), (a0))
PCRE2_WRAP_RET_G16(int, pcre2_dfa_match_16, (const pcre2_code_16 * a0, PCRE2_SPTR16 a1, size_t a2, size_t a3, uint32_t a4, pcre2_match_data_16 * a5, pcre2_match_context_16 * a6, int * a7, size_t a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
PCRE2_WRAP_RET_G16(pcre2_general_context_16 *, pcre2_general_context_copy_16, (pcre2_general_context_16 * a0), (a0), NULL)
PCRE2_WRAP_RET_G16(pcre2_general_context_16 *, pcre2_general_context_create_16, (void *(*a0 ) (size_t, void *), void (*a1 ) (void *, void *), void * a2), (a0, a1, a2), NULL)
PCRE2_WRAP_VOID_G16(pcre2_general_context_free_16, (pcre2_general_context_16 * a0), (a0))
PCRE2_WRAP_RET_G16(int, pcre2_get_error_message_16, (int a0, PCRE2_UCHAR16 * a1, size_t a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G16(PCRE2_SPTR16, pcre2_get_mark_16, (pcre2_match_data_16 * a0), (a0), ((PCRE2_SPTR16)0))
PCRE2_WRAP_RET_G16(size_t, pcre2_get_match_data_heapframes_size_16, (pcre2_match_data_16 * a0), (a0), 0)
PCRE2_WRAP_RET_G16(size_t, pcre2_get_match_data_size_16, (pcre2_match_data_16 * a0), (a0), 0)
PCRE2_WRAP_RET_G16(uint32_t, pcre2_get_ovector_count_16, (pcre2_match_data_16 * a0), (a0), 0)
PCRE2_WRAP_RET_G16(size_t *, pcre2_get_ovector_pointer_16, (pcre2_match_data_16 * a0), (a0), NULL)
PCRE2_WRAP_RET_G16(size_t, pcre2_get_startchar_16, (pcre2_match_data_16 * a0), (a0), 0)
PCRE2_WRAP_RET_G16(int, pcre2_jit_compile_16, (pcre2_code_16 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_VOID_G16(pcre2_jit_free_unused_memory_16, (pcre2_general_context_16 * a0), (a0))
PCRE2_WRAP_RET_G16(int, pcre2_jit_match_16, (const pcre2_code_16 * a0, PCRE2_SPTR16 a1, size_t a2, size_t a3, uint32_t a4, pcre2_match_data_16 * a5, pcre2_match_context_16 * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
PCRE2_WRAP_VOID_G16(pcre2_jit_stack_assign_16, (pcre2_match_context_16 * a0, pcre2_jit_callback_16 a1, void * a2), (a0, a1, a2))
PCRE2_WRAP_RET_G16(pcre2_jit_stack_16 *, pcre2_jit_stack_create_16, (size_t a0, size_t a1, pcre2_general_context_16 * a2), (a0, a1, a2), NULL)
PCRE2_WRAP_VOID_G16(pcre2_jit_stack_free_16, (pcre2_jit_stack_16 * a0), (a0))
PCRE2_WRAP_RET_G16(const uint8_t *, pcre2_maketables_16, (pcre2_general_context_16 * a0), (a0), NULL)
PCRE2_WRAP_VOID_G16(pcre2_maketables_free_16, (pcre2_general_context_16 * a0, const uint8_t * a1), (a0, a1))
PCRE2_WRAP_RET_G16(int, pcre2_match_16, (const pcre2_code_16 * a0, PCRE2_SPTR16 a1, size_t a2, size_t a3, uint32_t a4, pcre2_match_data_16 * a5, pcre2_match_context_16 * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
PCRE2_WRAP_RET_G16(pcre2_match_context_16 *, pcre2_match_context_copy_16, (pcre2_match_context_16 * a0), (a0), NULL)
PCRE2_WRAP_RET_G16(pcre2_match_context_16 *, pcre2_match_context_create_16, (pcre2_general_context_16 * a0), (a0), NULL)
PCRE2_WRAP_VOID_G16(pcre2_match_context_free_16, (pcre2_match_context_16 * a0), (a0))
PCRE2_WRAP_RET_G16(pcre2_match_data_16 *, pcre2_match_data_create_16, (uint32_t a0, pcre2_general_context_16 * a1), (a0, a1), NULL)
PCRE2_WRAP_RET_G16(pcre2_match_data_16 *, pcre2_match_data_create_from_pattern_16, (const pcre2_code_16 * a0, pcre2_general_context_16 * a1), (a0, a1), NULL)
PCRE2_WRAP_VOID_G16(pcre2_match_data_free_16, (pcre2_match_data_16 * a0), (a0))
PCRE2_WRAP_RET_G16(int, pcre2_next_match_16, (pcre2_match_data_16 * a0, size_t * a1, uint32_t * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G16(int, pcre2_pattern_convert_16, (PCRE2_SPTR16 a0, size_t a1, uint32_t a2, PCRE2_UCHAR16 ** a3, size_t * a4, pcre2_convert_context_16 * a5), (a0, a1, a2, a3, a4, a5), -1)
PCRE2_WRAP_RET_G16(int, pcre2_pattern_info_16, (const pcre2_code_16 * a0, uint32_t a1, void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G16(int32_t, pcre2_serialize_decode_16, (pcre2_code_16 ** a0, int32_t a1, const uint8_t * a2, pcre2_general_context_16 * a3), (a0, a1, a2, a3), ((int32_t)0))
PCRE2_WRAP_RET_G16(int32_t, pcre2_serialize_encode_16, (const pcre2_code_16 ** a0, int32_t a1, uint8_t ** a2, size_t * a3, pcre2_general_context_16 * a4), (a0, a1, a2, a3, a4), ((int32_t)0))
PCRE2_WRAP_VOID_G16(pcre2_serialize_free_16, (uint8_t * a0), (a0))
PCRE2_WRAP_RET_G16(int32_t, pcre2_serialize_get_number_of_codes_16, (const uint8_t * a0), (a0), ((int32_t)0))
PCRE2_WRAP_RET_G16(int, pcre2_set_bsr_16, (pcre2_compile_context_16 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_callout_16, (pcre2_match_context_16 * a0, int (*a1 ) (pcre2_callout_block_16 *, void *), void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_character_tables_16, (pcre2_compile_context_16 * a0, const uint8_t * a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_compile_extra_options_16, (pcre2_compile_context_16 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_compile_recursion_guard_16, (pcre2_compile_context_16 * a0, int (*a1 ) (uint32_t, void *), void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_depth_limit_16, (pcre2_match_context_16 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_glob_escape_16, (pcre2_convert_context_16 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_glob_separator_16, (pcre2_convert_context_16 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_heap_limit_16, (pcre2_match_context_16 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_match_limit_16, (pcre2_match_context_16 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_max_pattern_compiled_length_16, (pcre2_compile_context_16 * a0, size_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_max_pattern_length_16, (pcre2_compile_context_16 * a0, size_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_max_varlookbehind_16, (pcre2_compile_context_16 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_newline_16, (pcre2_compile_context_16 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_offset_limit_16, (pcre2_match_context_16 * a0, size_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_optimize_16, (pcre2_compile_context_16 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_parens_nest_limit_16, (pcre2_compile_context_16 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_recursion_limit_16, (pcre2_match_context_16 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_recursion_memory_management_16, (pcre2_match_context_16 * a0, void *(*a1 ) (size_t, void *), void (*a2 ) (void *, void *), void * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_substitute_callout_16, (pcre2_match_context_16 * a0, int (*a1 ) (pcre2_substitute_callout_block_16 *, void *), void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G16(int, pcre2_set_substitute_case_callout_16, (pcre2_match_context_16 * a0, size_t (*a1 ) (PCRE2_SPTR16, size_t, PCRE2_UCHAR16 *, size_t, int, void *), void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G16(int, pcre2_substitute_16, (const pcre2_code_16 * a0, PCRE2_SPTR16 a1, size_t a2, size_t a3, uint32_t a4, pcre2_match_data_16 * a5, pcre2_match_context_16 * a6, PCRE2_SPTR16 a7, size_t a8, PCRE2_UCHAR16 * a9, size_t * a10), (a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10), -1)
PCRE2_WRAP_RET_G16(int, pcre2_substring_copy_byname_16, (pcre2_match_data_16 * a0, PCRE2_SPTR16 a1, PCRE2_UCHAR16 * a2, size_t * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_RET_G16(int, pcre2_substring_copy_bynumber_16, (pcre2_match_data_16 * a0, uint32_t a1, PCRE2_UCHAR16 * a2, size_t * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_VOID_G16(pcre2_substring_free_16, (PCRE2_UCHAR16 * a0), (a0))
PCRE2_WRAP_RET_G16(int, pcre2_substring_get_byname_16, (pcre2_match_data_16 * a0, PCRE2_SPTR16 a1, PCRE2_UCHAR16 ** a2, size_t * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_RET_G16(int, pcre2_substring_get_bynumber_16, (pcre2_match_data_16 * a0, uint32_t a1, PCRE2_UCHAR16 ** a2, size_t * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_RET_G16(int, pcre2_substring_length_byname_16, (pcre2_match_data_16 * a0, PCRE2_SPTR16 a1, size_t * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G16(int, pcre2_substring_length_bynumber_16, (pcre2_match_data_16 * a0, uint32_t a1, size_t * a2), (a0, a1, a2), -1)
PCRE2_WRAP_VOID_G16(pcre2_substring_list_free_16, (PCRE2_UCHAR16 ** a0), (a0))
PCRE2_WRAP_RET_G16(int, pcre2_substring_list_get_16, (pcre2_match_data_16 * a0, PCRE2_UCHAR16 *** a1, size_t ** a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G16(int, pcre2_substring_nametable_scan_16, (const pcre2_code_16 * a0, PCRE2_SPTR16 a1, PCRE2_SPTR16 * a2, PCRE2_SPTR16 * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_RET_G16(int, pcre2_substring_number_from_name_16, (const pcre2_code_16 * a0, PCRE2_SPTR16 a1), (a0, a1), -1)
/* ---------- 全量函数封装：32 位 ---------- */
PCRE2_WRAP_RET_G32(int, pcre2_callout_enumerate_32, (const pcre2_code_32 * a0, int (*a1 ) (pcre2_callout_enumerate_block_32 *, void *), void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G32(pcre2_code_32 *, pcre2_code_copy_32, (const pcre2_code_32 * a0), (a0), NULL)
PCRE2_WRAP_RET_G32(pcre2_code_32 *, pcre2_code_copy_with_tables_32, (const pcre2_code_32 * a0), (a0), NULL)
PCRE2_WRAP_VOID_G32(pcre2_code_free_32, (pcre2_code_32 * a0), (a0))
PCRE2_WRAP_RET_G32(pcre2_code_32 *, pcre2_compile_32, (PCRE2_SPTR32 a0, size_t a1, uint32_t a2, int * a3, size_t * a4, pcre2_compile_context_32 * a5), (a0, a1, a2, a3, a4, a5), NULL)
PCRE2_WRAP_RET_G32(pcre2_compile_context_32 *, pcre2_compile_context_copy_32, (pcre2_compile_context_32 * a0), (a0), NULL)
PCRE2_WRAP_RET_G32(pcre2_compile_context_32 *, pcre2_compile_context_create_32, (pcre2_general_context_32 * a0), (a0), NULL)
PCRE2_WRAP_VOID_G32(pcre2_compile_context_free_32, (pcre2_compile_context_32 * a0), (a0))
PCRE2_WRAP_RET_G32(int, pcre2_config_32, (uint32_t a0, void * a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(pcre2_convert_context_32 *, pcre2_convert_context_copy_32, (pcre2_convert_context_32 * a0), (a0), NULL)
PCRE2_WRAP_RET_G32(pcre2_convert_context_32 *, pcre2_convert_context_create_32, (pcre2_general_context_32 * a0), (a0), NULL)
PCRE2_WRAP_VOID_G32(pcre2_convert_context_free_32, (pcre2_convert_context_32 * a0), (a0))
PCRE2_WRAP_VOID_G32(pcre2_converted_pattern_free_32, (PCRE2_UCHAR32 * a0), (a0))
PCRE2_WRAP_RET_G32(int, pcre2_dfa_match_32, (const pcre2_code_32 * a0, PCRE2_SPTR32 a1, size_t a2, size_t a3, uint32_t a4, pcre2_match_data_32 * a5, pcre2_match_context_32 * a6, int * a7, size_t a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
PCRE2_WRAP_RET_G32(pcre2_general_context_32 *, pcre2_general_context_copy_32, (pcre2_general_context_32 * a0), (a0), NULL)
PCRE2_WRAP_RET_G32(pcre2_general_context_32 *, pcre2_general_context_create_32, (void *(*a0 ) (size_t, void *), void (*a1 ) (void *, void *), void * a2), (a0, a1, a2), NULL)
PCRE2_WRAP_VOID_G32(pcre2_general_context_free_32, (pcre2_general_context_32 * a0), (a0))
PCRE2_WRAP_RET_G32(int, pcre2_get_error_message_32, (int a0, PCRE2_UCHAR32 * a1, size_t a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G32(PCRE2_SPTR32, pcre2_get_mark_32, (pcre2_match_data_32 * a0), (a0), ((PCRE2_SPTR32)0))
PCRE2_WRAP_RET_G32(size_t, pcre2_get_match_data_heapframes_size_32, (pcre2_match_data_32 * a0), (a0), 0)
PCRE2_WRAP_RET_G32(size_t, pcre2_get_match_data_size_32, (pcre2_match_data_32 * a0), (a0), 0)
PCRE2_WRAP_RET_G32(uint32_t, pcre2_get_ovector_count_32, (pcre2_match_data_32 * a0), (a0), 0)
PCRE2_WRAP_RET_G32(size_t *, pcre2_get_ovector_pointer_32, (pcre2_match_data_32 * a0), (a0), NULL)
PCRE2_WRAP_RET_G32(size_t, pcre2_get_startchar_32, (pcre2_match_data_32 * a0), (a0), 0)
PCRE2_WRAP_RET_G32(int, pcre2_jit_compile_32, (pcre2_code_32 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_VOID_G32(pcre2_jit_free_unused_memory_32, (pcre2_general_context_32 * a0), (a0))
PCRE2_WRAP_RET_G32(int, pcre2_jit_match_32, (const pcre2_code_32 * a0, PCRE2_SPTR32 a1, size_t a2, size_t a3, uint32_t a4, pcre2_match_data_32 * a5, pcre2_match_context_32 * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
PCRE2_WRAP_VOID_G32(pcre2_jit_stack_assign_32, (pcre2_match_context_32 * a0, pcre2_jit_callback_32 a1, void * a2), (a0, a1, a2))
PCRE2_WRAP_RET_G32(pcre2_jit_stack_32 *, pcre2_jit_stack_create_32, (size_t a0, size_t a1, pcre2_general_context_32 * a2), (a0, a1, a2), NULL)
PCRE2_WRAP_VOID_G32(pcre2_jit_stack_free_32, (pcre2_jit_stack_32 * a0), (a0))
PCRE2_WRAP_RET_G32(const uint8_t *, pcre2_maketables_32, (pcre2_general_context_32 * a0), (a0), NULL)
PCRE2_WRAP_VOID_G32(pcre2_maketables_free_32, (pcre2_general_context_32 * a0, const uint8_t * a1), (a0, a1))
PCRE2_WRAP_RET_G32(int, pcre2_match_32, (const pcre2_code_32 * a0, PCRE2_SPTR32 a1, size_t a2, size_t a3, uint32_t a4, pcre2_match_data_32 * a5, pcre2_match_context_32 * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
PCRE2_WRAP_RET_G32(pcre2_match_context_32 *, pcre2_match_context_copy_32, (pcre2_match_context_32 * a0), (a0), NULL)
PCRE2_WRAP_RET_G32(pcre2_match_context_32 *, pcre2_match_context_create_32, (pcre2_general_context_32 * a0), (a0), NULL)
PCRE2_WRAP_VOID_G32(pcre2_match_context_free_32, (pcre2_match_context_32 * a0), (a0))
PCRE2_WRAP_RET_G32(pcre2_match_data_32 *, pcre2_match_data_create_32, (uint32_t a0, pcre2_general_context_32 * a1), (a0, a1), NULL)
PCRE2_WRAP_RET_G32(pcre2_match_data_32 *, pcre2_match_data_create_from_pattern_32, (const pcre2_code_32 * a0, pcre2_general_context_32 * a1), (a0, a1), NULL)
PCRE2_WRAP_VOID_G32(pcre2_match_data_free_32, (pcre2_match_data_32 * a0), (a0))
PCRE2_WRAP_RET_G32(int, pcre2_next_match_32, (pcre2_match_data_32 * a0, size_t * a1, uint32_t * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G32(int, pcre2_pattern_convert_32, (PCRE2_SPTR32 a0, size_t a1, uint32_t a2, PCRE2_UCHAR32 ** a3, size_t * a4, pcre2_convert_context_32 * a5), (a0, a1, a2, a3, a4, a5), -1)
PCRE2_WRAP_RET_G32(int, pcre2_pattern_info_32, (const pcre2_code_32 * a0, uint32_t a1, void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G32(int32_t, pcre2_serialize_decode_32, (pcre2_code_32 ** a0, int32_t a1, const uint8_t * a2, pcre2_general_context_32 * a3), (a0, a1, a2, a3), ((int32_t)0))
PCRE2_WRAP_RET_G32(int32_t, pcre2_serialize_encode_32, (const pcre2_code_32 ** a0, int32_t a1, uint8_t ** a2, size_t * a3, pcre2_general_context_32 * a4), (a0, a1, a2, a3, a4), ((int32_t)0))
PCRE2_WRAP_VOID_G32(pcre2_serialize_free_32, (uint8_t * a0), (a0))
PCRE2_WRAP_RET_G32(int32_t, pcre2_serialize_get_number_of_codes_32, (const uint8_t * a0), (a0), ((int32_t)0))
PCRE2_WRAP_RET_G32(int, pcre2_set_bsr_32, (pcre2_compile_context_32 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_callout_32, (pcre2_match_context_32 * a0, int (*a1 ) (pcre2_callout_block_32 *, void *), void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_character_tables_32, (pcre2_compile_context_32 * a0, const uint8_t * a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_compile_extra_options_32, (pcre2_compile_context_32 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_compile_recursion_guard_32, (pcre2_compile_context_32 * a0, int (*a1 ) (uint32_t, void *), void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_depth_limit_32, (pcre2_match_context_32 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_glob_escape_32, (pcre2_convert_context_32 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_glob_separator_32, (pcre2_convert_context_32 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_heap_limit_32, (pcre2_match_context_32 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_match_limit_32, (pcre2_match_context_32 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_max_pattern_compiled_length_32, (pcre2_compile_context_32 * a0, size_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_max_pattern_length_32, (pcre2_compile_context_32 * a0, size_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_max_varlookbehind_32, (pcre2_compile_context_32 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_newline_32, (pcre2_compile_context_32 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_offset_limit_32, (pcre2_match_context_32 * a0, size_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_optimize_32, (pcre2_compile_context_32 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_parens_nest_limit_32, (pcre2_compile_context_32 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_recursion_limit_32, (pcre2_match_context_32 * a0, uint32_t a1), (a0, a1), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_recursion_memory_management_32, (pcre2_match_context_32 * a0, void *(*a1 ) (size_t, void *), void (*a2 ) (void *, void *), void * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_substitute_callout_32, (pcre2_match_context_32 * a0, int (*a1 ) (pcre2_substitute_callout_block_32 *, void *), void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G32(int, pcre2_set_substitute_case_callout_32, (pcre2_match_context_32 * a0, size_t (*a1 ) (PCRE2_SPTR32, size_t, PCRE2_UCHAR32 *, size_t, int, void *), void * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G32(int, pcre2_substitute_32, (const pcre2_code_32 * a0, PCRE2_SPTR32 a1, size_t a2, size_t a3, uint32_t a4, pcre2_match_data_32 * a5, pcre2_match_context_32 * a6, PCRE2_SPTR32 a7, size_t a8, PCRE2_UCHAR32 * a9, size_t * a10), (a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10), -1)
PCRE2_WRAP_RET_G32(int, pcre2_substring_copy_byname_32, (pcre2_match_data_32 * a0, PCRE2_SPTR32 a1, PCRE2_UCHAR32 * a2, size_t * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_RET_G32(int, pcre2_substring_copy_bynumber_32, (pcre2_match_data_32 * a0, uint32_t a1, PCRE2_UCHAR32 * a2, size_t * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_VOID_G32(pcre2_substring_free_32, (PCRE2_UCHAR32 * a0), (a0))
PCRE2_WRAP_RET_G32(int, pcre2_substring_get_byname_32, (pcre2_match_data_32 * a0, PCRE2_SPTR32 a1, PCRE2_UCHAR32 ** a2, size_t * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_RET_G32(int, pcre2_substring_get_bynumber_32, (pcre2_match_data_32 * a0, uint32_t a1, PCRE2_UCHAR32 ** a2, size_t * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_RET_G32(int, pcre2_substring_length_byname_32, (pcre2_match_data_32 * a0, PCRE2_SPTR32 a1, size_t * a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G32(int, pcre2_substring_length_bynumber_32, (pcre2_match_data_32 * a0, uint32_t a1, size_t * a2), (a0, a1, a2), -1)
PCRE2_WRAP_VOID_G32(pcre2_substring_list_free_32, (PCRE2_UCHAR32 ** a0), (a0))
PCRE2_WRAP_RET_G32(int, pcre2_substring_list_get_32, (pcre2_match_data_32 * a0, PCRE2_UCHAR32 *** a1, size_t ** a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_G32(int, pcre2_substring_nametable_scan_32, (const pcre2_code_32 * a0, PCRE2_SPTR32 a1, PCRE2_SPTR32 * a2, PCRE2_SPTR32 * a3), (a0, a1, a2, a3), -1)
PCRE2_WRAP_RET_G32(int, pcre2_substring_number_from_name_32, (const pcre2_code_32 * a0, PCRE2_SPTR32 a1), (a0, a1), -1)
/* ---------- 全量函数封装：POSIX ---------- */
PCRE2_WRAP_RET_POSIX(int, pcre2_regcomp, (regex_t * a0, const char * a1, int a2), (a0, a1, a2), -1)
PCRE2_WRAP_RET_POSIX(size_t, pcre2_regerror, (int a0, const regex_t * a1, char * a2, size_t a3), (a0, a1, a2, a3), 0)
PCRE2_WRAP_RET_POSIX(int, pcre2_regexec, (const regex_t * a0, const char * a1, size_t a2, regmatch_t * a3, int a4), (a0, a1, a2, a3, a4), -1)
PCRE2_WRAP_VOID_POSIX(pcre2_regfree, (regex_t * a0), (a0))
