/* rtdb/tests/test_zmq.c - libzmq C API 动态封装定向测试 */
#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <signal.h>
#include <errno.h>
#include <sys/uio.h>

/* ---------- 类型（不引入目标库头文件） ---------- */
typedef int zmq_fd_t;
typedef struct zmq_msg_t {
    unsigned char _[64] __attribute__((aligned(sizeof(void *))));
} zmq_msg_t;
typedef struct zmq_pollitem_t {
    void *socket;
    zmq_fd_t fd;
    short events;
    short revents;
} zmq_pollitem_t;
typedef struct zmq_poller_event_t {
    void *socket;
    zmq_fd_t fd;
    void *user_data;
    short events;
} zmq_poller_event_t;
typedef void (zmq_timer_fn)(int timer_id, void *arg);
#define ZMQ_PAIR 0
#define ZMQ_IO_THREADS 1
#define ZMQ_LINGER 17
#define ZMQ_POLLIN 1
#define ZMQ_SNDMORE 2

/* ---------- 动态层接口 ---------- */
extern int zmq_dyn_init(const char *);
extern int zmq_dyn_libok(void);
extern void zmq_dyn_cleanup(void);
extern int zmq_dyn_has_symbol(const char *);

/* ---------- 本测试直接调用的原生声明 ---------- */
extern int zmq_errno(void);
extern const char *zmq_strerror(int);
extern void zmq_version(int *, int *, int *);
extern void *zmq_ctx_new(void);
extern int zmq_ctx_term(void *);
extern int zmq_ctx_set(void *, int, int);
extern int zmq_ctx_get(void *, int);
extern void *zmq_socket(void *, int);
extern int zmq_close(void *);
extern int zmq_setsockopt(void *, int, const void *, size_t);
extern int zmq_getsockopt(void *, int, void *, size_t *);
extern int zmq_bind(void *, const char *);
extern int zmq_connect(void *, const char *);
extern int zmq_send(void *, const void *, size_t, int);
extern int zmq_recv(void *, void *, size_t, int);
extern int zmq_msg_init(zmq_msg_t *);
extern int zmq_msg_init_size(zmq_msg_t *, size_t);
extern void *zmq_msg_data(zmq_msg_t *);
extern size_t zmq_msg_size(const zmq_msg_t *);
extern int zmq_msg_close(zmq_msg_t *);
extern int zmq_sendmsg(void *, zmq_msg_t *, int);
extern int zmq_recvmsg(void *, zmq_msg_t *, int);
extern int zmq_poll(zmq_pollitem_t *, int, long);
extern int zmq_has(const char *);
extern char *zmq_z85_encode(char *, const uint8_t *, size_t);
extern uint8_t *zmq_z85_decode(uint8_t *, const char *);
extern void *zmq_atomic_counter_new(void);
extern int zmq_atomic_counter_inc(void *);
extern int zmq_atomic_counter_dec(void *);
extern int zmq_atomic_counter_value(void *);
extern void zmq_atomic_counter_set(void *, int);
extern void zmq_atomic_counter_destroy(void **);
extern void *zmq_timers_new(void);
extern int zmq_timers_add(void *, size_t, zmq_timer_fn, void *);
extern int zmq_timers_cancel(void *, int);
extern long zmq_timers_timeout(void *);
extern int zmq_timers_destroy(void **);
extern void *zmq_poller_new(void);
extern int zmq_poller_add(void *, void *, void *, short);
extern int zmq_poller_remove(void *, void *);
extern int zmq_poller_size(void *);
extern int zmq_poller_destroy(void **);

static void dummy_timer(int timer_id, void *arg) {
    (void)timer_id;
    (void)arg;
}

