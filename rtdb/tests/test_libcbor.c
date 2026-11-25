/* libcbor_dyn 测试：只链 -ldl，不链 libcbor
 * 覆盖：动态加载、181 个公开导出的符号探测，以及构造/访问/序列化/
 *       流式解码/引用计数/容器/标签/映射等真实调用。
 * 库缺失时按 TEST_SKIP 正常退出。
 */
#include "test_common.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct cbor_item_t cbor_item_t;
typedef const unsigned char *cbor_data;
typedef unsigned char *cbor_mutable_data;

typedef enum cbor_type {
  CBOR_TYPE_UINT, CBOR_TYPE_NEGINT, CBOR_TYPE_BYTESTRING, CBOR_TYPE_STRING,
  CBOR_TYPE_ARRAY, CBOR_TYPE_MAP, CBOR_TYPE_TAG, CBOR_TYPE_FLOAT_CTRL
} cbor_type;
typedef enum { CBOR_INT_8, CBOR_INT_16, CBOR_INT_32, CBOR_INT_64 } cbor_int_width;
typedef enum { CBOR_FLOAT_0, CBOR_FLOAT_16, CBOR_FLOAT_32, CBOR_FLOAT_64 } cbor_float_width;

struct cbor_pair { cbor_item_t *key, *value; };
enum cbor_decoder_status { CBOR_DECODER_FINISHED, CBOR_DECODER_NEDATA, CBOR_DECODER_ERROR };
struct cbor_decoder_result { size_t read; enum cbor_decoder_status status; size_t required; };

typedef void (*cbor_int8_callback)(void*, uint8_t);
typedef void (*cbor_int16_callback)(void*, uint16_t);
typedef void (*cbor_int32_callback)(void*, uint32_t);
typedef void (*cbor_int64_callback)(void*, uint64_t);
typedef void (*cbor_simple_callback)(void*);
typedef void (*cbor_string_callback)(void*, cbor_data, uint64_t);
typedef void (*cbor_collection_callback)(void*, uint64_t);
typedef void (*cbor_float_callback)(void*, float);
typedef void (*cbor_double_callback)(void*, double);
typedef void (*cbor_bool_callback)(void*, bool);

struct cbor_callbacks {
  cbor_int8_callback uint8;  cbor_int16_callback uint16;
  cbor_int32_callback uint32; cbor_int64_callback uint64;
  cbor_int64_callback negint64; cbor_int32_callback negint32;
  cbor_int16_callback negint16; cbor_int8_callback negint8;
  cbor_simple_callback byte_string_start; cbor_string_callback byte_string;
  cbor_string_callback string; cbor_simple_callback string_start;
  cbor_simple_callback indef_array_start; cbor_collection_callback array_start;
  cbor_simple_callback indef_map_start; cbor_collection_callback map_start;
  cbor_int64_callback tag;
  cbor_float_callback float2; cbor_float_callback float4;
  cbor_double_callback float8;
  cbor_simple_callback undefined; cbor_simple_callback null;
  cbor_bool_callback boolean; cbor_simple_callback indef_break;
};

extern int libcbor_dyn_init(const char *);
extern int libcbor_dyn_libok(void);
extern void libcbor_dyn_cleanup(void);
extern int libcbor_dyn_has_symbol(const char *);

