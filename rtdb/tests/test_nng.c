/* rtdb/tests/test_nng.c - NNG v2.1.0 C API 动态封装定向测试 */
#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* ---------- 类型（不引入目标库头文件） ---------- */
#define NNG_MAXADDRLEN (128)

typedef struct nng_ctx_s      { uint32_t id; } nng_ctx;
typedef struct nng_dialer_s   { uint32_t id; } nng_dialer;
typedef struct nng_listener_s { uint32_t id; } nng_listener;
typedef struct nng_pipe_s     { uint32_t id; } nng_pipe;
typedef struct nng_socket_s   { uint32_t id; } nng_socket;

typedef int32_t  nng_duration;
typedef uint64_t nng_time;

typedef enum {
    NNG_OK     = 0,
    NNG_EINVAL = 3,
    NNG_ETRANERR = 0x20000000
} nng_err;

typedef struct nng_msg   nng_msg;
typedef struct nng_aio   nng_aio;
typedef struct nng_url   nng_url;
typedef struct nng_stat  nng_stat;
typedef struct nng_tls_config nng_tls_config;
typedef struct nng_tls_cert_s nng_tls_cert;
typedef struct nng_mtx   nng_mtx;
typedef struct nng_id_map_s nng_id_map;
typedef struct nng_http_conn nng_http;
typedef struct nng_http_handler nng_http_handler;

struct nng_sockaddr_storage_t { uint16_t sa_family; uint64_t sa_pad[16]; };
typedef union nng_sockaddr {
    uint16_t s_family;
    struct nng_sockaddr_storage_t s_storage;
} nng_sockaddr;

typedef struct nng_iov { void *iov_buf; size_t iov_len; } nng_iov;

typedef enum nng_log_level {
    NNG_LOG_NONE = 0, NNG_LOG_ERR = 3, NNG_LOG_WARN = 4,
    NNG_LOG_NOTICE = 5, NNG_LOG_INFO = 6, NNG_LOG_DEBUG = 7
} nng_log_level;
typedef enum nng_log_facility { NNG_LOG_USER = 1, NNG_LOG_AUTH = 10 } nng_log_facility;
typedef void (*nng_logger)(nng_log_level, nng_log_facility, const char *, const char *);
typedef enum nng_tls_mode { NNG_TLS_MODE_CLIENT = 0, NNG_TLS_MODE_SERVER = 1 } nng_tls_mode;
typedef enum { NNG_HTTP_STATUS_OK = 200, NNG_HTTP_STATUS_NOT_FOUND = 404 } nng_http_status;
typedef void (*nng_http_handler_func)(nng_http *, void *, nng_aio *);

/* ---------- 动态层接口 ---------- */
extern int nng_dyn_init(const char *);
extern int nng_dyn_libok(void);
extern void nng_dyn_cleanup(void);
extern int nng_dyn_has_symbol(const char *);

