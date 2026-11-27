/* ============================================================
 * re2_dyn.c — RE2 动态加载封装层（公开头文件 API 全量封装）
 *
 * 统计：导出函数 481 / 封装函数 60 / 跳过 420 / 数据符号 1
 * 设计要点：
 *   1. 单文件、无 .h；链接期不链接目标库（只 -ldl）。
 *   2. wrapper 函数名就是 C++ 原始 mangled symbol，不自定义 API。
 *   3. 非静态成员的第一个参数是 this；构造/析构的 C1/C2/D1/D2 全部封装。
 *   4. absl::string_view 按 libstdc++ 布局 {size_t len; const char *ptr;} 传递。
 *   5. 首次调用才 dlsym 并缓存；内部/私有导出符号列在文件尾 skip 清单。
 * ============================================================ */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
/* ---------- C ABI 对齐类型 ---------- */
typedef struct {
    size_t len;
    const char *ptr;
} re2_sv;

typedef struct {
    void *ptr;
    size_t len;
    union {
        char bytes[16];
        void *align;
    } tail;
} re2_std_string;

static const re2_std_string re2_string_zero = {0};

/* ---------- 前向声明 ---------- */
void *re2_sym(const char *name);
void re2_dyn_sync_globals(void);

/* ---------- 数据符号副本 ---------- */
int _ZN3re23RE27Options14kDefaultMaxMemE = 0;

void re2_dyn_sync_globals(void) {
    void *p;
    if ((p = re2_sym("_ZN3re23RE27Options14kDefaultMaxMemE")) != NULL)
        memcpy(&_ZN3re23RE27Options14kDefaultMaxMemE, p, sizeof(_ZN3re23RE27Options14kDefaultMaxMemE));
}

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

/* ============================================================
 * 全局状态与库加载
 * ============================================================ */
static lib_handle_t re2_handle = NULL;
static int re2_init_state = 0;

#define MAX_CANDIDATES 64
#define PATH_BUF_LEN   1024

static const char *re2_defaults[] = {
"libre2.so.11", "libre2.so", "libre2.dylib", "re2.dll"
};

static int re2_do_load(const char *library_path) {
    if (re2_handle) return 0;
    static char bufs[MAX_CANDIDATES][PATH_BUF_LEN];
    const char *candidates[MAX_CANDIDATES];
    int n = 0, i;

    if (library_path && *library_path && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    const char *explicit_lib = getenv("RE2_DYN_LIB");
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
                    for (i = 0; i < (int)(sizeof(re2_defaults)/sizeof(re2_defaults[0])); i++) {
                        if (n >= MAX_CANDIDATES - 2) break;
                        snprintf(bufs[n], PATH_BUF_LEN, "%s/%s", p, re2_defaults[i]);
                        candidates[n] = bufs[n]; n++;
                    }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }
    for (i = 0; i < (int)(sizeof(re2_defaults)/sizeof(re2_defaults[0])) && n < MAX_CANDIDATES - 1; i++)
        candidates[n++] = re2_defaults[i];
    const char *home = getenv("HOME");
    if (home && *home && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s/.nix-profile/lib/%s", home, re2_defaults[0]);
        candidates[n] = bufs[n]; n++;
    }
    if (n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "/nix/var/nix/profiles/default/lib/%s", re2_defaults[0]);
        candidates[n] = bufs[n]; n++;
    }
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        re2_handle = load_lib(candidates[i]);
        if (re2_handle) break;
    }
    if (!re2_handle) {
        fprintf(stderr, "re2_dyn: cannot load library (%s)\n", lib_error());
        return -1;
    }
    if (!get_sym(re2_handle, "_ZNK3re23RE211ProgramSizeEv")) {
        fprintf(stderr, "re2_dyn: loaded library does not export _ZNK3re23RE211ProgramSizeEv\n");
        free_lib(re2_handle); re2_handle = NULL;
        return -1;
    }
    fprintf(stderr, "re2_dyn: loaded %d %s\n", i, candidates[i]);
    re2_dyn_sync_globals();
    return 0;
}

void *re2_sym(const char *name) { return re2_handle ? get_sym(re2_handle, name) : NULL; }

int re2_libok(void) {
    if (re2_init_state == 0)
        re2_init_state = (re2_do_load(NULL) == 0) ? 1 : -1;
    return (re2_init_state == 1 && re2_handle != NULL) ? 1 : 0;
}

int re2_dyn_init(const char *library_path) {
    if (re2_do_load(library_path) == 0) { re2_init_state = 1; return 0; }
    if (re2_init_state == 0) re2_init_state = -1;
    return -1;
}

void re2_dyn_cleanup(void) {
    if (!re2_handle) re2_init_state = 0;
    else if (re2_init_state == -1) re2_init_state = 0;
}

