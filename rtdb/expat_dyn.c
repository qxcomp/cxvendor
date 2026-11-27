/* ============================================================
 * expat_dyn.c - type safe dynamic lib load for expat 2.9
 * ============================================================
 * 设计要点：
 *   1. 单文件、无 .h、无 wrapper 层；链接期不需要目标库。
 *   覆盖 libexpat.so.1 全部 83 个导出符号（XML_*，nm -D 全量）。
 *   签名与公开结构按官方 expat.h / expat_external.h 核对。
 *   XML_Parser 为不透明句柄；XML_Content / XML_Expat_Version /
 *   XML_Feature / XML_ParsingStatus 按值传递，布局完整复制。
 *   XML_Char 按本机默认构建为 char（非 XML_UNICODE）。
 *   2. 每个函数首次调用时 dlsym 缓存；失败打印 stderr 并返回安全值。
 *   3. 加载顺序：显式路径 > EXPAT_DYN_LIB > LD_LIBRARY_PATH >
 *      soname > .so > Nix > 平台默认；成功后不卸载。
 *
 *   使用：
 *     expat_dyn_init(NULL);
 *     ... 原版 API 直接调用 ...
 *     expat_dyn_cleanup();   // 仅复位加载状态，不卸载库
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
 typedef HMODULE expat_lib_handle_t;
 static expat_lib_handle_t expat_load_lib(const char *path) { return LoadLibraryA(path); }
 static void *expat_get_sym(expat_lib_handle_t h, const char *name) { return (void *)GetProcAddress(h, name); }
 static void expat_free_lib(expat_lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *expat_lib_error(void) { return "LoadLibrary failed"; }
 #define EXPAT_PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *expat_lib_handle_t;
 static expat_lib_handle_t expat_load_lib(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
 static void *expat_get_sym(expat_lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void expat_free_lib(expat_lib_handle_t h) { if (h) dlclose(h); }
 static const char *expat_lib_error(void) { return dlerror(); }
 #define EXPAT_PATH_SEP ':'
#endif

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Warray-parameter"
#endif

#if defined(__has_include)
#  if __has_include(<expat.h>)
#    include <expat.h>
#    define EXPAT_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef EXPAT_DYN_HAVE_HEADER
/* ============================================================
 * 类型定义（expat.h）
 * ============================================================ */
struct XML_ParserStruct;
typedef struct XML_ParserStruct *XML_Parser;

typedef char XML_Char;
typedef char XML_LChar;
typedef unsigned char XML_Bool;
typedef long XML_Index;
typedef unsigned long XML_Size;

#define XML_TRUE  ((XML_Bool)1)
#define XML_FALSE ((XML_Bool)0)

enum XML_Status {
    XML_STATUS_ERROR = 0,
    XML_STATUS_OK = 1,
    XML_STATUS_SUSPENDED = 2
};

