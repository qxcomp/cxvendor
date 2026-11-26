/* ============================================================
 * tinyxml2_dyn.c - type safe dynamic lib load for tinyxml2 11
 * ============================================================
 * 设计要点：
 *   1. 单文件、无 .h、无 wrapper 层；链接期不需要目标库。
 *   tinyxml2 是纯 C++ 类库，libtinyxml2.so 的 220 个导出符号
 *   全部是 Itanium ABI mangled 成员函数，没有 C API。本文件用
 *   纯 C 直接 dlsym mangled 名，成员函数的 this 作为首参 void*
 *   传入，const 方法首参 const void*。
 *
 *   ABI 映射约定：
 *     bool            -> unsigned char（x86-64 返回值只在 AL）
 *     enum            -> int
 *     Class&/Class*   -> void*
 *     FILE*           -> FILE*
 *     size_t          -> size_t
 *
 *   ABI 绑定：tinyxml2 11.x / x86-64（实测布局）
 *     XMLDocument 880  XMLPrinter 328  XMLElement 120
 *     XMLAttribute 80  XMLNode 104
 *   分配策略：统一 4096 字节 arena + placement 构造，不依赖精确
 *   sizeof；tinyxml2_dyn_sizeof_*() 仅作调用方参考。
 *
 *   2. 每个函数首次调用时 dlsym 缓存；失败打印 stderr 并返回安全值。
 *   3. 加载顺序：显式路径 > TINYXML2_DYN_LIB > LD_LIBRARY_PATH >
 *      soname > .so > Nix > 平台默认；成功后不卸载。
 *   init 成功后自动跑 selftest（ctor->Parse("<a/>")->FirstChildElement
 *   ->Value->dtor），失败则拒绝初始化；TINYXML2_DYN_FORCE=1 强制，
 *   TINYXML2_DYN_NO_SELFTEST=1 跳过。
 *
 *   使用：
 *     tinyxml2_dyn_init(NULL);
 *     void *doc = tinyxml2_dyn_document_new();
 *     tinyxml2_XMLDocument_Parse_mem(doc, "<a b='1'/>", 10);
 *     ...
 *     tinyxml2_dyn_document_free(doc);
 *     tinyxml2_dyn_cleanup();   // 仅复位加载状态，不卸载库
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>

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

/* ============================================================
 * 对象 arena（不依赖精确 sizeof，远大于实测最大 880 字节）
 * ============================================================ */
#define TINYXML2_OBJ_ARENA 4096

size_t tinyxml2_dyn_sizeof_XMLDocument(void)  { return 880; }
size_t tinyxml2_dyn_sizeof_XMLPrinter(void)   { return 328; }
size_t tinyxml2_dyn_sizeof_XMLElement(void)   { return 120; }
size_t tinyxml2_dyn_sizeof_XMLAttribute(void) { return 80; }
size_t tinyxml2_dyn_sizeof_XMLNode(void)      { return 104; }

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
#define TINYXML2_ANCHOR "_ZN8tinyxml211XMLDocumentC1EbNS_10WhitespaceE"

static lib_handle_t tinyxml2_handle = NULL;
static int tinyxml2_init_state = 0;

int tinyxml2_dyn_selftest(void);

static void *tinyxml2_sym(const char *name) {
    return tinyxml2_handle ? get_sym(tinyxml2_handle, name) : NULL;
}

