/* avro_dyn 测试：只链 -ldl，不链 libavro
 * 覆盖：动态加载、240 个公开导出的符号探测，以及 schema/datum/
 *       reader/writer/value/raw-buffer/memoize/变参错误接口真实调用。
 * 库缺失时按 TEST_SKIP 正常退出。
 */
#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>

/* ---------- 与 avro_dyn.c 一致的 ABI 类型（不引入目标库头文件） ---------- */
enum avro_type_t {
    AVRO_STRING, AVRO_BYTES, AVRO_INT32, AVRO_INT64, AVRO_FLOAT,
    AVRO_DOUBLE, AVRO_BOOLEAN, AVRO_NULL, AVRO_RECORD, AVRO_ENUM,
    AVRO_FIXED, AVRO_MAP, AVRO_ARRAY, AVRO_UNION, AVRO_LINK,
    AVRO_INVALID = EINVAL
};
typedef enum avro_type_t avro_type_t;
enum avro_class_t { AVRO_SCHEMA, AVRO_DATUM };
typedef enum avro_class_t avro_class_t;
struct avro_obj_t {
    avro_type_t type;
    avro_class_t class_type;
    volatile int refcount;
};
typedef struct avro_obj_t *avro_schema_t;
typedef struct avro_obj_t *avro_datum_t;
typedef struct avro_reader_t_ *avro_reader_t;
typedef struct avro_writer_t_ *avro_writer_t;
typedef struct avro_file_reader_t_ *avro_file_reader_t;
typedef struct avro_file_writer_t_ *avro_file_writer_t;
typedef struct avro_schema_error_t_ *avro_schema_error_t;
typedef void *(*avro_allocator_t)(void *, void *, size_t, size_t);
typedef void (*avro_free_func_t)(void *, size_t);

typedef struct avro_raw_array {
    size_t element_size, element_count, allocated_size;
    void *data;
} avro_raw_array_t;
typedef struct avro_raw_map_entry { const char *key; } avro_raw_map_entry_t;
typedef struct avro_raw_map {
    avro_raw_array_t elements;
    void *indices_by_key;
} avro_raw_map_t;
typedef struct avro_wrapped_buffer avro_wrapped_buffer_t;
struct avro_wrapped_buffer {
    const void *buf;
    size_t size;
    void *user_data;
    void (*free)(avro_wrapped_buffer_t *self);
    int (*copy)(avro_wrapped_buffer_t *, const avro_wrapped_buffer_t *, size_t, size_t);
    int (*slice)(avro_wrapped_buffer_t *, size_t, size_t);
};
typedef struct avro_raw_string { avro_wrapped_buffer_t wrapped; } avro_raw_string_t;
typedef struct avro_memoize { void *cache; } avro_memoize_t;
typedef struct avro_consumer_t avro_consumer_t;
typedef struct avro_value_iface avro_value_iface_t;
typedef struct avro_value {
    avro_value_iface_t *iface;
    void *self;
} avro_value_t;

/* ---------- 动态层接口 ---------- */
extern int avro_dyn_init(const char *);
extern int avro_dyn_libok(void);
extern void avro_dyn_cleanup(void);
extern int avro_dyn_has_symbol(const char *);

