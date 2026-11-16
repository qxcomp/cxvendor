/* nghttp3_dyn 测试：链接 -ldl，不链 libnghttp3（不依赖 ngtcp2）
 * 覆盖：version → mem_default/settings_default → uvarint 往返
 *       → qpack encoder→decoder 头部往返 → 错误/工具函数
 *
 * 类型/常量声明与 ../nghttp3_dyn.c 保持一致（布局核对相同）。
 */
#include "test_common.h"
#include <stddef.h>
#include <stdint.h>

typedef ptrdiff_t nghttp3_ssize;
typedef struct nghttp3_rcbuf nghttp3_rcbuf;
typedef struct nghttp3_qpack_encoder nghttp3_qpack_encoder;
typedef struct nghttp3_qpack_decoder nghttp3_qpack_decoder;
typedef struct nghttp3_qpack_stream_context nghttp3_qpack_stream_context;
typedef enum nghttp3_qpack_indexing_strat nghttp3_qpack_indexing_strat;

typedef enum nghttp3_qpack_indexing_strat {
    NGHTTP3_QPACK_INDEXING_STRAT_NONE,
    NGHTTP3_QPACK_INDEXING_STRAT_EAGER
} nghttp3_qpack_indexing_strat_dup; /* 值相同，仅防冲突占位 */

typedef struct {
    uint8_t *base;
    size_t len;
} nghttp3_vec;

typedef struct {
    const uint8_t *name;
    const uint8_t *value;
    size_t namelen;
    size_t valuelen;
    uint8_t flags;
} nghttp3_nv;

typedef struct {
    uint8_t *begin;
    uint8_t *end;
    uint8_t *pos;
    uint8_t *last;
} nghttp3_buf;

typedef struct {
    int age;
    int version_num;
    const char *version_str;
} nghttp3_info;

typedef struct {
    nghttp3_rcbuf *name;
    nghttp3_rcbuf *value;
    int32_t token;
    uint8_t flags;
} nghttp3_qpack_nv;

typedef void *(*nghttp3_malloc_t)(size_t size, void *user_data);
typedef void (*nghttp3_free_t)(void *ptr, void *user_data);
typedef void *(*nghttp3_calloc_t)(size_t nmemb, size_t size, void *user_data);
typedef void *(*nghttp3_realloc_t)(void *ptr, void *user_data, size_t size);

typedef struct {
    void *user_data;
    nghttp3_malloc_t malloc;
    nghttp3_free_t free;
    nghttp3_calloc_t calloc;
    nghttp3_realloc_t realloc;
} nghttp3_mem;

typedef struct nghttp3_settings {
    uint64_t max_field_section_size;
    size_t qpack_max_dtable_capacity;
    size_t qpack_encoder_max_dtable_capacity;
    size_t qpack_blocked_streams;
    uint8_t enable_connect_protocol;
    uint8_t h3_datagram;
    const nghttp3_vec *origin_list;
    uint64_t glitch_ratelim_burst;
    uint64_t glitch_ratelim_rate;
    nghttp3_qpack_indexing_strat qpack_indexing_strat;
} nghttp3_settings;

#define NGHTTP3_CALLBACKS_VERSION 4
#define NGHTTP3_SETTINGS_VERSION 4
#define NGHTTP3_PRI_VERSION 1
#define NGHTTP3_NV_FLAG_NONE 0x00U
#define NGHTTP3_QPACK_DECODE_FLAG_EMIT 0x01U
#define NGHTTP3_QPACK_DECODE_FLAG_FINAL 0x02U
#define NGHTTP3_ERR_INVALID_ARGUMENT (-101)
#define NGHTTP3_ERR_MALFORMED_HTTP_HEADER (-105)
#define NGHTTP3_ERR_FATAL (-900)
#define NGHTTP3_ERR_NOMEM (-901)

/* ---- 封装函数声明（定义在 ../nghttp3_dyn.c） ---- */
int nghttp3_dyn_init(const char *library_path);
int nghttp3_dyn_libok(void);
void nghttp3_dyn_cleanup(void);

const nghttp3_info *nghttp3_version(int least_version);
const nghttp3_mem *nghttp3_mem_default(void);
void nghttp3_settings_default_versioned(int settings_version,
                                        nghttp3_settings *settings);