int main(int argc, char **argv) {
    int errs = 0;
    unsigned int i, found = 0;
    const char *names[] = {
        "zmq_atomic_counter_dec", "zmq_atomic_counter_destroy", "zmq_atomic_counter_inc",
        "zmq_atomic_counter_new", "zmq_atomic_counter_set", "zmq_atomic_counter_value",
        "zmq_bind", "zmq_close", "zmq_connect", "zmq_connect_peer", "zmq_ctx_destroy",
        "zmq_ctx_get", "zmq_ctx_get_ext", "zmq_ctx_new", "zmq_ctx_set", "zmq_ctx_set_ext",
        "zmq_ctx_shutdown", "zmq_ctx_term", "zmq_curve_keypair", "zmq_curve_public",
        "zmq_device", "zmq_disconnect", "zmq_errno", "zmq_getsockopt", "zmq_has",
        "zmq_init", "zmq_join", "zmq_leave", "zmq_msg_close", "zmq_msg_copy",
        "zmq_msg_data", "zmq_msg_get", "zmq_msg_gets", "zmq_msg_group", "zmq_msg_init",
        "zmq_msg_init_buffer", "zmq_msg_init_data", "zmq_msg_init_size", "zmq_msg_more",
        "zmq_msg_move", "zmq_msg_recv", "zmq_msg_routing_id", "zmq_msg_send",
        "zmq_msg_set", "zmq_msg_set_group", "zmq_msg_set_routing_id", "zmq_msg_size",
        "zmq_poll", "zmq_poller_add", "zmq_poller_add_fd", "zmq_poller_destroy",
        "zmq_poller_fd", "zmq_poller_modify", "zmq_poller_modify_fd", "zmq_poller_new",
        "zmq_poller_remove", "zmq_poller_remove_fd", "zmq_poller_size", "zmq_poller_wait",
        "zmq_poller_wait_all", "zmq_ppoll", "zmq_proxy", "zmq_proxy_steerable",
        "zmq_recv", "zmq_recviov", "zmq_recvmsg", "zmq_send", "zmq_send_const",
        "zmq_sendiov", "zmq_sendmsg", "zmq_setsockopt", "zmq_sleep", "zmq_socket",
        "zmq_socket_get_peer_state", "zmq_socket_monitor", "zmq_socket_monitor_pipes_stats",
        "zmq_socket_monitor_versioned", "zmq_stopwatch_intermediate",
        "zmq_stopwatch_start", "zmq_stopwatch_stop", "zmq_strerror", "zmq_term",
        "zmq_threadclose", "zmq_threadstart", "zmq_timers_add", "zmq_timers_cancel",
        "zmq_timers_destroy", "zmq_timers_execute", "zmq_timers_new", "zmq_timers_reset",
        "zmq_timers_set_interval", "zmq_timers_timeout", "zmq_unbind", "zmq_version",
        "zmq_z85_decode", "zmq_z85_encode"
    };

    if (zmq_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("zmq not installed, skipping zmq_dyn test\n");

    errs += CHECK(zmq_dyn_libok());
    errs += CHECK(!zmq_dyn_has_symbol("zmq_no_such_symbol_xyz"));
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (zmq_dyn_has_symbol(names[i])) found++;
    }
    printf("info: zmq public exports found = %u/%u\n",
           found, (unsigned int)(sizeof names / sizeof names[0]));
    errs += CHECK(found == sizeof names / sizeof names[0]);

    {
        int major = 0, minor = 0, patch = 0;
        zmq_version(&major, &minor, &patch);
        errs += CHECK(major == 4 && minor >= 3);
        printf("info: libzmq %d.%d.%d\n", major, minor, patch);
    }
    errs += CHECK(zmq_strerror(EINVAL) != NULL);
    errs += CHECK(zmq_has("ipc") >= 0);

    {
        void *ctx = zmq_ctx_new();
        errs += CHECK(ctx != NULL);
        if (!ctx) goto done;
        errs += CHECK(zmq_ctx_set(ctx, ZMQ_IO_THREADS, 1) == 0);
        errs += CHECK(zmq_ctx_get(ctx, ZMQ_IO_THREADS) == 1);

        {
            void *a = zmq_socket(ctx, ZMQ_PAIR);
            void *b = zmq_socket(ctx, ZMQ_PAIR);
            int linger = 0;
            errs += CHECK(a != NULL && b != NULL);
            if (a && b) {
                errs += CHECK(zmq_setsockopt(a, ZMQ_LINGER, &linger, sizeof linger) == 0);
                errs += CHECK(zmq_setsockopt(b, ZMQ_LINGER, &linger, sizeof linger) == 0);
                errs += CHECK(zmq_bind(a, "inproc://rtdb_zmq_dyn_test") == 0);
                errs += CHECK(zmq_connect(b, "inproc://rtdb_zmq_dyn_test") == 0);
                errs += CHECK(zmq_send(a, "hello", 5, 0) == 5);
                {
                    char buf[16];
                    memset(buf, 0, sizeof buf);
                    errs += CHECK(zmq_recv(b, buf, sizeof buf, 0) == 5);
                    errs += CHECK(memcmp(buf, "hello", 5) == 0);
                }
                {
                    zmq_msg_t msg;
                    errs += CHECK(zmq_msg_init_size(&msg, 3) == 0);
                    errs += CHECK(zmq_msg_size(&msg) == 3);
                    memcpy(zmq_msg_data(&msg), "ack", 3);
                    errs += CHECK(zmq_sendmsg(b, &msg, 0) == 3);
                    errs += CHECK(zmq_msg_close(&msg) == 0);
                }
                {
                    zmq_msg_t msg;
                    errs += CHECK(zmq_msg_init(&msg) == 0);
                    errs += CHECK(zmq_recvmsg(a, &msg, 0) == 3);
                    errs += CHECK(zmq_msg_size(&msg) == 3);
                    errs += CHECK(memcmp(zmq_msg_data(&msg), "ack", 3) == 0);
                    errs += CHECK(zmq_msg_close(&msg) == 0);
                }
                errs += CHECK(zmq_send(b, "poll", 4, 0) == 4);
                {
                    zmq_pollitem_t items[1];
                    items[0].socket = a;
                    items[0].fd = 0;
                    items[0].events = ZMQ_POLLIN;
                    items[0].revents = 0;
                    errs += CHECK(zmq_poll(items, 1, 1000) == 1);
                    errs += CHECK((items[0].revents & ZMQ_POLLIN) != 0);
                    {
                        char buf[16];
                        errs += CHECK(zmq_recv(a, buf, sizeof buf, 0) == 4);
                    }
                }
                {
                    int bad_opt = 999999;
                    int v = 1;
                    errs += CHECK(zmq_setsockopt(a, bad_opt, &v, sizeof v) == -1);
                    printf("info: zmq_errno()=%d\n", zmq_errno());
                }
                {
                    void *poller = zmq_poller_new();
                    errs += CHECK(poller != NULL);
                    if (poller) {
                        errs += CHECK(zmq_poller_add(poller, a, NULL, ZMQ_POLLIN) == 0);
                        errs += CHECK(zmq_poller_size(poller) == 1);
                        errs += CHECK(zmq_poller_remove(poller, a) == 0);
                        errs += CHECK(zmq_poller_destroy(&poller) == 0);
                        errs += CHECK(poller == NULL);
                    }
                }
                errs += CHECK(zmq_close(a) == 0);
                errs += CHECK(zmq_close(b) == 0);
            }
        }
        errs += CHECK(zmq_ctx_term(ctx) == 0);
    }

    {
        void *c = zmq_atomic_counter_new();
        errs += CHECK(c != NULL);
        if (c) {
            errs += CHECK(zmq_atomic_counter_inc(c) == 0);
            errs += CHECK(zmq_atomic_counter_value(c) == 1);
            zmq_atomic_counter_set(c, 5);
            errs += CHECK(zmq_atomic_counter_value(c) == 5);
            printf("info: zmq_atomic_counter_dec rc=%d\n", zmq_atomic_counter_dec(c));
            errs += CHECK(zmq_atomic_counter_value(c) == 4);
            zmq_atomic_counter_destroy(&c);
            errs += CHECK(c == NULL);
        }
    }
    {
        void *t = zmq_timers_new();
        errs += CHECK(t != NULL);
        if (t) {
            errs += CHECK(zmq_timers_add(t, 1000000, dummy_timer, NULL) >= 0);
            errs += CHECK(zmq_timers_timeout(t) > 0);
            errs += CHECK(zmq_timers_cancel(t, 1) == 0);
            errs += CHECK(zmq_timers_destroy(&t) == 0);
            errs += CHECK(t == NULL);
        }
    }
    {
        uint8_t raw[32];
        char encoded[41];
        uint8_t decoded[32];
        memset(raw, 0x5a, sizeof raw);
        errs += CHECK(zmq_z85_encode(encoded, raw, sizeof raw) != NULL);
        errs += CHECK(zmq_z85_decode(decoded, encoded) != NULL);
        errs += CHECK(memcmp(raw, decoded, sizeof raw) == 0);
    }

done:
    zmq_dyn_cleanup();
    if (errs == 0) printf("zmq_dyn: all checks passed\n");
    return errs ? 1 : 0;
}
