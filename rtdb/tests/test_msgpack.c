/* msgpack_dyn 测试：只链 -ldl，不链 libmsgpack-c
 * 覆盖：动态加载、48 个公开导出的符号探测，以及 object/zone/
 *       vrefbuffer/unpack/unpacker/pack/version 的真实调用。
 * 库缺失时按 TEST_SKIP 正常退出。
 */
#include "test_common.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---------- 与 msgpack_dyn.c 一致的 ABI 类型（不引入目标库头文件） ---------- */
struct msgpack_iovec {
    void *iov_base;
    size_t iov_len;
};
typedef struct msgpack_iovec msgpack_iovec;

typedef struct msgpack_zone_finalizer {
    void (*func)(void *data);
    void *data;
} msgpack_zone_finalizer;
typedef struct msgpack_zone_finalizer_array {
    msgpack_zone_finalizer *tail;
    msgpack_zone_finalizer *end;
    msgpack_zone_finalizer *array;
} msgpack_zone_finalizer_array;
typedef struct msgpack_zone_chunk msgpack_zone_chunk;
typedef struct msgpack_zone_chunk_list {
    size_t free;
    char *ptr;
    msgpack_zone_chunk *head;
} msgpack_zone_chunk_list;
typedef struct msgpack_zone {
    msgpack_zone_chunk_list chunk_list;
    msgpack_zone_finalizer_array finalizer_array;
    size_t chunk_size;
} msgpack_zone;

typedef enum {
    MSGPACK_OBJECT_NIL = 0x00, MSGPACK_OBJECT_BOOLEAN = 0x01,
    MSGPACK_OBJECT_POSITIVE_INTEGER = 0x02, MSGPACK_OBJECT_NEGATIVE_INTEGER = 0x03,
    MSGPACK_OBJECT_FLOAT32 = 0x0a, MSGPACK_OBJECT_FLOAT64 = 0x04,
    MSGPACK_OBJECT_FLOAT = 0x04, MSGPACK_OBJECT_STR = 0x05,
    MSGPACK_OBJECT_ARRAY = 0x06, MSGPACK_OBJECT_MAP = 0x07,
    MSGPACK_OBJECT_BIN = 0x08, MSGPACK_OBJECT_EXT = 0x09
} msgpack_object_type;

struct msgpack_object;
struct msgpack_object_kv;
typedef struct { uint32_t size; struct msgpack_object *ptr; } msgpack_object_array;
typedef struct { uint32_t size; struct msgpack_object_kv *ptr; } msgpack_object_map;
typedef struct { uint32_t size; const char *ptr; } msgpack_object_str;
typedef struct { uint32_t size; const char *ptr; } msgpack_object_bin;
typedef struct { int8_t type; uint32_t size; const char *ptr; } msgpack_object_ext;
typedef union {
    bool boolean; uint64_t u64; int64_t i64; double f64;
    msgpack_object_array array; msgpack_object_map map;
    msgpack_object_str str; msgpack_object_bin bin; msgpack_object_ext ext;
} msgpack_object_union;
typedef struct msgpack_object {
    msgpack_object_type type;
    msgpack_object_union via;
} msgpack_object;
typedef struct msgpack_object_kv { msgpack_object key, val; } msgpack_object_kv;

typedef int (*msgpack_packer_write)(void *data, const char *buf, size_t len);
typedef struct msgpack_packer {
    void *data;
    msgpack_packer_write callback;
} msgpack_packer;

typedef struct msgpack_unpacked {
    msgpack_zone *zone;
    msgpack_object data;
} msgpack_unpacked;
typedef enum {
    MSGPACK_UNPACK_SUCCESS = 2, MSGPACK_UNPACK_EXTRA_BYTES = 1,
    MSGPACK_UNPACK_CONTINUE = 0, MSGPACK_UNPACK_PARSE_ERROR = -1,
    MSGPACK_UNPACK_NOMEM_ERROR = -2
} msgpack_unpack_return;
typedef struct msgpack_unpacker {
    char *buffer;
    size_t used, free, off, parsed;
    msgpack_zone *z;
    size_t initial_buffer_size;
    void *ctx;
} msgpack_unpacker;

