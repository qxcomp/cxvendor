/* ============================================================
 * tinycbor_dyn.c - dlopen 模拟链接：Intel TinyCBOR (C99)
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照上游 main 的 src/cbor.h（36 个 CBOR_API）
 * 与 src/cborjson.h（1 个 CBOR_API），共 37 个公开函数声明。
 * 排除 56 个 CBOR_INLINE_API/static inline、7 个 CBOR_PRIVATE_API
 * (_cbor_value_*) 以及所有宏；导出函数名保持原生原样。
 * 类型按公开头文件原样内嵌，字段顺序即 ABI；链接期不需要
 * libtinycbor，仅需 -ldl。
 * 加载顺序：显式路径 > TINYCBOR_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > Nix > 平台默认；成功后不卸载。
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
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
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

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<tinycbor/cbor.h>) && __has_include(<tinycbor/cborjson.h>)
#    include <tinycbor/cbor.h>
#    include <tinycbor/cborjson.h>
#    define TINYCBOR_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef TINYCBOR_DYN_HAVE_HEADER
/* ---------- 类型（src/cbor.h / src/cborjson.h 原样内嵌） ---------- */
typedef enum CborType {
    CborIntegerType     = 0x00,
    CborByteStringType  = 0x40,
    CborTextStringType  = 0x60,
    CborArrayType       = 0x80,
    CborMapType         = 0xa0,
    CborTagType         = 0xc0,
    CborSimpleType      = 0xe0,
    CborBooleanType     = 0xf5,
    CborNullType        = 0xf6,
    CborUndefinedType   = 0xf7,
    CborHalfFloatType   = 0xf9,
    CborFloatType       = 0xfa,
    CborDoubleType      = 0xfb,
    CborInvalidType     = 0xff
} CborType;

typedef uint64_t CborTag;

typedef enum CborKnownTags {
    CborDateTimeStringTag          = 0,
    CborUnixTime_tTag              = 1,
    CborPositiveBignumTag          = 2,
    CborNegativeBignumTag          = 3,
    CborDecimalTag                 = 4,
    CborBigfloatTag                = 5,
    CborCOSE_Encrypt0Tag           = 16,
    CborCOSE_Mac0Tag               = 17,
    CborCOSE_Sign1Tag              = 18,
    CborExpectedBase64urlTag       = 21,
    CborExpectedBase64Tag          = 22,
    CborExpectedBase16Tag          = 23,
    CborEncodedCborTag             = 24,
    CborUrlTag                     = 32,
    CborBase64urlTag               = 33,
    CborBase64Tag                  = 34,
    CborRegularExpressionTag       = 35,
    CborMimeMessageTag             = 36,
    CborCOSE_EncryptTag            = 96,
    CborCOSE_MacTag                = 97,
    CborCOSE_SignTag               = 98,
    CborSignatureTag               = 55799
} CborKnownTags;

typedef enum CborError {
    CborNoError = 0,
    CborUnknownError,
    CborErrorUnknownLength,
    CborErrorAdvancePastEOF,
    CborErrorIO,
    CborErrorGarbageAtEnd = 256,
    CborErrorUnexpectedEOF,
    CborErrorUnexpectedBreak,
    CborErrorUnknownType,
    CborErrorIllegalType,
    CborErrorIllegalNumber,
    CborErrorIllegalSimpleType,
    CborErrorNoMoreStringChunks,
    CborErrorUnknownSimpleType = 512,
    CborErrorUnknownTag,
    CborErrorInappropriateTagForType,
    CborErrorDuplicateObjectKeys,
    CborErrorInvalidUtf8TextString,
    CborErrorExcludedType,
    CborErrorExcludedValue,
    CborErrorImproperValue,
    CborErrorOverlongEncoding,
    CborErrorMapKeyNotString,
    CborErrorMapNotSorted,
    CborErrorMapKeysNotUnique,
    CborErrorTooManyItems = 768,
    CborErrorTooFewItems,
    CborErrorDataTooLarge = 1024,
    CborErrorNestingTooDeep,
    CborErrorUnsupportedType,
    CborErrorUnimplementedValidation,
    CborErrorJsonObjectKeyIsAggregate = 1280,
    CborErrorJsonObjectKeyNotString,
    CborErrorJsonNotImplemented,
    CborErrorOutOfMemory = (int)(~0U / 2 + 1),
    CborErrorInternalError = (int)(~0U / 2)
} CborError;

typedef enum CborEncoderAppendType {
    CborEncoderAppendCborData = 0,
    CborEncoderAppendStringData = 1,
    CborEncoderAppendRawData = 2
} CborEncoderAppendType;

typedef CborError (*CborEncoderWriteFunction)(void *, const void *, size_t, CborEncoderAppendType);