/* ---------- 测试中调用的原生声明 ---------- */
extern int nng_init(const void *);
extern const char *nng_version(void);
extern const char *nng_strerror(nng_err);
extern void *nng_alloc(size_t);
extern void nng_free(void *, size_t);
extern char *nng_strdup(const char *);
extern void nng_strfree(char *);
extern nng_err nng_url_parse(nng_url **, const char *);
extern const char *nng_url_scheme(const nng_url *);
extern const char *nng_url_hostname(const nng_url *);
extern uint32_t nng_url_port(const nng_url *);
extern const char *nng_url_path(const nng_url *);
extern int nng_url_sprintf(char *, size_t, const nng_url *);
extern void nng_url_free(nng_url *);
extern int nng_pair0_open(nng_socket *);
extern int nng_listen(nng_socket, const char *, nng_listener *, int);
extern int nng_dial(nng_socket, const char *, nng_dialer *, int);
extern int nng_socket_set_ms(nng_socket, const char *, nng_duration);
extern int nng_send(nng_socket, const void *, size_t, int);
extern int nng_recv(nng_socket, void *, size_t *, int);
extern int nng_socket_close(nng_socket);
extern int nng_msg_alloc(nng_msg **, size_t);
extern int nng_msg_append(nng_msg *, const void *, size_t);
extern int nng_msg_append_u32(nng_msg *, uint32_t);
extern size_t nng_msg_len(const nng_msg *);
extern void *nng_msg_body(nng_msg *);
extern void nng_msg_free(nng_msg *);
extern nng_err nng_aio_alloc(nng_aio **, void (*)(void *), void *);
extern void nng_aio_set_timeout(nng_aio *, nng_duration);
extern size_t nng_aio_count(nng_aio *);
extern void nng_aio_free(nng_aio *);
extern int nng_mtx_alloc(nng_mtx **);
extern void nng_mtx_lock(nng_mtx *);
extern void nng_mtx_unlock(nng_mtx *);
extern void nng_mtx_free(nng_mtx *);
extern nng_time nng_clock(void);
extern void nng_msleep(nng_duration);
extern uint32_t nng_random(void);
extern void nng_log_set_level(nng_log_level);
extern void nng_log_set_logger(nng_logger);
extern void nng_null_logger(nng_log_level, nng_log_facility, const char *, const char *);
extern void nng_log_err(const char *, const char *, ...);
extern void nng_log_warn(const char *, const char *, ...);
extern void nng_log_notice(const char *, const char *, ...);
extern void nng_log_info(const char *, const char *, ...);
extern void nng_log_debug(const char *, const char *, ...);
extern void nng_log_auth(nng_log_level, const char *, const char *, ...);
extern int nng_stats_get(nng_stat **);
extern const char *nng_stat_name(const nng_stat *);
extern void nng_stats_free(nng_stat *);
extern int nng_id_map_alloc(nng_id_map **, uint64_t, uint64_t, int);
extern int nng_id_set(nng_id_map *, uint64_t, void *);
extern void *nng_id_get(nng_id_map *, uint64_t);
extern int nng_id_remove(nng_id_map *, uint64_t);
extern void nng_id_map_free(nng_id_map *);
extern int nng_tls_config_alloc(nng_tls_config **, nng_tls_mode);
extern void nng_tls_config_free(nng_tls_config *);
extern nng_err nng_http_handler_alloc(nng_http_handler **, const char *, nng_http_handler_func);
extern void nng_http_handler_free(nng_http_handler *);

static void aio_cb(void *arg) { (void)arg; }
static void http_cb(nng_http *c, void *arg, nng_aio *aio) { (void)c; (void)arg; (void)aio; }