/* ---------- 本次调用到的原生声明 ---------- */
extern const char *avro_strerror(void);
extern void avro_set_error(const char *, ...);
extern void avro_prefix_error(const char *, ...);
extern avro_schema_t avro_schema_int(void);
extern avro_schema_t avro_schema_string(void);
extern avro_schema_t avro_schema_null(void);
extern avro_schema_t avro_schema_array(avro_schema_t);
extern avro_schema_t avro_schema_map(avro_schema_t);
extern const char *avro_schema_type_name(avro_schema_t);
extern int avro_schema_equal(avro_schema_t, avro_schema_t);
extern int avro_schema_decref(avro_schema_t);
extern int avro_schema_from_json_length(const char *, size_t, avro_schema_t *);
extern int avro_schema_to_json(avro_schema_t, avro_writer_t);
extern size_t avro_schema_fixed_size(avro_schema_t);
extern avro_datum_t avro_string(const char *);
extern avro_datum_t avro_int32(int32_t);
extern avro_datum_t avro_null(void);
extern int avro_string_get(avro_datum_t, char **);
extern int avro_int32_get(avro_datum_t, int32_t *);
extern int avro_datum_equal(avro_datum_t, avro_datum_t);
extern void avro_datum_decref(avro_datum_t);
extern int avro_datum_to_json(avro_datum_t, int, char **);
extern avro_writer_t avro_writer_memory(char *, int64_t);
extern int64_t avro_writer_tell(avro_writer_t);
extern void avro_writer_free(avro_writer_t);
extern avro_reader_t avro_reader_memory(const char *, int64_t);
extern int avro_reader_is_eof(avro_reader_t);
extern void avro_reader_free(avro_reader_t);
extern int avro_write_data(avro_writer_t, avro_schema_t, avro_datum_t);
extern int avro_read_data(avro_reader_t, avro_schema_t, avro_schema_t, avro_datum_t *);
extern int avro_file_writer_create(const char *, avro_schema_t, avro_file_writer_t *);
extern int avro_file_writer_append(avro_file_writer_t, avro_datum_t);
extern int avro_file_writer_close(avro_file_writer_t);
extern int avro_file_reader(const char *, avro_file_reader_t *);
extern int avro_file_reader_read(avro_file_reader_t, avro_schema_t, avro_datum_t *);
extern int avro_file_reader_close(avro_file_reader_t);
extern char *avro_strdup(const char *);
extern void avro_str_free(char *);
extern void *avro_calloc(size_t, size_t);
extern void avro_alloc_free_func(void *, size_t);
extern void avro_raw_array_init(avro_raw_array_t *, size_t);
extern void *avro_raw_array_append(avro_raw_array_t *);
extern void avro_raw_array_done(avro_raw_array_t *);
extern void avro_raw_map_init(avro_raw_map_t *, size_t);
extern void *avro_raw_map_get(avro_raw_map_t *, const char *, size_t *);
extern void avro_raw_map_done(avro_raw_map_t *);
extern void avro_raw_string_init(avro_raw_string_t *);
extern void avro_raw_string_set(avro_raw_string_t *, const char *);
extern int avro_raw_string_equals(avro_raw_string_t *, avro_raw_string_t *);
extern void avro_raw_string_done(avro_raw_string_t *);
extern void avro_memoize_init(avro_memoize_t *);
extern void avro_memoize_set(avro_memoize_t *, void *, void *, void *);
extern int avro_memoize_get(avro_memoize_t *, void *, void *, void **);
extern void avro_memoize_done(avro_memoize_t *);
extern avro_value_iface_t *avro_generic_string_class(void);
extern int avro_generic_string_new(avro_value_t *, const char *);
extern int avro_value_equal(avro_value_t *, avro_value_t *);
extern void avro_value_decref(avro_value_t *);