typedef struct msgpack_vrefbuffer_chunk msgpack_vrefbuffer_chunk;
typedef struct msgpack_vrefbuffer_inner_buffer {
    size_t free;
    char *ptr;
    msgpack_vrefbuffer_chunk *head;
} msgpack_vrefbuffer_inner_buffer;
typedef struct msgpack_vrefbuffer {
    msgpack_iovec *tail, *end, *array;
    size_t chunk_size, ref_size;
    msgpack_vrefbuffer_inner_buffer inner_buffer;
} msgpack_vrefbuffer;

/* ---------- 动态层接口 ---------- */
extern int msgpack_dyn_init(const char *);
extern int msgpack_libok(void);
extern void msgpack_dyn_cleanup(void);
extern int msgpack_dyn_has_symbol(const char *);

/* ---------- 48 个公开函数的原生声明 ---------- */
extern void msgpack_object_init_nil(msgpack_object *);
extern void msgpack_object_init_boolean(msgpack_object *, bool);
extern void msgpack_object_init_unsigned_integer(msgpack_object *, uint64_t);
extern void msgpack_object_init_signed_integer(msgpack_object *, int64_t);
extern void msgpack_object_init_float32(msgpack_object *, float);
extern void msgpack_object_init_float64(msgpack_object *, double);
extern void msgpack_object_init_str(msgpack_object *, const char *, uint32_t);
extern void msgpack_object_init_bin(msgpack_object *, const char *, uint32_t);
extern void msgpack_object_init_ext(msgpack_object *, int8_t, const char *, uint32_t);
extern void msgpack_object_init_array(msgpack_object *, msgpack_object *, uint32_t);
extern void msgpack_object_init_map(msgpack_object *, msgpack_object_kv *, uint32_t);
extern void msgpack_object_print(FILE *, msgpack_object);
extern int msgpack_object_print_buffer(char *, size_t, msgpack_object);
extern bool msgpack_object_equal(const msgpack_object, const msgpack_object);
extern int msgpack_pack_object(msgpack_packer *, msgpack_object);
extern msgpack_unpack_return msgpack_unpack_next(msgpack_unpacked *, const char *, size_t, size_t *);
extern bool msgpack_unpacker_init(msgpack_unpacker *, size_t);
extern void msgpack_unpacker_destroy(msgpack_unpacker *);
extern msgpack_unpacker *msgpack_unpacker_new(size_t);
extern void msgpack_unpacker_free(msgpack_unpacker *);
extern msgpack_unpack_return msgpack_unpacker_next(msgpack_unpacker *, msgpack_unpacked *);
extern msgpack_unpack_return msgpack_unpacker_next_with_size(msgpack_unpacker *, msgpack_unpacked *, size_t *);
extern int msgpack_unpacker_execute(msgpack_unpacker *);
extern msgpack_object msgpack_unpacker_data(msgpack_unpacker *);
extern msgpack_zone *msgpack_unpacker_release_zone(msgpack_unpacker *);
extern void msgpack_unpacker_reset_zone(msgpack_unpacker *);
extern void msgpack_unpacker_reset(msgpack_unpacker *);
extern bool msgpack_unpacker_flush_zone(msgpack_unpacker *);
extern bool msgpack_unpacker_expand_buffer(msgpack_unpacker *, size_t);
extern msgpack_unpack_return msgpack_unpack(const char *, size_t, size_t *, msgpack_zone *, msgpack_object *);
extern const char *msgpack_version(void);
extern int msgpack_version_major(void);
extern int msgpack_version_minor(void);
extern int msgpack_version_revision(void);
extern bool msgpack_vrefbuffer_init(msgpack_vrefbuffer *, size_t, size_t);
extern void msgpack_vrefbuffer_destroy(msgpack_vrefbuffer *);
extern int msgpack_vrefbuffer_append_copy(msgpack_vrefbuffer *, const char *, size_t);
extern int msgpack_vrefbuffer_append_ref(msgpack_vrefbuffer *, const char *, size_t);
extern int msgpack_vrefbuffer_migrate(msgpack_vrefbuffer *, msgpack_vrefbuffer *);
extern void msgpack_vrefbuffer_clear(msgpack_vrefbuffer *);
extern bool msgpack_zone_init(msgpack_zone *, size_t);
extern void msgpack_zone_destroy(msgpack_zone *);
extern msgpack_zone *msgpack_zone_new(size_t);
extern void msgpack_zone_free(msgpack_zone *);
extern bool msgpack_zone_is_empty(msgpack_zone *);
extern void msgpack_zone_clear(msgpack_zone *);
extern void *msgpack_zone_malloc_expand(msgpack_zone *, size_t);
extern bool msgpack_zone_push_finalizer_expand(msgpack_zone *, void (*)(void *), void *);

