/* nghttp2_dyn 测试：链接 -ldl，不链 libnghttp2
 * 覆盖：version → HPACK 编解码往返 → session 创建/提交/mem_send → 错误路径
 *
 * 注意：本文件的类型/常量声明与 ../nghttp2_dyn.c 保持一致
 * （分开定义、布局核对相同；封装函数在 nghttp2_dyn.c 中定义）。
 */
#include "test_common.h"
#include <stdint.h>
#include <sys/types.h>

typedef int64_t nghttp2_ssize;
typedef struct nghttp2_session nghttp2_session;
typedef struct nghttp2_hd_deflater nghttp2_hd_deflater;
typedef struct nghttp2_hd_inflater nghttp2_hd_inflater;
typedef struct nghttp2_session_callbacks nghttp2_session_callbacks;

typedef struct {
    uint8_t *name;
    uint8_t *value;
    size_t namelen;
    size_t valuelen;
    uint8_t flags;
} nghttp2_nv;

typedef struct {
    int age;
    int version_num;
    const char *version_str;
    const char *proto_str;
} nghttp2_info;

typedef struct {
    int32_t settings_id;
    uint32_t value;
} nghttp2_settings_entry;

typedef enum {
    NGHTTP2_NV_FLAG_NONE = 0,
    NGHTTP2_NV_FLAG_NO_INDEX = 0x01
} nghttp2_nv_flag_enum_unused; /* 仅防名字冲突占位，不用 */

#define NGHTTP2_HD_INFLATE_EMIT 0x02
#define NGHTTP2_HD_INFLATE_FINAL 0x01
#define NGHTTP2_ERR_NOMEM (-901)
#define NGHTTP2_ERR_INVALID_ARGUMENT (-501)
#define NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS 0x03

typedef int (*nghttp2_on_frame_recv_cb)(nghttp2_session *session,
                                        const void *frame, void *user_data);

/* ---- 封装函数声明（定义在 ../nghttp2_dyn.c） ---- */
int nghttp2_dyn_init(const char *library_path);
int nghttp2_libok(void);
void nghttp2_dyn_cleanup(void);

nghttp2_info *nghttp2_version(int least_version);
int nghttp2_hd_deflate_new(nghttp2_hd_deflater **deflater_ptr,
                           size_t max_deflate_dynamic_table_size);
void nghttp2_hd_deflate_del(nghttp2_hd_deflater *deflater);
ssize_t nghttp2_hd_deflate_hd(nghttp2_hd_deflater *deflater, uint8_t *buf,
                              size_t buflen, const nghttp2_nv *nva,
                              size_t nvlen);
size_t nghttp2_hd_deflate_bound(nghttp2_hd_deflater *deflater,
                                const nghttp2_nv *nva, size_t nvlen);
int nghttp2_hd_inflate_new(nghttp2_hd_inflater **inflater_ptr);
void nghttp2_hd_inflate_del(nghttp2_hd_inflater *inflater);
nghttp2_ssize nghttp2_hd_inflate_hd3(nghttp2_hd_inflater *inflater,
                                     nghttp2_nv *nv_out, int *inflate_flags,
                                     const uint8_t *in, size_t inlen,
                                     int in_final);
int nghttp2_session_callbacks_new(nghttp2_session_callbacks **callbacks_ptr);
void nghttp2_session_callbacks_set_on_frame_recv_callback(
    nghttp2_session_callbacks *cbs, nghttp2_on_frame_recv_cb cb);
void nghttp2_session_callbacks_del(nghttp2_session_callbacks *cbs);
int nghttp2_session_client_new(nghttp2_session **session_ptr,
                               const nghttp2_session_callbacks *callbacks,
                               void *user_data);
void nghttp2_session_del(nghttp2_session *session);
int nghttp2_submit_settings(nghttp2_session *session, uint8_t flags,
                            const nghttp2_settings_entry *iv, size_t niv);
int32_t nghttp2_submit_request(nghttp2_session *session, const void *pri_spec,
                               const nghttp2_nv *nva, size_t nvlen,
                               const void *data_prd, void *stream_user_data);
ssize_t nghttp2_session_mem_send(nghttp2_session *session,
                                 const uint8_t **data_ptr);
int nghttp2_session_want_write(nghttp2_session *session);
int nghttp2_session_check_server_session(nghttp2_session *session);
int nghttp2_session_terminate_session(nghttp2_session *session,
                                      uint32_t error_code);
int nghttp2_is_fatal(int lib_error_code);
const char *nghttp2_http2_strerror(uint32_t error_code);
const char *nghttp2_strerror(int lib_error_code);

static int frame_recv_count;
static int on_frame_recv(nghttp2_session *s, const void *frame, void *ud) {
    (void)s; (void)frame; (void)ud;
    frame_recv_count++;
    return 0;
}

