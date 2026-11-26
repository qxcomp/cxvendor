/* rtdb/tests/test_fastipc.c - FastIPC C API 动态封装定向测试 */
#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/* ---------- 类型（不引入目标库头文件） ---------- */
typedef struct fipc_listener fipc_listener_t;
typedef struct fipc_conn fipc_conn_t;
typedef struct fipc_queue fipc_queue_t;
typedef struct fipc_service fipc_service_t;

typedef enum
{
    FIPC_OK = 0,
    FIPC_TIMEOUT = 1,
    FIPC_DISCONNECTED = 2,
    FIPC_CANCELLED = 3,
    FIPC_TOO_LARGE = 4,
    FIPC_INVALID = 5,
    FIPC_NO_MEMORY = 6,
    FIPC_ADDR_IN_USE = 7,
    FIPC_OVERRUN = 8,
    FIPC_UNSUBSCRIBED = 9,
    FIPC_ABANDONED = 10
} fipc_result_t;

#define FIPC_NO_WAIT 0
#define FIPC_FOREVER (-1)
#define FIPC_SUBSCRIBER_LOSSY 1
#define FIPC_SUBSCRIBER_GATED 2
#define FIPC_PRODUCER 3
#define FIPC_CONSUMER 4
#define FIPC_CLIENT 5
#define FIPC_SERVER 6

/* ---------- 动态层接口 ---------- */
extern int fastipc_dyn_init(const char *);
extern int fastipc_libok(void);
extern void fastipc_dyn_cleanup(void);
extern int fastipc_dyn_has_symbol(const char *);

/* ---------- 30 个公开函数的原生声明 ---------- */
extern const char *fipc_result_str(fipc_result_t result);
extern fipc_result_t fipc_stream_create(const char *name, size_t capacity, fipc_listener_t **out_listener);
extern fipc_result_t fipc_stream_accept(fipc_listener_t *listener, fipc_conn_t **out_conn, int timeout_ms);
extern void fipc_listener_cancel(fipc_listener_t *listener);
extern void fipc_listener_close(fipc_listener_t *listener);
extern fipc_result_t fipc_stream_connect(const char *name, fipc_conn_t **out_conn, int timeout_ms);
extern size_t fipc_max_piece(const fipc_conn_t *conn);
extern void fipc_cancel(fipc_conn_t *conn);
extern void fipc_close(fipc_conn_t *conn);
extern fipc_result_t fipc_send(fipc_conn_t *conn, const void *data, size_t len, int timeout_ms);
extern fipc_result_t fipc_recv(fipc_conn_t *conn, void *buf, size_t buf_len, size_t *out_len, int timeout_ms);
extern fipc_result_t fipc_send_many(fipc_conn_t *conn, const void *const *datas, const size_t *lens, size_t count, size_t *out_sent, int timeout_ms);
extern fipc_result_t fipc_recv_many(fipc_conn_t *conn, void *buf, size_t buf_len, size_t *lens, size_t max_count, size_t *out_count, int timeout_ms);
extern fipc_result_t fipc_send_acquire(fipc_conn_t *conn, size_t len, void **out_buf, int timeout_ms);
extern fipc_result_t fipc_send_commit(fipc_conn_t *conn, size_t len);
extern fipc_result_t fipc_recv_acquire(fipc_conn_t *conn, const void **out_data, size_t *out_len, int timeout_ms);
extern void fipc_recv_release(fipc_conn_t *conn);
extern fipc_result_t fipc_broadcast_create(const char *name, size_t capacity, uint32_t max_subscribers, int max_stall_ms, fipc_conn_t **out_publisher);
extern fipc_result_t fipc_broadcast_join(const char *name, int role, fipc_conn_t **out_conn, int timeout_ms);
extern fipc_result_t fipc_broadcast_wait_for_members(fipc_conn_t *publisher, uint32_t at_least, uint32_t *out_count, int timeout_ms);
extern fipc_result_t fipc_queue_create(const char *name, size_t capacity, uint32_t max_producers, uint32_t max_consumers, fipc_queue_t **out_queue);
extern fipc_result_t fipc_queue_join(const char *name, int role, fipc_conn_t **out_conn, int timeout_ms);
extern fipc_result_t fipc_queue_wait_for_members(fipc_queue_t *queue, int role, uint32_t at_least, uint32_t *out_count, int timeout_ms);
extern void fipc_queue_cancel(fipc_queue_t *queue);
extern void fipc_queue_close(fipc_queue_t *queue);
extern fipc_result_t fipc_service_create(const char *name, size_t capacity, size_t max_message, uint32_t max_clients, uint32_t max_servers, fipc_service_t **out_service);
extern fipc_result_t fipc_service_join(const char *name, int role, fipc_conn_t **out_conn, int timeout_ms);
extern fipc_result_t fipc_service_wait_for_members(fipc_service_t *service, int role, uint32_t at_least, uint32_t *out_count, int timeout_ms);
extern void fipc_service_cancel(fipc_service_t *service);
extern void fipc_service_close(fipc_service_t *service);