static int tinyxml2_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    int n = 0, i;

    if (tinyxml2_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("TINYXML2_DYN_LIB");
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
                char *q = copy;
                while (q && *q && n < 80) {
                    char *sep = strchr(q, PATH_SEP);
                    if (sep) *sep = 0;
                    if (*q) {
                        snprintf(bufs[n], sizeof bufs[0], "%s/libtinyxml2.so.11", q);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/libtinyxml2.so", q);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = "libtinyxml2.so.11";
    candidates[n++] = "libtinyxml2.so";
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libtinyxml2.so.11", home);
            candidates[n] = bufs[n]; n++;
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libtinyxml2.so", home);
            candidates[n] = bufs[n]; n++;
        }
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libtinyxml2.so.11";
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libtinyxml2.so";
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libtinyxml2.so.11";
    candidates[n++] = "/usr/local/lib/libtinyxml2.so.11";
    candidates[n++] = "/opt/homebrew/lib/libtinyxml2.so";
    candidates[n++] = "/usr/local/lib/libtinyxml2.so";
#elif defined(_WIN32)
    candidates[n++] = "tinyxml2.dll";
#else
    candidates[n++] = "/usr/local/lib/libtinyxml2.so.11";
    candidates[n++] = "/usr/lib/libtinyxml2.so.11";
    candidates[n++] = "/usr/local/lib/libtinyxml2.so";
    candidates[n++] = "/usr/lib/libtinyxml2.so";
#endif

    for (i = 0; i < n; i++) {
        tinyxml2_handle = load_lib(candidates[i]);
        if (tinyxml2_handle) break;
    }
    if (!tinyxml2_handle || !tinyxml2_sym(TINYXML2_ANCHOR)) {
        fprintf(stderr, "tinyxml2_dyn: cannot load tinyxml2 (%s)\n", lib_error());
        if (tinyxml2_handle) { free_lib(tinyxml2_handle); tinyxml2_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "tinyxml2_dyn: loaded %d %s (11 headers)\n", i, candidates[i]);

    if (!getenv("TINYXML2_DYN_NO_SELFTEST")) {
        if (tinyxml2_dyn_selftest() != 0) {
            fprintf(stderr, "tinyxml2_dyn: selftest failed\n");
            if (!getenv("TINYXML2_DYN_FORCE")) {
                free_lib(tinyxml2_handle);
                tinyxml2_handle = NULL;
                return -1;
            }
            fprintf(stderr, "tinyxml2_dyn: TINYXML2_DYN_FORCE set, continuing\n");
        }
    }
    return 0;
}

int tinyxml2_libok(void) {
    if (tinyxml2_init_state == 0)
        tinyxml2_init_state = (tinyxml2_do_load(NULL) == 0) ? 1 : -1;
    return (tinyxml2_init_state == 1 && tinyxml2_handle != NULL) ? 1 : 0;
}

int tinyxml2_dyn_init(const char *library_path) {
    if (tinyxml2_do_load(library_path) == 0) {
        tinyxml2_init_state = 1;
        return 0;
    }
    if (tinyxml2_init_state == 0) tinyxml2_init_state = -1;
    return -1;
}

void tinyxml2_dyn_cleanup(void) {
    /* 仅复位加载状态，不卸载已成功加载的库（dlclose 不安全） */
    if (!tinyxml2_handle) tinyxml2_init_state = 0;
    else if (tinyxml2_init_state == -1) tinyxml2_init_state = 0;
}

int tinyxml2_dyn_has_symbol(const char *name) {
    if (!tinyxml2_libok()) return 0;
    return tinyxml2_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define TX2_RET(ret, name, mangled, params, args, fallback)                 \
    ret name params {                                                       \
        static ret (*fn) params = NULL;                                     \
        if (!fn) {                                                          \
            if (!tinyxml2_libok()) return fallback;                         \
            *(void **)(&fn) = tinyxml2_sym(mangled);                        \
            if (!fn) {                                                      \
                fprintf(stderr, "tinyxml2_dyn: dlsym failed: %s\n", mangled); \
                return fallback;                                            \
            }                                                               \
        }                                                                   \
        return fn args;                                                     \
    }

#define TX2_VOID(name, mangled, params, args)                               \
    void name params {                                                      \
        static void (*fn) params = NULL;                                    \
        if (!fn) {                                                          \
            if (!tinyxml2_libok()) return;                                  \
            *(void **)(&fn) = tinyxml2_sym(mangled);                        \
            if (!fn) {                                                      \
                fprintf(stderr, "tinyxml2_dyn: dlsym failed: %s\n", mangled); \
                return;                                                     \
            }                                                               \
        }                                                                   \
        fn args;                                                            \
    }

/* ============================================================
 * StrPair（9）
 * ============================================================ */
TX2_VOID(tinyxml2_StrPair_TransferTo, "_ZN8tinyxml27StrPair10TransferToEPS0_", (void *a0, void *a1), (a0, a1))
TX2_VOID(tinyxml2_StrPair_CollapseWhitespace, "_ZN8tinyxml27StrPair18CollapseWhitespaceEv", (void *a0), (a0))
TX2_VOID(tinyxml2_StrPair_Reset, "_ZN8tinyxml27StrPair5ResetEv", (void *a0), (a0))
TX2_RET(char *, tinyxml2_StrPair_GetStr, "_ZN8tinyxml27StrPair6GetStrEv", (void *a0), (a0), NULL)
TX2_VOID(tinyxml2_StrPair_SetStr, "_ZN8tinyxml27StrPair6SetStrEPKci", (void *a0, const char *a1, int a2), (a0, a1, a2))
TX2_RET(char *, tinyxml2_StrPair_ParseName, "_ZN8tinyxml27StrPair9ParseNameEPc", (void *a0, char *a1), (a0, a1), NULL)
TX2_RET(char *, tinyxml2_StrPair_ParseText, "_ZN8tinyxml27StrPair9ParseTextEPcPKciPi", (void *a0, char *a1, const char *a2, int a3, int *a4), (a0, a1, a2, a3, a4), NULL)
TX2_VOID(tinyxml2_StrPair_dtor_d1, "_ZN8tinyxml27StrPairD1Ev", (void *a0), (a0))
TX2_VOID(tinyxml2_StrPair_dtor_d2, "_ZN8tinyxml27StrPairD2Ev", (void *a0), (a0))

/* ============================================================
 * XMLAttribute（19）
 * ============================================================ */
TX2_VOID(tinyxml2_XMLAttribute_SetAttribute_bool, "_ZN8tinyxml212XMLAttribute12SetAttributeEb", (void *a0, unsigned char a1), (a0, a1))
TX2_VOID(tinyxml2_XMLAttribute_SetAttribute_double, "_ZN8tinyxml212XMLAttribute12SetAttributeEd", (void *a0, double a1), (a0, a1))
TX2_VOID(tinyxml2_XMLAttribute_SetAttribute_float, "_ZN8tinyxml212XMLAttribute12SetAttributeEf", (void *a0, float a1), (a0, a1))
TX2_VOID(tinyxml2_XMLAttribute_SetAttribute_int, "_ZN8tinyxml212XMLAttribute12SetAttributeEi", (void *a0, int a1), (a0, a1))
TX2_VOID(tinyxml2_XMLAttribute_SetAttribute_uint, "_ZN8tinyxml212XMLAttribute12SetAttributeEj", (void *a0, unsigned int a1), (a0, a1))
TX2_VOID(tinyxml2_XMLAttribute_SetAttribute_long, "_ZN8tinyxml212XMLAttribute12SetAttributeEl", (void *a0, long a1), (a0, a1))
TX2_VOID(tinyxml2_XMLAttribute_SetAttribute_ulong, "_ZN8tinyxml212XMLAttribute12SetAttributeEm", (void *a0, unsigned long a1), (a0, a1))
TX2_VOID(tinyxml2_XMLAttribute_SetAttribute_cstr, "_ZN8tinyxml212XMLAttribute12SetAttributeEPKc", (void *a0, const char *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLAttribute_SetName, "_ZN8tinyxml212XMLAttribute7SetNameEPKc", (void *a0, const char *a1), (a0, a1))
TX2_RET(unsigned char, tinyxml2_XMLAttribute_ParseDeep, "_ZN8tinyxml212XMLAttribute9ParseDeepEPcbPi", (void *a0, char *a1, unsigned char a2, int *a3), (a0, a1, a2, a3), 0)
TX2_RET(unsigned char, tinyxml2_XMLAttribute_QueryBoolValue, "_ZNK8tinyxml212XMLAttribute14QueryBoolValueEPb", (const void *a0, unsigned char *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLAttribute_QueryDoubleValue, "_ZNK8tinyxml212XMLAttribute16QueryDoubleValueEPd", (const void *a0, double *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLAttribute_QueryFloatValue, "_ZNK8tinyxml212XMLAttribute15QueryFloatValueEPf", (const void *a0, float *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLAttribute_QueryInt64Value, "_ZNK8tinyxml212XMLAttribute15QueryInt64ValueEPl", (const void *a0, long *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLAttribute_QueryIntValue, "_ZNK8tinyxml212XMLAttribute13QueryIntValueEPi", (const void *a0, int *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLAttribute_QueryUnsigned64Value, "_ZNK8tinyxml212XMLAttribute20QueryUnsigned64ValueEPm", (const void *a0, unsigned long *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLAttribute_QueryUnsignedValue, "_ZNK8tinyxml212XMLAttribute18QueryUnsignedValueEPj", (const void *a0, unsigned int *a1), (a0, a1), 0)
TX2_RET(const char *, tinyxml2_XMLAttribute_Name, "_ZNK8tinyxml212XMLAttribute4NameEv", (const void *a0), (a0), NULL)
TX2_RET(const char *, tinyxml2_XMLAttribute_Value, "_ZNK8tinyxml212XMLAttribute5ValueEv", (const void *a0), (a0), NULL)

/* ============================================================
 * XMLComment（9）
 * ============================================================ */
TX2_RET(unsigned char, tinyxml2_XMLComment_ParseDeep, "_ZN8tinyxml210XMLComment9ParseDeepEPcPNS_7StrPairEPi", (void *a0, char *a1, void *a2, int *a3), (a0, a1, a2, a3), 0)
TX2_VOID(tinyxml2_XMLComment_ctor_c1, "_ZN8tinyxml210XMLCommentC1EPNS_11XMLDocumentE", (void *a0, void *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLComment_ctor_c2, "_ZN8tinyxml210XMLCommentC2EPNS_11XMLDocumentE", (void *a0, void *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLComment_dtor_d0, "_ZN8tinyxml210XMLCommentD0Ev", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLComment_dtor_d1, "_ZN8tinyxml210XMLCommentD1Ev", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLComment_dtor_d2, "_ZN8tinyxml210XMLCommentD2Ev", (void *a0), (a0))
TX2_RET(void *, tinyxml2_XMLComment_ShallowClone, "_ZNK8tinyxml210XMLComment12ShallowCloneEPNS_11XMLDocumentE", (const void *a0, void *a1), (a0, a1), NULL)
TX2_RET(unsigned char, tinyxml2_XMLComment_ShallowEqual, "_ZNK8tinyxml210XMLComment12ShallowEqualEPKNS_7XMLNodeE", (const void *a0, const void *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLComment_Accept, "_ZNK8tinyxml210XMLComment6AcceptEPNS_10XMLVisitorE", (const void *a0, void *a1), (a0, a1), 0)

/* ============================================================
 * XMLText（4）
 * ============================================================ */
TX2_RET(unsigned char, tinyxml2_XMLText_ParseDeep, "_ZN8tinyxml27XMLText9ParseDeepEPcPNS_7StrPairEPi", (void *a0, char *a1, void *a2, int *a3), (a0, a1, a2, a3), 0)
TX2_RET(void *, tinyxml2_XMLText_ShallowClone, "_ZNK8tinyxml27XMLText12ShallowCloneEPNS_11XMLDocumentE", (const void *a0, void *a1), (a0, a1), NULL)
TX2_RET(unsigned char, tinyxml2_XMLText_ShallowEqual, "_ZNK8tinyxml27XMLText12ShallowEqualEPKNS_7XMLNodeE", (const void *a0, const void *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLText_Accept, "_ZNK8tinyxml27XMLText6AcceptEPNS_10XMLVisitorE", (const void *a0, void *a1), (a0, a1), 0)

/* ============================================================
 * XMLDeclaration（9）
 * ============================================================ */
TX2_RET(unsigned char, tinyxml2_XMLDeclaration_ParseDeep, "_ZN8tinyxml214XMLDeclaration9ParseDeepEPcPNS_7StrPairEPi", (void *a0, char *a1, void *a2, int *a3), (a0, a1, a2, a3), 0)
TX2_VOID(tinyxml2_XMLDeclaration_ctor_c1, "_ZN8tinyxml214XMLDeclarationC1EPNS_11XMLDocumentE", (void *a0, void *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLDeclaration_ctor_c2, "_ZN8tinyxml214XMLDeclarationC2EPNS_11XMLDocumentE", (void *a0, void *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLDeclaration_dtor_d0, "_ZN8tinyxml214XMLDeclarationD0Ev", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLDeclaration_dtor_d1, "_ZN8tinyxml214XMLDeclarationD1Ev", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLDeclaration_dtor_d2, "_ZN8tinyxml214XMLDeclarationD2Ev", (void *a0), (a0))
TX2_RET(void *, tinyxml2_XMLDeclaration_ShallowClone, "_ZNK8tinyxml214XMLDeclaration12ShallowCloneEPNS_11XMLDocumentE", (const void *a0, void *a1), (a0, a1), NULL)
TX2_RET(unsigned char, tinyxml2_XMLDeclaration_ShallowEqual, "_ZNK8tinyxml214XMLDeclaration12ShallowEqualEPKNS_7XMLNodeE", (const void *a0, const void *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLDeclaration_Accept, "_ZNK8tinyxml214XMLDeclaration6AcceptEPNS_10XMLVisitorE", (const void *a0, void *a1), (a0, a1), 0)

/* ============================================================
 * XMLUnknown（9）
 * ============================================================ */
TX2_RET(unsigned char, tinyxml2_XMLUnknown_ParseDeep, "_ZN8tinyxml210XMLUnknown9ParseDeepEPcPNS_7StrPairEPi", (void *a0, char *a1, void *a2, int *a3), (a0, a1, a2, a3), 0)
TX2_VOID(tinyxml2_XMLUnknown_ctor_c1, "_ZN8tinyxml210XMLUnknownC1EPNS_11XMLDocumentE", (void *a0, void *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLUnknown_ctor_c2, "_ZN8tinyxml210XMLUnknownC2EPNS_11XMLDocumentE", (void *a0, void *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLUnknown_dtor_d0, "_ZN8tinyxml210XMLUnknownD0Ev", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLUnknown_dtor_d1, "_ZN8tinyxml210XMLUnknownD1Ev", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLUnknown_dtor_d2, "_ZN8tinyxml210XMLUnknownD2Ev", (void *a0), (a0))
TX2_RET(void *, tinyxml2_XMLUnknown_ShallowClone, "_ZNK8tinyxml210XMLUnknown12ShallowCloneEPNS_11XMLDocumentE", (const void *a0, void *a1), (a0, a1), NULL)
TX2_RET(unsigned char, tinyxml2_XMLUnknown_ShallowEqual, "_ZNK8tinyxml210XMLUnknown12ShallowEqualEPKNS_7XMLNodeE", (const void *a0, const void *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLUnknown_Accept, "_ZNK8tinyxml210XMLUnknown6AcceptEPNS_10XMLVisitorE", (const void *a0, void *a1), (a0, a1), 0)

/* ============================================================
 * XMLElement 非 const（24）
 * ============================================================ */
TX2_RET(void *, tinyxml2_XMLElement_InsertNewText, "_ZN8tinyxml210XMLElement13InsertNewTextEPKc", (void *a0, const char *a1), (a0, a1), NULL)
TX2_RET(void *, tinyxml2_XMLElement_CreateAttribute, "_ZN8tinyxml210XMLElement15CreateAttributeEv", (void *a0), (a0), NULL)
TX2_VOID(tinyxml2_XMLElement_DeleteAttribute_cstr, "_ZN8tinyxml210XMLElement15DeleteAttributeEPKc", (void *a0, const char *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLElement_DeleteAttribute_attr, "_ZN8tinyxml210XMLElement15DeleteAttributeEPNS_12XMLAttributeE", (void *a0), (a0))
TX2_RET(char *, tinyxml2_XMLElement_ParseAttributes, "_ZN8tinyxml210XMLElement15ParseAttributesEPcPi", (void *a0, char *a1, int *a2), (a0, a1, a2), NULL)
TX2_RET(void *, tinyxml2_XMLElement_InsertNewComment, "_ZN8tinyxml210XMLElement16InsertNewCommentEPKc", (void *a0, const char *a1), (a0, a1), NULL)
TX2_RET(void *, tinyxml2_XMLElement_InsertNewUnknown, "_ZN8tinyxml210XMLElement16InsertNewUnknownEPKc", (void *a0, const char *a1), (a0, a1), NULL)
TX2_RET(void *, tinyxml2_XMLElement_InsertNewDeclaration, "_ZN8tinyxml210XMLElement20InsertNewDeclarationEPKc", (void *a0, const char *a1), (a0, a1), NULL)
TX2_RET(void *, tinyxml2_XMLElement_FindOrCreateAttribute, "_ZN8tinyxml210XMLElement21FindOrCreateAttributeEPKc", (void *a0, const char *a1), (a0, a1), NULL)
TX2_RET(void *, tinyxml2_XMLElement_InsertNewChildElement, "_ZN8tinyxml210XMLElement21InsertNewChildElementEPKc", (void *a0, const char *a1), (a0, a1), NULL)
TX2_VOID(tinyxml2_XMLElement_SetText_bool, "_ZN8tinyxml210XMLElement7SetTextEb", (void *a0, unsigned char a1), (a0, a1))
TX2_VOID(tinyxml2_XMLElement_SetText_double, "_ZN8tinyxml210XMLElement7SetTextEd", (void *a0, double a1), (a0, a1))
TX2_VOID(tinyxml2_XMLElement_SetText_float, "_ZN8tinyxml210XMLElement7SetTextEf", (void *a0, float a1), (a0, a1))
TX2_VOID(tinyxml2_XMLElement_SetText_int, "_ZN8tinyxml210XMLElement7SetTextEi", (void *a0, int a1), (a0, a1))
TX2_VOID(tinyxml2_XMLElement_SetText_uint, "_ZN8tinyxml210XMLElement7SetTextEj", (void *a0, unsigned int a1), (a0, a1))
TX2_VOID(tinyxml2_XMLElement_SetText_long, "_ZN8tinyxml210XMLElement7SetTextEl", (void *a0, long a1), (a0, a1))
TX2_VOID(tinyxml2_XMLElement_SetText_ulong, "_ZN8tinyxml210XMLElement7SetTextEm", (void *a0, unsigned long a1), (a0, a1))
TX2_VOID(tinyxml2_XMLElement_SetText_cstr, "_ZN8tinyxml210XMLElement7SetTextEPKc", (void *a0, const char *a1), (a0, a1))
TX2_RET(unsigned char, tinyxml2_XMLElement_ParseDeep, "_ZN8tinyxml210XMLElement9ParseDeepEPcPNS_7StrPairEPi", (void *a0, char *a1, void *a2, int *a3), (a0, a1, a2, a3), 0)
TX2_VOID(tinyxml2_XMLElement_ctor_c1, "_ZN8tinyxml210XMLElementC1EPNS_11XMLDocumentE", (void *a0, void *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLElement_ctor_c2, "_ZN8tinyxml210XMLElementC2EPNS_11XMLDocumentE", (void *a0, void *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLElement_dtor_d0, "_ZN8tinyxml210XMLElementD0Ev", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLElement_dtor_d1, "_ZN8tinyxml210XMLElementD1Ev", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLElement_dtor_d2, "_ZN8tinyxml210XMLElementD2Ev", (void *a0), (a0))

/* ============================================================
 * XMLElement const（27）
 * ============================================================ */
TX2_RET(double, tinyxml2_XMLElement_DoubleText, "_ZNK8tinyxml210XMLElement10DoubleTextEd", (const void *a0, double a1), (a0, a1), 0.0)
TX2_RET(int, tinyxml2_XMLElement_IntAttribute, "_ZNK8tinyxml210XMLElement12IntAttributeEPKci", (const void *a0, const char *a1, int a2), (a0, a1, a2), 0)
TX2_RET(unsigned char, tinyxml2_XMLElement_QueryIntText, "_ZNK8tinyxml210XMLElement12QueryIntTextEPi", (const void *a0, int *a1), (a0, a1), 0)
TX2_RET(void *, tinyxml2_XMLElement_ShallowClone, "_ZNK8tinyxml210XMLElement12ShallowCloneEPNS_11XMLDocumentE", (const void *a0, void *a1), (a0, a1), NULL)
TX2_RET(unsigned char, tinyxml2_XMLElement_ShallowEqual, "_ZNK8tinyxml210XMLElement12ShallowEqualEPKNS_7XMLNodeE", (const void *a0, const void *a1), (a0, a1), 0)
TX2_RET(unsigned int, tinyxml2_XMLElement_UnsignedText, "_ZNK8tinyxml210XMLElement12UnsignedTextEj", (const void *a0, unsigned int a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLElement_BoolAttribute, "_ZNK8tinyxml210XMLElement13BoolAttributeEPKcb", (const void *a0, const char *a1, unsigned char a2), (a0, a1, a2), 0)
TX2_RET(void *, tinyxml2_XMLElement_FindAttribute, "_ZNK8tinyxml210XMLElement13FindAttributeEPKc", (const void *a0, const char *a1), (a0, a1), NULL)
TX2_RET(unsigned char, tinyxml2_XMLElement_QueryBoolText, "_ZNK8tinyxml210XMLElement13QueryBoolTextEPb", (const void *a0, unsigned char *a1), (a0, a1), 0)
TX2_RET(float, tinyxml2_XMLElement_FloatAttribute, "_ZNK8tinyxml210XMLElement14FloatAttributeEPKcf", (const void *a0, const char *a1, float a2), (a0, a1, a2), 0.0f)
TX2_RET(long, tinyxml2_XMLElement_Int64Attribute, "_ZNK8tinyxml210XMLElement14Int64AttributeEPKcl", (const void *a0, const char *a1, long a2), (a0, a1, a2), 0)
TX2_RET(unsigned char, tinyxml2_XMLElement_QueryFloatText, "_ZNK8tinyxml210XMLElement14QueryFloatTextEPf", (const void *a0, float *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLElement_QueryInt64Text, "_ZNK8tinyxml210XMLElement14QueryInt64TextEPl", (const void *a0, long *a1), (a0, a1), 0)
TX2_RET(unsigned long, tinyxml2_XMLElement_Unsigned64Text, "_ZNK8tinyxml210XMLElement14Unsigned64TextEm", (const void *a0, unsigned long a1), (a0, a1), 0)
TX2_RET(double, tinyxml2_XMLElement_DoubleAttribute, "_ZNK8tinyxml210XMLElement15DoubleAttributeEPKcd", (const void *a0, const char *a1, double a2), (a0, a1, a2), 0.0)
TX2_RET(unsigned char, tinyxml2_XMLElement_QueryDoubleText, "_ZNK8tinyxml210XMLElement15QueryDoubleTextEPd", (const void *a0, double *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLElement_QueryUnsignedText, "_ZNK8tinyxml210XMLElement17QueryUnsignedTextEPj", (const void *a0, unsigned int *a1), (a0, a1), 0)
TX2_RET(unsigned int, tinyxml2_XMLElement_UnsignedAttribute, "_ZNK8tinyxml210XMLElement17UnsignedAttributeEPKcj", (const void *a0, const char *a1, unsigned int a2), (a0, a1, a2), 0)
TX2_RET(unsigned char, tinyxml2_XMLElement_QueryUnsigned64Text, "_ZNK8tinyxml210XMLElement19QueryUnsigned64TextEPm", (const void *a0, unsigned long *a1), (a0, a1), 0)
TX2_RET(unsigned long, tinyxml2_XMLElement_Unsigned64Attribute, "_ZNK8tinyxml210XMLElement19Unsigned64AttributeEPKcm", (const void *a0, const char *a1, unsigned long a2), (a0, a1, a2), 0)
TX2_RET(unsigned char, tinyxml2_XMLElement_Accept, "_ZNK8tinyxml210XMLElement6AcceptEPNS_10XMLVisitorE", (const void *a0, void *a1), (a0, a1), 0)
TX2_RET(const char *, tinyxml2_XMLElement_GetText, "_ZNK8tinyxml210XMLElement7GetTextEv", (const void *a0), (a0), NULL)
TX2_RET(int, tinyxml2_XMLElement_IntText, "_ZNK8tinyxml210XMLElement7IntTextEi", (const void *a0, int a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLElement_BoolText, "_ZNK8tinyxml210XMLElement8BoolTextEb", (const void *a0, unsigned char a1), (a0, a1), 0)
TX2_RET(const char *, tinyxml2_XMLElement_Attribute, "_ZNK8tinyxml210XMLElement9AttributeEPKcS2_", (const void *a0, const char *a1, const char *a2), (a0, a1, a2), NULL)
TX2_RET(float, tinyxml2_XMLElement_FloatText, "_ZNK8tinyxml210XMLElement9FloatTextEf", (const void *a0, float a1), (a0, a1), 0.0f)
TX2_RET(long, tinyxml2_XMLElement_Int64Text, "_ZNK8tinyxml210XMLElement9Int64TextEl", (const void *a0, long a1), (a0, a1), 0)

/* ============================================================
 * XMLNode（24）
 * ============================================================ */
TX2_VOID(tinyxml2_XMLNode_DeleteNode, "_ZN8tinyxml27XMLNode10DeleteNodeEPS0_", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLNode_DeleteChild, "_ZN8tinyxml27XMLNode11DeleteChildEPS0_", (void *a0, void *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLNode_DeleteChildren, "_ZN8tinyxml27XMLNode14DeleteChildrenEv", (void *a0), (a0))
TX2_RET(void *, tinyxml2_XMLNode_InsertEndChild, "_ZN8tinyxml27XMLNode14InsertEndChildEPS0_", (void *a0, void *a1), (a0, a1), NULL)
TX2_RET(void *, tinyxml2_XMLNode_InsertAfterChild, "_ZN8tinyxml27XMLNode16InsertAfterChildEPS0_S1_", (void *a0, void *a1, void *a2), (a0, a1, a2), NULL)
TX2_RET(void *, tinyxml2_XMLNode_InsertFirstChild, "_ZN8tinyxml27XMLNode16InsertFirstChildEPS0_", (void *a0, void *a1), (a0, a1), NULL)
TX2_VOID(tinyxml2_XMLNode_Unlink, "_ZN8tinyxml27XMLNode6UnlinkEPS0_", (void *a0, void *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLNode_SetValue, "_ZN8tinyxml27XMLNode8SetValueEPKcb", (void *a0, const char *a1, unsigned char a2), (a0, a1, a2))
TX2_RET(unsigned char, tinyxml2_XMLNode_ParseDeep, "_ZN8tinyxml27XMLNode9ParseDeepEPcPNS_7StrPairEPi", (void *a0, char *a1, void *a2, int *a3), (a0, a1, a2, a3), 0)
TX2_VOID(tinyxml2_XMLNode_ctor_c1, "_ZN8tinyxml27XMLNodeC1EPNS_11XMLDocumentE", (void *a0, void *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLNode_ctor_c2, "_ZN8tinyxml27XMLNodeC2EPNS_11XMLDocumentE", (void *a0, void *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLNode_dtor_d0, "_ZN8tinyxml27XMLNodeD0Ev", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLNode_dtor_d1, "_ZN8tinyxml27XMLNodeD1Ev", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLNode_dtor_d2, "_ZN8tinyxml27XMLNodeD2Ev", (void *a0), (a0))
TX2_RET(void *, tinyxml2_XMLNode_LastChildElement, "_ZNK8tinyxml27XMLNode16LastChildElementEPKc", (const void *a0, const char *a1), (a0, a1), NULL)
TX2_RET(int, tinyxml2_XMLNode_ChildElementCount_cstr, "_ZNK8tinyxml27XMLNode17ChildElementCountEPKc", (const void *a0, const char *a1), (a0, a1), -1)
TX2_RET(int, tinyxml2_XMLNode_ChildElementCount, "_ZNK8tinyxml27XMLNode17ChildElementCountEv", (const void *a0), (a0), -1)
TX2_RET(void *, tinyxml2_XMLNode_FirstChildElement, "_ZNK8tinyxml27XMLNode17FirstChildElementEPKc", (const void *a0, const char *a1), (a0, a1), NULL)
TX2_RET(void *, tinyxml2_XMLNode_ToElementWithName, "_ZNK8tinyxml27XMLNode17ToElementWithNameEPKc", (const void *a0, const char *a1), (a0, a1), NULL)
TX2_RET(void *, tinyxml2_XMLNode_NextSiblingElement, "_ZNK8tinyxml27XMLNode18NextSiblingElementEPKc", (const void *a0, const char *a1), (a0, a1), NULL)
TX2_VOID(tinyxml2_XMLNode_InsertChildPreamble, "_ZNK8tinyxml27XMLNode19InsertChildPreambleEPS0_", (const void *a0, void *a1), (a0, a1))
TX2_RET(void *, tinyxml2_XMLNode_PreviousSiblingElement, "_ZNK8tinyxml27XMLNode22PreviousSiblingElementEPKc", (const void *a0, const char *a1), (a0, a1), NULL)
TX2_RET(const char *, tinyxml2_XMLNode_Value, "_ZNK8tinyxml27XMLNode5ValueEv", (const void *a0), (a0), NULL)
TX2_RET(void *, tinyxml2_XMLNode_DeepClone, "_ZNK8tinyxml27XMLNode9DeepCloneEPNS_11XMLDocumentE", (const void *a0, void *a1), (a0, a1), NULL)

/* ============================================================
 * XMLDocument（29；SetError 为变参，单独实现在文件末尾）
 * ============================================================ */
TX2_VOID(tinyxml2_XMLDocument_ClearError, "_ZN8tinyxml211XMLDocument10ClearErrorEv", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLDocument_DeleteNode, "_ZN8tinyxml211XMLDocument10DeleteNodeEPNS_7XMLNodeE", (void *a0, void *a1), (a0, a1))
TX2_RET(void *, tinyxml2_XMLDocument_NewComment, "_ZN8tinyxml211XMLDocument10NewCommentEPKc", (void *a0, const char *a1), (a0, a1), NULL)
TX2_RET(void *, tinyxml2_XMLDocument_NewElement, "_ZN8tinyxml211XMLDocument10NewElementEPKc", (void *a0, const char *a1), (a0, a1), NULL)
TX2_RET(void *, tinyxml2_XMLDocument_NewUnknown, "_ZN8tinyxml211XMLDocument10NewUnknownEPKc", (void *a0, const char *a1), (a0, a1), NULL)
TX2_RET(const char *, tinyxml2_XMLDocument_ErrorIDToName, "_ZN8tinyxml211XMLDocument13ErrorIDToNameENS_8XMLErrorE", (int a0), (a0), NULL)
TX2_RET(void *, tinyxml2_XMLDocument_NewDeclaration, "_ZN8tinyxml211XMLDocument14NewDeclarationEPKc", (void *a0, const char *a1), (a0, a1), NULL)
TX2_VOID(tinyxml2_XMLDocument_Clear, "_ZN8tinyxml211XMLDocument5ClearEv", (void *a0), (a0))
TX2_RET(int, tinyxml2_XMLDocument_Parse_mem, "_ZN8tinyxml211XMLDocument5ParseEPKcm", (void *a0, const char *a1, size_t a2), (a0, a1, a2), -1)
TX2_RET(int, tinyxml2_XMLDocument_Parse, "_ZN8tinyxml211XMLDocument5ParseEv", (void *a0), (a0), -1)
TX2_RET(void *, tinyxml2_XMLDocument_NewText, "_ZN8tinyxml211XMLDocument7NewTextEPKc", (void *a0, const char *a1), (a0, a1), NULL)
TX2_RET(unsigned char, tinyxml2_XMLDocument_Identify, "_ZN8tinyxml211XMLDocument8IdentifyEPcPPNS_7XMLNodeEb", (void *a0, char *a1, void **a2, unsigned char a3), (a0, a1, a2, a3), 0)
TX2_RET(int, tinyxml2_XMLDocument_LoadFile_file, "_ZN8tinyxml211XMLDocument8LoadFileEP8_IO_FILE", (void *a0, FILE *a1), (a0, a1), -1)
TX2_RET(int, tinyxml2_XMLDocument_LoadFile, "_ZN8tinyxml211XMLDocument8LoadFileEPKc", (void *a0, const char *a1), (a0, a1), -1)
TX2_VOID(tinyxml2_XMLDocument_PopDepth, "_ZN8tinyxml211XMLDocument8PopDepthEv", (void *a0), (a0))
TX2_RET(int, tinyxml2_XMLDocument_SaveFile_file, "_ZN8tinyxml211XMLDocument8SaveFileEP8_IO_FILEb", (void *a0, FILE *a1, unsigned char a2), (a0, a1, a2), -1)
TX2_RET(int, tinyxml2_XMLDocument_SaveFile, "_ZN8tinyxml211XMLDocument8SaveFileEPKcb", (void *a0, const char *a1, unsigned char a2), (a0, a1, a2), -1)
TX2_VOID(tinyxml2_XMLDocument_MarkInUse, "_ZN8tinyxml211XMLDocument9MarkInUseEPKNS_7XMLNodeE", (void *a0, const void *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLDocument_PushDepth, "_ZN8tinyxml211XMLDocument9PushDepthEv", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLDocument_ctor_c1, "_ZN8tinyxml211XMLDocumentC1EbNS_10WhitespaceE", (void *a0, unsigned char a1, int a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLDocument_ctor_c2, "_ZN8tinyxml211XMLDocumentC2EbNS_10WhitespaceE", (void *a0, unsigned char a1, int a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLDocument_dtor_d0, "_ZN8tinyxml211XMLDocumentD0Ev", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLDocument_dtor_d1, "_ZN8tinyxml211XMLDocumentD1Ev", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLDocument_dtor_d2, "_ZN8tinyxml211XMLDocumentD2Ev", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLDocument_PrintError, "_ZNK8tinyxml211XMLDocument10PrintErrorEv", (const void *a0), (a0))
TX2_VOID(tinyxml2_XMLDocument_Print, "_ZNK8tinyxml211XMLDocument5PrintEPNS_10XMLPrinterE", (const void *a0, void *a1), (a0, a1))
TX2_RET(unsigned char, tinyxml2_XMLDocument_Accept, "_ZNK8tinyxml211XMLDocument6AcceptEPNS_10XMLVisitorE", (const void *a0, void *a1), (a0, a1), 0)
TX2_VOID(tinyxml2_XMLDocument_DeepCopy, "_ZNK8tinyxml211XMLDocument8DeepCopyEPS0_", (const void *a0, void *a1), (a0, a1))
TX2_RET(const char *, tinyxml2_XMLDocument_ErrorStr, "_ZNK8tinyxml211XMLDocument8ErrorStrEv", (const void *a0), (a0), NULL)
TX2_RET(const char *, tinyxml2_XMLDocument_ErrorName, "_ZNK8tinyxml211XMLDocument9ErrorNameEv", (const void *a0), (a0), NULL)

/* ============================================================
 * XMLPrinter（35；Print 为变参，单独实现在文件末尾）
 * ============================================================ */
TX2_VOID(tinyxml2_XMLPrinter_PrintSpace, "_ZN8tinyxml210XMLPrinter10PrintSpaceEi", (void *a0, int a1), (a0, a1))
TX2_VOID(tinyxml2_XMLPrinter_PushHeader, "_ZN8tinyxml210XMLPrinter10PushHeaderEbb", (void *a0, unsigned char a1, unsigned char a2), (a0, a1, a2))
TX2_RET(unsigned char, tinyxml2_XMLPrinter_VisitEnter_element, "_ZN8tinyxml210XMLPrinter10VisitEnterERKNS_10XMLElementEPKNS_12XMLAttributeE", (const void *a0, const void *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLPrinter_VisitEnter_document, "_ZN8tinyxml210XMLPrinter10VisitEnterERKNS_11XMLDocumentE", (const void *a0), (a0), 0)
TX2_VOID(tinyxml2_XMLPrinter_OpenElement, "_ZN8tinyxml210XMLPrinter11OpenElementEPKcb", (void *a0, const char *a1, unsigned char a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLPrinter_PrintString, "_ZN8tinyxml210XMLPrinter11PrintStringEPKcb", (void *a0, const char *a1, unsigned char a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLPrinter_PushComment, "_ZN8tinyxml210XMLPrinter11PushCommentEPKc", (void *a0, const char *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLPrinter_PushUnknown, "_ZN8tinyxml210XMLPrinter11PushUnknownEPKc", (void *a0, const char *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLPrinter_CloseElement, "_ZN8tinyxml210XMLPrinter12CloseElementEb", (void *a0, unsigned char a1), (a0, a1))
TX2_VOID(tinyxml2_XMLPrinter_PushAttribute_bool, "_ZN8tinyxml210XMLPrinter13PushAttributeEPKcb", (void *a0, const char *a1, unsigned char a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLPrinter_PushAttribute_double, "_ZN8tinyxml210XMLPrinter13PushAttributeEPKcd", (void *a0, const char *a1, double a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLPrinter_PushAttribute_int, "_ZN8tinyxml210XMLPrinter13PushAttributeEPKci", (void *a0, const char *a1, int a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLPrinter_PushAttribute_uint, "_ZN8tinyxml210XMLPrinter13PushAttributeEPKcj", (void *a0, const char *a1, unsigned int a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLPrinter_PushAttribute_long, "_ZN8tinyxml210XMLPrinter13PushAttributeEPKcl", (void *a0, const char *a1, long a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLPrinter_PushAttribute_ulong, "_ZN8tinyxml210XMLPrinter13PushAttributeEPKcm", (void *a0, const char *a1, unsigned long a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLPrinter_PushAttribute_cstr, "_ZN8tinyxml210XMLPrinter13PushAttributeEPKcS2_", (void *a0, const char *a1, const char *a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLPrinter_PushDeclaration, "_ZN8tinyxml210XMLPrinter15PushDeclarationEPKc", (void *a0, const char *a1), (a0, a1))
TX2_VOID(tinyxml2_XMLPrinter_PrepareForNewNode, "_ZN8tinyxml210XMLPrinter17PrepareForNewNodeEb", (void *a0, unsigned char a1), (a0, a1))
TX2_VOID(tinyxml2_XMLPrinter_SealElementIfJustOpened, "_ZN8tinyxml210XMLPrinter23SealElementIfJustOpenedEv", (void *a0), (a0))
TX2_VOID(tinyxml2_XMLPrinter_Putc, "_ZN8tinyxml210XMLPrinter4PutcEc", (void *a0, char a1), (a0, a1))
TX2_RET(unsigned char, tinyxml2_XMLPrinter_Visit_comment, "_ZN8tinyxml210XMLPrinter5VisitERKNS_10XMLCommentE", (const void *a0, const void *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLPrinter_Visit_unknown, "_ZN8tinyxml210XMLPrinter5VisitERKNS_10XMLUnknownE", (const void *a0, const void *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLPrinter_Visit_declaration, "_ZN8tinyxml210XMLPrinter5VisitERKNS_14XMLDeclarationE", (const void *a0, const void *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLPrinter_Visit_text, "_ZN8tinyxml210XMLPrinter5VisitERKNS_7XMLTextE", (const void *a0, const void *a1), (a0, a1), 0)
TX2_VOID(tinyxml2_XMLPrinter_Write, "_ZN8tinyxml210XMLPrinter5WriteEPKcm", (void *a0, const char *a1, size_t a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLPrinter_PushText_bool, "_ZN8tinyxml210XMLPrinter8PushTextEb", (void *a0, unsigned char a1), (a0, a1))
TX2_VOID(tinyxml2_XMLPrinter_PushText_double, "_ZN8tinyxml210XMLPrinter8PushTextEd", (void *a0, double a1), (a0, a1))
TX2_VOID(tinyxml2_XMLPrinter_PushText_float, "_ZN8tinyxml210XMLPrinter8PushTextEf", (void *a0, float a1), (a0, a1))
TX2_VOID(tinyxml2_XMLPrinter_PushText_int, "_ZN8tinyxml210XMLPrinter8PushTextEi", (void *a0, int a1), (a0, a1))
TX2_VOID(tinyxml2_XMLPrinter_PushText_uint, "_ZN8tinyxml210XMLPrinter8PushTextEj", (void *a0, unsigned int a1), (a0, a1))
TX2_VOID(tinyxml2_XMLPrinter_PushText_long, "_ZN8tinyxml210XMLPrinter8PushTextEl", (void *a0, long a1), (a0, a1))
TX2_VOID(tinyxml2_XMLPrinter_PushText_ulong, "_ZN8tinyxml210XMLPrinter8PushTextEm", (void *a0, unsigned long a1), (a0, a1))
TX2_VOID(tinyxml2_XMLPrinter_PushText_cstr, "_ZN8tinyxml210XMLPrinter8PushTextEPKcb", (void *a0, const char *a1, unsigned char a2), (a0, a1, a2))
TX2_RET(unsigned char, tinyxml2_XMLPrinter_VisitExit, "_ZN8tinyxml210XMLPrinter9VisitExitERKNS_10XMLElementE", (const void *a0, const void *a1), (a0, a1), 0)
TX2_VOID(tinyxml2_XMLPrinter_ctor_c1, "_ZN8tinyxml210XMLPrinterC1EP8_IO_FILEbi", (void *a0, FILE *a1, unsigned char a2, int a3), (a0, a1, a2, a3))
TX2_VOID(tinyxml2_XMLPrinter_ctor_c2, "_ZN8tinyxml210XMLPrinterC2EP8_IO_FILEbi", (void *a0, FILE *a1, unsigned char a2, int a3), (a0, a1, a2, a3))

/* ============================================================
 * XMLUtil（18）
 * ============================================================ */
TX2_RET(unsigned char, tinyxml2_XMLUtil_ToUnsigned, "_ZN8tinyxml27XMLUtil10ToUnsignedEPKcPj", (const char *a0, unsigned int *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLUtil_ToUnsigned64, "_ZN8tinyxml27XMLUtil12ToUnsigned64EPKcPm", (const char *a0, unsigned long *a1), (a0, a1), 0)
TX2_RET(int, tinyxml2_XMLUtil_GetCharacterRef, "_ZN8tinyxml27XMLUtil15GetCharacterRefEPKcPcPi", (const char *a0, char *a1, int *a2), (a0, a1, a2), 0)
TX2_VOID(tinyxml2_XMLUtil_ConvertUTF32ToUTF8, "_ZN8tinyxml27XMLUtil18ConvertUTF32ToUTF8EmPcPi", (unsigned long a0, char *a1, int *a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLUtil_SetBoolSerialization, "_ZN8tinyxml27XMLUtil20SetBoolSerializationEPKcS2_", (const char *a0, const char *a1), (a0, a1))
TX2_RET(unsigned char, tinyxml2_XMLUtil_ToInt, "_ZN8tinyxml27XMLUtil5ToIntEPKcPi", (const char *a0, int *a1), (a0, a1), 0)
TX2_VOID(tinyxml2_XMLUtil_ToStr_bool, "_ZN8tinyxml27XMLUtil5ToStrEbPci", (unsigned char a0, char *a1, int a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLUtil_ToStr_double, "_ZN8tinyxml27XMLUtil5ToStrEdPci", (double a0, char *a1, int a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLUtil_ToStr_float, "_ZN8tinyxml27XMLUtil5ToStrEfPci", (float a0, char *a1, int a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLUtil_ToStr_int, "_ZN8tinyxml27XMLUtil5ToStrEiPci", (int a0, char *a1, int a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLUtil_ToStr_uint, "_ZN8tinyxml27XMLUtil5ToStrEjPci", (unsigned int a0, char *a1, int a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLUtil_ToStr_long, "_ZN8tinyxml27XMLUtil5ToStrElPci", (long a0, char *a1, int a2), (a0, a1, a2))
TX2_VOID(tinyxml2_XMLUtil_ToStr_ulong, "_ZN8tinyxml27XMLUtil5ToStrEmPci", (unsigned long a0, char *a1, int a2), (a0, a1, a2))
TX2_RET(unsigned char, tinyxml2_XMLUtil_ToBool, "_ZN8tinyxml27XMLUtil6ToBoolEPKcPb", (const char *a0, unsigned char *a1), (a0, a1), 0)
TX2_RET(const char *, tinyxml2_XMLUtil_ReadBOM, "_ZN8tinyxml27XMLUtil7ReadBOMEPKcPb", (const char *a0, unsigned char *a1), (a0, a1), NULL)
TX2_RET(unsigned char, tinyxml2_XMLUtil_ToFloat, "_ZN8tinyxml27XMLUtil7ToFloatEPKcPf", (const char *a0, float *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLUtil_ToInt64, "_ZN8tinyxml27XMLUtil7ToInt64EPKcPl", (const char *a0, long *a1), (a0, a1), 0)
TX2_RET(unsigned char, tinyxml2_XMLUtil_ToDouble, "_ZN8tinyxml27XMLUtil8ToDoubleEPKcPd", (const char *a0, double *a1), (a0, a1), 0)

/* ============================================================
 * 变参转发辅助
 * ============================================================ */
static char *tinyxml2_vformat(const char *fmt, va_list ap) {
    va_list ap2;
    int n;
    char *b;
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) return NULL;
    b = (char *)malloc((size_t)n + 1);
    if (!b) return NULL;
    vsnprintf(b, (size_t)n + 1, fmt, ap);
    return b;
}

/* XMLDocument::SetError(XMLError, int, char const*, ...) */
void tinyxml2_XMLDocument_SetError(void *a0, int a1, int a2, const char *a3, ...) {
    static __typeof__(tinyxml2_XMLDocument_SetError) *fn = NULL;
    va_list ap;
    char *b;
    if (!fn) {
        if (!tinyxml2_libok()) return;
        *(void **)(&fn) = tinyxml2_sym("_ZN8tinyxml211XMLDocument8SetErrorENS_8XMLErrorEiPKcz");
        if (!fn) {
            fprintf(stderr, "tinyxml2_dyn: dlsym failed: XMLDocument::SetError\n");
            return;
        }
    }
    va_start(ap, a3);
    b = tinyxml2_vformat(a3, ap);
    va_end(ap);
    if (!b) return;
    fn(a0, a1, a2, "%s", b);
    free(b);
}

/* XMLPrinter::Print(char const*, ...) */
void tinyxml2_XMLPrinter_Print(void *a0, const char *a1, ...) {
    static __typeof__(tinyxml2_XMLPrinter_Print) *fn = NULL;
    va_list ap;
    char *b;
    if (!fn) {
        if (!tinyxml2_libok()) return;
        *(void **)(&fn) = tinyxml2_sym("_ZN8tinyxml210XMLPrinter5PrintEPKcz");
        if (!fn) {
            fprintf(stderr, "tinyxml2_dyn: dlsym failed: XMLPrinter::Print\n");
            return;
        }
    }
    va_start(ap, a1);
    b = tinyxml2_vformat(a1, ap);
    va_end(ap);
    if (!b) return;
    fn(a0, "%s", b);
    free(b);
}

/* ============================================================
 * 便捷层
 * ============================================================ */
void *tinyxml2_dyn_document_new(void) {
    void *p = calloc(1, TINYXML2_OBJ_ARENA);
    if (!p) return NULL;
    /* processEntities=true, Whitespace=PRESERVE_WHITESPACE(0) */
    tinyxml2_XMLDocument_ctor_c1(p, 1, 0);
    return p;
}

void tinyxml2_dyn_document_free(void *doc) {
    if (!doc) return;
    tinyxml2_XMLDocument_dtor_d1(doc);
    free(doc);
}

void *tinyxml2_dyn_printer_new(void) {
    void *p = calloc(1, TINYXML2_OBJ_ARENA);
    if (!p) return NULL;
    /* FILE*=NULL(内存模式), compact=false, depth=0 */
    tinyxml2_XMLPrinter_ctor_c1(p, NULL, 0, 0);
    return p;
}

void tinyxml2_dyn_printer_free(void *printer) {
    if (!printer) return;
    /* ~XMLPrinter 未导出（inline virtual），走 vtable slot 0 的完整析构
       以释放内部 DynArray 堆缓冲 */
    void **vptr = *(void ***)printer;
    if (vptr && vptr[0]) ((void (*)(void *))vptr[0])(printer);
    free(printer);
}

/* ============================================================
 * 自检：ctor -> Parse("<a/>") -> FirstChildElement -> Value -> dtor
 * 返回 0 表示通过；负值为失败步骤。
 * ============================================================ */
int tinyxml2_dyn_selftest(void) {
    static int running = 0;
    void *doc;
    void *root;
    const char *name;

    if (running) return 0;
    running = 1;

    doc = tinyxml2_dyn_document_new();
    if (!doc) { running = 0; return -1; }

    if (tinyxml2_XMLDocument_Parse_mem(doc, "<a/>", 4) != 0) {
        tinyxml2_dyn_document_free(doc);
        running = 0;
        return -2;
    }

    root = tinyxml2_XMLNode_FirstChildElement(doc, NULL);
    if (!root) {
        tinyxml2_dyn_document_free(doc);
        running = 0;
        return -3;
    }

    name = tinyxml2_XMLNode_Value(root);
    if (!name || strcmp(name, "a") != 0) {
        tinyxml2_dyn_document_free(doc);
        running = 0;
        return -4;
    }

    tinyxml2_dyn_document_free(doc);
    running = 0;
    return 0;
}