extern cbor_item_t *cbor_build_uint8(uint8_t);
extern cbor_item_t *cbor_build_bool(bool);
extern cbor_item_t *cbor_build_tag(uint64_t, cbor_item_t*);
extern cbor_item_t *cbor_build_string(const char*);
extern cbor_item_t *cbor_new_null(void);
extern cbor_item_t *cbor_new_definite_array(size_t);
extern cbor_item_t *cbor_new_definite_map(size_t);
extern cbor_item_t *cbor_incref(cbor_item_t*);
extern void cbor_decref(cbor_item_t**);
extern void cbor_intermediate_decref(cbor_item_t*);
extern size_t cbor_refcount(const cbor_item_t*);
extern cbor_type cbor_typeof(const cbor_item_t*);
extern bool cbor_isa_uint(const cbor_item_t*);
extern bool cbor_isa_float_ctrl(const cbor_item_t*);
extern bool cbor_is_int(const cbor_item_t*);
extern bool cbor_is_null(const cbor_item_t*);
extern uint8_t cbor_get_uint8(const cbor_item_t*);
extern uint64_t cbor_get_int(const cbor_item_t*);
extern cbor_int_width cbor_int_get_width(const cbor_item_t*);
extern size_t cbor_serialized_size(const cbor_item_t*);
extern size_t cbor_serialize(const cbor_item_t*, cbor_mutable_data, size_t);
extern size_t cbor_encode_uint8(uint8_t, unsigned char*, size_t);
extern struct cbor_decoder_result cbor_stream_decode(cbor_data, size_t, const struct cbor_callbacks*, void*);
extern bool cbor_structurally_equal(const cbor_item_t*, const cbor_item_t*);
extern size_t cbor_array_size(const cbor_item_t*);
extern size_t cbor_array_allocated(const cbor_item_t*);
extern bool cbor_array_is_definite(const cbor_item_t*);
extern bool cbor_array_push(cbor_item_t*, cbor_item_t*);
extern cbor_item_t *cbor_array_get(const cbor_item_t*, size_t);
extern size_t cbor_string_length(const cbor_item_t*);
extern size_t cbor_string_codepoint_count(const cbor_item_t*);
extern bool cbor_string_is_definite(const cbor_item_t*);
extern uint64_t cbor_tag_value(const cbor_item_t*);
extern cbor_item_t *cbor_tag_item(const cbor_item_t*);
extern bool cbor_map_add(cbor_item_t*, struct cbor_pair);
extern size_t cbor_map_size(const cbor_item_t*);
extern void cbor_null_uint8_callback(void*, uint8_t);
extern void cbor_null_uint16_callback(void*, uint16_t);
extern void cbor_null_uint32_callback(void*, uint32_t);
extern void cbor_null_uint64_callback(void*, uint64_t);
extern void cbor_null_negint8_callback(void*, uint8_t);
extern void cbor_null_negint16_callback(void*, uint16_t);
extern void cbor_null_negint32_callback(void*, uint32_t);
extern void cbor_null_negint64_callback(void*, uint64_t);
extern void cbor_null_string_callback(void*, cbor_data, uint64_t);
extern void cbor_null_string_start_callback(void*);
extern void cbor_null_byte_string_callback(void*, cbor_data, uint64_t);
extern void cbor_null_byte_string_start_callback(void*);
extern void cbor_null_array_start_callback(void*, uint64_t);
extern void cbor_null_indef_array_start_callback(void*);
extern void cbor_null_map_start_callback(void*, uint64_t);
extern void cbor_null_indef_map_start_callback(void*);
extern void cbor_null_tag_callback(void*, uint64_t);
extern void cbor_null_float2_callback(void*, float);
extern void cbor_null_float4_callback(void*, float);
extern void cbor_null_float8_callback(void*, double);
extern void cbor_null_null_callback(void*);
extern void cbor_null_undefined_callback(void*);
extern void cbor_null_boolean_callback(void*, bool);
extern void cbor_null_indef_break_callback(void*);