enum CborEncoderFlags {
    CborIteratorFlag_WriterFunction         = 0x01,
    CborIteratorFlag_ContainerIsMap_        = 0x20
};

struct CborEncoder
{
    union {
        uint8_t *ptr;
        ptrdiff_t bytes_needed;
        CborEncoderWriteFunction writer;
    } data;
    uint8_t *end;
    size_t remaining;
    int flags;
};
typedef struct CborEncoder CborEncoder;

enum CborParserGlobalFlags {
    CborParserFlag_ExternalSource = 0x01
};

enum CborParserIteratorFlags {
    CborIteratorFlag_IntegerValueIs64Bit    = 0x01,
    CborIteratorFlag_IntegerValueTooLarge   = 0x02,
    CborIteratorFlag_NegativeInteger        = 0x04,
    CborIteratorFlag_BeforeFirstStringChunk = 0x04,
    CborIteratorFlag_IteratingStringChunks  = 0x08,
    CborIteratorFlag_UnknownLength          = 0x10,
    CborIteratorFlag_ContainerIsMap         = 0x20,
    CborIteratorFlag_NextIsMapKey           = 0x40
};

struct CborValue;
struct CborParserOperations
{
    bool (*can_read_bytes)(void *token, size_t len);
    void *(*read_bytes)(void *token, void *dst, size_t offset, size_t len);
    void (*advance_bytes)(void *token, size_t len);
    CborError (*transfer_string)(void *token, const void **userptr, size_t offset, size_t len);
};

struct CborParser
{
    union {
        const uint8_t *end;
        const struct CborParserOperations *ops;
    } source;
    enum CborParserGlobalFlags flags;
};
typedef struct CborParser CborParser;

struct CborValue
{
    const CborParser *parser;
    union {
        const uint8_t *ptr;
        void *token;
    } source;
    uint32_t remaining;
    uint16_t extra;
    uint8_t type;
    uint8_t flags;
};
typedef struct CborValue CborValue;

enum CborValidationFlags {
    CborValidateShortestIntegrals           = 0x0001,
    CborValidateShortestFloatingPoint       = 0x0002,
    CborValidateShortestNumbers             = CborValidateShortestIntegrals | CborValidateShortestFloatingPoint,
    CborValidateNoIndeterminateLength       = 0x0100,
    CborValidateMapIsSorted                 = 0x0200 | CborValidateNoIndeterminateLength,
    CborValidateCanonicalFormat             = 0x0fff,
    CborValidateMapKeysAreUnique            = 0x1000 | CborValidateMapIsSorted,
    CborValidateTagUse                      = 0x2000,
    CborValidateUtf8                        = 0x4000,
    CborValidateStrictMode                  = 0xfff00,
    CborValidateMapKeysAreString            = 0x100000,
    CborValidateNoUndefined                 = 0x200000,
    CborValidateNoTags                      = 0x400000,
    CborValidateFiniteFloatingPoint         = 0x800000,
    CborValidateNoUnknownSimpleTypesSA      = 0x4000000,
    CborValidateNoUnknownSimpleTypes        = 0x8000000 | CborValidateNoUnknownSimpleTypesSA,
    CborValidateNoUnknownTagsSA             = 0x10000000,
    CborValidateNoUnknownTagsSR             = 0x20000000 | CborValidateNoUnknownTagsSA,
    CborValidateNoUnknownTags               = 0x40000000 | CborValidateNoUnknownTagsSR,
    CborValidateCompleteData                = (int)0x80000000,
    CborValidateStrictest                   = (int)~0U,
    CborValidateBasic                       = 0
};

enum CborPrettyFlags {
    CborPrettyNumericEncodingIndicators     = 0x01,
    CborPrettyTextualEncodingIndicators     = 0,
    CborPrettyIndicateIndeterminateLength   = 0x02,
    CborPrettyIndicateIndetermineLength     = CborPrettyIndicateIndeterminateLength,
    CborPrettyIndicateOverlongNumbers       = 0x04,
    CborPrettyShowStringFragments           = 0x100,
    CborPrettyMergeStringFragments          = 0,
    CborPrettyDefaultFlags                  = CborPrettyIndicateIndeterminateLength
};

typedef CborError (*CborStreamFunction)(void *token, const char *fmt, ...);

enum CborToJsonFlags {
    CborConvertAddMetadata = 1,
    CborConvertTagsToObjects = 2,
    CborConvertIgnoreTags = 0,
    CborConvertObeyByteStringTags = 0,
    CborConvertByteStringsToBase64Url = 4,
    CborConvertRequireMapStringKeys = 0,
    CborConvertStringifyMapKeys = 8,
    CborConvertDefaultFlags = 0
};

#endif /* TINYCBOR_DYN_HAVE_HEADER */

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static lib_handle_t tinycbor_handle = NULL;
static int tinycbor_init_state = 0;

