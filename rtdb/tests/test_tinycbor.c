/* tinycbor_dyn 测试：只链 -ldl，不链 libtinycbor
 * 覆盖：动态加载、37 个公开导出的符号探测，以及编码/解析/映射/
 *       标签/字符串/浮点/JSON/pretty 输出等真实调用。
 * 库缺失时按 TEST_SKIP 正常退出；本测试不作整数除法。
 */
#include "test_common.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---------- 与 tinycbor_dyn.c 一致的 ABI 类型（不引入目标库头文件） ---------- */
typedef enum CborType {
    CborIntegerType = 0x00, CborByteStringType = 0x40, CborTextStringType = 0x60,
    CborArrayType = 0x80, CborMapType = 0xa0, CborTagType = 0xc0,
    CborSimpleType = 0xe0, CborBooleanType = 0xf5, CborNullType = 0xf6,
    CborUndefinedType = 0xf7, CborHalfFloatType = 0xf9, CborFloatType = 0xfa,
    CborDoubleType = 0xfb, CborInvalidType = 0xff
} CborType;

typedef uint64_t CborTag;

typedef enum CborError {
    CborNoError = 0, CborUnknownError, CborErrorUnknownLength,
    CborErrorAdvancePastEOF, CborErrorIO,
    CborErrorGarbageAtEnd = 256, CborErrorUnexpectedEOF,
    CborErrorUnexpectedBreak, CborErrorUnknownType, CborErrorIllegalType,
    CborErrorIllegalNumber, CborErrorIllegalSimpleType, CborErrorNoMoreStringChunks,
    CborErrorUnknownSimpleType = 512, CborErrorUnknownTag,
    CborErrorInappropriateTagForType, CborErrorDuplicateObjectKeys,
    CborErrorInvalidUtf8TextString, CborErrorExcludedType, CborErrorExcludedValue,
    CborErrorImproperValue, CborErrorOverlongEncoding, CborErrorMapKeyNotString,
    CborErrorMapNotSorted, CborErrorMapKeysNotUnique,
    CborErrorTooManyItems = 768, CborErrorTooFewItems,
    CborErrorDataTooLarge = 1024, CborErrorNestingTooDeep,
    CborErrorUnsupportedType, CborErrorUnimplementedValidation,
    CborErrorJsonObjectKeyIsAggregate = 1280, CborErrorJsonObjectKeyNotString,
    CborErrorJsonNotImplemented, CborErrorOutOfMemory = (int)(~0U / 2 + 1),
    CborErrorInternalError = (int)(~0U / 2)
} CborError;

typedef enum CborEncoderAppendType {
    CborEncoderAppendCborData = 0, CborEncoderAppendStringData = 1,
    CborEncoderAppendRawData = 2
} CborEncoderAppendType;

typedef CborError (*CborEncoderWriteFunction)(void *, const void *, size_t, CborEncoderAppendType);