int main(int argc, char **argv) {
    int errs = 0;
    unsigned int i, found = 0;
    const char *names[] = {
                "avro_alloc_free_func", "avro_array", "avro_array_append_datum", "avro_array_get", "avro_array_size",
        "avro_boolean", "avro_boolean_get", "avro_boolean_set", "avro_bytes", "avro_bytes_get",
        "avro_bytes_set", "avro_calloc", "avro_consume_binary", "avro_consumer_free", "avro_datum_as_value",
        "avro_datum_class", "avro_datum_decref", "avro_datum_equal", "avro_datum_from_schema", "avro_datum_get_schema",
        "avro_datum_incref", "avro_datum_print", "avro_datum_reset", "avro_datum_to_json", "avro_double",
        "avro_double_get", "avro_double_set", "avro_enum", "avro_enum_get", "avro_enum_get_name",
        "avro_enum_set", "avro_enum_set_name", "avro_file_reader", "avro_file_reader_close", "avro_file_reader_fp",
        "avro_file_reader_get_writer_schema", "avro_file_reader_read", "avro_file_reader_read_value", "avro_file_writer_append", "avro_file_writer_append_encoded",
        "avro_file_writer_append_value", "avro_file_writer_close", "avro_file_writer_create", "avro_file_writer_create_fp", "avro_file_writer_create_with_codec",
        "avro_file_writer_create_with_codec_fp", "avro_file_writer_flush", "avro_file_writer_open", "avro_file_writer_open_bs", "avro_file_writer_sync",
        "avro_fixed", "avro_fixed_get", "avro_fixed_set", "avro_float", "avro_float_get",
        "avro_float_set", "avro_generic_boolean_class", "avro_generic_boolean_new", "avro_generic_bytes_class", "avro_generic_bytes_new",
        "avro_generic_class_from_schema", "avro_generic_double_class", "avro_generic_double_new", "avro_generic_float_class", "avro_generic_float_new",
        "avro_generic_int_class", "avro_generic_int_new", "avro_generic_long_class", "avro_generic_long_new", "avro_generic_null_class",
        "avro_generic_null_new", "avro_generic_string_class", "avro_generic_string_new", "avro_generic_string_new_length", "avro_generic_value_new",
        "avro_givebytes", "avro_givebytes_set", "avro_givefixed", "avro_givefixed_set", "avro_givestring",
        "avro_givestring_set", "avro_int32", "avro_int32_get", "avro_int32_set", "avro_int64",
        "avro_int64_get", "avro_int64_set", "avro_map", "avro_map_get", "avro_map_get_index",
        "avro_map_get_key", "avro_map_set", "avro_map_size", "avro_memoize_delete", "avro_memoize_done",
        "avro_memoize_get", "avro_memoize_init", "avro_memoize_set", "avro_null", "avro_prefix_error",
        "avro_raw_array_append", "avro_raw_array_clear", "avro_raw_array_done", "avro_raw_array_ensure_size", "avro_raw_array_ensure_size0",
        "avro_raw_array_init", "avro_raw_map_clear", "avro_raw_map_done", "avro_raw_map_ensure_size", "avro_raw_map_get",
        "avro_raw_map_get_or_create", "avro_raw_map_init", "avro_raw_string_append", "avro_raw_string_append_length", "avro_raw_string_clear",
        "avro_raw_string_done", "avro_raw_string_equals", "avro_raw_string_give", "avro_raw_string_grab", "avro_raw_string_init",
        "avro_raw_string_set", "avro_raw_string_set_length", "avro_read", "avro_read_data", "avro_reader_dump",
        "avro_reader_file", "avro_reader_file_fp", "avro_reader_free", "avro_reader_is_eof", "avro_reader_memory",
        "avro_reader_memory_set_source", "avro_reader_reset", "avro_record", "avro_record_get", "avro_record_set",
        "avro_resolved_reader_clear_source", "avro_resolved_reader_new", "avro_resolved_reader_new_value", "avro_resolved_reader_set_source", "avro_resolved_writer_clear_dest",
        "avro_resolved_writer_new", "avro_resolved_writer_new_value", "avro_resolved_writer_set_dest", "avro_resolver_new", "avro_schema_array",
        "avro_schema_array_items", "avro_schema_boolean", "avro_schema_bytes", "avro_schema_copy", "avro_schema_datum_validate",
        "avro_schema_decref", "avro_schema_double", "avro_schema_enum", "avro_schema_enum_get", "avro_schema_enum_get_by_name",
        "avro_schema_enum_ns", "avro_schema_enum_number_of_symbols", "avro_schema_enum_symbol_append", "avro_schema_equal", "avro_schema_fixed",
        "avro_schema_fixed_ns", "avro_schema_fixed_size", "avro_schema_float", "avro_schema_from_json", "avro_schema_from_json_length",
        "avro_schema_get_subschema", "avro_schema_incref", "avro_schema_int", "avro_schema_link", "avro_schema_link_target",
        "avro_schema_long", "avro_schema_map", "avro_schema_map_values", "avro_schema_match", "avro_schema_name",
        "avro_schema_namespace", "avro_schema_null", "avro_schema_record", "avro_schema_record_field_append", "avro_schema_record_field_get",
        "avro_schema_record_field_get_by_index", "avro_schema_record_field_get_index", "avro_schema_record_field_name", "avro_schema_record_size", "avro_schema_string",
        "avro_schema_to_json", "avro_schema_type_name", "avro_schema_union", "avro_schema_union_append", "avro_schema_union_branch",
        "avro_schema_union_branch_by_name", "avro_schema_union_size", "avro_set_allocator", "avro_set_error", "avro_size_data",
        "avro_skip", "avro_skip_data", "avro_str_alloc", "avro_strdup", "avro_strerror",
        "avro_str_free", "avro_string", "avro_string_get", "avro_string_set", "avro_strndup",
        "avro_union", "avro_union_current_branch", "avro_union_discriminant", "avro_union_set_discriminant", "avro_value_cmp",
        "avro_value_cmp_fast", "avro_value_copy", "avro_value_copy_fast", "avro_value_copy_ref", "avro_value_decref",
        "avro_value_equal", "avro_value_equal_fast", "avro_value_hash", "avro_value_incref", "avro_value_move_ref",
        "avro_value_read", "avro_value_sizeof", "avro_value_to_json", "avro_value_write", "avro_wrapped_buffer_copy",
        "avro_wrapped_buffer_move", "avro_wrapped_buffer_new", "avro_wrapped_buffer_new_copy", "avro_wrapped_buffer_slice", "avro_write",
        "avro_write_data", "avro_writer_dump", "avro_writer_file", "avro_writer_file_fp", "avro_writer_flush",
        "avro_writer_free", "avro_writer_memory", "avro_writer_memory_set_dest", "avro_writer_reset", "avro_writer_tell"
    };

    if (avro_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("avro not installed, skipping avro_dyn test\n");

    errs += CHECK(avro_dyn_libok());
    errs += CHECK(!avro_dyn_has_symbol("avro_no_such_symbol_xyz"));
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (avro_dyn_has_symbol(names[i])) found++;
    }
    printf("info: avro public exports found = %u/%u\n",
           found, (unsigned int)(sizeof names / sizeof names[0]));
    errs += CHECK(found == 240);

    errs += CHECK(avro_strerror() != NULL);

    /* schema */
    {
        avro_schema_t si = avro_schema_int();
        avro_schema_t ss = avro_schema_string();
        avro_schema_t s1, s2;
        const char *json = "{\"type\":\"string\"}";
        errs += CHECK(si != NULL && ss != NULL);
        errs += CHECK(avro_schema_type_name(si) != NULL);
        errs += CHECK(!avro_schema_equal(si, ss));
        errs += CHECK(avro_schema_equal(si, si));
        errs += CHECK(avro_schema_from_json_length(json, strlen(json), &s1) == 0);
        errs += CHECK(s1 != NULL && avro_schema_equal(s1, ss));
        errs += CHECK(avro_schema_from_json_length(json, strlen(json), &s2) == 0);
        errs += CHECK(s2 != NULL);
        {
            char mem[256];
            avro_writer_t w = avro_writer_memory(mem, (int64_t)sizeof mem);
            errs += CHECK(w != NULL);
            if (w) {
                errs += CHECK(avro_schema_to_json(s1, w) == 0);
                errs += CHECK(avro_writer_tell(w) > 0);
                avro_writer_free(w);
            }
        }
        errs += CHECK(avro_schema_decref(s2) >= 0);
        errs += CHECK(avro_schema_decref(s1) >= 0);
        errs += CHECK(avro_schema_decref(ss) >= 0);
        errs += CHECK(avro_schema_decref(si) >= 0);
    }

    /* datum + 内存 writer/reader */
    {
        avro_schema_t si = avro_schema_int();
        avro_datum_t d = avro_int32(42), d2 = NULL;
        char mem[256];
        avro_writer_t w;
        avro_reader_t r;
        int32_t v = 0;
        char *js = NULL;
        errs += CHECK(si != NULL && d != NULL);
        errs += CHECK(avro_int32_get(d, &v) == 0 && v == 42);
        errs += CHECK(avro_datum_to_json(d, 1, &js) == 0 && js != NULL);
        if (js) { errs += CHECK(strstr(js, "42") != NULL); free(js); }
        w = avro_writer_memory(mem, (int64_t)sizeof mem);
        errs += CHECK(w != NULL);
        if (w) {
            errs += CHECK(avro_write_data(w, si, d) == 0);
            errs += CHECK(avro_writer_tell(w) > 0);
            avro_writer_free(w);
        }
        r = avro_reader_memory(mem, (int64_t)sizeof mem);
        errs += CHECK(r != NULL);
        if (r) {
            errs += CHECK(avro_read_data(r, si, si, &d2) == 0);
            errs += CHECK(d2 != NULL && avro_datum_equal(d, d2));
            if (d2) avro_datum_decref(d2);
            avro_reader_free(r);
        }
        avro_datum_decref(d);
        errs += CHECK(avro_schema_decref(si) >= 0);
    }

    /* 文件 writer/reader */
    {
        const char *path = "/tmp/opencode/rtdb-build/avro-dyn-test.avro";
        avro_schema_t si = avro_schema_int();
        avro_file_writer_t fw = NULL;
        avro_file_reader_t fr = NULL;
        avro_datum_t d = avro_int32(7), got = NULL;
        errs += CHECK(si != NULL && d != NULL);
        errs += CHECK(avro_file_writer_create(path, si, &fw) == 0 && fw != NULL);
        if (fw) {
            errs += CHECK(avro_file_writer_append(fw, d) == 0);
            errs += CHECK(avro_file_writer_close(fw) == 0);
        }
        errs += CHECK(avro_file_reader(path, &fr) == 0 && fr != NULL);
        if (fr) {
            errs += CHECK(avro_file_reader_read(fr, si, &got) == 0);
            errs += CHECK(got != NULL && avro_datum_equal(d, got));
            if (got) avro_datum_decref(got);
            errs += CHECK(avro_file_reader_close(fr) == 0);
        }
        avro_datum_decref(d);
        errs += CHECK(avro_schema_decref(si) >= 0);
    }

    /* raw 容器 */
    {
        avro_raw_array_t a;
        avro_raw_map_t m;
        size_t idx = 123;
        int *slot;
        avro_raw_array_init(&a, sizeof(int));
        slot = (int *)avro_raw_array_append(&a);
        errs += CHECK(slot != NULL);
        if (slot) *slot = 11;
        avro_raw_array_done(&a);
        avro_raw_map_init(&m, sizeof(int));
        slot = (int *)avro_raw_map_get(&m, "k", &idx);
        errs += CHECK(slot == NULL);
        avro_raw_map_done(&m);
    }
    {
        avro_raw_string_t s1, s2;
        avro_raw_string_init(&s1);
        avro_raw_string_init(&s2);
        avro_raw_string_set(&s1, "abc");
        avro_raw_string_set(&s2, "abc");
        errs += CHECK(avro_raw_string_equals(&s1, &s2));
        avro_raw_string_done(&s2);
        avro_raw_string_done(&s1);
    }
    {
        avro_memoize_t mem;
        void *result = NULL;
        avro_memoize_init(&mem);
        avro_memoize_set(&mem, (void *)"k1", (void *)"k2", (void *)0x2a);
        errs += CHECK(avro_memoize_get(&mem, (void *)"k1", (void *)"k2", &result));
        errs += CHECK(result == (void *)0x2a);
        avro_memoize_done(&mem);
    }

    /* generic value */
    {
        avro_value_iface_t *iface = avro_generic_string_class();
        avro_value_t v1, v2;
        errs += CHECK(iface != NULL);
        if (iface) {
            memset(&v1, 0, sizeof v1);
            memset(&v2, 0, sizeof v2);
            errs += CHECK(avro_generic_string_new(&v1, "hello") == 0);
            errs += CHECK(avro_generic_string_new(&v2, "hello") == 0);
            errs += CHECK(avro_value_equal(&v1, &v2));
            avro_value_decref(&v2);
            avro_value_decref(&v1);
        }
    }

    /* strdup / calloc / allocator free */
    {
        char *s = avro_strdup("xyz");
        void *p = avro_calloc(4, 8);
        errs += CHECK(s != NULL && strcmp(s, "xyz") == 0);
        errs += CHECK(p != NULL);
        if (p) { memset(p, 0, 32); avro_alloc_free_func(p, 32); }
        if (s) avro_str_free(s);
    }

    /* 变参错误接口 */
    avro_set_error("code=%d", 7);
    errs += CHECK(avro_strerror() != NULL && strstr(avro_strerror(), "code=7") != NULL);
    avro_prefix_error("tag=%d", 9);
    errs += CHECK(avro_strerror() != NULL);

    avro_dyn_cleanup();
    if (errs) { fprintf(stderr, "avro_dyn: %d failure(s)\n", errs); return 1; }
    printf("avro_dyn: all checks passed\n");
    return 0;
}