static void *tinycbor_sym(const char *name) {
    return tinycbor_handle ? get_sym(tinycbor_handle, name) : NULL;
}

static int tinycbor_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    const char *so_soname = "libtinycbor.so.0";
    const char *so_ver7 = "libtinycbor.so.0.7";
    const char *so_ver6 = "libtinycbor.so.0.6";
    const char *so_plain = "libtinycbor.so";
    int n = 0, i;
    int res = -1;

    if (tinycbor_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("TINYCBOR_DYN_LIB");
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
                    if (*q && n + 4 < 96) {
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_soname);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_ver7);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_ver6);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_plain);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = so_soname;
    candidates[n++] = so_ver7;
    candidates[n++] = so_ver6;
    candidates[n++] = so_plain;
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/%s", home, so_soname);
            candidates[n] = bufs[n]; n++;
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/%s", home, so_ver6);
            candidates[n] = bufs[n]; n++;
        }
        snprintf(bufs[n], sizeof bufs[0], "/nix/var/nix/profiles/default/lib/%s", so_soname);
        candidates[n] = bufs[n]; n++;
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libtinycbor.dylib";
    candidates[n++] = "/usr/local/lib/libtinycbor.dylib";
#elif defined(_WIN32)
    candidates[n++] = "tinycbor.dll";
    candidates[n++] = "libtinycbor.dll";
#else
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_ver6);
    candidates[n] = bufs[n]; n++;
#endif

    for (i = 0; i < n; i++) {
        tinycbor_handle = load_lib(candidates[i]);
        // printf("try %d %s\n", i, candidates[i]);
        if (tinycbor_handle) { res = i; break; }
    }
    if (!tinycbor_handle || !tinycbor_sym("cbor_parser_init")) {
        fprintf(stderr, "tinycbor_dyn: cannot load tinycbor (%s)\n", lib_error());
        if (tinycbor_handle) { free_lib(tinycbor_handle); tinycbor_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "tinycbor_dyn: loaded tinycbor %d %s\n", res, candidates[res]);
    return 0;
}

int tinycbor_dyn_libok(void) {
    if (tinycbor_init_state == 0)
        tinycbor_init_state = (tinycbor_do_load(NULL) == 0) ? 1 : -1;
    return (tinycbor_init_state == 1 && tinycbor_handle != NULL) ? 1 : 0;
}

int tinycbor_dyn_init(const char *library_path) {
    if (tinycbor_do_load(library_path) == 0) {
        tinycbor_init_state = 1;
        return 0;
    }
    if (tinycbor_init_state == 0) tinycbor_init_state = -1;
    return -1;
}

void tinycbor_dyn_cleanup(void) {
    if (tinycbor_handle) { free_lib(tinycbor_handle); tinycbor_handle = NULL; }
    tinycbor_init_state = 0;
}