int main(void) {
    int errs = 0;

    if (libcbor_dyn_init(NULL) != 0)
        TEST_SKIP("libcbor not installed, skipping libcbor_dyn test\n");

    errs += CHECK(libcbor_dyn_libok());
    errs += CHECK(!libcbor_dyn_has_symbol("cbor_no_such_symbol_xyz"));

    /* 181 个公开声明逐一探测 dlsym */
    {
        const char *names[] = {
            "cbor_array_allocated",
            "cbor_array_get",
            "cbor_array_handle",
            "cbor_array_is_definite",
            "cbor_array_is_indefinite",
            "cbor_array_push",
            "cbor_array_replace",
            "cbor_array_set",
            "cbor_array_size",
            "cbor_build_bool",
            "cbor_build_bytestring",
            "cbor_build_ctrl",
            "cbor_build_float2",
            "cbor_build_float4",
            "cbor_build_float8",
            "cbor_build_negint16",
            "cbor_build_negint32",
            "cbor_build_negint64",
            "cbor_build_negint8",
            "cbor_build_string",
            "cbor_build_stringn",
            "cbor_build_tag",
            "cbor_build_uint16",
            "cbor_build_uint32",
            "cbor_build_uint64",
            "cbor_build_uint8",
            "cbor_bytestring_add_chunk",
            "cbor_bytestring_chunk_count",
            "cbor_bytestring_chunks_handle",
            "cbor_bytestring_handle",
            "cbor_bytestring_is_definite",
            "cbor_bytestring_is_indefinite",
            "cbor_bytestring_length",
            "cbor_bytestring_set_handle",
            "cbor_ctrl_value",
            "cbor_decref",
            "cbor_encode_array_start",
            "cbor_encode_bool",
            "cbor_encode_break",
            "cbor_encode_bytestring_start",
            "cbor_encode_ctrl",
            "cbor_encode_double",
            "cbor_encode_half",
            "cbor_encode_indef_array_start",
            "cbor_encode_indef_bytestring_start",
            "cbor_encode_indef_map_start",
            "cbor_encode_indef_string_start",
            "cbor_encode_map_start",
            "cbor_encode_negint",
            "cbor_encode_negint16",
            "cbor_encode_negint32",
            "cbor_encode_negint64",
            "cbor_encode_negint8",
            "cbor_encode_null",
            "cbor_encode_single",
            "cbor_encode_string_start",
            "cbor_encode_tag",
            "cbor_encode_uint",
            "cbor_encode_uint16",
            "cbor_encode_uint32",
            "cbor_encode_uint64",
            "cbor_encode_uint8",
            "cbor_encode_undef",
            "cbor_float_ctrl_is_ctrl",
            "cbor_float_get_float",
            "cbor_float_get_float2",
            "cbor_float_get_float4",
            "cbor_float_get_float8",
            "cbor_float_get_width",
            "cbor_get_bool",
            "cbor_get_int",
            "cbor_get_uint16",
            "cbor_get_uint32",
            "cbor_get_uint64",
            "cbor_get_uint8",
            "cbor_incref",
            "cbor_int_get_width",
            "cbor_intermediate_decref",
            "cbor_is_bool",
            "cbor_is_float",
            "cbor_is_int",
            "cbor_is_null",
            "cbor_is_undef",
            "cbor_isa_array",
            "cbor_isa_bytestring",
            "cbor_isa_float_ctrl",
            "cbor_isa_map",
            "cbor_isa_negint",
            "cbor_isa_string",
            "cbor_isa_tag",
            "cbor_isa_uint",
            "cbor_map_add",
            "cbor_map_allocated",
            "cbor_map_get",
            "cbor_map_handle",
            "cbor_map_is_definite",
            "cbor_map_is_indefinite",
            "cbor_map_size",
            "cbor_mark_negint",
            "cbor_mark_uint",
            "cbor_move",
            "cbor_new_ctrl",
            "cbor_new_definite_array",
            "cbor_new_definite_bytestring",
            "cbor_new_definite_map",
            "cbor_new_definite_string",
            "cbor_new_float2",
            "cbor_new_float4",
            "cbor_new_float8",
            "cbor_new_indefinite_array",
            "cbor_new_indefinite_bytestring",
            "cbor_new_indefinite_map",
            "cbor_new_indefinite_string",
            "cbor_new_int16",
            "cbor_new_int32",
            "cbor_new_int64",
            "cbor_new_int8",
            "cbor_new_null",
            "cbor_new_tag",
            "cbor_new_undef",
            "cbor_null_array_start_callback",
            "cbor_null_boolean_callback",
            "cbor_null_byte_string_callback",
            "cbor_null_byte_string_start_callback",
            "cbor_null_float2_callback",
            "cbor_null_float4_callback",
            "cbor_null_float8_callback",
            "cbor_null_indef_array_start_callback",
            "cbor_null_indef_break_callback",
            "cbor_null_indef_map_start_callback",
            "cbor_null_map_start_callback",
            "cbor_null_negint16_callback",
            "cbor_null_negint32_callback",
            "cbor_null_negint64_callback",
            "cbor_null_negint8_callback",
            "cbor_null_null_callback",
            "cbor_null_string_callback",
            "cbor_null_string_start_callback",
            "cbor_null_tag_callback",
            "cbor_null_uint16_callback",
            "cbor_null_uint32_callback",
            "cbor_null_uint64_callback",
            "cbor_null_uint8_callback",
            "cbor_null_undefined_callback",
            "cbor_refcount",
            "cbor_serialize",
            "cbor_serialize_alloc",
            "cbor_serialize_array",
            "cbor_serialize_bytestring",
            "cbor_serialize_float_ctrl",
            "cbor_serialize_map",
            "cbor_serialize_negint",
            "cbor_serialize_string",
            "cbor_serialize_tag",
            "cbor_serialize_uint",
            "cbor_serialized_size",
            "cbor_set_allocs",
            "cbor_set_bool",
            "cbor_set_ctrl",
            "cbor_set_float2",
            "cbor_set_float4",
            "cbor_set_float8",
            "cbor_set_uint16",
            "cbor_set_uint32",
            "cbor_set_uint64",
            "cbor_set_uint8",
            "cbor_stream_decode",
            "cbor_string_add_chunk",
            "cbor_string_chunk_count",
            "cbor_string_chunks_handle",
            "cbor_string_codepoint_count",
            "cbor_string_handle",
            "cbor_string_is_definite",
            "cbor_string_is_indefinite",
            "cbor_string_length",
            "cbor_string_set_handle",
            "cbor_structurally_equal",
            "cbor_tag_item",
            "cbor_tag_set_item",
            "cbor_tag_value",
            "cbor_typeof"
        };
        unsigned int i, found = 0;
        for (i = 0; i < sizeof names / sizeof names[0]; i++)
            if (libcbor_dyn_has_symbol(names[i])) found++;
        printf("info: libcbor public exports found = %u/%u\n",
               found, (unsigned int)(sizeof names / sizeof names[0]));
        errs += CHECK(found == sizeof names / sizeof names[0]);
    }

    /* 构造 / 类型 / 序列化 */
    {
        cbor_item_t *it = cbor_build_uint8(42);
        cbor_item_t *it2 = cbor_build_uint8(42);
        unsigned char buf[16];
        size_t n;
        errs += CHECK(it != NULL && it2 != NULL);
        errs += CHECK(cbor_typeof(it) == CBOR_TYPE_UINT);
        errs += CHECK(cbor_isa_uint(it));
        errs += CHECK(cbor_is_int(it));
        errs += CHECK(cbor_get_uint8(it) == 42);
        errs += CHECK(cbor_get_int(it) == 42);
        errs += CHECK(cbor_int_get_width(it) == CBOR_INT_8);
        errs += CHECK(cbor_serialized_size(it) == 2);
        n = cbor_serialize(it, buf, sizeof buf);
        errs += CHECK(n == 2 && buf[0] == 0x18 && buf[1] == 0x2a);
        errs += CHECK(cbor_structurally_equal(it, it2));
        cbor_decref(&it2);
        cbor_decref(&it);
        errs += CHECK(it == NULL);
    }

    /* 底层编码 */
    {
        unsigned char ebuf[8];
        size_t n = cbor_encode_uint8(42, ebuf, sizeof ebuf);
        errs += CHECK(n == 2 && ebuf[0] == 0x18 && ebuf[1] == 0x2a);
    }

    /* 流式解码（按值返回 struct；挂全部 null 回调避免空指针） */
    {
        unsigned char src[2] = {0x18, 0x2a};
        struct cbor_callbacks cbs;
        struct cbor_decoder_result r;
        memset(&cbs, 0, sizeof cbs);
        cbs.uint8 = cbor_null_uint8_callback;
        cbs.uint16 = cbor_null_uint16_callback;
        cbs.uint32 = cbor_null_uint32_callback;
        cbs.uint64 = cbor_null_uint64_callback;
        cbs.negint8 = cbor_null_negint8_callback;
        cbs.negint16 = cbor_null_negint16_callback;
        cbs.negint32 = cbor_null_negint32_callback;
        cbs.negint64 = cbor_null_negint64_callback;
        cbs.string = cbor_null_string_callback;
        cbs.string_start = cbor_null_string_start_callback;
        cbs.byte_string = cbor_null_byte_string_callback;
        cbs.byte_string_start = cbor_null_byte_string_start_callback;
        cbs.array_start = cbor_null_array_start_callback;
        cbs.indef_array_start = cbor_null_indef_array_start_callback;
        cbs.map_start = cbor_null_map_start_callback;
        cbs.indef_map_start = cbor_null_indef_map_start_callback;
        cbs.tag = cbor_null_tag_callback;
        cbs.float2 = cbor_null_float2_callback;
        cbs.float4 = cbor_null_float4_callback;
        cbs.float8 = cbor_null_float8_callback;
        cbs.null = cbor_null_null_callback;
        cbs.undefined = cbor_null_undefined_callback;
        cbs.boolean = cbor_null_boolean_callback;
        cbs.indef_break = cbor_null_indef_break_callback;
        r = cbor_stream_decode(src, sizeof src, &cbs, NULL);
        errs += CHECK(r.status == CBOR_DECODER_FINISHED);
        errs += CHECK(r.read == 2);
        errs += CHECK(r.required == 0);
    }

    /* 引用计数 */
    {
        cbor_item_t *it = cbor_build_bool(true);
        errs += CHECK(cbor_refcount(it) == 1);
        errs += CHECK(cbor_incref(it) == it);
        errs += CHECK(cbor_refcount(it) == 2);
        cbor_intermediate_decref(it);
        errs += CHECK(cbor_refcount(it) == 1);
        cbor_decref(&it);
        errs += CHECK(it == NULL);
    }

    /* 数组 / 字符串 / null / 标签 / 映射 */
    {
        cbor_item_t *arr = cbor_new_definite_array(2);
        cbor_item_t *v = cbor_build_uint8(7);
        errs += CHECK(arr != NULL && v != NULL);
        errs += CHECK(cbor_array_allocated(arr) == 2);
        errs += CHECK(cbor_array_size(arr) == 0);
        errs += CHECK(cbor_array_is_definite(arr));
        errs += CHECK(cbor_array_push(arr, v));
        errs += CHECK(cbor_array_size(arr) == 1);
        errs += CHECK(cbor_array_get(arr, 0) == v);
        cbor_decref(&arr);
        cbor_decref(&v);
    }
    {
        cbor_item_t *s = cbor_build_string("hi");
        errs += CHECK(cbor_string_length(s) == 2);
        errs += CHECK(cbor_string_codepoint_count(s) == 2);
        errs += CHECK(cbor_string_is_definite(s));
        cbor_decref(&s);
    }
    {
        cbor_item_t *n = cbor_new_null();
        errs += CHECK(cbor_is_null(n));
        errs += CHECK(cbor_isa_float_ctrl(n));
        cbor_decref(&n);
    }
    {
        cbor_item_t *v = cbor_build_uint8(9);
        cbor_item_t *t = cbor_build_tag(123, v);
        errs += CHECK(cbor_tag_value(t) == 123);
        errs += CHECK(cbor_tag_item(t) == v);
        cbor_decref(&t);
        cbor_decref(&v);
    }
    {
        cbor_item_t *m = cbor_new_definite_map(1);
        cbor_item_t *k = cbor_build_uint8(1);
        cbor_item_t *v = cbor_build_uint8(2);
        struct cbor_pair p;
        p.key = k;
        p.value = v;
        errs += CHECK(cbor_map_add(m, p));
        errs += CHECK(cbor_map_size(m) == 1);
        cbor_decref(&m);
        cbor_decref(&k);
        cbor_decref(&v);
    }

    libcbor_dyn_cleanup();
    if (errs) { fprintf(stderr, "libcbor_dyn: %d failure(s)\n", errs); return 1; }
    printf("libcbor_dyn: all checks passed\n");
    return 0;
}