/* ---------- 438 个原生导出名（全量符号清单） ---------- */
static const char *names[] = {
    "nng_aio_abort", "nng_aio_alloc", "nng_aio_busy", "nng_aio_cancel",
    "nng_aio_count", "nng_aio_finish", "nng_aio_free", "nng_aio_get_input",
    "nng_aio_get_msg", "nng_aio_get_output", "nng_aio_reap", "nng_aio_reset",
    "nng_aio_result", "nng_aio_set_expire", "nng_aio_set_input", "nng_aio_set_iov",
    "nng_aio_set_msg", "nng_aio_set_output", "nng_aio_set_timeout",
    "nng_aio_skip_callback", "nng_aio_start", "nng_aio_stop", "nng_aio_wait",
    "nng_alloc", "nng_bus0_open", "nng_bus0_open_raw", "nng_clock",
    "nng_ctx_close", "nng_ctx_get_bool", "nng_ctx_get_int", "nng_ctx_get_ms",
    "nng_ctx_get_size", "nng_ctx_id", "nng_ctx_open", "nng_ctx_recv",
    "nng_ctx_recvmsg", "nng_ctx_send", "nng_ctx_sendmsg", "nng_ctx_set",
    "nng_ctx_set_bool", "nng_ctx_set_int", "nng_ctx_set_ms", "nng_ctx_set_size",
    "nng_cv_alloc", "nng_cv_free", "nng_cv_until", "nng_cv_wait", "nng_cv_wake",
    "nng_cv_wake1", "nng_device", "nng_device_aio", "nng_dial",
    "nng_dialer_bind", "nng_dialer_close", "nng_dialer_create",
    "nng_dialer_create_url", "nng_dialer_get_bool", "nng_dialer_get_int",
    "nng_dialer_get_ms", "nng_dialer_get_size", "nng_dialer_get_string",
    "nng_dialer_get_tls", "nng_dialer_get_uint64", "nng_dialer_get_url",
    "nng_dialer_id", "nng_dialer_set_bool", "nng_dialer_set_int",
    "nng_dialer_set_ms", "nng_dialer_set_size", "nng_dialer_set_string",
    "nng_dialer_set_tls", "nng_dialer_set_uint64", "nng_dialer_start",
    "nng_dialer_start_aio", "nng_dial_url", "nng_fini", "nng_free",
    "nng_http_add_header", "nng_http_client_alloc", "nng_http_client_connect",
    "nng_http_client_free", "nng_http_client_get_tls", "nng_http_client_set_tls",
    "nng_http_close", "nng_http_copy_body", "nng_http_del_header",
    "nng_http_get_body", "nng_http_get_bool", "nng_http_get_header",
    "nng_http_get_int", "nng_http_get_method", "nng_http_get_ms",
    "nng_http_get_reason", "nng_http_get_size", "nng_http_get_status",
    "nng_http_get_string", "nng_http_get_uint64", "nng_http_get_uri",
    "nng_http_get_version", "nng_http_handler_alloc",
    "nng_http_handler_alloc_directory", "nng_http_handler_alloc_file",
    "nng_http_handler_alloc_redirect", "nng_http_handler_alloc_static",
    "nng_http_handler_collect_body", "nng_http_handler_free",
    "nng_http_handler_set_data", "nng_http_handler_set_host",
    "nng_http_handler_set_method", "nng_http_handler_set_tree",
    "nng_http_hijack_stream", "nng_http_local_address", "nng_http_next_header",
    "nng_http_peer_cert", "nng_http_read", "nng_http_read_all",
    "nng_http_read_response", "nng_http_remote_address", "nng_http_reset",
    "nng_http_server_add_handler",
    "nng_http_server_del_handler", "nng_http_server_error",
    "nng_http_server_get_port", "nng_http_server_get_tls",
    "nng_http_server_hold", "nng_http_server_release",
    "nng_http_server_set_error_file", "nng_http_server_set_error_page",
    "nng_http_server_set_tls", "nng_http_server_start",
    "nng_http_server_stop", "nng_http_set_body", "nng_http_set_header",
    "nng_http_set_method", "nng_http_set_status", "nng_http_set_uri",
    "nng_http_set_version", "nng_http_transact", "nng_http_write",
    "nng_http_write_all", "nng_http_write_request", "nng_http_write_response",
    "nng_id_alloc", "nng_id_get", "nng_id_map_alloc", "nng_id_map_free",
    "nng_id_remove", "nng_id_set", "nng_id_visit", "nng_init", "nng_listen",
    "nng_listener_close", "nng_listener_create", "nng_listener_create_url",
    "nng_listener_get_bool", "nng_listener_get_int", "nng_listener_get_ms",
    "nng_listener_get_size", "nng_listener_get_string", "nng_listener_get_tls",
    "nng_listener_get_uint64", "nng_listener_get_url", "nng_listener_id",
    "nng_listener_set_bool", "nng_listener_set_int", "nng_listener_set_ms",
    "nng_listener_set_security_descriptor", "nng_listener_set_size",
    "nng_listener_set_string", "nng_listener_set_tls",
    "nng_listener_set_uint64", "nng_listener_start", "nng_listen_url",
    "nng_log_auth", "nng_log_debug", "nng_log_err", "nng_log_get_level",
    "nng_log_info", "nng_log_notice", "nng_log_set_facility",
    "nng_log_set_level", "nng_log_set_logger", "nng_log_warn",
    "nng_msg_alloc", "nng_msg_append", "nng_msg_append_u16",
    "nng_msg_append_u32", "nng_msg_append_u64", "nng_msg_body",
    "nng_msg_capacity", "nng_msg_chop", "nng_msg_chop_u16",
    "nng_msg_chop_u32", "nng_msg_chop_u64", "nng_msg_clear", "nng_msg_dup",
    "nng_msg_free", "nng_msg_get_pipe", "nng_msg_header",
    "nng_msg_header_append", "nng_msg_header_append_u16",
    "nng_msg_header_append_u32", "nng_msg_header_append_u64",
    "nng_msg_header_chop", "nng_msg_header_chop_u16",
    "nng_msg_header_chop_u32", "nng_msg_header_chop_u64",
    "nng_msg_header_clear", "nng_msg_header_insert",
    "nng_msg_header_insert_u16", "nng_msg_header_insert_u32",
    "nng_msg_header_insert_u64", "nng_msg_header_len", "nng_msg_header_trim",
    "nng_msg_header_trim_u16", "nng_msg_header_trim_u32",
    "nng_msg_header_trim_u64", "nng_msg_insert", "nng_msg_insert_u16",
    "nng_msg_insert_u32", "nng_msg_insert_u64", "nng_msg_len",
    "nng_msg_realloc", "nng_msg_reserve", "nng_msg_set_pipe", "nng_msg_trim",
    "nng_msg_trim_u16", "nng_msg_trim_u32", "nng_msg_trim_u64", "nng_msleep",
    "nng_mtx_alloc", "nng_mtx_free", "nng_mtx_lock", "nng_mtx_unlock",
    "nng_null_logger", "nng_pair0_open", "nng_pair0_open_raw",
    "nng_pair1_open", "nng_pair1_open_poly", "nng_pair1_open_raw",
    "nng_pipe_close", "nng_pipe_dialer", "nng_pipe_get_bool",
    "nng_pipe_get_int", "nng_pipe_get_ms", "nng_pipe_get_scheme",
    "nng_pipe_get_size", "nng_pipe_get_strcpy", "nng_pipe_get_strdup",
    "nng_pipe_get_string", "nng_pipe_get_strlen", "nng_pipe_id",
    "nng_pipe_listener", "nng_pipe_notify", "nng_pipe_peer_addr",
    "nng_pipe_peer_cert", "nng_pipe_self_addr", "nng_pipe_socket",
    "nng_pub0_open", "nng_pub0_open_raw", "nng_pull0_open",
    "nng_pull0_open_raw", "nng_push0_open", "nng_push0_open_raw",
    "nng_random", "nng_recv", "nng_recvmsg", "nng_rep0_open",
    "nng_rep0_open_raw", "nng_req0_open", "nng_req0_open_raw",
    "nng_respondent0_open", "nng_respondent0_open_raw", "nng_send",
    "nng_sendmsg", "nng_sleep_aio", "nng_sockaddr_equal",
    "nng_sockaddr_hash", "nng_sockaddr_port", "nng_socket_close",
    "nng_socket_get_bool", "nng_socket_get_int", "nng_socket_get_ms",
    "nng_socket_get_recv_poll_fd", "nng_socket_get_send_poll_fd",
    "nng_socket_get_size", "nng_socket_id", "nng_socket_pair",
    "nng_socket_peer_id", "nng_socket_peer_name", "nng_socket_proto_id",
    "nng_socket_proto_name", "nng_socket_raw", "nng_socket_recv",
    "nng_socket_send", "nng_socket_set_bool", "nng_socket_set_int",
    "nng_socket_set_ms", "nng_socket_set_size", "nng_stat_bool",
    "nng_stat_child", "nng_stat_desc", "nng_stat_find",
    "nng_stat_find_dialer", "nng_stat_find_listener", "nng_stat_find_socket",
    "nng_stat_name", "nng_stat_next", "nng_stats_dump", "nng_stats_free",
    "nng_stats_get", "nng_stat_string", "nng_stat_timestamp", "nng_stat_type",
    "nng_stat_unit", "nng_stat_value", "nng_stderr_logger", "nng_strdup",
    "nng_stream_close", "nng_stream_dialer_alloc",
    "nng_stream_dialer_alloc_url", "nng_stream_dialer_bind",
    "nng_stream_dialer_close", "nng_stream_dialer_dial",
    "nng_stream_dialer_free", "nng_stream_dialer_get_bool",
    "nng_stream_dialer_get_int", "nng_stream_dialer_get_ms",
    "nng_stream_dialer_get_size", "nng_stream_dialer_get_string",
    "nng_stream_dialer_get_tls", "nng_stream_dialer_get_uint64",
    "nng_stream_dialer_set_bool", "nng_stream_dialer_set_int",
    "nng_stream_dialer_set_ms", "nng_stream_dialer_set_size",
    "nng_stream_dialer_set_string", "nng_stream_dialer_set_tls",
    "nng_stream_dialer_set_uint64", "nng_stream_dialer_stop",
    "nng_stream_free", "nng_stream_get_bool", "nng_stream_get_int",
    "nng_stream_get_ms", "nng_stream_get_size", "nng_stream_get_string",
    "nng_stream_get_uint64", "nng_stream_listener_accept",
    "nng_stream_listener_alloc", "nng_stream_listener_alloc_url",
    "nng_stream_listener_close", "nng_stream_listener_free",
    "nng_stream_listener_get_bool", "nng_stream_listener_get_int",
    "nng_stream_listener_get_ms", "nng_stream_listener_get_size",
    "nng_stream_listener_get_string", "nng_stream_listener_get_tls",
    "nng_stream_listener_get_uint64", "nng_stream_listener_listen",
    "nng_stream_listener_set_bool", "nng_stream_listener_set_int",
    "nng_stream_listener_set_ms", "nng_stream_listener_set_security_descriptor",
    "nng_stream_listener_set_size", "nng_stream_listener_set_string",
    "nng_stream_listener_set_tls", "nng_stream_listener_set_uint64",
    "nng_stream_listener_stop", "nng_stream_peer_addr",
    "nng_stream_peer_cert", "nng_stream_recv", "nng_stream_self_addr",
    "nng_stream_send", "nng_stream_stop", "nng_strerror", "nng_strfree",
    "nng_str_sockaddr", "nng_sub0_ctx_subscribe",
    "nng_sub0_ctx_unsubscribe", "nng_sub0_open", "nng_sub0_open_raw",
    "nng_sub0_socket_subscribe", "nng_sub0_socket_unsubscribe",
    "nng_surveyor0_open", "nng_surveyor0_open_raw", "nng_system_logger",
    "nng_thread_create", "nng_thread_destroy", "nng_thread_set_name",
    "nng_tls_cert_der", "nng_tls_cert_free", "nng_tls_cert_issuer",
    "nng_tls_cert_next_alt", "nng_tls_cert_not_after",
    "nng_tls_cert_not_before", "nng_tls_cert_parse_der",
    "nng_tls_cert_parse_pem", "nng_tls_cert_serial_number",
    "nng_tls_cert_subject", "nng_tls_cert_subject_cn",
    "nng_tls_config_alloc", "nng_tls_config_auth_mode",
    "nng_tls_config_ca_chain", "nng_tls_config_ca_file",
    "nng_tls_config_cert_key_file", "nng_tls_config_free",
    "nng_tls_config_groups", "nng_tls_config_hold", "nng_tls_config_key",
    "nng_tls_config_own_cert", "nng_tls_config_pass", "nng_tls_config_psk",
    "nng_tls_config_server_name", "nng_tls_config_version",
    "nng_tls_engine_description", "nng_tls_engine_fips_mode",
    "nng_tls_engine_name", "nng_udp_close",
    "nng_udp_multicast_membership", "nng_udp_open", "nng_udp_recv",
    "nng_udp_send", "nng_udp_sockname", "nng_udp_stop", "nng_url_clone",
    "nng_url_fragment", "nng_url_free", "nng_url_hostname", "nng_url_parse",
    "nng_url_path", "nng_url_port", "nng_url_query",
    "nng_url_resolve_port", "nng_url_scheme", "nng_url_sprintf",
    "nng_url_userinfo", "nng_version"
};