void nghttp3_settings_default(nghttp3_settings *settings);
uint8_t *nghttp3_put_uvarint(uint8_t *p, uint64_t n);
const uint8_t *nghttp3_get_uvarint(uint64_t *dest, const uint8_t *p);
int nghttp3_qpack_encoder_new(nghttp3_qpack_encoder **pencoder,
                              size_t hard_max_dtable_capacity,
                              const nghttp3_mem *mem);
int nghttp3_qpack_encoder_encode(nghttp3_qpack_encoder *encoder,
                                 nghttp3_buf *pbuf, nghttp3_buf *rbuf,
                                 nghttp3_buf *ebuf, int64_t stream_id,
                                 const nghttp3_nv *nva, size_t nvlen);
void nghttp3_qpack_encoder_del(nghttp3_qpack_encoder *encoder);
int nghttp3_qpack_decoder_new(nghttp3_qpack_decoder **pdecoder,
                              size_t hard_max_dtable_capacity,
                              size_t max_blocked_streams,
                              const nghttp3_mem *mem);
void nghttp3_qpack_decoder_del(nghttp3_qpack_decoder *decoder);
int nghttp3_qpack_decoder_read_encoder(nghttp3_qpack_decoder *decoder,
                                       const uint8_t *src, size_t srclen);
int nghttp3_qpack_decoder_read_request(nghttp3_qpack_decoder *decoder,
                                       nghttp3_qpack_stream_context *sctx,
                                       nghttp3_qpack_nv *nv, uint8_t *pflags,
                                       const uint8_t *src, size_t srclen,
                                       int fin);
int nghttp3_qpack_stream_context_new(nghttp3_qpack_stream_context **psctx,
                                     int64_t stream_id, const nghttp3_mem *mem);
void nghttp3_qpack_stream_context_del(nghttp3_qpack_stream_context *sctx);
int nghttp3_err_is_fatal(int liberr);
const char *nghttp3_strerror(int liberr);
int nghttp3_check_header_name(const uint8_t *name, size_t len);
int nghttp3_check_header_value(const uint8_t *value, size_t len);

static void buf_init(nghttp3_buf *b, uint8_t *area, size_t len) {
    b->begin = area;
    b->end = area + len;
    b->pos = area;
    b->last = area;
}