int re2_dyn_has_symbol(const char *name) {
    if (!re2_libok()) return 0;
    return re2_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏：首次调用 dlsym 并缓存；失败打印 stderr 并返回安全值
 * 注意：name 就是 C++ mangled symbol；非静态成员的第一个参数是 this。
 * ============================================================ */
#define WRAP_RE2_RET(ret, name, params, args, fallback)                    \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!re2_libok()) return fallback;                                \
        *(void **)(&fn) = re2_sym(#name);                                 \
        if (!fn) {                                                        \
            fprintf(stderr, "re2_dyn: dlsym failed: %s\n", #name);        \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define WRAP_RE2_VOID(name, params, args)                                 \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!re2_libok()) return;                                         \
        *(void **)(&fn) = re2_sym(#name);                                 \
        if (!fn) {                                                        \
            fprintf(stderr, "re2_dyn: dlsym failed: %s\n", #name);        \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}
/* ---------- 全量函数封装（原始 mangled symbol） ---------- */
WRAP_RE2_RET(void *, _ZN3re23RE2C1EPKc, (void *self, const char *a0), (self, a0), self)
WRAP_RE2_RET(void *, _ZN3re23RE2C2EPKc, (void *self, const char *a0), (self, a0), self)
WRAP_RE2_RET(void *, _ZN3re23RE2C1ERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE, (void *self, void *a0), (self, a0), self)
WRAP_RE2_RET(void *, _ZN3re23RE2C2ERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE, (void *self, void *a0), (self, a0), self)
WRAP_RE2_RET(void *, _ZN3re23RE2C1ESt17basic_string_viewIcSt11char_traitsIcEE, (void *self, re2_sv a0), (self, a0), self)
WRAP_RE2_RET(void *, _ZN3re23RE2C2ESt17basic_string_viewIcSt11char_traitsIcEE, (void *self, re2_sv a0), (self, a0), self)
WRAP_RE2_RET(void *, _ZN3re23RE2C1ESt17basic_string_viewIcSt11char_traitsIcEERKNS0_7OptionsE, (void *self, re2_sv a0, void *a1), (self, a0, a1), self)
WRAP_RE2_RET(void *, _ZN3re23RE2C2ESt17basic_string_viewIcSt11char_traitsIcEERKNS0_7OptionsE, (void *self, re2_sv a0, void *a1), (self, a0, a1), self)
WRAP_RE2_VOID(_ZN3re23RE2D1Ev, (void *self), (self))
WRAP_RE2_VOID(_ZN3re23RE2D2Ev, (void *self), (self))
WRAP_RE2_RET(int, _ZNK3re23RE211ProgramSizeEv, (void *self), (self), 0)
WRAP_RE2_RET(int, _ZNK3re23RE218ReverseProgramSizeEv, (void *self), (self), 0)
WRAP_RE2_RET(int, _ZNK3re23RE213ProgramFanoutEPSt6vectorIiSaIiEE, (void *self, void *a0), (self, a0), 0)
WRAP_RE2_RET(int, _ZNK3re23RE220ReverseProgramFanoutEPSt6vectorIiSaIiEE, (void *self, void *a0), (self, a0), 0)
WRAP_RE2_RET(unsigned char, _ZN3re23RE210FullMatchNESt17basic_string_viewIcSt11char_traitsIcEERKS0_PKPKNS0_3ArgEi, (re2_sv a0, void *a1, const void *const *a2, int a3), (a0, a1, a2, a3), 0)
WRAP_RE2_RET(unsigned char, _ZN3re23RE213PartialMatchNESt17basic_string_viewIcSt11char_traitsIcEERKS0_PKPKNS0_3ArgEi, (re2_sv a0, void *a1, const void *const *a2, int a3), (a0, a1, a2, a3), 0)
WRAP_RE2_RET(unsigned char, _ZN3re23RE28ConsumeNEPSt17basic_string_viewIcSt11char_traitsIcEERKS0_PKPKNS0_3ArgEi, (re2_sv *a0, void *a1, const void *const *a2, int a3), (a0, a1, a2, a3), 0)
WRAP_RE2_RET(unsigned char, _ZN3re23RE215FindAndConsumeNEPSt17basic_string_viewIcSt11char_traitsIcEERKS0_PKPKNS0_3ArgEi, (re2_sv *a0, void *a1, const void *const *a2, int a3), (a0, a1, a2, a3), 0)
WRAP_RE2_RET(re2_std_string, _ZN3re23RE29QuoteMetaB5cxx11ESt17basic_string_viewIcSt11char_traitsIcEE, (re2_sv a0), (a0), re2_string_zero)
WRAP_RE2_RET(unsigned char, _ZNK3re23RE218CheckRewriteStringESt17basic_string_viewIcSt11char_traitsIcEEPNSt7__cxx1112basic_stringIcS3_SaIcEEE, (void *self, re2_sv a0, void *a1), (self, a0, a1), 0)
WRAP_RE2_RET(int, _ZN3re23RE211MaxSubmatchESt17basic_string_viewIcSt11char_traitsIcEE, (re2_sv a0), (a0), 0)
WRAP_RE2_RET(void *, _ZNK3re23RE220NamedCapturingGroupsB5cxx11Ev, (void *self), (self), NULL)
WRAP_RE2_RET(void *, _ZNK3re23RE219CapturingGroupNamesB5cxx11Ev, (void *self), (self), NULL)
WRAP_RE2_RET(unsigned char, _ZNK3re23RE218PossibleMatchRangeEPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES7_i, (void *self, void *a0, void *a1, int a2), (self, a0, a1, a2), 0)
WRAP_RE2_RET(unsigned char, _ZNK3re23RE25MatchESt17basic_string_viewIcSt11char_traitsIcEEmmNS0_6AnchorEPS4_i, (void *self, re2_sv a0, size_t a1, size_t a2, int a3, re2_sv *a4, int a5), (self, a0, a1, a2, a3, a4, a5), 0)
WRAP_RE2_RET(unsigned char, _ZNK3re23RE27RewriteEPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt17basic_string_viewIcS4_EPKS9_i, (void *self, void *a0, re2_sv a1, const re2_sv *a2, int a3), (self, a0, a1, a2, a3), 0)
WRAP_RE2_RET(unsigned char, _ZN3re23RE27ReplaceEPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEERKS0_St17basic_string_viewIcS4_E, (void *a0, void *a1, re2_sv a2), (a0, a1, a2), 0)
WRAP_RE2_RET(int, _ZN3re23RE213GlobalReplaceEPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEERKS0_St17basic_string_viewIcS4_E, (void *a0, void *a1, re2_sv a2), (a0, a1, a2), 0)
WRAP_RE2_RET(unsigned char, _ZN3re23RE27ExtractESt17basic_string_viewIcSt11char_traitsIcEERKS0_S4_PNSt7__cxx1112basic_stringIcS3_SaIcEEE, (re2_sv a0, void *a1, re2_sv a2, void *a3), (a0, a1, a2, a3), 0)
WRAP_RE2_VOID(_ZN3re23RE245FUZZING_ONLY_set_maximum_global_replace_countEi, (int a0), (a0))
WRAP_RE2_RET(void *, _ZN3re23RE27OptionsC1ENS0_13CannedOptionsE, (void *self, int a0), (self, a0), self)
WRAP_RE2_RET(void *, _ZN3re23RE27OptionsC2ENS0_13CannedOptionsE, (void *self, int a0), (self, a0), self)
WRAP_RE2_RET(int, _ZNK3re23RE27Options10ParseFlagsEv, (void *self), (self), 0)
WRAP_RE2_RET(void *, _ZN3re23RE23SetC1ERKNS0_7OptionsENS0_6AnchorE, (void *self, void *a0, int a1), (self, a0, a1), self)
WRAP_RE2_RET(void *, _ZN3re23RE23SetC2ERKNS0_7OptionsENS0_6AnchorE, (void *self, void *a0, int a1), (self, a0, a1), self)
WRAP_RE2_VOID(_ZN3re23RE23SetD1Ev, (void *self), (self))
WRAP_RE2_VOID(_ZN3re23RE23SetD2Ev, (void *self), (self))
WRAP_RE2_RET(void *, _ZN3re23RE23SetC1EOS1_, (void *self, void *a0), (self, a0), self)
WRAP_RE2_RET(void *, _ZN3re23RE23SetC2EOS1_, (void *self, void *a0), (self, a0), self)
WRAP_RE2_RET(void *, _ZN3re23RE23SetaSEOS1_, (void *self, void *a0), (self, a0), self)
WRAP_RE2_RET(int, _ZN3re23RE23Set3AddESt17basic_string_viewIcSt11char_traitsIcEEPNSt7__cxx1112basic_stringIcS4_SaIcEEE, (void *self, re2_sv a0, void *a1), (self, a0, a1), 0)
WRAP_RE2_RET(int, _ZNK3re23RE23Set4SizeEv, (void *self), (self), 0)
WRAP_RE2_RET(unsigned char, _ZN3re23RE23Set7CompileEv, (void *self), (self), 0)
WRAP_RE2_RET(unsigned char, _ZNK3re23RE23Set5MatchESt17basic_string_viewIcSt11char_traitsIcEEPSt6vectorIiSaIiEE, (void *self, re2_sv a0, void *a1), (self, a0, a1), 0)
WRAP_RE2_RET(unsigned char, _ZNK3re23RE23Set5MatchESt17basic_string_viewIcSt11char_traitsIcEEPSt6vectorIiSaIiEEPNS1_9ErrorInfoE, (void *self, re2_sv a0, void *a1, void *a2), (self, a0, a1, a2), 0)
WRAP_RE2_RET(void *, _ZN3re211FilteredRE2C1Ev, (void *self), (self), self)
WRAP_RE2_RET(void *, _ZN3re211FilteredRE2C2Ev, (void *self), (self), self)
WRAP_RE2_RET(void *, _ZN3re211FilteredRE2C1Ei, (void *self, int a0), (self, a0), self)
WRAP_RE2_RET(void *, _ZN3re211FilteredRE2C2Ei, (void *self, int a0), (self, a0), self)
WRAP_RE2_VOID(_ZN3re211FilteredRE2D1Ev, (void *self), (self))
WRAP_RE2_VOID(_ZN3re211FilteredRE2D2Ev, (void *self), (self))
WRAP_RE2_RET(void *, _ZN3re211FilteredRE2C1EOS0_, (void *self, void *a0), (self, a0), self)
WRAP_RE2_RET(void *, _ZN3re211FilteredRE2C2EOS0_, (void *self, void *a0), (self, a0), self)
WRAP_RE2_RET(void *, _ZN3re211FilteredRE2aSEOS0_, (void *self, void *a0), (self, a0), self)
WRAP_RE2_RET(int, _ZN3re211FilteredRE23AddESt17basic_string_viewIcSt11char_traitsIcEERKNS_3RE27OptionsEPi, (void *self, re2_sv a0, void *a1, int *a2), (self, a0, a1, a2), 0)
WRAP_RE2_VOID(_ZN3re211FilteredRE27CompileEPSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS7_EE, (void *self, void *a0), (self, a0))
WRAP_RE2_RET(int, _ZNK3re211FilteredRE214SlowFirstMatchESt17basic_string_viewIcSt11char_traitsIcEE, (void *self, re2_sv a0), (self, a0), 0)
WRAP_RE2_RET(int, _ZNK3re211FilteredRE210FirstMatchESt17basic_string_viewIcSt11char_traitsIcEERKSt6vectorIiSaIiEE, (void *self, re2_sv a0, void *a1), (self, a0, a1), 0)
WRAP_RE2_RET(unsigned char, _ZNK3re211FilteredRE210AllMatchesESt17basic_string_viewIcSt11char_traitsIcEERKSt6vectorIiSaIiEEPS7_, (void *self, re2_sv a0, void *a1, void *a2), (self, a0, a1, a2), 0)
WRAP_RE2_VOID(_ZNK3re211FilteredRE213AllPotentialsERKSt6vectorIiSaIiEEPS3_, (void *self, void *a0, void *a1), (self, a0, a1))
/* ---------- 未封装的内部/私有导出符号，共 420 个 ---------- */
/* _ZN3re210PCREWalker9PostVisitEPNS_6RegexpEbbPbi */
/* _ZN3re210chartoruneEPiPKc */
/* _ZN3re210runetocharEPcPKi */
/* _ZN3re211FilteredRE214PrintPrefilterEi */
/* _ZN3re211FilteredRE219RegexpsGivenStringsERKSt6vectorIiSaIiEEPS3_ */
/* _ZN3re211SparseArrayIiE4lessERKNS1_10IndexValueES4_ */
/* _ZN3re211perl_groupsE */
/* _ZN3re212RegexpStatus4CopyERKS0_ */
/* _ZN3re212RegexpStatus8CodeTextB5cxx11ENS_16RegexpStatusCodeE */
/* _ZN3re212posix_groupsE */
/* _ZN3re212re2_internal5ParseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEEEbPKcmPT_ */
/* _ZN3re212re2_internal5ParseISt17basic_string_viewIcSt11char_traitsIcEEEEbPKcmPT_ */
/* _ZN3re212re2_internal5ParseIaEEbPKcmPT_ */
/* _ZN3re212re2_internal5ParseIcEEbPKcmPT_ */
/* _ZN3re212re2_internal5ParseIdEEbPKcmPT_ */
/* _ZN3re212re2_internal5ParseIfEEbPKcmPT_ */
/* _ZN3re212re2_internal5ParseIhEEbPKcmPT_ */
/* _ZN3re212re2_internal5ParseIiEEbPKcmPT_i */
/* _ZN3re212re2_internal5ParseIjEEbPKcmPT_i */
/* _ZN3re212re2_internal5ParseIlEEbPKcmPT_i */
/* _ZN3re212re2_internal5ParseImEEbPKcmPT_i */
/* _ZN3re212re2_internal5ParseIsEEbPKcmPT_i */
/* _ZN3re212re2_internal5ParseItEEbPKcmPT_i */
/* _ZN3re212re2_internal5ParseIvEEbPKcmPT_ */
/* _ZN3re212re2_internal5ParseIxEEbPKcmPT_i */
/* _ZN3re212re2_internal5ParseIyEEbPKcmPT_i */
/* _ZN3re213CycleFoldRuneEi */
/* _ZN3re213PrefilterTree13CanonicalNodeEPN4absl12lts_2026081713flat_hash_setIPNS_9PrefilterENS0_13PrefilterHashENS0_14PrefilterEqualESaIS5_EEES5_ */
/* _ZN3re213PrefilterTree14PrintDebugInfoEPN4absl12lts_2026081713flat_hash_setIPNS_9PrefilterENS0_13PrefilterHashENS0_14PrefilterEqualESaIS5_EEE */
/* _ZN3re213PrefilterTree14PrintPrefilterEi */
/* _ZN3re213PrefilterTree15AssignUniqueIdsEPN4absl12lts_2026081713flat_hash_setIPNS_9PrefilterENS0_13PrefilterHashENS0_14PrefilterEqualESaIS5_EEEPSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaISH_EE */
/* _ZN3re213PrefilterTree3AddEPNS_9PrefilterE */
/* _ZN3re213PrefilterTree7CompileEPSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS7_EE */
/* _ZN3re213PrefilterTreeC1Ei */
/* _ZN3re213PrefilterTreeC1Ev */
/* _ZN3re213PrefilterTreeC2Ei */
/* _ZN3re213PrefilterTreeC2Ev */
/* _ZN3re213PrefilterTreeD1Ev */
/* _ZN3re213PrefilterTreeD2Ev */
/* _ZN3re214ByteMapBuilder4MarkEii */
/* _ZN3re214ByteMapBuilder5BuildEPhPi */
/* _ZN3re214ByteMapBuilder5MergeEv */
/* _ZN3re214ByteMapBuilder7RecolorEi */
/* _ZN3re214CoalesceWalker10DoCoalesceEPPNS_6RegexpES3_ */
/* _ZN3re214CoalesceWalker10ShortVisitEPNS_6RegexpES2_ */
/* _ZN3re214CoalesceWalker11CanCoalesceEPNS_6RegexpES2_ */
/* _ZN3re214CoalesceWalker4CopyEPNS_6RegexpE */
/* _ZN3re214CoalesceWalker9PostVisitEPNS_6RegexpES2_S2_PS2_i */
/* _ZN3re214LookupCaseFoldEPKNS_8CaseFoldEii */
/* _ZN3re214OnePass_ChecksEv */
/* _ZN3re214SimplifyWalker10ShortVisitEPNS_6RegexpES2_ */
/* _ZN3re214SimplifyWalker14SimplifyRepeatEPNS_6RegexpEiiNS1_10ParseFlagsE */
/* _ZN3re214SimplifyWalker17SimplifyCharClassEPNS_6RegexpE */
/* _ZN3re214SimplifyWalker4CopyEPNS_6RegexpE */
/* _ZN3re214SimplifyWalker7Concat2EPNS_6RegexpES2_NS1_10ParseFlagsE */
/* _ZN3re214SimplifyWalker8PreVisitEPNS_6RegexpES2_Pb */
/* _ZN3re214SimplifyWalker9PostVisitEPNS_6RegexpES2_S2_PS2_i */
/* _ZN3re214ToStringWalker8PreVisitEPNS_6RegexpEiPb */
/* _ZN3re214ToStringWalker9PostVisitEPNS_6RegexpEiiPii */
/* _ZN3re214unicode_groupsE */
/* _ZN3re215PrefixSuccessorEPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE */
/* _ZN3re215num_perl_groupsE */
/* _ZN3re215unicode_tolowerE */
/* _ZN3re216CharClassBuilder10FoldsASCIIEv */
/* _ZN3re216CharClassBuilder11RemoveAboveEi */
/* _ZN3re216CharClassBuilder12AddCharClassEPS0_ */
/* _ZN3re216CharClassBuilder12GetCharClassEv */
/* _ZN3re216CharClassBuilder13AddRangeFlagsEiiNS_6Regexp10ParseFlagsE */
/* _ZN3re216CharClassBuilder4CopyEv */
/* _ZN3re216CharClassBuilder6NegateEv */
/* _ZN3re216CharClassBuilder8AddRangeEii */
/* _ZN3re216CharClassBuilder8ContainsEi */
/* _ZN3re216CharClassBuilderC1Ev */
/* _ZN3re216CharClassBuilderC2Ev */
/* _ZN3re216RepetitionWalker10ShortVisitEPNS_6RegexpEi */
/* _ZN3re216RepetitionWalker8PreVisitEPNS_6RegexpEiPb */
/* _ZN3re216RepetitionWalker9PostVisitEPNS_6RegexpEiiPii */
/* _ZN3re216num_posix_groupsE */
/* _ZN3re216unicode_casefoldE */
/* _ZN3re217EmptyStringWalker9PostVisitEPNS_6RegexpEbbPbi */
/* _ZN3re217ParseUnicodeGroupEPSt17basic_string_viewIcSt11char_traitsIcEENS_6Regexp10ParseFlagsEPNS_16CharClassBuilderEPNS_12RegexpStatusE */
/* _ZN3re218num_unicode_groupsE */
/* _ZN3re219ConvertLatin1ToUTF8ESt17basic_string_viewIcSt11char_traitsIcEEPNSt7__cxx1112basic_stringIcS2_SaIcEEE */
/* _ZN3re219ConvertRunesToBytesEbPiiPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE */
/* _ZN3re219num_unicode_tolowerE */
/* _ZN3re220num_unicode_casefoldE */
/* _ZN3re221FactorAlternationImpl6Round1EPPNS_6RegexpEiNS1_10ParseFlagsEPSt6vectorINS_6SpliceESaIS6_EE */
/* _ZN3re221FactorAlternationImpl6Round2EPPNS_6RegexpEiNS1_10ParseFlagsEPSt6vectorINS_6SpliceESaIS6_EE */
/* _ZN3re221FactorAlternationImpl6Round3EPPNS_6RegexpEiNS1_10ParseFlagsEPSt6vectorINS_6SpliceESaIS6_EE */
/* _ZN3re222MaybeParsePerlCCEscapeEPSt17basic_string_viewIcSt11char_traitsIcEENS_6Regexp10ParseFlagsE */
/* _ZN3re23DFA10AddToQueueEPNS0_5WorkqEij */
/* _ZN3re23DFA10ClearCacheEv */
/* _ZN3re23DFA10ResetCacheEPNS0_8RWLockerE */
/* _ZN3re23DFA10StateSaver7RestoreEv */
/* _ZN3re23DFA10StateSaverC1EPS0_PNS0_5StateE */
/* _ZN3re23DFA10StateSaverC2EPS0_PNS0_5StateE */
/* _ZN3re23DFA10StateSaverD1Ev */
/* _ZN3re23DFA10StateSaverD2Ev */
/* _ZN3re23DFA11CachedStateEPiij */
/* _ZN3re23DFA12StateToWorkqEPNS0_5StateEPNS0_5WorkqE */
/* _ZN3re23DFA13AnalyzeSearchEPNS0_12SearchParamsE */
/* _ZN3re23DFA14BuildAllStatesERKSt8functionIFvPKibEE */
/* _ZN3re23DFA14FastSearchLoopEPNS0_12SearchParamsE */
/* _ZN3re23DFA14RunStateOnByteEPNS0_5StateEi */
/* _ZN3re23DFA14RunWorkqOnByteEPNS0_5WorkqES2_ijPb */
/* _ZN3re23DFA18PossibleMatchRangeEPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES7_i */
/* _ZN3re23DFA18WorkqToCachedStateEPNS0_5WorkqES2_j */
/* _ZN3re23DFA19AnalyzeSearchHelperEPNS0_12SearchParamsEPNS0_9StartInfoEj */
/* _ZN3re23DFA21RunWorkqOnEmptyStringEPNS0_5WorkqES2_j */
/* _ZN3re23DFA22RunStateOnByteUnlockedEPNS0_5StateEi */
/* _ZN3re23DFA6SearchESt17basic_string_viewIcSt11char_traitsIcEES4_bbbPbPPKcPNS_10SparseSetTIvEE */
/* _ZN3re23DFA8RWLocker14LockForWritingEv */
/* _ZN3re23DFA8RWLockerC1EPN4absl12lts_202608175MutexE */
/* _ZN3re23DFA8RWLockerC2EPN4absl12lts_202608175MutexE */
/* _ZN3re23DFA8RWLockerD1Ev */
/* _ZN3re23DFA8RWLockerD2Ev */
/* _ZN3re23DFA9DumpStateB5cxx11EPNS0_5StateE */
/* _ZN3re23DFA9DumpWorkqB5cxx11EPNS0_5WorkqE */
/* _ZN3re23DFA9SearchFFFEPNS0_12SearchParamsE */
/* _ZN3re23DFA9SearchFFTEPNS0_12SearchParamsE */
/* _ZN3re23DFA9SearchFTFEPNS0_12SearchParamsE */
/* _ZN3re23DFA9SearchFTTEPNS0_12SearchParamsE */
/* _ZN3re23DFA9SearchTFFEPNS0_12SearchParamsE */
/* _ZN3re23DFA9SearchTFTEPNS0_12SearchParamsE */
/* _ZN3re23DFA9SearchTTFEPNS0_12SearchParamsE */
/* _ZN3re23DFA9SearchTTTEPNS0_12SearchParamsE */
/* _ZN3re23DFAC1EPNS_4ProgENS1_9MatchKindEl */
/* _ZN3re23DFAC2EPNS_4ProgENS1_9MatchKindEl */
/* _ZN3re23DFAD1Ev */
/* _ZN3re23DFAD2Ev */
/* _ZN3re23NFA12AddToThreadqEPNS_11SparseArrayIPNS0_6ThreadEEEiiSt17basic_string_viewIcSt11char_traitsIcEEPKcS3_ */
/* _ZN3re23NFA13FormatCaptureB5cxx11EPPKc */
/* _ZN3re23NFA4StepEPNS_11SparseArrayIPNS0_6ThreadEEES5_iSt17basic_string_viewIcSt11char_traitsIcEEPKc */
/* _ZN3re23NFA6SearchESt17basic_string_viewIcSt11char_traitsIcEES4_bbPS4_i */
/* _ZN3re23NFAC1EPNS_4ProgE */
/* _ZN3re23NFAC2EPNS_4ProgE */
/* _ZN3re23NFAD1Ev */
/* _ZN3re23NFAD2Ev */
/* _ZN3re23RE24InitESt17basic_string_viewIcSt11char_traitsIcEERKNS0_7OptionsE */
/* _ZN3re24Prog10CompileSetEPNS_6RegexpENS_3RE26AnchorEl */
/* _ZN3re24Prog10EmptyFlagsESt17basic_string_viewIcSt11char_traitsIcEEPKc */
/* _ZN3re24Prog11DumpByteMapB5cxx11Ev */
/* _ZN3re24Prog12ComputeHintsEPSt6vectorINS0_4InstESaIS2_EEii */
/* _ZN3re24Prog13MarkDominatorEiPNS_11SparseArrayIiEES3_PSt6vectorIS4_IiSaIiEESaIS6_EEPNS_10SparseSetTIvEEPS6_ */
/* _ZN3re24Prog13SearchOnePassESt17basic_string_viewIcSt11char_traitsIcEES4_NS0_6AnchorENS0_9MatchKindEPS4_i */
/* _ZN3re24Prog14BuildEntireDFAENS0_9MatchKindERKSt8functionIFvPKibEE */
/* _ZN3re24Prog14ComputeByteMapEv */
/* _ZN3re24Prog14DumpUnanchoredB5cxx11Ev */
/* _ZN3re24Prog14MarkSuccessorsEPNS_11SparseArrayIiEES3_PSt6vectorIS4_IiSaIiEESaIS6_EEPNS_10SparseSetTIvEEPS6_ */
/* _ZN3re24Prog14SearchBitStateESt17basic_string_viewIcSt11char_traitsIcEES4_NS0_6AnchorENS0_9MatchKindEPS4_i */
/* _ZN3re24Prog18PossibleMatchRangeEPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES7_i */
/* _ZN3re24Prog20ConfigurePrefixAccelERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEb */
/* _ZN3re24Prog20PrefixAccel_ShiftDFAEPKvm */
/* _ZN3re24Prog24PrefixAccel_FrontAndBackEPKvm */
/* _ZN3re24Prog42TESTING_ONLY_set_dfa_should_bail_when_slowEb */
/* _ZN3re24Prog4DumpB5cxx11Ev */
/* _ZN3re24Prog4Inst11InitCaptureEij */
/* _ZN3re24Prog4Inst13InitByteRangeEiiij */
/* _ZN3re24Prog4Inst14InitEmptyWidthENS_7EmptyOpEj */
/* _ZN3re24Prog4Inst4DumpB5cxx11Ev */
/* _ZN3re24Prog4Inst7InitAltEjj */
/* _ZN3re24Prog4Inst7InitNopEj */
/* _ZN3re24Prog4Inst8InitFailEv */
/* _ZN3re24Prog4Inst9InitMatchEi */
/* _ZN3re24Prog6FanoutEPNS_11SparseArrayIiEE */
/* _ZN3re24Prog6GetDFAENS0_9MatchKindE */
/* _ZN3re24Prog7FlattenEv */
/* _ZN3re24Prog8EmitListEiPNS_11SparseArrayIiEEPSt6vectorINS0_4InstESaIS5_EEPNS_10SparseSetTIvEEPS4_IiSaIiEE */
/* _ZN3re24Prog8OptimizeEv */
/* _ZN3re24Prog9DeleteDFAEPNS_3DFAE */
/* _ZN3re24Prog9IsOnePassEv */
/* _ZN3re24Prog9SearchDFAESt17basic_string_viewIcSt11char_traitsIcEES4_NS0_6AnchorENS0_9MatchKindEPS4_PbPNS_10SparseSetTIvEE */
/* _ZN3re24Prog9SearchNFAESt17basic_string_viewIcSt11char_traitsIcEES4_NS0_6AnchorENS0_9MatchKindEPS4_i */
/* _ZN3re24ProgC1Ev */
/* _ZN3re24ProgC2Ev */
/* _ZN3re24ProgD1Ev */
/* _ZN3re24ProgD2Ev */
/* _ZN3re25hooks23GetDFASearchFailureHookEv */
/* _ZN3re25hooks23SetDFASearchFailureHookEPFvRKNS0_16DFASearchFailureEE */
/* _ZN3re25hooks25GetDFAStateCacheResetHookEv */
/* _ZN3re25hooks25SetDFAStateCacheResetHookEPFvRKNS0_18DFAStateCacheResetEE */
/* _ZN3re25hooks7contextE */
/* _ZN3re26Regexp10MimicsPCREEv */
/* _ZN3re26Regexp10NewLiteralEiNS0_10ParseFlagsE */
/* _ZN3re26Regexp10ParseState10DoCollapseENS_8RegexpOpE */
/* _ZN3re26Regexp10ParseState10PushDollarEv */
/* _ZN3re26Regexp10ParseState10PushRegexpEPS0_ */
/* _ZN3re26Regexp10ParseState11DoLeftParenESt17basic_string_viewIcSt11char_traitsIcEE */
/* _ZN3re26Regexp10ParseState11PushLiteralEi */
/* _ZN3re26Regexp10ParseState12DoRightParenEv */
/* _ZN3re26Regexp10ParseState12FinishRegexpEPS0_ */
/* _ZN3re26Regexp10ParseState12ParseCCRangeEPSt17basic_string_viewIcSt11char_traitsIcEEPNS_9RuneRangeES5_PNS_12RegexpStatusE */
/* _ZN3re26Regexp10ParseState12PushRepeatOpENS_8RegexpOpESt17basic_string_viewIcSt11char_traitsIcEEb */
/* _ZN3re26Regexp10ParseState12PushSimpleOpENS_8RegexpOpE */
/* _ZN3re26Regexp10ParseState13DoAlternationEv */
/* _ZN3re26Regexp10ParseState13DoVerticalBarEv */
/* _ZN3re26Regexp10ParseState14ParseCharClassEPSt17basic_string_viewIcSt11char_traitsIcEEPPS0_PNS_12RegexpStatusE */
/* _ZN3re26Regexp10ParseState14ParsePerlFlagsEPSt17basic_string_viewIcSt11char_traitsIcEE */
/* _ZN3re26Regexp10ParseState14PushRepetitionEiiSt17basic_string_viewIcSt11char_traitsIcEEb */
/* _ZN3re26Regexp10ParseState15DoConcatenationEv */
/* _ZN3re26Regexp10ParseState16ParseCCCharacterEPSt17basic_string_viewIcSt11char_traitsIcEEPiS5_PNS_12RegexpStatusE */
/* _ZN3re26Regexp10ParseState16PushWordBoundaryEb */
/* _ZN3re26Regexp10ParseState17MaybeConcatStringEiNS0_10ParseFlagsE */
/* _ZN3re26Regexp10ParseState20DoLeftParenNoCaptureEv */
/* _ZN3re26Regexp10ParseState7PushDotEv */
/* _ZN3re26Regexp10ParseState8DoFinishEv */
/* _ZN3re26Regexp10ParseState8IsMarkerENS_8RegexpOpE */
/* _ZN3re26Regexp10ParseState9PushCaretEv */
/* _ZN3re26Regexp10ParseStateC1ENS0_10ParseFlagsESt17basic_string_viewIcSt11char_traitsIcEEPNS_12RegexpStatusE */
/* _ZN3re26Regexp10ParseStateC2ENS0_10ParseFlagsESt17basic_string_viewIcSt11char_traitsIcEEPNS_12RegexpStatusE */
/* _ZN3re26Regexp10ParseStateD1Ev */
/* _ZN3re26Regexp10ParseStateD2Ev */
/* _ZN3re26Regexp11NumCapturesEv */
/* _ZN3re26Regexp12CaptureNamesB5cxx11Ev */
/* _ZN3re26Regexp12NewCharClassEPNS_9CharClassENS0_10ParseFlagsE */
/* _ZN3re26Regexp12QuickDestroyEv */
/* _ZN3re26Regexp13CompileToProgEl */
/* _ZN3re26Regexp13ComputeSimpleEv */
/* _ZN3re26Regexp13LeadingRegexpEPS0_ */
/* _ZN3re26Regexp13LeadingStringEPS0_PiPNS0_10ParseFlagsE */
/* _ZN3re26Regexp13LiteralStringEPiiNS0_10ParseFlagsE */
/* _ZN3re26Regexp13NamedCapturesB5cxx11Ev */
/* _ZN3re26Regexp14RequiredPrefixEPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEPbPPS0_ */
/* _ZN3re26Regexp14SimplifyRegexpESt17basic_string_viewIcSt11char_traitsIcEENS0_10ParseFlagsEPNSt7__cxx1112basic_stringIcS3_SaIcEEEPNS_12RegexpStatusE */
/* _ZN3re26Regexp15AddRuneToStringEi */
/* _ZN3re26Regexp15StarPlusOrQuestENS_8RegexpOpEPS0_NS0_10ParseFlagsE */
/* _ZN3re26Regexp17AlternateNoFactorEPPS0_iNS0_10ParseFlagsE */
/* _ZN3re26Regexp17ConcatOrAlternateENS_8RegexpOpEPPS0_iNS0_10ParseFlagsEb */
/* _ZN3re26Regexp17FactorAlternationEPPS0_iNS0_10ParseFlagsE */
/* _ZN3re26Regexp19RemoveLeadingRegexpEPS0_ */
/* _ZN3re26Regexp19RemoveLeadingStringEPS0_i */
/* _ZN3re26Regexp20CompileToReverseProgEl */
/* _ZN3re26Regexp22RequiredPrefixForAccelEPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEPb */
/* _ZN3re26Regexp37FUZZING_ONLY_set_maximum_repeat_countEi */
/* _ZN3re26Regexp3RefEv */
/* _ZN3re26Regexp4PlusEPS0_NS0_10ParseFlagsE */
/* _ZN3re26Regexp4StarEPS0_NS0_10ParseFlagsE */
/* _ZN3re26Regexp4SwapEPS0_ */
/* _ZN3re26Regexp5EqualEPS0_S1_ */
/* _ZN3re26Regexp5ParseESt17basic_string_viewIcSt11char_traitsIcEENS0_10ParseFlagsEPNS_12RegexpStatusE */
/* _ZN3re26Regexp5QuestEPS0_NS0_10ParseFlagsE */
/* _ZN3re26Regexp6ConcatEPPS0_iNS0_10ParseFlagsE */
/* _ZN3re26Regexp6DecrefEv */
/* _ZN3re26Regexp6IncrefEv */
/* _ZN3re26Regexp6RepeatEPS0_NS0_10ParseFlagsEii */
/* _ZN3re26Regexp7CaptureEPS0_NS0_10ParseFlagsEi */
/* _ZN3re26Regexp7DestroyEv */
/* _ZN3re26Regexp8SimplifyEv */
/* _ZN3re26Regexp8ToStringB5cxx11Ev */
/* _ZN3re26Regexp9AlternateEPPS0_iNS0_10ParseFlagsE */
/* _ZN3re26Regexp9HaveMatchEiNS0_10ParseFlagsE */
/* _ZN3re26RegexpC1ENS_8RegexpOpENS0_10ParseFlagsE */
/* _ZN3re26RegexpC2ENS_8RegexpOpENS0_10ParseFlagsE */
/* _ZN3re26RegexpD1Ev */
/* _ZN3re26RegexpD2Ev */
/* _ZN3re26utflenEPKc */
/* _ZN3re27runelenEi */
/* _ZN3re27utfruneEPKci */
/* _ZN3re28BitState4PushEiPKc */
/* _ZN3re28BitState6SearchESt17basic_string_viewIcSt11char_traitsIcEES4_bbPS4_i */
/* _ZN3re28BitState9GrowStackEv */
/* _ZN3re28BitState9TrySearchEiPKc */
/* _ZN3re28BitStateC1EPNS_4ProgE */
/* _ZN3re28BitStateC2EPNS_4ProgE */
/* _ZN3re28Compiler10BeginRangeEv */
/* _ZN3re28Compiler10CompileSetEPNS_6RegexpENS_3RE26AnchorEl */
/* _ZN3re28Compiler10EmptyWidthENS_7EmptyOpE */
/* _ZN3re28Compiler10ShortVisitEPNS_6RegexpENS_4FragE */
/* _ZN3re28Compiler12AddRuneRangeEiib */
/* _ZN3re28Compiler13Add_80_10ffffEv */
/* _ZN3re28Compiler13FindByteRangeEii */
/* _ZN3re28Compiler14ByteRangeEqualEii */
/* _ZN3re28Compiler16AddRuneRangeUTF8Eiib */
/* _ZN3re28Compiler18AddRuneRangeLatin1Eiib */
/* _ZN3re28Compiler18AddSuffixRecursiveEii */
/* _ZN3re28Compiler20CachedRuneByteSuffixEhhbi */
/* _ZN3re28Compiler22IsCachedRuneByteSuffixEi */
/* _ZN3re28Compiler22UncachedRuneByteSuffixEhhbi */
/* _ZN3re28Compiler3AltENS_4FragES1_ */
/* _ZN3re28Compiler3CatENS_4FragES1_ */
/* _ZN3re28Compiler3NopEv */
/* _ZN3re28Compiler4CopyENS_4FragE */
/* _ZN3re28Compiler4PlusENS_4FragEb */
/* _ZN3re28Compiler4StarENS_4FragEb */
/* _ZN3re28Compiler5MatchEi */
/* _ZN3re28Compiler5QuestENS_4FragEb */
/* _ZN3re28Compiler5SetupENS_6Regexp10ParseFlagsElNS_3RE26AnchorE */
/* _ZN3re28Compiler6FinishEPNS_6RegexpE */
/* _ZN3re28Compiler7CaptureENS_4FragEi */
/* _ZN3re28Compiler7CompileEPNS_6RegexpEbl */
/* _ZN3re28Compiler7DotStarEv */
/* _ZN3re28Compiler7LiteralEib */
/* _ZN3re28Compiler7NoMatchEv */
/* _ZN3re28Compiler8EndRangeEv */
/* _ZN3re28Compiler8PreVisitEPNS_6RegexpENS_4FragEPb */
/* _ZN3re28Compiler9AddSuffixEi */
/* _ZN3re28Compiler9AllocInstEi */
/* _ZN3re28Compiler9ByteRangeEiib */
/* _ZN3re28Compiler9PostVisitEPNS_6RegexpENS_4FragES3_PS3_i */
/* _ZN3re28CompilerC1Ev */
/* _ZN3re28CompilerC2Ev */
/* _ZN3re28CompilerD0Ev */
/* _ZN3re28CompilerD1Ev */
/* _ZN3re28CompilerD2Ev */
/* _ZN3re28fullruneEPKci */
/* _ZN3re29ApplyFoldEPKNS_8CaseFoldEi */
/* _ZN3re29CharClass3NewEm */
/* _ZN3re29CharClass6DeleteEv */
/* _ZN3re29CharClass6NegateEv */
/* _ZN3re29Prefilter10FromRegexpEPNS_6RegexpE */
/* _ZN3re29Prefilter10FromStringERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE */
/* _ZN3re29Prefilter12CrossProductERKSt3setINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEENS0_13LengthThenLexESaIS7_EESC_PSA_ */
/* _ZN3re29Prefilter17SimplifyStringSetEPSt3setINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEENS0_13LengthThenLexESaIS7_EE */
/* _ZN3re29Prefilter2OrEPS0_S1_ */
/* _ZN3re29Prefilter3AndEPS0_S1_ */
/* _ZN3re29Prefilter4Info11EmptyStringEv */
/* _ZN3re29Prefilter4Info13LiteralLatin1Ei */
/* _ZN3re29Prefilter4Info16AnyCharOrAnyByteEv */
/* _ZN3re29Prefilter4Info3AltEPS1_S2_ */
/* _ZN3re29Prefilter4Info3AndEPS1_S2_ */
/* _ZN3re29Prefilter4Info4PlusEPS1_ */
/* _ZN3re29Prefilter4Info4StarEPS1_ */
/* _ZN3re29Prefilter4Info5QuestEPS1_ */
/* _ZN3re29Prefilter4Info6CClassEPNS_9CharClassEb */
/* _ZN3re29Prefilter4Info6ConcatEPS1_S2_ */
/* _ZN3re29Prefilter4Info6Walker10ShortVisitEPNS_6RegexpEPS1_ */
/* _ZN3re29Prefilter4Info6Walker9PostVisitEPNS_6RegexpEPS1_S5_PS5_i */
/* _ZN3re29Prefilter4Info7LiteralEi */
/* _ZN3re29Prefilter4Info7NoMatchEv */
/* _ZN3re29Prefilter4Info8AnyMatchEv */
/* _ZN3re29Prefilter4Info8ToStringB5cxx11Ev */
/* _ZN3re29Prefilter4Info9TakeMatchEv */
/* _ZN3re29Prefilter4InfoC1Ev */
/* _ZN3re29Prefilter4InfoC2Ev */
/* _ZN3re29Prefilter4InfoD1Ev */
/* _ZN3re29Prefilter4InfoD2Ev */
/* _ZN3re29Prefilter5AndOrENS0_2OpEPS0_S2_ */
/* _ZN3re29Prefilter7FromRE2EPKNS_3RE2E */
/* _ZN3re29Prefilter8SimplifyEv */
/* _ZN3re29Prefilter9BuildInfoEPNS_6RegexpE */
/* _ZN3re29Prefilter9OrStringsEPSt3setINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEENS0_13LengthThenLexESaIS7_EE */
/* _ZN3re29PrefilterC1ENS0_2OpE */
/* _ZN3re29PrefilterC2ENS0_2OpE */
/* _ZN3re29PrefilterD1Ev */
/* _ZN3re29PrefilterD2Ev */
/* _ZN4absl12lts_2026081713hash_internal17kStaticRandomDataE */
/* _ZN4absl12lts_2026081718container_internal12raw_hash_setINS1_17FlatHashMapPolicyIPN3re23DFA5StateEiEEJEE46transfer_unprobed_elements_to_next_capacity_fnERNS1_12CommonFieldsEPKNS1_6ctrl_tEPvSF_PFvSF_hmmE */
/* _ZN4absl12lts_2026081718container_internal12raw_hash_setINS1_17FlatHashMapPolicyIPN3re26RegexpEiEEJEE46transfer_unprobed_elements_to_next_capacity_fnERNS1_12CommonFieldsEPKNS1_6ctrl_tEPvSE_PFvSE_hmmE */
/* _ZN4absl12lts_2026081718container_internal12raw_hash_setINS1_17FlatHashMapPolicyImiEEJEE46transfer_unprobed_elements_to_next_capacity_fnERNS1_12CommonFieldsEPKNS1_6ctrl_tEPvSB_PFvSB_hmmE */
/* _ZN4absl12lts_2026081718container_internal12raw_hash_setINS1_17FlatHashSetPolicyIPN3re23DFA5StateEEEJNS5_9StateHashENS5_10StateEqualEEE46transfer_unprobed_elements_to_next_capacity_fnERNS1_12CommonFieldsEPKNS1_6ctrl_tEPvSH_PFvSH_hmmE */
/* _ZN4absl12lts_2026081718container_internal12raw_hash_setINS1_17FlatHashSetPolicyIPN3re29PrefilterEEEJNS4_13PrefilterTree13PrefilterHashENS8_14PrefilterEqualEEE46transfer_unprobed_elements_to_next_capacity_fnERNS1_12CommonFieldsEPKNS1_6ctrl_tEPvSH_PFvSH_hmmE */
/* _ZN4absl12lts_2026081718container_internal20TransferNRelocatableILm16EEEvPvS3_S3_m */
/* _ZN4absl12lts_2026081718container_internal20TransferNRelocatableILm8EEEvPvS3_S3_m */
/* _ZN4absl12lts_2026081718container_internal23TypeErasedApplyToSlotFnIN3re213PrefilterTree13PrefilterHashEPNS3_9PrefilterELb0EEEmPKvPvm */
/* _ZN4absl12lts_2026081718container_internal23TypeErasedApplyToSlotFnIN3re23DFA9StateHashEPNS4_5StateELb0EEEmPKvPvm */
/* _ZN4absl12lts_2026081718container_internal23TypeErasedApplyToSlotFnINS0_13hash_internal4HashImEEmLb1EEEmPKvPvm */
/* _ZN4absl12lts_2026081718container_internal23TypeErasedApplyToSlotFnINS1_6HashEqIPN3re23DFA5StateEvE4HashES7_Lb0EEEmPKvPvm */
/* _ZN4absl12lts_2026081718container_internal23TypeErasedApplyToSlotFnINS1_6HashEqIPN3re26RegexpEvE4HashES6_Lb0EEEmPKvPvm */
/* _ZN4absl12lts_2026081719functional_internal12InvokeObjectIRKZNS0_18container_internal12raw_hash_setINS3_17FlatHashMapPolicyIPN3re23DFA5StateEiEEJEE22AssertHashEqConsistentIS9_EEvRKT_EUlPKNS3_6ctrl_tEPvE_vJSI_SJ_EEET0_NS1_7VoidPtrEDpNS1_8ForwardTIT1_E4typeE */
/* _ZN4absl12lts_2026081719functional_internal12InvokeObjectIRKZNS0_18container_internal12raw_hash_setINS3_17FlatHashMapPolicyIPN3re26RegexpEiEEJEE22AssertHashEqConsistentIS8_EEvRKT_EUlPKNS3_6ctrl_tEPvE_vJSH_SI_EEET0_NS1_7VoidPtrEDpNS1_8ForwardTIT1_E4typeE */
/* _ZN4absl12lts_2026081719functional_internal12InvokeObjectIRKZNS0_18container_internal12raw_hash_setINS3_17FlatHashSetPolicyIPN3re23DFA5StateEEEJNS7_9StateHashENS7_10StateEqualEEE22AssertHashEqConsistentIS9_EEvRKT_EUlPKNS3_6ctrl_tEPvE_vJSK_SL_EEET0_NS1_7VoidPtrEDpNS1_8ForwardTIT1_E4typeE */
/* _ZN4absl12lts_2026081719functional_internal12InvokeObjectIRKZNS0_18container_internal12raw_hash_setINS3_17FlatHashSetPolicyIPN3re29PrefilterEEEJNS6_13PrefilterTree13PrefilterHashENSA_14PrefilterEqualEEE22AssertHashEqConsistentIS8_EEvRKT_EUlPKNS3_6ctrl_tEPvE_vJSK_SL_EEET0_NS1_7VoidPtrEDpNS1_8ForwardTIT1_E4typeE */
/* _ZN4absl12lts_2026081719functional_internal12InvokeObjectIRNS0_18container_internal7HashKeyIN3re213PrefilterTree13PrefilterHashEPNS5_9PrefilterELb0EEEmJmEEET0_NS1_7VoidPtrEDpNS1_8ForwardTIT1_E4typeE */
/* _ZN4absl12lts_2026081719functional_internal12InvokeObjectIRNS0_18container_internal7HashKeyIN3re23DFA9StateHashEPNS6_5StateELb0EEEmJmEEET0_NS1_7VoidPtrEDpNS1_8ForwardTIT1_E4typeE */
/* _ZN4absl12lts_2026081719functional_internal12InvokeObjectIRNS0_18container_internal7HashKeyINS0_13hash_internal4HashImEEmLb1EEEmJmEEET0_NS1_7VoidPtrEDpNS1_8ForwardTIT1_E4typeE */
/* _ZN4absl12lts_2026081719functional_internal12InvokeObjectIRNS0_18container_internal7HashKeyINS3_6HashEqIPN3re23DFA5StateEvE4HashES9_Lb0EEEmJmEEET0_NS1_7VoidPtrEDpNS1_8ForwardTIT1_E4typeE */
/* _ZN4absl12lts_2026081719functional_internal12InvokeObjectIRNS0_18container_internal7HashKeyINS3_6HashEqIPN3re26RegexpEvE4HashES8_Lb0EEEmJmEEET0_NS1_7VoidPtrEDpNS1_8ForwardTIT1_E4typeE */
/* _ZNK3re212RegexpStatus4TextB5cxx11Ev */
/* _ZNK3re213PrefilterTree14PropagateMatchERKSt6vectorIiSaIiEEPNS_11SparseArrayIiEE */
/* _ZNK3re213PrefilterTree15DebugNodeStringB5cxx11EPNS_9PrefilterE */
/* _ZNK3re213PrefilterTree19RegexpsGivenStringsERKSt6vectorIiSaIiEEPS3_ */
/* _ZNK3re213PrefilterTree8KeepNodeEPNS_9PrefilterE */
/* _ZNK3re23RE211ReverseProgEv */
/* _ZNK3re23RE27DoMatchESt17basic_string_viewIcSt11char_traitsIcEENS0_6AnchorEPmPKPKNS0_3ArgEi */
/* _ZNK3re29Bitmap25614FindNextSetBitEi */
/* _ZNK3re29CharClass8ContainsEi */
/* _ZNK3re29Prefilter11DebugStringB5cxx11Ev */
/* _ZSt19piecewise_construct */
/* _ZTIN3re210PCREWalkerE */
/* _ZTIN3re214CoalesceWalkerE */
/* _ZTIN3re214SimplifyWalkerE */
/* _ZTIN3re214ToStringWalkerE */
/* _ZTIN3re216RepetitionWalkerE */
/* _ZTIN3re217EmptyStringWalkerE */
/* _ZTIN3re26Regexp6WalkerINS_4FragEEE */
/* _ZTIN3re26Regexp6WalkerIPNS_9Prefilter4InfoEEE */
/* _ZTIN3re26Regexp6WalkerIPS0_EE */
/* _ZTIN3re26Regexp6WalkerIbEE */
/* _ZTIN3re26Regexp6WalkerIiEE */
/* _ZTIN3re28CompilerE */
/* _ZTIN3re29Prefilter4Info6WalkerE */
/* _ZTSN3re210PCREWalkerE */
/* _ZTSN3re214CoalesceWalkerE */
/* _ZTSN3re214SimplifyWalkerE */
/* _ZTSN3re214ToStringWalkerE */
/* _ZTSN3re216RepetitionWalkerE */
/* _ZTSN3re217EmptyStringWalkerE */
/* _ZTSN3re217NumCapturesWalkerE */
/* _ZTSN3re218CaptureNamesWalkerE */
/* _ZTSN3re219NamedCapturesWalkerE */
/* _ZTSN3re26Regexp6WalkerINS_4FragEEE */
/* _ZTSN3re26Regexp6WalkerIPNS_9Prefilter4InfoEEE */
/* _ZTSN3re26Regexp6WalkerIPS0_EE */
/* _ZTSN3re26Regexp6WalkerIbEE */
/* _ZTSN3re26Regexp6WalkerIiEE */
/* _ZTSN3re28CompilerE */
/* _ZTSN3re29Prefilter4Info6WalkerE */
/* _ZTVN3re210PCREWalkerE */
/* _ZTVN3re214CoalesceWalkerE */
/* _ZTVN3re214SimplifyWalkerE */
/* _ZTVN3re214ToStringWalkerE */
/* _ZTVN3re216RepetitionWalkerE */
/* _ZTVN3re217EmptyStringWalkerE */
/* _ZTVN3re28CompilerE */
/* _ZTVN3re29Prefilter4Info6WalkerE */
/* _ZZN4absl12lts_2026081718container_internal12raw_hash_setINS1_17FlatHashMapPolicyIPN3re23DFA5StateEiEEJEE18GetPolicyFunctionsEvE5value */
/* _ZZN4absl12lts_2026081718container_internal12raw_hash_setINS1_17FlatHashMapPolicyIPN3re26RegexpEiEEJEE18GetPolicyFunctionsEvE5value */
/* _ZZN4absl12lts_2026081718container_internal12raw_hash_setINS1_17FlatHashMapPolicyImiEEJEE18GetPolicyFunctionsEvE5value */
/* _ZZN4absl12lts_2026081718container_internal12raw_hash_setINS1_17FlatHashSetPolicyIPN3re23DFA5StateEEEJNS5_9StateHashENS5_10StateEqualEEE18GetPolicyFunctionsEvE5value */
/* _ZZN4absl12lts_2026081718container_internal12raw_hash_setINS1_17FlatHashSetPolicyIPN3re29PrefilterEEEJNS4_13PrefilterTree13PrefilterHashENS8_14PrefilterEqualEEE18GetPolicyFunctionsEvE5value */
