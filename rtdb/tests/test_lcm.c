/* rtdb/tests/test_lcm.c - LCM 1.5.3 C API 动态封装定向测试 */
#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

/* ---------- 类型（不引入目标库头文件） ---------- */
typedef struct _lcm_t lcm_t;
typedef struct _lcm_subscription_t lcm_subscription_t;
typedef struct _lcm_recv_buf_t lcm_recv_buf_t;
struct _lcm_recv_buf_t {
    void *data;
    uint32_t data_size;
    int64_t recv_utime;
    lcm_t *lcm;
};
typedef void (*lcm_msg_handler_t)(const lcm_recv_buf_t *rbuf, const char *channel, void *user_data);

typedef struct _lcm_eventlog_t lcm_eventlog_t;
struct _lcm_eventlog_t {
    FILE *f;
    int64_t eventcount;
};
typedef struct _lcm_eventlog_event_t lcm_eventlog_event_t;
struct _lcm_eventlog_event_t {
    int64_t eventnum;
    int64_t timestamp;
    int32_t channellen;
    int32_t datalen;
    char *channel;
    void *data;
};

/* ---------- 动态层接口 ---------- */
extern int lcm_dyn_init(const char *);
extern int lcm_libok(void);
extern void lcm_dyn_cleanup(void);
extern int lcm_dyn_has_symbol(const char *);

/* ---------- 16 个公开函数的原生声明 ---------- */
extern lcm_t *lcm_create(const char *provider);
extern void lcm_destroy(lcm_t *lcm);
extern int lcm_get_fileno(lcm_t *lcm);
extern lcm_subscription_t *lcm_subscribe(lcm_t *lcm, const char *channel, lcm_msg_handler_t handler, void *user_data);
extern int lcm_unsubscribe(lcm_t *lcm, lcm_subscription_t *handler);
extern int lcm_publish(lcm_t *lcm, const char *channel, const void *data, unsigned int datalen);
extern int lcm_handle(lcm_t *lcm);
extern int lcm_handle_timeout(lcm_t *lcm, int timeout_millis);
extern int lcm_subscription_set_queue_capacity(lcm_subscription_t *handler, int num_messages);
extern int lcm_subscription_get_queue_size(lcm_subscription_t *handler);
extern lcm_eventlog_t *lcm_eventlog_create(const char *path, const char *mode);
extern lcm_eventlog_event_t *lcm_eventlog_read_next_event(lcm_eventlog_t *eventlog);
extern void lcm_eventlog_free_event(lcm_eventlog_event_t *event);
extern int lcm_eventlog_seek_to_timestamp(lcm_eventlog_t *eventlog, int64_t ts);
extern int lcm_eventlog_write_event(lcm_eventlog_t *eventlog, lcm_eventlog_event_t *event);
extern void lcm_eventlog_destroy(lcm_eventlog_t *eventlog);

static void lcm_test_handler(const lcm_recv_buf_t *rbuf, const char *channel, void *user_data) {
    int *hits = (int *)user_data;
    (void)rbuf;
    (void)channel;
    if (hits) (*hits)++;
}

int main(int argc, char **argv) {
    int errs = 0;
    unsigned int i, found = 0;
    const char *names[] = {
        "lcm_create",
        "lcm_destroy",
        "lcm_get_fileno",
        "lcm_subscribe",
        "lcm_unsubscribe",
        "lcm_publish",
        "lcm_handle",
        "lcm_handle_timeout",
        "lcm_subscription_set_queue_capacity",
        "lcm_subscription_get_queue_size",
        "lcm_eventlog_create",
        "lcm_eventlog_read_next_event",
        "lcm_eventlog_free_event",
        "lcm_eventlog_seek_to_timestamp",
        "lcm_eventlog_write_event",
        "lcm_eventlog_destroy"
    };

    if (lcm_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("lcm not installed, skipping lcm_dyn test\n");

    errs += CHECK(lcm_libok());
    errs += CHECK(!lcm_dyn_has_symbol("lcm_no_such_symbol_xyz"));
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (lcm_dyn_has_symbol(names[i])) found++;
    }
    printf("info: lcm public exports found = %u/%u\n",
           found, (unsigned int)(sizeof names / sizeof names[0]));
    errs += CHECK(found == sizeof names / sizeof names[0]);

    {
        char path[160];
        char channel[] = "rtdb_test";
        char payload[] = "abc";
        lcm_eventlog_t *w = NULL;
        lcm_eventlog_t *r = NULL;
        lcm_eventlog_event_t ev;
        lcm_eventlog_event_t *got = NULL;

        snprintf(path, sizeof path, "/tmp/opencode/rtdb_lcm_%ld.log", (long)getpid());
        memset(&ev, 0, sizeof ev);
        ev.eventnum = 1;
        ev.timestamp = 123456;
        ev.channellen = (int32_t)strlen(channel);
        ev.datalen = (int32_t)strlen(payload);
        ev.channel = channel;
        ev.data = payload;

        w = lcm_eventlog_create(path, "w");
        errs += CHECK(w != NULL);
        if (w) {
            errs += CHECK(lcm_eventlog_write_event(w, &ev) == 0);
            lcm_eventlog_destroy(w);
        }

        r = lcm_eventlog_create(path, "r");
        errs += CHECK(r != NULL);
        if (r) {
            got = lcm_eventlog_read_next_event(r);
            errs += CHECK(got != NULL);
            if (got) {
                printf("info: lcm eventnum=%lld timestamp=%lld\n",
                       (long long)got->eventnum, (long long)got->timestamp);
                errs += CHECK(got->eventnum == 0 && got->timestamp == 123456);
                errs += CHECK(got->channellen == (int32_t)strlen(channel) &&
                              got->channel != NULL &&
                              memcmp(got->channel, channel, strlen(channel)) == 0);
                errs += CHECK(got->datalen == (int32_t)strlen(payload) &&
                              got->data != NULL &&
                              memcmp(got->data, payload, strlen(payload)) == 0);
                lcm_eventlog_free_event(got);
            }
            lcm_eventlog_destroy(r);
        }
        unlink(path);
    }

    {
        int hits = 0;
        lcm_t *lc = lcm_create("memq://");
        errs += CHECK(lc != NULL);
        if (lc) {
            lcm_subscription_t *sub = lcm_subscribe(lc, "rtdb_memq", lcm_test_handler, &hits);
            errs += CHECK(sub != NULL);
            errs += CHECK(lcm_publish(lc, "rtdb_memq", "x", 1) == 0);
            errs += CHECK(lcm_handle(lc) >= 0);
            printf("info: lcm memq handler hits = %d\n", hits);
            if (sub) errs += CHECK(lcm_unsubscribe(lc, sub) == 0);
            lcm_destroy(lc);
        }
    }

    if (errs == 0) printf("lcm_dyn: all checks passed\n");
    return errs ? 1 : 0;
}