enum XML_Error {
    XML_ERROR_NONE,
    XML_ERROR_NO_MEMORY,
    XML_ERROR_SYNTAX,
    XML_ERROR_NO_ELEMENTS,
    XML_ERROR_INVALID_TOKEN,
    XML_ERROR_UNCLOSED_TOKEN,
    XML_ERROR_PARTIAL_CHAR,
    XML_ERROR_TAG_MISMATCH,
    XML_ERROR_DUPLICATE_ATTRIBUTE,
    XML_ERROR_JUNK_AFTER_DOC_ELEMENT,
    XML_ERROR_PARAM_ENTITY_REF,
    XML_ERROR_UNDEFINED_ENTITY,
    XML_ERROR_RECURSIVE_ENTITY_REF,
    XML_ERROR_ASYNC_ENTITY,
    XML_ERROR_BAD_CHAR_REF,
    XML_ERROR_BINARY_ENTITY_REF,
    XML_ERROR_ATTRIBUTE_EXTERNAL_ENTITY_REF,
    XML_ERROR_MISPLACED_XML_PI,
    XML_ERROR_UNKNOWN_ENCODING,
    XML_ERROR_INCORRECT_ENCODING,
    XML_ERROR_UNCLOSED_CDATA_SECTION,
    XML_ERROR_EXTERNAL_ENTITY_HANDLING,
    XML_ERROR_NOT_STANDALONE,
    XML_ERROR_UNEXPECTED_STATE,
    XML_ERROR_ENTITY_DECLARED_IN_PE,
    XML_ERROR_FEATURE_REQUIRES_XML_DTD,
    XML_ERROR_CANT_CHANGE_FEATURE_ONCE_PARSING,
    XML_ERROR_UNBOUND_PREFIX,
    XML_ERROR_UNDECLARING_PREFIX,
    XML_ERROR_INCOMPLETE_PE,
    XML_ERROR_XML_DECL,
    XML_ERROR_TEXT_DECL,
    XML_ERROR_PUBLICID,
    XML_ERROR_SUSPENDED,
    XML_ERROR_NOT_SUSPENDED,
    XML_ERROR_ABORTED,
    XML_ERROR_FINISHED,
    XML_ERROR_SUSPEND_PE,
    XML_ERROR_RESERVED_PREFIX_XML,
    XML_ERROR_RESERVED_PREFIX_XMLNS,
    XML_ERROR_RESERVED_NAMESPACE_URI,
    XML_ERROR_INVALID_ARGUMENT,
    XML_ERROR_NO_BUFFER,
    XML_ERROR_AMPLIFICATION_LIMIT_BREACH,
    XML_ERROR_NOT_STARTED
};

enum XML_Prop_Error {
    XML_PROP_ERROR_NONE = 0,
    XML_PROP_ERROR_INVALID_KEY = 1,
    XML_PROP_ERROR_INVALID_TYPE = 2,
    XML_PROP_ERROR_INVALID_VALUE = 3,
    XML_PROP_ERROR_PARSER_NULL = 4,
    XML_PROP_ERROR_PARSER_NOT_ROOT = 5
};

enum XML_Content_Type {
    XML_CTYPE_EMPTY = 1,
    XML_CTYPE_ANY,
    XML_CTYPE_MIXED,
    XML_CTYPE_NAME,
    XML_CTYPE_CHOICE,
    XML_CTYPE_SEQ
};

enum XML_Content_Quant {
    XML_CQUANT_NONE,
    XML_CQUANT_OPT,
    XML_CQUANT_REP,
    XML_CQUANT_PLUS
};

typedef struct XML_cp XML_Content;
struct XML_cp {
    enum XML_Content_Type type;
    enum XML_Content_Quant quant;
    XML_Char *name;
    unsigned int numchildren;
    XML_Content *children;
};

enum XML_Parsing { XML_INITIALIZED, XML_PARSING, XML_FINISHED, XML_SUSPENDED };

typedef struct {
    enum XML_Parsing parsing;
    XML_Bool finalBuffer;
} XML_ParsingStatus;

enum XML_ParamEntityParsing {
    XML_PARAM_ENTITY_PARSING_NEVER,
    XML_PARAM_ENTITY_PARSING_UNLESS_STANDALONE,
    XML_PARAM_ENTITY_PARSING_ALWAYS
};

typedef struct {
    int major;
    int minor;
    int micro;
} XML_Expat_Version;

enum XML_FeatureEnum {
    XML_FEATURE_END = 0,
    XML_FEATURE_UNICODE,
    XML_FEATURE_UNICODE_WCHAR_T,
    XML_FEATURE_DTD,
    XML_FEATURE_CONTEXT_BYTES,
    XML_FEATURE_MIN_SIZE,
    XML_FEATURE_SIZEOF_XML_CHAR,
    XML_FEATURE_SIZEOF_XML_LCHAR,
    XML_FEATURE_NS,
    XML_FEATURE_LARGE_SIZE,
    XML_FEATURE_ATTR_INFO,
    XML_FEATURE_BILLION_LAUGHS_ATTACK_PROTECTION_MAXIMUM_AMPLIFICATION_DEFAULT,
    XML_FEATURE_BILLION_LAUGHS_ATTACK_PROTECTION_ACTIVATION_THRESHOLD_DEFAULT,
    XML_FEATURE_GE,
    XML_FEATURE_ALLOC_TRACKER_MAXIMUM_AMPLIFICATION_DEFAULT,
    XML_FEATURE_ALLOC_TRACKER_ACTIVATION_THRESHOLD_DEFAULT
};