int tinycbor_dyn_has_symbol(const char *name) {
    if (!tinycbor_dyn_libok()) return 0;
    return tinycbor_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                            \
ret name params {                                                              \
    static ret (*fn) params = NULL;                                            \
    if (!fn) {                                                                 \
        if (!tinycbor_dyn_libok()) return fallback;                            \
        *(void **)(&fn) = tinycbor_sym(#name);                                 \
        if (!fn) {                                                             \
            fprintf(stderr, "tinycbor_dyn: dlsym failed: %s\n", #name);        \
            return fallback;                                                   \
        }                                                                      \
    }                                                                          \
    return fn args;                                                            \
}
#define WRAP_VOID(name, params, args)                                          \
void name params {                                                             \
    static void (*fn) params = NULL;                                           \
    if (!fn) {                                                                 \
        if (!tinycbor_dyn_libok()) return;                                     \
        *(void **)(&fn) = tinycbor_sym(#name);                                 \
        if (!fn) {                                                             \
            fprintf(stderr, "tinycbor_dyn: dlsym failed: %s\n", #name);        \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn args;                                                                   \
}

/* ---------- 37 个 CBOR_API 公开函数（函数名原样） ---------- */
WRAP_RET(CborError, cbor_encode_byte_string, (CborEncoder *encoder, const uint8_t *string, size_t length), (encoder, string, length), CborNoError)
WRAP_RET(CborError, cbor_encode_float_as_half_float, (CborEncoder *encoder, float value), (encoder, value), CborNoError)
WRAP_RET(CborError, cbor_encode_floating_point, (CborEncoder *encoder, CborType fpType, const void *value), (encoder, fpType, value), CborNoError)
WRAP_RET(CborError, cbor_encode_int, (CborEncoder *encoder, int64_t value), (encoder, value), CborNoError)
WRAP_RET(CborError, cbor_encode_negative_int, (CborEncoder *encoder, uint64_t absolute_value), (encoder, absolute_value), CborNoError)
WRAP_RET(CborError, cbor_encode_raw, (CborEncoder *encoder, const uint8_t *raw, size_t length), (encoder, raw, length), CborNoError)
WRAP_RET(CborError, cbor_encoder_close_container, (CborEncoder *parentEncoder, const CborEncoder *containerEncoder), (parentEncoder, containerEncoder), CborNoError)
WRAP_RET(CborError, cbor_encoder_close_container_checked, (CborEncoder *parentEncoder, const CborEncoder *containerEncoder), (parentEncoder, containerEncoder), CborNoError)
WRAP_RET(CborError, cbor_encoder_create_array, (CborEncoder *parentEncoder, CborEncoder *arrayEncoder, size_t length), (parentEncoder, arrayEncoder, length), CborNoError)
WRAP_RET(CborError, cbor_encoder_create_map, (CborEncoder *parentEncoder, CborEncoder *mapEncoder, size_t length), (parentEncoder, mapEncoder, length), CborNoError)
WRAP_VOID(cbor_encoder_init, (CborEncoder *encoder, uint8_t *buffer, size_t size, int flags), (encoder, buffer, size, flags))
WRAP_VOID(cbor_encoder_init_writer, (CborEncoder *encoder, CborEncoderWriteFunction writer, void *token), (encoder, writer, token))
WRAP_RET(CborError, cbor_encode_simple_value, (CborEncoder *encoder, uint8_t value), (encoder, value), CborNoError)
WRAP_RET(CborError, cbor_encode_tag, (CborEncoder *encoder, CborTag tag), (encoder, tag), CborNoError)
WRAP_RET(CborError, cbor_encode_text_string, (CborEncoder *encoder, const char *string, size_t length), (encoder, string, length), CborNoError)
WRAP_RET(CborError, cbor_encode_uint, (CborEncoder *encoder, uint64_t value), (encoder, value), CborNoError)
WRAP_RET(const char *, cbor_error_string, (CborError error), (error), NULL)
WRAP_RET(CborError, cbor_parser_init, (const uint8_t *buffer, size_t size, uint32_t flags, CborParser *parser, CborValue *it), (buffer, size, flags, parser, it), CborNoError)
WRAP_RET(CborError, cbor_parser_init_reader, (const struct CborParserOperations *ops, CborParser *parser, CborValue *it, void *token), (ops, parser, it, token), CborNoError)
WRAP_RET(CborError, cbor_value_advance, (CborValue *it), (it), CborNoError)
WRAP_RET(CborError, cbor_value_advance_fixed, (CborValue *it), (it), CborNoError)
WRAP_RET(CborError, cbor_value_calculate_string_length, (const CborValue *value, size_t *length), (value, length), CborNoError)
WRAP_RET(CborError, cbor_value_enter_container, (const CborValue *it, CborValue *recursed), (it, recursed), CborNoError)
WRAP_RET(CborError, cbor_value_get_half_float_as_float, (const CborValue *value, float *result), (value, result), CborNoError)
WRAP_RET(CborError, cbor_value_get_int64_checked, (const CborValue *value, int64_t *result), (value, result), CborNoError)
WRAP_RET(CborError, cbor_value_get_int_checked, (const CborValue *value, int *result), (value, result), CborNoError)
WRAP_RET(CborError, cbor_value_leave_container, (CborValue *it, const CborValue *recursed), (it, recursed), CborNoError)
WRAP_RET(CborError, cbor_value_map_find_value, (const CborValue *map, const char *string, CborValue *element), (map, string, element), CborNoError)
WRAP_RET(CborError, cbor_value_reparse, (CborValue *it), (it), CborNoError)
WRAP_RET(CborError, cbor_value_skip_tag, (CborValue *it), (it), CborNoError)
WRAP_RET(CborError, cbor_value_text_string_equals, (const CborValue *value, const char *string, bool *result), (value, string, result), CborNoError)
WRAP_RET(CborError, cbor_value_to_json_advance, (FILE *out, CborValue *value, int flags), (out, value, flags), CborNoError)
WRAP_RET(CborError, cbor_value_to_pretty_advance, (FILE *out, CborValue *value), (out, value), CborNoError)
WRAP_RET(CborError, cbor_value_to_pretty_advance_flags, (FILE *out, CborValue *value, int flags), (out, value, flags), CborNoError)
WRAP_RET(CborError, cbor_value_to_pretty_stream, (CborStreamFunction streamFunction, void *token, CborValue *value, int flags), (streamFunction, token, value, flags), CborNoError)
WRAP_RET(CborError, cbor_value_validate, (const CborValue *it, uint32_t flags), (it, flags), CborNoError)
WRAP_RET(CborError, cbor_value_validate_basic, (const CborValue *it), (it), CborNoError)