int main(int argc, char **argv) {
    int errs = 0;
    unsigned int i, found = 0;

    if (nng_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("nng not installed, skipping nng_dyn test\n");

    errs += CHECK(nng_dyn_libok());
    /* 该构建要求显式初始化，否则 aio 内部除零（上游行为，非封装问题） */
    errs += CHECK(nng_init(NULL) == 0);
    errs += CHECK(!nng_dyn_has_symbol("nng_no_such_symbol_xyz"));
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (nng_dyn_has_symbol(names[i])) found++;
    }
    printf("info: nng public exports found = %u/%u\n",
           found, (unsigned int)(sizeof names / sizeof names[0]));
    /* 21 个条件编译符号（TLS cert / uint64 选项）在部分构建中不导出 */
    errs += check(found >= 417,
        "found >= 417 of 438 public exports (21 build-conditional absent)");

    errs += CHECK(NNG_OK == 0 && NNG_EINVAL == 3 && NNG_ETRANERR == 0x20000000);
    errs += CHECK(NNG_MAXADDRLEN == 128);
    errs += CHECK(NNG_LOG_ERR == 3 && NNG_LOG_DEBUG == 7);
    errs += CHECK(NNG_TLS_MODE_CLIENT == 0 && NNG_TLS_MODE_SERVER == 1);
    errs += CHECK(NNG_HTTP_STATUS_OK == 200 && NNG_HTTP_STATUS_NOT_FOUND == 404);
    errs += CHECK(sizeof(nng_socket) == 4 && sizeof(nng_pipe) == 4);
    errs += CHECK(sizeof(nng_sockaddr) == 136);
    errs += CHECK(sizeof(nng_iov) == 2 * sizeof(void *));

    {
        const char *v = nng_version();
        errs += CHECK(v != NULL && strstr(v, "2.1.0") != NULL);
        errs += CHECK(nng_strerror(NNG_OK) != NULL);
        errs += CHECK(nng_strerror(NNG_EINVAL) != NULL);
        printf("info: nng version = %s\n", v ? v : "(null)");
    }

    {
        void *p = nng_alloc(64);
        char *s = nng_strdup("nng-dyn");
        errs += CHECK(p != NULL);
        errs += CHECK(s != NULL && strcmp(s, "nng-dyn") == 0);
        nng_strfree(s);
        nng_free(p, 64);
        printf("ok: nng_alloc/nng_strdup/nng_strfree/nng_free\n");
    }

    {
        nng_url *u = NULL;
        char buf[256];
        errs += CHECK(nng_url_parse(&u, "tcp://127.0.0.1:12345/path") == 0 && u != NULL);
        errs += CHECK(u != NULL && strcmp(nng_url_scheme(u), "tcp") == 0);
        errs += CHECK(u != NULL && strcmp(nng_url_hostname(u), "127.0.0.1") == 0);
        errs += CHECK(u != NULL && nng_url_port(u) == 12345);
        errs += CHECK(u != NULL && strcmp(nng_url_path(u), "/path") == 0);
        errs += CHECK(u != NULL && nng_url_sprintf(buf, sizeof buf, u) > 0);
        nng_url_free(u);
        printf("ok: nng_url_parse/accessors/sprintf/free\n");
    }

    {
        nng_msg *m = NULL;
        errs += CHECK(nng_msg_alloc(&m, 0) == 0 && m != NULL);
        errs += CHECK(nng_msg_append(m, "abc", 3) == 0);
        errs += CHECK(nng_msg_append_u32(m, 0x01020304u) == 0);
        errs += CHECK(nng_msg_len(m) == 7);
        errs += CHECK(nng_msg_body(m) != NULL && memcmp(nng_msg_body(m), "abc", 3) == 0);
        nng_msg_free(m);
        printf("ok: nng_msg_alloc/append/len/body/free\n");
    }

    {
        nng_aio *aio = NULL;
        errs += CHECK(nng_aio_alloc(&aio, aio_cb, NULL) == 0 && aio != NULL);
        nng_aio_set_timeout(aio, 100);
        errs += CHECK(nng_aio_count(aio) == 0);
        nng_aio_free(aio);
        printf("ok: nng_aio_alloc/set_timeout/count/free\n");
    }

    {
        nng_mtx *mtx = NULL;
        errs += CHECK(nng_mtx_alloc(&mtx) == 0 && mtx != NULL);
        nng_mtx_lock(mtx);
        nng_mtx_unlock(mtx);
        nng_mtx_free(mtx);
        printf("ok: nng_mtx_alloc/lock/unlock/free\n");
    }

    {
        nng_id_map *map = NULL;
        errs += CHECK(nng_id_map_alloc(&map, 1, 100, 0) == 0 && map != NULL);
        errs += CHECK(nng_id_set(map, 5, (void *)0x1) == 0);
        errs += CHECK(nng_id_get(map, 5) == (void *)0x1);
        errs += CHECK(nng_id_remove(map, 5) == 0);
        nng_id_map_free(map);
        printf("ok: nng_id_map_alloc/set/get/remove/free\n");
    }

    {
        nng_tls_config *cfg = NULL;
        int r = nng_tls_config_alloc(&cfg, NNG_TLS_MODE_CLIENT);
        printf("info: nng_tls_config_alloc = %d\n", r);
        if (r == 0 && cfg) nng_tls_config_free(cfg);
    }

    {
        nng_http_handler *h = NULL;
        nng_err r = nng_http_handler_alloc(&h, "/x", http_cb);
        printf("info: nng_http_handler_alloc = %d\n", (int)r);
        if (r == 0 && h) nng_http_handler_free(h);
    }

    errs += CHECK(nng_clock() > 0);
    nng_msleep(1);
    printf("ok: nng_clock/nng_msleep\n");
    printf("info: nng_random = %u\n", nng_random());

    /* 6 个变参日志函数：以 null logger 静默处理，验证调用链返回 */
    nng_log_set_logger(nng_null_logger);
    nng_log_set_level(NNG_LOG_DEBUG);
    nng_log_err("TEST", "err %d", 1);
    nng_log_warn("TEST", "warn %s", "ok");
    nng_log_notice("TEST", "notice %d", 2);
    nng_log_info("TEST", "info %d", 3);
    nng_log_debug("TEST", "debug %d", 4);
    nng_log_auth(NNG_LOG_INFO, "TEST", "auth %d", 5);
    printf("ok: 6 variadic log functions returned\n");

    /* inproc 收发（同进程内，无外部依赖，带 1s 超时避免挂起） */
    {
        nng_socket s1, s2;
        nng_listener l;
        nng_dialer d;
        nng_stat *stats = NULL;
        char ibuf[16] = {0};
        size_t n = sizeof ibuf;

        errs += CHECK(nng_pair0_open(&s1) == 0 && nng_pair0_open(&s2) == 0);
        errs += CHECK(nng_socket_set_ms(s1, "send-timeout", 1000) == 0);
        errs += CHECK(nng_socket_set_ms(s2, "recv-timeout", 1000) == 0);
        errs += CHECK(nng_listen(s1, "inproc://nng-dyn-test", &l, 0) == 0);
        errs += CHECK(nng_dial(s2, "inproc://nng-dyn-test", &d, 0) == 0);
        errs += CHECK(nng_send(s1, "ping", 5, 0) == 0);
        errs += CHECK(nng_recv(s2, ibuf, &n, 0) == 0);
        errs += CHECK(n == 5 && memcmp(ibuf, "ping", 5) == 0);
        errs += CHECK(nng_socket_close(s1) == 0 && nng_socket_close(s2) == 0);
        printf("ok: inproc send/recv via nng_pair0_open + listen/dial\n");

        errs += CHECK(nng_stats_get(&stats) == 0);
        if (stats) {
            printf("info: nng stats root = %s\n", nng_stat_name(stats));
            nng_stats_free(stats);
        }
    }

    nng_dyn_cleanup();
    if (errs == 0) {
        printf("nng_dyn: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "nng_dyn: %d check(s) failed\n", errs);
    return 1;
}