/* ---------- 回调 ---------- */
static int finalizer_calls = 0;
static void test_finalizer(void *data) {
    ++(*(int *)data);
    finalizer_calls++;
}

struct pack_buf { char data[128]; size_t len; };
static int test_pack_write(void *token, const char *buf, size_t len) {
    struct pack_buf *pb = (struct pack_buf *)token;
    if (pb->len + len > sizeof pb->data) return -1;
    memcpy(pb->data + pb->len, buf, len);
    pb->len += len;
    return 0;
}

int main(int argc, char **argv) {
    int errs = 0;
    unsigned int i, found = 0;
    const char *names[] = {
        "msgpack_object_init_nil", "msgpack_object_init_boolean",
        "msgpack_object_init_unsigned_integer", "msgpack_object_init_signed_integer",
        "msgpack_object_init_float32", "msgpack_object_init_float64",
        "msgpack_object_init_str", "msgpack_object_init_bin", "msgpack_object_init_ext",
        "msgpack_object_init_array", "msgpack_object_init_map", "msgpack_object_print",
        "msgpack_object_print_buffer", "msgpack_object_equal", "msgpack_pack_object",
        "msgpack_unpack_next", "msgpack_unpacker_init", "msgpack_unpacker_destroy",
        "msgpack_unpacker_new", "msgpack_unpacker_free", "msgpack_unpacker_next",
        "msgpack_unpacker_next_with_size", "msgpack_unpacker_execute",
        "msgpack_unpacker_data", "msgpack_unpacker_release_zone",
        "msgpack_unpacker_reset_zone", "msgpack_unpacker_reset",
        "msgpack_unpacker_flush_zone", "msgpack_unpacker_expand_buffer", "msgpack_unpack",
        "msgpack_version", "msgpack_version_major", "msgpack_version_minor",
        "msgpack_version_revision", "msgpack_vrefbuffer_init", "msgpack_vrefbuffer_destroy",
        "msgpack_vrefbuffer_append_copy", "msgpack_vrefbuffer_append_ref",
        "msgpack_vrefbuffer_migrate", "msgpack_vrefbuffer_clear",
        "msgpack_zone_init", "msgpack_zone_destroy", "msgpack_zone_new", "msgpack_zone_free",
        "msgpack_zone_is_empty", "msgpack_zone_clear", "msgpack_zone_malloc_expand",
        "msgpack_zone_push_finalizer_expand"
    };

    if (msgpack_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("msgpack not installed, skipping msgpack_dyn test\n");

    errs += CHECK(msgpack_libok());
    errs += CHECK(!msgpack_dyn_has_symbol("msgpack_no_such_symbol_xyz"));
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (msgpack_dyn_has_symbol(names[i])) found++;
    }
    printf("info: msgpack public exports found = %u/%u\n",
           found, (unsigned int)(sizeof names / sizeof names[0]));
    errs += CHECK(found == sizeof names / sizeof names[0]);

    errs += CHECK(msgpack_version() != NULL);
    errs += CHECK(msgpack_version_major() > 0);
    errs += CHECK(msgpack_version_minor() >= 0);
    errs += CHECK(msgpack_version_revision() >= 0);

    /* object 初始化族与打印/比较 */
    {
        msgpack_object o, a, b;
        char pbuf[128];
        FILE *out = tmpfile();
        msgpack_object_init_nil(&o);
        errs += CHECK(o.type == MSGPACK_OBJECT_NIL);
        msgpack_object_init_boolean(&o, true);
        errs += CHECK(o.type == MSGPACK_OBJECT_BOOLEAN && o.via.boolean);
        msgpack_object_init_unsigned_integer(&o, 42);
        errs += CHECK(o.type == MSGPACK_OBJECT_POSITIVE_INTEGER && o.via.u64 == 42);
        msgpack_object_init_signed_integer(&o, -7);
        errs += CHECK(o.type == MSGPACK_OBJECT_NEGATIVE_INTEGER && o.via.i64 == -7);
        msgpack_object_init_float32(&o, 1.5f);
        errs += CHECK(o.type == MSGPACK_OBJECT_FLOAT32 && o.via.f64 == 1.5);
        msgpack_object_init_float64(&o, 2.5);
        errs += CHECK(o.type == MSGPACK_OBJECT_FLOAT64 && o.via.f64 == 2.5);
        msgpack_object_init_str(&o, "hi", 2);
        errs += CHECK(o.type == MSGPACK_OBJECT_STR && o.via.str.size == 2 && o.via.str.ptr != NULL);
        msgpack_object_init_bin(&o, "ab", 2);
        errs += CHECK(o.type == MSGPACK_OBJECT_BIN && o.via.bin.size == 2 && o.via.bin.ptr != NULL);
        msgpack_object_init_ext(&o, 1, "x", 1);
        errs += CHECK(o.type == MSGPACK_OBJECT_EXT && o.via.ext.type == 1 && o.via.ext.size == 1);
        msgpack_object_init_array(&o, NULL, 0);
        errs += CHECK(o.type == MSGPACK_OBJECT_ARRAY && o.via.array.size == 0);
        msgpack_object_init_map(&o, NULL, 0);
        errs += CHECK(o.type == MSGPACK_OBJECT_MAP && o.via.map.size == 0);

        msgpack_object_init_nil(&o);
        errs += CHECK(msgpack_object_print_buffer(pbuf, sizeof pbuf, o) > 0);
        errs += CHECK(out != NULL);
        if (out) {
            msgpack_object_print(out, o);
            fclose(out);
        }
        msgpack_object_init_nil(&a);
        msgpack_object_init_nil(&b);
        errs += CHECK(msgpack_object_equal(a, b));
        msgpack_object_init_boolean(&b, true);
        errs += CHECK(!msgpack_object_equal(a, b));
    }

    /* zone */
    {
        msgpack_zone z, z2;
        void *p;
        errs += CHECK(msgpack_zone_init(&z, 8192));
        p = msgpack_zone_malloc_expand(&z, 32);
        errs += CHECK(p != NULL);
        errs += CHECK(!msgpack_zone_is_empty(&z));
        {
            int finalizer_data = 0;
            errs += CHECK(msgpack_zone_push_finalizer_expand(&z, test_finalizer, &finalizer_data));
            msgpack_zone_destroy(&z);
            errs += CHECK(finalizer_data == 1 && finalizer_calls == 1);
        }

        errs += CHECK(msgpack_zone_init(&z2, 1024));
        errs += CHECK(msgpack_zone_is_empty(&z2));
        msgpack_zone_clear(&z2);
        msgpack_zone_destroy(&z2);
        {
            msgpack_zone *hz = msgpack_zone_new(1024);
            errs += CHECK(hz != NULL);
            msgpack_zone_free(hz);
        }
    }

    /* vrefbuffer */
    {
        msgpack_vrefbuffer vb, to;
        memset(&vb, 0, sizeof vb);
        memset(&to, 0, sizeof to);
        errs += CHECK(msgpack_vrefbuffer_init(&vb, 32, 8192));
        errs += CHECK(msgpack_vrefbuffer_init(&to, 32, 8192));
        errs += CHECK(msgpack_vrefbuffer_append_copy(&vb, "abc", 3) == 0);
        errs += CHECK(msgpack_vrefbuffer_append_ref(&vb, "def", 3) == 0);
        errs += CHECK(msgpack_vrefbuffer_migrate(&vb, &to) == 0);
        msgpack_vrefbuffer_clear(&vb);
        msgpack_vrefbuffer_destroy(&vb);
        msgpack_vrefbuffer_destroy(&to);
    }

    /* msgpack_unpack_next / msgpack_unpack（0x2a = 正数 42） */
    {
        const char buf[1] = {(char)0x2a};
        size_t off = 0;
        msgpack_unpacked res;
        msgpack_zone z;
        msgpack_object obj;
        memset(&res, 0, sizeof res);
        errs += CHECK(msgpack_unpack_next(&res, buf, 1, &off) == MSGPACK_UNPACK_SUCCESS);
        errs += CHECK(res.data.type == MSGPACK_OBJECT_POSITIVE_INTEGER && res.data.via.u64 == 42);
        errs += CHECK(off == 1);
        msgpack_zone_free(res.zone);

        errs += CHECK(msgpack_zone_init(&z, 1024));
        off = 0;
        errs += CHECK(msgpack_unpack(buf, 1, &off, &z, &obj) == MSGPACK_UNPACK_SUCCESS);
        errs += CHECK(obj.type == MSGPACK_OBJECT_POSITIVE_INTEGER && obj.via.u64 == 42);
        errs += CHECK(off == 1);
        msgpack_zone_destroy(&z);
    }

    /* 流式 unpacker */
    {
        msgpack_unpacker u, *up;
        msgpack_unpacked res, res2;
        size_t bytes = 0;
        errs += CHECK(msgpack_unpacker_init(&u, 1024));
        errs += CHECK(u.buffer != NULL);
        errs += CHECK(msgpack_unpacker_flush_zone(&u));
        errs += CHECK(msgpack_unpacker_expand_buffer(&u, 2048));
        /* 0xa2 'h' 'i'：fixstr，解析时会真正分配 zone */
        memcpy(u.buffer + u.used, "\xa2hi", 3);
        u.used += 3;
        u.free -= 3;
        errs += CHECK(msgpack_unpacker_execute(&u) > 0);
        errs += CHECK(msgpack_unpacker_data(&u).type == MSGPACK_OBJECT_STR);
        msgpack_unpacker_reset_zone(&u);
        msgpack_unpacker_reset(&u);
        memcpy(u.buffer + u.used, "\x2a", 1);
        u.used += 1;
        u.free -= 1;
        memset(&res, 0, sizeof res);
        errs += CHECK(msgpack_unpacker_next(&u, &res) == MSGPACK_UNPACK_SUCCESS);
        errs += CHECK(res.data.type == MSGPACK_OBJECT_POSITIVE_INTEGER && res.data.via.u64 == 42);
        msgpack_zone_free(res.zone);

        up = msgpack_unpacker_new(1024);
        errs += CHECK(up != NULL);
        if (up) {
            memcpy(up->buffer + up->used, "\x2b", 1);
            up->used += 1;
            up->free -= 1;
            memset(&res2, 0, sizeof res2);
            errs += CHECK(msgpack_unpacker_next_with_size(up, &res2, &bytes) == MSGPACK_UNPACK_SUCCESS);
            errs += CHECK(bytes == 1);
            errs += CHECK(res2.data.type == MSGPACK_OBJECT_POSITIVE_INTEGER && res2.data.via.u64 == 43);
            msgpack_zone_free(res2.zone);
            msgpack_unpacker_free(up);
        }
        msgpack_unpacker_destroy(&u);
    }

    /* pack_object（0x2a = 正数 42） */
    {
        struct pack_buf pb;
        msgpack_packer pk;
        msgpack_object o;
        memset(&pb, 0, sizeof pb);
        pk.data = &pb;
        pk.callback = test_pack_write;
        msgpack_object_init_unsigned_integer(&o, 42);
        errs += CHECK(msgpack_pack_object(&pk, o) == 0);
        errs += CHECK(pb.len == 1 && (unsigned char)pb.data[0] == 0x2a);
    }

    msgpack_dyn_cleanup();
    if (errs) { fprintf(stderr, "msgpack_dyn: %d failure(s)\n", errs); return 1; }
    printf("msgpack_dyn: all checks passed\n");
    return 0;
}