int main(int argc, char **argv) {
    int failures = 0;

    if (nghttp3_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        TEST_SKIP("libnghttp3 不可加载\n");
    }
    failures += CHECK(nghttp3_dyn_libok() != 0);

    /* 1. version + mem_default + settings_default */
    {
        const nghttp3_info *v = nghttp3_version(0);
        failures += CHECK(v != NULL && v->version_num >= (1 << 16));
        printf("nghttp3 %s\n", v ? v->version_str : "?");
        failures += CHECK(nghttp3_mem_default() != NULL);

        nghttp3_settings st;
        memset(&st, 0xff, sizeof(st));
        nghttp3_settings_default(&st);
        failures += CHECK(st.qpack_max_dtable_capacity == 0);
        failures += CHECK(st.qpack_blocked_streams == 0);
        failures += CHECK(st.enable_connect_protocol == 0);
        failures += CHECK(st.origin_list == NULL);
        printf("settings default: max_field_section_size=%llu\n",
               (unsigned long long)st.max_field_section_size);
    }

    /* 2. uvarint 编解码往返 */
    {
        uint8_t buf[16];
        uint64_t val = 0xdeadbeefULL;
        uint8_t *p = nghttp3_put_uvarint(buf, val);
        failures += CHECK(p != NULL && p > buf);
        uint64_t out = 0;
        const uint8_t *q = nghttp3_get_uvarint(&out, buf);
        failures += CHECK(q != NULL && out == val);
        printf("uvarint %llu -> %zu bytes -> %llu\n",
               (unsigned long long)val, (size_t)(p - buf),
               (unsigned long long)out);

        /* 小值 1 字节编码 */
        p = nghttp3_put_uvarint(buf, 7);
        failures += CHECK(p == buf + 1);
        out = 0;
        q = nghttp3_get_uvarint(&out, buf);
        failures += CHECK(q != NULL && out == 7);
    }

    /* 3. qpack encoder → decoder 头部往返（核心无连接路径） */
    {
        nghttp3_qpack_encoder *enc = NULL;
        nghttp3_qpack_decoder *dec = NULL;
        nghttp3_qpack_stream_context *sctx = NULL;
        const nghttp3_mem *mem = nghttp3_mem_default();
        failures += CHECK(nghttp3_qpack_encoder_new(&enc, 4096, mem) == 0);
        failures += CHECK(nghttp3_qpack_decoder_new(&dec, 4096, 100, mem) == 0);
        failures += CHECK(nghttp3_qpack_stream_context_new(&sctx, 4, mem) == 0);

        if (enc && dec && sctx) {
            nghttp3_nv nva[1];
            nva[0].name = (const uint8_t *)"foo";
            nva[0].value = (const uint8_t *)"bar";
            nva[0].namelen = 3;
            nva[0].valuelen = 3;
            nva[0].flags = (uint8_t)NGHTTP3_NV_FLAG_NONE;

            nghttp3_buf pbuf, rbuf, ebuf;
            uint8_t parea[512], rarea[512], earea[512];
            buf_init(&pbuf, parea, sizeof(parea));
            buf_init(&rbuf, rarea, sizeof(rarea));
            buf_init(&ebuf, earea, sizeof(earea));

            int rc = nghttp3_qpack_encoder_encode(enc, &pbuf, &rbuf, &ebuf,
                                                  4, nva, 1);
            failures += CHECK(rc == 0);
            size_t plen = (size_t)(pbuf.last - pbuf.pos);   /* 前缀 */
            size_t rlen = (size_t)(rbuf.last - rbuf.pos);   /* 字段节 */
            size_t elen = (size_t)(ebuf.last - ebuf.pos);   /* encoder 流 */
            printf("qpack encoded: prefix=%zu req=%zu enc=%zu\n", plen, rlen,
                   elen);

            /* decoder 读 encoder 流（可能为空） */
            failures += CHECK(nghttp3_qpack_decoder_read_encoder(
                                  dec, ebuf.pos, elen) == 0);

            /* 读请求流：pbuf + rbuf 依次连接（前缀 + 字段节） */
            uint8_t req[1024];
            size_t reqlen = 0;
            memcpy(req + reqlen, pbuf.pos, plen);
            reqlen += plen;
            memcpy(req + reqlen, rbuf.pos, rlen);
            reqlen += rlen;
            int got = 0;
            const uint8_t *src = req;
            size_t srclen = reqlen;
            while (srclen > 0 && got < 1) {
                nghttp3_qpack_nv out;
                uint8_t pflags = 0;
                rc = nghttp3_qpack_decoder_read_request(
                    dec, sctx, &out, &pflags, src, srclen, 1);
                failures += CHECK(rc >= 0);
                if (rc < 0) break;
                src += rc;
                srclen -= (size_t)rc;
                if (pflags & NGHTTP3_QPACK_DECODE_FLAG_EMIT) {
                    got++;
                }
                if (pflags & NGHTTP3_QPACK_DECODE_FLAG_FINAL) break;
                /* 防御：无进展也无 EMIT 时退出 */
                if (rc == 0) break;
            }
            failures += CHECK(got == 1);
        }
        if (sctx) nghttp3_qpack_stream_context_del(sctx);
        if (dec) nghttp3_qpack_decoder_del(dec);
        if (enc) nghttp3_qpack_encoder_del(enc);
    }

    /* 4. 错误/工具函数 */
    failures += CHECK(nghttp3_err_is_fatal(NGHTTP3_ERR_NOMEM) != 0);
    failures +=
        CHECK(nghttp3_err_is_fatal(NGHTTP3_ERR_MALFORMED_HTTP_HEADER) == 0);
    failures += CHECK(nghttp3_err_is_fatal(NGHTTP3_ERR_FATAL - 1) != 0);
    failures += CHECK(nghttp3_strerror(NGHTTP3_ERR_INVALID_ARGUMENT) != NULL);
    /* check_* 返回非 0 表示合法（头文件文档核对） */
    failures += CHECK(nghttp3_check_header_name((const uint8_t *)"foo", 3) != 0);
    failures += CHECK(nghttp3_check_header_name((const uint8_t *)"fo o", 4) == 0);
    failures +=
        CHECK(nghttp3_check_header_value((const uint8_t *)"bar", 3) != 0);

    nghttp3_dyn_cleanup();

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