typedef struct {
    enum XML_FeatureEnum feature;
    const XML_LChar *name;
    long int value;
} XML_Feature;

enum XML_Parser_Property {
    XML_PROP_ALLOC_TRACKER_ACTIVATION_THRESHOLD = 1,
    XML_PROP_ALLOC_TRACKER_MAXIMUM_AMPLIFICATION = 2,
    XML_PROP_BILLION_LAUGHS_ACTIVATION_THRESHOLD = 3,
    XML_PROP_BILLION_LAUGHS_MAXIMUM_AMPLIFICATION = 4,
    XML_PROP_REPARSE_DEFERRAL_ENABLED = 5
};

typedef struct {
    void *(*malloc_fcn)(size_t size);
    void *(*realloc_fcn)(void *ptr, size_t size);
    void (*free_fcn)(void *ptr);
} XML_Memory_Handling_Suite;

typedef struct {
    int map[256];
    void *data;
    int (*convert)(void *data, const char *s);
    void (*release)(void *data);
} XML_Encoding;

#endif /* EXPAT_DYN_HAVE_HEADER */

/* ---------- 回调 typedef ---------- */
typedef void (*XML_ElementDeclHandler)(void *userData, const XML_Char *name, XML_Content *model);
typedef void (*XML_AttlistDeclHandler)(void *userData, const XML_Char *elname,
                                       const XML_Char *attname, const XML_Char *att_type,
                                       const XML_Char *dflt, int isrequired);
typedef void (*XML_XmlDeclHandler)(void *userData, const XML_Char *version,
                                   const XML_Char *encoding, int standalone);
typedef void (*XML_StartElementHandler)(void *userData, const XML_Char *name,
                                        const XML_Char **atts);
typedef void (*XML_EndElementHandler)(void *userData, const XML_Char *name);
typedef void (*XML_CharacterDataHandler)(void *userData, const XML_Char *s, int len);
typedef void (*XML_ProcessingInstructionHandler)(void *userData, const XML_Char *target,
                                                 const XML_Char *data);
typedef void (*XML_CommentHandler)(void *userData, const XML_Char *data);
typedef void (*XML_StartCdataSectionHandler)(void *userData);
typedef void (*XML_EndCdataSectionHandler)(void *userData);
typedef void (*XML_DefaultHandler)(void *userData, const XML_Char *s, int len);
typedef void (*XML_StartDoctypeDeclHandler)(void *userData, const XML_Char *doctypeName,
                                            const XML_Char *sysid, const XML_Char *pubid,
                                            int has_internal_subset);
typedef void (*XML_EndDoctypeDeclHandler)(void *userData);
typedef void (*XML_EntityDeclHandler)(void *userData, const XML_Char *entityName,
                                      int is_parameter_entity, const XML_Char *value,
                                      int value_length, const XML_Char *base,
                                      const XML_Char *systemId, const XML_Char *publicId,
                                      const XML_Char *notationName);
typedef void (*XML_UnparsedEntityDeclHandler)(void *userData, const XML_Char *entityName,
                                              const XML_Char *base, const XML_Char *systemId,
                                              const XML_Char *publicId,
                                              const XML_Char *notationName);
typedef void (*XML_NotationDeclHandler)(void *userData, const XML_Char *notationName,
                                        const XML_Char *base, const XML_Char *systemId,
                                        const XML_Char *publicId);
typedef void (*XML_StartNamespaceDeclHandler)(void *userData, const XML_Char *prefix,
                                              const XML_Char *uri);
typedef void (*XML_EndNamespaceDeclHandler)(void *userData, const XML_Char *prefix);
typedef int  (*XML_NotStandaloneHandler)(void *userData);
typedef int  (*XML_ExternalEntityRefHandler)(XML_Parser parser, const XML_Char *context,
                                             const XML_Char *base, const XML_Char *systemId,
                                             const XML_Char *publicId);