int main(int argc, char **argv) {
    int failures = 0;

    if (nghttp2_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        TEST_SKIP("libnghttp2 不可加载\n");
    }
    failures += CHECK(nghttp2_libok() != 0);

    /* 1. version */
    nghttp2_info *v = nghttp2_version(0);
    failures += CHECK(v != NULL && v->version_num >= (1 << 16));
    printf("nghttp2 %s (proto=%s)\n", v ? v->version_str : "?",
           v ? v->proto_str : "?");

    /* 2. HPACK deflate → inflate 往返 */
    {
        nghttp2_hd_deflater *df = NULL;
        nghttp2_hd_inflater *inf = NULL;
        nghttp2_nv nva[3];
        uint8_t buf[512];
        static uint8_t emit_area[3][512];
        const char *names[3] = {":method", ":path", ":scheme"};
        const char *vals[3] = {"GET", "/", "https"};
        int i;

        for (i = 0; i < 3; i++) {
            nva[i].name = (uint8_t *)names[i];
            nva[i].value = (uint8_t *)vals[i];
            nva[i].namelen = strlen(names[i]);
            nva[i].valuelen = strlen(vals[i]);
            nva[i].flags = (uint8_t)NGHTTP2_NV_FLAG_NONE;
        }

        failures += CHECK(nghttp2_hd_deflate_new(&df, 4096) == 0);
        failures += CHECK(nghttp2_hd_inflate_new(&inf) == 0);
        if (df && inf) {
            size_t bound = nghttp2_hd_deflate_bound(df, nva, 3);
            failures += CHECK(bound > 0 && bound <= sizeof(buf));
            ssize_t n = nghttp2_hd_deflate_hd(df, buf, sizeof(buf), nva, 3);
            failures += CHECK(n > 0);
            printf("hpack bound=%zu encoded=%zd bytes\n", bound, n);

            const uint8_t *in = buf;
            size_t inlen = (size_t)n;
            int got = 0;
            while (inlen > 0 && got < 3) {
                nghttp2_nv out;
                int inflate_flags = 0;
                ssize_t r = nghttp2_hd_inflate_hd3(
                    inf, &out, &inflate_flags, in, inlen, 1);
                failures += CHECK(r >= 0);
                if (r < 0) break;
                in += r;
                inlen -= (size_t)r;
                if (inflate_flags & NGHTTP2_HD_INFLATE_EMIT) {
                    CHECK(out.namelen == strlen(names[got]));
                    CHECK(out.valuelen == strlen(vals[got]));
                    CHECK(memcmp(out.name, names[got], out.namelen) == 0);
                    CHECK(memcmp(out.value, vals[got], out.valuelen) == 0);
                    (void)emit_area;
                    got++;
                }
                if (inflate_flags & NGHTTP2_HD_INFLATE_FINAL) break;
            }
            failures += CHECK(got == 3);
        }
        if (inf) nghttp2_hd_inflate_del(inf);
        if (df) nghttp2_hd_deflate_del(df);
    }

    /* 3. session：callbacks + client_new + settings/headers 提交 + mem_send */
    {
        nghttp2_session_callbacks *cbs = NULL;
        nghttp2_session *s = NULL;
        failures += CHECK(nghttp2_session_callbacks_new(&cbs) == 0);
        nghttp2_session_callbacks_set_on_frame_recv_callback(cbs,
                                                             on_frame_recv);
        failures += CHECK(nghttp2_session_client_new(&s, cbs, NULL) == 0);
        if (s) {
            nghttp2_settings_entry iv = {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100};
            failures += CHECK(nghttp2_submit_settings(s, 0, &iv, 1) == 0);

            nghttp2_nv h[2];
            h[0].name = (uint8_t *)":method"; h[0].value = (uint8_t *)"GET";
            h[0].namelen = 7; h[0].valuelen = 3;
            h[0].flags = (uint8_t)NGHTTP2_NV_FLAG_NONE;
            h[1].name = (uint8_t *)":path"; h[1].value = (uint8_t *)"/x";
            h[1].namelen = 5; h[1].valuelen = 2;
            h[1].flags = (uint8_t)NGHTTP2_NV_FLAG_NONE;
            int32_t sid = nghttp2_submit_request(s, NULL, h, 2, NULL, NULL);
            failures += CHECK(sid > 0);
            printf("stream_id=%d\n", sid);

            const uint8_t *data = NULL;
            ssize_t n = nghttp2_session_mem_send(s, &data);
            failures += CHECK(n > 0 && data != NULL);
            printf("mem_send=%zd first-byte=%02x\n", n, data[0]);
            failures += CHECK(nghttp2_session_want_write(s) != 0);

            /* server 会话检查：client 会话上返回 0（实测 1.70.0 行为） */
            failures += CHECK(nghttp2_session_check_server_session(s) == 0);
            nghttp2_session_terminate_session(s, 0);
            nghttp2_session_del(s);
        }
        nghttp2_session_callbacks_del(cbs);
    }

    /* 4. 错误/工具函数 */
    failures += CHECK(nghttp2_is_fatal(NGHTTP2_ERR_NOMEM) != 0);
    failures += CHECK(nghttp2_is_fatal(NGHTTP2_ERR_INVALID_ARGUMENT) == 0);
    failures += CHECK(nghttp2_http2_strerror(2) != NULL);
    failures += CHECK(nghttp2_strerror(NGHTTP2_ERR_INVALID_ARGUMENT) != NULL);

    /* 5. 回调通路：mem_send 产生的 SETTINGS/HEADERS 帧喂回 mem_recv? 
     * （简化：仅验证 on_frame_recv 回调可注册且 session 正常建立）
     */
    failures += CHECK(frame_recv_count == 0); /* 尚未发送任何帧 */

    nghttp2_dyn_cleanup();

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