struct CborEncoder {
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

enum CborParserGlobalFlags { CborParserFlag_ExternalSource = 0x01 };

struct CborValue;
struct CborParserOperations {
    bool (*can_read_bytes)(void *token, size_t len);
    void *(*read_bytes)(void *token, void *dst, size_t offset, size_t len);
    void (*advance_bytes)(void *token, size_t len);
    CborError (*transfer_string)(void *token, const void **userptr, size_t offset, size_t len);
};
struct CborParser {
    union {
        const uint8_t *end;
        const struct CborParserOperations *ops;
    } source;
    enum CborParserGlobalFlags flags;
};
typedef struct CborParser CborParser;
struct CborValue {
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
    CborValidateShortestIntegrals = 0x0001,
    CborValidateNoIndeterminateLength = 0x0100,
    CborValidateCanonicalFormat = 0x0fff,
    CborValidateStrictMode = 0xfff00,
    CborValidateBasic = 0
};

typedef CborError (*CborStreamFunction)(void *token, const char *fmt, ...);

enum CborToJsonFlags {
    CborConvertAddMetadata = 1, CborConvertTagsToObjects = 2,
    CborConvertIgnoreTags = 0, CborConvertByteStringsToBase64Url = 4,
    CborConvertStringifyMapKeys = 8, CborConvertDefaultFlags = 0
};

/* ---------- 动态层接口 ---------- */
extern int tinycbor_dyn_init(const char *);
extern int tinycbor_dyn_libok(void);
extern void tinycbor_dyn_cleanup(void);
extern int tinycbor_dyn_has_symbol(const char *);

/* ---------- 37 个公开函数的原生声明 ---------- */
extern CborError cbor_encode_byte_string(CborEncoder *, const uint8_t *, size_t);
extern CborError cbor_encode_float_as_half_float(CborEncoder *, float);
extern CborError cbor_encode_floating_point(CborEncoder *, CborType, const void *);
extern CborError cbor_encode_int(CborEncoder *, int64_t);
extern CborError cbor_encode_negative_int(CborEncoder *, uint64_t);
extern CborError cbor_encode_raw(CborEncoder *, const uint8_t *, size_t);
extern CborError cbor_encoder_close_container(CborEncoder *, const CborEncoder *);
extern CborError cbor_encoder_close_container_checked(CborEncoder *, const CborEncoder *);
extern CborError cbor_encoder_create_array(CborEncoder *, CborEncoder *, size_t);
extern CborError cbor_encoder_create_map(CborEncoder *, CborEncoder *, size_t);
extern void cbor_encoder_init(CborEncoder *, uint8_t *, size_t, int);
extern void cbor_encoder_init_writer(CborEncoder *, CborEncoderWriteFunction, void *);
extern CborError cbor_encode_simple_value(CborEncoder *, uint8_t);
extern CborError cbor_encode_tag(CborEncoder *, CborTag);
extern CborError cbor_encode_text_string(CborEncoder *, const char *, size_t);
extern CborError cbor_encode_uint(CborEncoder *, uint64_t);
extern const char *cbor_error_string(CborError);
extern CborError cbor_parser_init(const uint8_t *, size_t, uint32_t, CborParser *, CborValue *);
extern CborError cbor_parser_init_reader(const struct CborParserOperations *, CborParser *, CborValue *, void *);
extern CborError cbor_value_advance(CborValue *);
extern CborError cbor_value_advance_fixed(CborValue *);
extern CborError cbor_value_calculate_string_length(const CborValue *, size_t *);
extern CborError cbor_value_enter_container(const CborValue *, CborValue *);
extern CborError cbor_value_get_half_float_as_float(const CborValue *, float *);
extern CborError cbor_value_get_int64_checked(const CborValue *, int64_t *);
extern CborError cbor_value_get_int_checked(const CborValue *, int *);
extern CborError cbor_value_leave_container(CborValue *, const CborValue *);
extern CborError cbor_value_map_find_value(const CborValue *, const char *, CborValue *);
extern CborError cbor_value_reparse(CborValue *);
extern CborError cbor_value_skip_tag(CborValue *);
extern CborError cbor_value_text_string_equals(const CborValue *, const char *, bool *);
extern CborError cbor_value_to_json_advance(FILE *, CborValue *, int);
extern CborError cbor_value_to_pretty_advance(FILE *, CborValue *);
extern CborError cbor_value_to_pretty_advance_flags(FILE *, CborValue *, int);
extern CborError cbor_value_to_pretty_stream(CborStreamFunction, void *, CborValue *, int);
extern CborError cbor_value_validate(const CborValue *, uint32_t);
extern CborError cbor_value_validate_basic(const CborValue *);

/* ---------- 回调 ---------- */
struct writer_state {
    uint8_t buf[64];
    size_t len;
};

static CborError test_writer(void *token, const void *data, size_t len, CborEncoderAppendType type) {
    struct writer_state *s = (struct writer_state *)token;
    (void)type;
    if (s->len + len > sizeof s->buf) return CborErrorOutOfMemory;
    memcpy(s->buf + s->len, data, len);
    s->len += len;
    return CborNoError;
}

static CborError pretty_sink(void *token, const char *fmt, ...) {
    (void)token;
    (void)fmt;
    return CborNoError;
}

int main(int argc, char **argv) {
    int errs = 0;
    unsigned int i, found = 0;
    const char *names[] = {
        "cbor_encode_byte_string",
        "cbor_encode_float_as_half_float",
        "cbor_encode_floating_point",
        "cbor_encode_int",
        "cbor_encode_negative_int",
        "cbor_encode_raw",
        "cbor_encoder_close_container",
        "cbor_encoder_close_container_checked",
        "cbor_encoder_create_array",
        "cbor_encoder_create_map",
        "cbor_encoder_init",
        "cbor_encoder_init_writer",
        "cbor_encode_simple_value",
        "cbor_encode_tag",
        "cbor_encode_text_string",
        "cbor_encode_uint",
        "cbor_error_string",
        "cbor_parser_init",
        "cbor_parser_init_reader",
        "cbor_value_advance",
        "cbor_value_advance_fixed",
        "cbor_value_calculate_string_length",
        "cbor_value_enter_container",
        "cbor_value_get_half_float_as_float",
        "cbor_value_get_int64_checked",
        "cbor_value_get_int_checked",
        "cbor_value_leave_container",
        "cbor_value_map_find_value",
        "cbor_value_reparse",
        "cbor_value_skip_tag",
        "cbor_value_text_string_equals",
        "cbor_value_to_json_advance",
        "cbor_value_to_pretty_advance",
        "cbor_value_to_pretty_advance_flags",
        "cbor_value_to_pretty_stream",
        "cbor_value_validate",
        "cbor_value_validate_basic"
    };

    if (tinycbor_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("tinycbor not installed, skipping tinycbor_dyn test\n");

    errs += CHECK(tinycbor_dyn_libok());
    errs += CHECK(!tinycbor_dyn_has_symbol("cbor_no_such_symbol_xyz"));
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (tinycbor_dyn_has_symbol(names[i])) found++;
    }
    printf("info: tinycbor public exports found = %u/%u\n",
           found, (unsigned int)(sizeof names / sizeof names[0]));
    errs += CHECK(found == sizeof names / sizeof names[0]);

    errs += CHECK(cbor_error_string(CborNoError) != NULL);

    /* uint 编码 -> 解析 -> 校验 -> 取值 */
    {
        uint8_t buf[64] = {0};
        CborEncoder enc;
        CborParser parser;
        CborValue it, re;
        int v32 = 0;
        int64_t v64 = 0;
        cbor_encoder_init(&enc, buf, sizeof buf, 0);
        errs += CHECK(cbor_encode_uint(&enc, 42) == CborNoError);
        errs += CHECK(buf[0] == 0x18 && buf[1] == 0x2a);
        errs += CHECK(cbor_parser_init(buf, (size_t)(enc.data.ptr - buf), 0, &parser, &it) == CborNoError);
        errs += CHECK(cbor_value_validate_basic(&it) == CborNoError);
        errs += CHECK(cbor_value_validate(&it, CborValidateBasic) == CborNoError);
        errs += CHECK(it.type == CborIntegerType);
        errs += CHECK(cbor_value_get_int_checked(&it, &v32) == CborNoError && v32 == 42);
        errs += CHECK(cbor_value_get_int64_checked(&it, &v64) == CborNoError && v64 == 42);
        re = it;
        errs += CHECK(cbor_value_reparse(&re) == CborNoError);
        errs += CHECK(cbor_value_advance_fixed(&it) == CborNoError);
        errs += CHECK(it.remaining == 0);
    }

    /* raw / 负整数 / simple / tag / 浮点 / 数组容器 */
    {
        uint8_t buf[128] = {0};
        CborEncoder enc, arr;
        const uint8_t raw[2] = {0x01, 0x02};
        double d = 1.0;
        cbor_encoder_init(&enc, buf, sizeof buf, 0);
        errs += CHECK(cbor_encode_raw(&enc, raw, 2) == CborNoError);
        errs += CHECK(buf[0] == 0x01 && buf[1] == 0x02);
        errs += CHECK(cbor_encode_negative_int(&enc, 1) == CborNoError);
        errs += CHECK(cbor_encode_simple_value(&enc, 20) == CborNoError);
        errs += CHECK(cbor_encode_tag(&enc, 24) == CborNoError);
        errs += CHECK(cbor_encode_float_as_half_float(&enc, 1.0f) == CborNoError);
        errs += CHECK(cbor_encode_floating_point(&enc, CborDoubleType, &d) == CborNoError);
        errs += CHECK(cbor_encode_byte_string(&enc, (const uint8_t *)"ab", 2) == CborNoError);
        errs += CHECK(cbor_encoder_create_array(&enc, &arr, 1) == CborNoError);
        errs += CHECK(cbor_encode_uint(&arr, 5) == CborNoError);
        errs += CHECK(cbor_encoder_close_container(&enc, &arr) == CborNoError);
    }

    /* writer 回调 */
    {
        CborEncoder enc;
        struct writer_state st;
        memset(&st, 0, sizeof st);
        cbor_encoder_init_writer(&enc, test_writer, &st);
        errs += CHECK(cbor_encode_uint(&enc, 1) == CborNoError);
        errs += CHECK(st.len == 1 && st.buf[0] == 0x01);
    }

    /* map 查找 */
    {
        uint8_t buf[128] = {0};
        CborEncoder enc, map;
        CborParser parser;
        CborValue root, elem;
        int v = 0;
        cbor_encoder_init(&enc, buf, sizeof buf, 0);
        errs += CHECK(cbor_encoder_create_map(&enc, &map, 1) == CborNoError);
        errs += CHECK(cbor_encode_text_string(&map, "k", 1) == CborNoError);
        errs += CHECK(cbor_encode_uint(&map, 7) == CborNoError);
        errs += CHECK(cbor_encoder_close_container_checked(&enc, &map) == CborNoError);
        errs += CHECK(cbor_parser_init(buf, (size_t)(enc.data.ptr - buf), 0, &parser, &root) == CborNoError);
        errs += CHECK(cbor_value_map_find_value(&root, "k", &elem) == CborNoError);
        errs += CHECK(cbor_value_get_int_checked(&elem, &v) == CborNoError && v == 7);
        errs += CHECK(cbor_value_advance(&elem) == CborNoError);
        errs += CHECK(cbor_value_leave_container(&root, &elem) == CborNoError);
    }

    /* 字符串比较 / 长度 / tag 跳过 */
    {
        uint8_t sbuf[16] = {0};
        uint8_t tbuf[8] = {0xd8, 0x18, 0x01, 0};
        CborEncoder enc;
        CborParser parser;
        CborValue it;
        bool eq = false;
        size_t len = 0;
        int v = 0;
        cbor_encoder_init(&enc, sbuf, sizeof sbuf, 0);
        errs += CHECK(cbor_encode_text_string(&enc, "hi", 2) == CborNoError);
        errs += CHECK(cbor_parser_init(sbuf, (size_t)(enc.data.ptr - sbuf), 0, &parser, &it) == CborNoError);
        errs += CHECK(cbor_value_text_string_equals(&it, "hi", &eq) == CborNoError && eq);
        errs += CHECK(cbor_value_calculate_string_length(&it, &len) == CborNoError && len == 2);

        errs += CHECK(cbor_parser_init(tbuf, 3, 0, &parser, &it) == CborNoError);
        errs += CHECK(cbor_value_skip_tag(&it) == CborNoError);
        errs += CHECK(cbor_value_get_int_checked(&it, &v) == CborNoError && v == 1);
    }

    /* half float 解析 */
    {
        uint8_t buf[8] = {0xf9, 0x3c, 0x00, 0};
        CborParser parser;
        CborValue it;
        float f = 0.0f;
        errs += CHECK(cbor_parser_init(buf, 3, 0, &parser, &it) == CborNoError);
        errs += CHECK(cbor_value_get_half_float_as_float(&it, &f) == CborNoError && f == 1.0f);
    }

    /* pretty / JSON 输出 */
    {
        uint8_t buf[8] = {0};
        CborEncoder enc;
        CborParser parser;
        CborValue it, p1, p2, p3, p4;
        FILE *out = tmpfile();
        cbor_encoder_init(&enc, buf, sizeof buf, 0);
        errs += CHECK(cbor_encode_uint(&enc, 3) == CborNoError);
        errs += CHECK(cbor_parser_init(buf, (size_t)(enc.data.ptr - buf), 0, &parser, &it) == CborNoError);
        errs += CHECK(out != NULL);
        if (out) {
            p1 = it; p2 = it; p3 = it; p4 = it;
            errs += CHECK(cbor_value_to_pretty_stream(pretty_sink, NULL, &p1, 0) == CborNoError);
            errs += CHECK(cbor_value_to_pretty_advance_flags(out, &p2, 0) == CborNoError);
            errs += CHECK(cbor_value_to_pretty_advance(out, &p3) == CborNoError);
            errs += CHECK(cbor_value_to_json_advance(out, &p4, CborConvertDefaultFlags) == CborNoError);
            fclose(out);
        }
    }

    /* advance 遍历 */
    {
        uint8_t buf[8] = {0};
        CborEncoder enc;
        CborParser parser;
        CborValue it;
        cbor_encoder_init(&enc, buf, sizeof buf, 0);
        errs += CHECK(cbor_encode_uint(&enc, 9) == CborNoError);
        errs += CHECK(cbor_parser_init(buf, (size_t)(enc.data.ptr - buf), 0, &parser, &it) == CborNoError);
        errs += CHECK(cbor_value_advance(&it) == CborNoError);
        errs += CHECK(it.remaining == 0);
    }

    tinycbor_dyn_cleanup();
    if (errs) { fprintf(stderr, "tinycbor_dyn: %d failure(s)\n", errs); return 1; }
    printf("tinycbor_dyn: all checks passed\n");
    return 0;
}