int main(int argc, char **argv) {
    int errs = 0;
    unsigned int i, found = 0;
    const char *names[] = {
        "fipc_result_str",
        "fipc_stream_create",
        "fipc_stream_accept",
        "fipc_listener_cancel",
        "fipc_listener_close",
        "fipc_stream_connect",
        "fipc_max_piece",
        "fipc_cancel",
        "fipc_close",
        "fipc_send",
        "fipc_recv",
        "fipc_send_many",
        "fipc_recv_many",
        "fipc_send_acquire",
        "fipc_send_commit",
        "fipc_recv_acquire",
        "fipc_recv_release",
        "fipc_broadcast_create",
        "fipc_broadcast_join",
        "fipc_broadcast_wait_for_members",
        "fipc_queue_create",
        "fipc_queue_join",
        "fipc_queue_wait_for_members",
        "fipc_queue_cancel",
        "fipc_queue_close",
        "fipc_service_create",
        "fipc_service_join",
        "fipc_service_wait_for_members",
        "fipc_service_cancel",
        "fipc_service_close"
    };

    if (fastipc_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("fastipc not installed, skipping fastipc_dyn test\n");

    errs += CHECK(fastipc_libok());
    errs += CHECK(!fastipc_dyn_has_symbol("fipc_no_such_symbol_xyz"));
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (fastipc_dyn_has_symbol(names[i])) found++;
    }
    printf("info: fastipc public exports found = %u/%u\n",
           found, (unsigned int)(sizeof names / sizeof names[0]));
    errs += CHECK(found == sizeof names / sizeof names[0]);

    {
        const char *s = fipc_result_str(FIPC_OK);
        errs += CHECK(s != NULL && strcmp(s, "FIPC_OK") == 0);
        s = fipc_result_str(FIPC_INVALID);
        errs += CHECK(s != NULL && strcmp(s, "FIPC_INVALID") == 0);
    }

    {
        char name[64];
        pid_t pid;
        int status = 0;

        snprintf(name, sizeof name, "rtdb_fipc_%ld", (long)getpid());
        pid = fork();
        if (pid == 0) {
            fipc_conn_t *c = NULL;
            const char msg[] = "ping";
            fipc_result_t r = fipc_stream_connect(name, &c, 5000);
            if (r != FIPC_OK || c == NULL) _exit(2);
            r = fipc_send(c, msg, sizeof msg, 5000);
            fipc_close(c);
            _exit(r == FIPC_OK ? 0 : 3);
        }
        if (pid < 0) {
            errs += CHECK(pid >= 0);
        } else {
            fipc_listener_t *listener = NULL;
            fipc_conn_t *conn = NULL;
            fipc_result_t r = fipc_stream_create(name, 4096, &listener);
            errs += CHECK(r == FIPC_OK && listener != NULL);
            if (r == FIPC_OK && listener != NULL) {
                r = fipc_stream_accept(listener, &conn, 5000);
                errs += CHECK(r == FIPC_OK && conn != NULL);
                if (r == FIPC_OK && conn != NULL) {
                    char buf[64] = {0};
                    size_t len = 0;
                    r = fipc_recv(conn, buf, sizeof buf, &len, 5000);
                    errs += CHECK(r == FIPC_OK && len == 5 && memcmp(buf, "ping", 5) == 0);
                    fipc_close(conn);
                }
                fipc_listener_close(listener);
            }
            if (waitpid(pid, &status, 0) == pid)
                errs += CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
            else
                errs += CHECK(0);
        }
    }

    if (errs == 0) printf("fastipc_dyn: all checks passed\n");
    return errs ? 1 : 0;
}