typedef void (*XML_SkippedEntityHandler)(void *userData, const XML_Char *entityName,
                                         int is_parameter_entity);
typedef int  (*XML_UnknownEncodingHandler)(void *encodingHandlerData, const XML_Char *name,
                                           XML_Encoding *info);

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static expat_lib_handle_t expat_handle = NULL;
static int expat_init_state = 0;

static void *expat_sym(const char *name) {
    return expat_handle ? expat_get_sym(expat_handle, name) : NULL;
}

static int expat_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    int n = 0, i;

    if (expat_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("EXPAT_DYN_LIB");
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
                    char *sep = strchr(q, EXPAT_PATH_SEP);
                    if (sep) *sep = 0;
                    if (*q) {
                        snprintf(bufs[n], sizeof bufs[0], "%s/libexpat.so.1", q);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/libexpat.so", q);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = "libexpat.so.1";
    candidates[n++] = "libexpat.so";
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libexpat.so.1", home);
            candidates[n] = bufs[n]; n++;
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libexpat.so", home);
            candidates[n] = bufs[n]; n++;
        }
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libexpat.so.1";
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libexpat.so";
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libexpat.so.1";
    candidates[n++] = "/usr/local/lib/libexpat.so.1";
    candidates[n++] = "/opt/homebrew/lib/libexpat.so";
    candidates[n++] = "/usr/local/lib/libexpat.so";
#elif defined(_WIN32)
    candidates[n++] = "libexpat.dll";
#else
    candidates[n++] = "/usr/local/lib/libexpat.so.1";
    candidates[n++] = "/usr/lib/libexpat.so.1";
    candidates[n++] = "/usr/local/lib/libexpat.so";
    candidates[n++] = "/usr/lib/libexpat.so";
#endif

    for (i = 0; i < n; i++) {
        expat_handle = expat_load_lib(candidates[i]);
        if (expat_handle) break;
    }
    if (!expat_handle || !expat_sym("XML_ParserCreate")) {
        fprintf(stderr, "expat_dyn: cannot load expat (%s)\n", expat_lib_error());
        if (expat_handle) { expat_free_lib(expat_handle); expat_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "expat_dyn: loaded %d %s (2.9 headers)\n", i, candidates[i]);
    return 0;
}

int expat_libok(void) {
    if (expat_init_state == 0)
        expat_init_state = (expat_do_load(NULL) == 0) ? 1 : -1;
    return (expat_init_state == 1 && expat_handle != NULL) ? 1 : 0;
}

int expat_dyn_init(const char *library_path) {
    if (expat_do_load(library_path) == 0) {
        expat_init_state = 1;
        return 0;
    }
    if (expat_init_state == 0) expat_init_state = -1;
    return -1;
}

void expat_dyn_cleanup(void) {
    /* 仅复位加载状态，不卸载已成功加载的库（dlclose 不安全） */
    if (!expat_handle) expat_init_state = 0;
    else if (expat_init_state == -1) expat_init_state = 0;
}

int expat_dyn_has_symbol(const char *name) {
    if (!expat_libok()) return 0;
    return expat_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define EXPAT_WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                          \
    static ret (*fn) params = NULL;                                        \
    if (!fn) {                                                             \
        if (!expat_libok()) return fallback;                               \
        *(void **)(&fn) = expat_sym(#name);                                \
        if (!fn) {                                                         \
            fprintf(stderr, "expat_dyn: dlsym failed: %s\n", #name);       \
            return fallback;                                               \
        }                                                                  \
    }                                                                      \
    return fn args;                                                        \
}

#define EXPAT_WRAP_VOID(name, params, args)                                      \
void name params {                                                         \
    static void (*fn) params = NULL;                                       \
    if (!fn) {                                                             \
        if (!expat_libok()) return;                                        \
        *(void **)(&fn) = expat_sym(#name);                                \
        if (!fn) {                                                         \
            fprintf(stderr, "expat_dyn: dlsym failed: %s\n", #name);       \
            return;                                                        \
        }                                                                  \
    }                                                                      \
    fn args;                                                               \
}

/* ============================================================
 * 导出函数封装（nm -D 全量 83 个）
 * ============================================================ */
EXPAT_WRAP_VOID(XML_DefaultCurrent, (XML_Parser a0), (a0))
EXPAT_WRAP_RET(const XML_LChar *, XML_ErrorString, (enum XML_Error a0), (a0), NULL)
EXPAT_WRAP_RET(const XML_LChar *, XML_ExpatVersion, (void), (), NULL)
EXPAT_WRAP_RET(XML_Expat_Version, XML_ExpatVersionInfo, (void), (), ((XML_Expat_Version){0, 0, 0}))
EXPAT_WRAP_RET(XML_Parser, XML_ExternalEntityParserCreate, (XML_Parser a0, const XML_Char *a1, const XML_Char *a2), (a0, a1, a2), NULL)
EXPAT_WRAP_VOID(XML_FreeContentModel, (XML_Parser a0, XML_Content *a1), (a0, a1))
EXPAT_WRAP_RET(const XML_Char *, XML_GetBase, (XML_Parser a0), (a0), NULL)
EXPAT_WRAP_RET(void *, XML_GetBuffer, (XML_Parser a0, int a1), (a0, a1), NULL)
EXPAT_WRAP_RET(int, XML_GetCurrentByteCount, (XML_Parser a0), (a0), 0)
EXPAT_WRAP_RET(uint64_t, XML_GetCurrentByteCount64, (XML_Parser a0), (a0), 0)
EXPAT_WRAP_RET(XML_Index, XML_GetCurrentByteIndex, (XML_Parser a0), (a0), 0)
EXPAT_WRAP_RET(int64_t, XML_GetCurrentByteIndex64, (XML_Parser a0), (a0), 0)
EXPAT_WRAP_RET(XML_Size, XML_GetCurrentColumnNumber, (XML_Parser a0), (a0), 0)
EXPAT_WRAP_RET(uint64_t, XML_GetCurrentColumnNumber64, (XML_Parser a0), (a0), 0)
EXPAT_WRAP_RET(XML_Size, XML_GetCurrentLineNumber, (XML_Parser a0), (a0), 0)
EXPAT_WRAP_RET(uint64_t, XML_GetCurrentLineNumber64, (XML_Parser a0), (a0), 0)
EXPAT_WRAP_RET(enum XML_Error, XML_GetErrorCode, (XML_Parser a0), (a0), XML_ERROR_NONE)
EXPAT_WRAP_RET(const XML_Feature *, XML_GetFeatureList, (void), (), NULL)
EXPAT_WRAP_RET(int, XML_GetIdAttributeIndex, (XML_Parser a0), (a0), -1)
EXPAT_WRAP_RET(const char *, XML_GetInputContext, (XML_Parser a0, int *a1, int *a2), (a0, a1, a2), NULL)
EXPAT_WRAP_RET(const char *, XML_GetInputContext64, (XML_Parser a0, int64_t *a1, uint64_t *a2), (a0, a1, a2), NULL)
EXPAT_WRAP_VOID(XML_GetParsingStatus, (XML_Parser a0, XML_ParsingStatus *a1), (a0, a1))
EXPAT_WRAP_RET(enum XML_Prop_Error, XML_GetPropertyBool, (XML_Parser a0, enum XML_Parser_Property a1, XML_Bool *a2), (a0, a1, a2), XML_PROP_ERROR_INVALID_KEY)
EXPAT_WRAP_RET(enum XML_Prop_Error, XML_GetPropertyDouble, (XML_Parser a0, enum XML_Parser_Property a1, double *a2), (a0, a1, a2), XML_PROP_ERROR_INVALID_KEY)
EXPAT_WRAP_RET(enum XML_Prop_Error, XML_GetPropertyUInt64, (XML_Parser a0, enum XML_Parser_Property a1, uint64_t *a2), (a0, a1, a2), XML_PROP_ERROR_INVALID_KEY)
EXPAT_WRAP_RET(int, XML_GetSpecifiedAttributeCount, (XML_Parser a0), (a0), -1)
EXPAT_WRAP_VOID(XML_MemFree, (XML_Parser a0, void *a1), (a0, a1))
EXPAT_WRAP_RET(void *, XML_MemMalloc, (XML_Parser a0, size_t a1), (a0, a1), NULL)
EXPAT_WRAP_RET(void *, XML_MemRealloc, (XML_Parser a0, void *a1, size_t a2), (a0, a1, a2), NULL)
EXPAT_WRAP_RET(enum XML_Status, XML_Parse, (XML_Parser a0, const char *a1, int a2, int a3), (a0, a1, a2, a3), XML_STATUS_ERROR)
EXPAT_WRAP_RET(enum XML_Status, XML_ParseBuffer, (XML_Parser a0, int a1, int a2), (a0, a1, a2), XML_STATUS_ERROR)
EXPAT_WRAP_RET(XML_Parser, XML_ParserCreate, (const XML_Char *a0), (a0), NULL)
EXPAT_WRAP_RET(XML_Parser, XML_ParserCreate_MM, (const XML_Char *a0, const XML_Memory_Handling_Suite *a1, const XML_Char *a2), (a0, a1, a2), NULL)
EXPAT_WRAP_RET(XML_Parser, XML_ParserCreateNS, (const XML_Char *a0, XML_Char a1), (a0, a1), NULL)
EXPAT_WRAP_VOID(XML_ParserFree, (XML_Parser a0), (a0))
EXPAT_WRAP_RET(XML_Bool, XML_ParserReset, (XML_Parser a0, const XML_Char *a1), (a0, a1), XML_FALSE)
EXPAT_WRAP_RET(enum XML_Status, XML_ResumeParser, (XML_Parser a0), (a0), XML_STATUS_ERROR)
EXPAT_WRAP_RET(XML_Bool, XML_SetAllocTrackerActivationThreshold, (XML_Parser a0, unsigned long long a1), (a0, a1), XML_FALSE)
EXPAT_WRAP_RET(XML_Bool, XML_SetAllocTrackerMaximumAmplification, (XML_Parser a0, float a1), (a0, a1), XML_FALSE)
EXPAT_WRAP_VOID(XML_SetAttlistDeclHandler, (XML_Parser a0, XML_AttlistDeclHandler a1), (a0, a1))
EXPAT_WRAP_RET(enum XML_Status, XML_SetBase, (XML_Parser a0, const XML_Char *a1), (a0, a1), XML_STATUS_ERROR)
EXPAT_WRAP_RET(XML_Bool, XML_SetBillionLaughsAttackProtectionActivationThreshold, (XML_Parser a0, unsigned long long a1), (a0, a1), XML_FALSE)
EXPAT_WRAP_RET(XML_Bool, XML_SetBillionLaughsAttackProtectionMaximumAmplification, (XML_Parser a0, float a1), (a0, a1), XML_FALSE)
EXPAT_WRAP_VOID(XML_SetCdataSectionHandler, (XML_Parser a0, XML_StartCdataSectionHandler a1, XML_EndCdataSectionHandler a2), (a0, a1, a2))
EXPAT_WRAP_VOID(XML_SetCharacterDataHandler, (XML_Parser a0, XML_CharacterDataHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetCommentHandler, (XML_Parser a0, XML_CommentHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetDefaultHandler, (XML_Parser a0, XML_DefaultHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetDefaultHandlerExpand, (XML_Parser a0, XML_DefaultHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetDoctypeDeclHandler, (XML_Parser a0, XML_StartDoctypeDeclHandler a1, XML_EndDoctypeDeclHandler a2), (a0, a1, a2))
EXPAT_WRAP_VOID(XML_SetElementDeclHandler, (XML_Parser a0, XML_ElementDeclHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetElementHandler, (XML_Parser a0, XML_StartElementHandler a1, XML_EndElementHandler a2), (a0, a1, a2))
EXPAT_WRAP_RET(enum XML_Status, XML_SetEncoding, (XML_Parser a0, const XML_Char *a1), (a0, a1), XML_STATUS_ERROR)
EXPAT_WRAP_VOID(XML_SetEndCdataSectionHandler, (XML_Parser a0, XML_EndCdataSectionHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetEndDoctypeDeclHandler, (XML_Parser a0, XML_EndDoctypeDeclHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetEndElementHandler, (XML_Parser a0, XML_EndElementHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetEndNamespaceDeclHandler, (XML_Parser a0, XML_EndNamespaceDeclHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetEntityDeclHandler, (XML_Parser a0, XML_EntityDeclHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetExternalEntityRefHandler, (XML_Parser a0, XML_ExternalEntityRefHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetExternalEntityRefHandlerArg, (XML_Parser a0, void *a1), (a0, a1))
EXPAT_WRAP_RET(int, XML_SetHashSalt, (XML_Parser a0, unsigned long a1), (a0, a1), 0)
EXPAT_WRAP_RET(XML_Bool, XML_SetHashSalt16Bytes, (XML_Parser a0, const uint8_t *a1), (a0, a1), XML_FALSE)
EXPAT_WRAP_VOID(XML_SetNamespaceDeclHandler, (XML_Parser a0, XML_StartNamespaceDeclHandler a1, XML_EndNamespaceDeclHandler a2), (a0, a1, a2))
EXPAT_WRAP_VOID(XML_SetNotationDeclHandler, (XML_Parser a0, XML_NotationDeclHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetNotStandaloneHandler, (XML_Parser a0, XML_NotStandaloneHandler a1), (a0, a1))
EXPAT_WRAP_RET(int, XML_SetParamEntityParsing, (XML_Parser a0, enum XML_ParamEntityParsing a1), (a0, a1), 0)
EXPAT_WRAP_VOID(XML_SetProcessingInstructionHandler, (XML_Parser a0, XML_ProcessingInstructionHandler a1), (a0, a1))
EXPAT_WRAP_RET(enum XML_Prop_Error, XML_SetPropertyBool, (XML_Parser a0, enum XML_Parser_Property a1, XML_Bool a2), (a0, a1, a2), XML_PROP_ERROR_INVALID_KEY)
EXPAT_WRAP_RET(enum XML_Prop_Error, XML_SetPropertyDouble, (XML_Parser a0, enum XML_Parser_Property a1, double a2), (a0, a1, a2), XML_PROP_ERROR_INVALID_KEY)
EXPAT_WRAP_RET(enum XML_Prop_Error, XML_SetPropertyUInt64, (XML_Parser a0, enum XML_Parser_Property a1, uint64_t a2), (a0, a1, a2), XML_PROP_ERROR_INVALID_KEY)
EXPAT_WRAP_RET(XML_Bool, XML_SetReparseDeferralEnabled, (XML_Parser a0, XML_Bool a1), (a0, a1), XML_FALSE)
EXPAT_WRAP_VOID(XML_SetReturnNSTriplet, (XML_Parser a0, int a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetSkippedEntityHandler, (XML_Parser a0, XML_SkippedEntityHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetStartCdataSectionHandler, (XML_Parser a0, XML_StartCdataSectionHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetStartDoctypeDeclHandler, (XML_Parser a0, XML_StartDoctypeDeclHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetStartElementHandler, (XML_Parser a0, XML_StartElementHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetStartNamespaceDeclHandler, (XML_Parser a0, XML_StartNamespaceDeclHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetUnknownEncodingHandler, (XML_Parser a0, XML_UnknownEncodingHandler a1, void *a2), (a0, a1, a2))
EXPAT_WRAP_VOID(XML_SetUnparsedEntityDeclHandler, (XML_Parser a0, XML_UnparsedEntityDeclHandler a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetUserData, (XML_Parser a0, void *a1), (a0, a1))
EXPAT_WRAP_VOID(XML_SetXmlDeclHandler, (XML_Parser a0, XML_XmlDeclHandler a1), (a0, a1))
EXPAT_WRAP_RET(enum XML_Status, XML_StopParser, (XML_Parser a0, XML_Bool a1), (a0, a1), XML_STATUS_ERROR)
EXPAT_WRAP_RET(enum XML_Error, XML_UseForeignDTD, (XML_Parser a0, XML_Bool a1), (a0, a1), XML_ERROR_NONE)
EXPAT_WRAP_VOID(XML_UseParserAsHandlerArg, (XML_Parser a0), (a0))
