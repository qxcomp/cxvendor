/* rtdb/tests/test_fswatch.c - libfswatch C API 动态封装定向测试 */
#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

/* ---------- 类型（不引入目标库头文件） ---------- */
typedef struct FSW_SESSION *FSW_HANDLE;
typedef int FSW_STATUS;

enum fsw_monitor_type
{
  system_default_monitor_type = 0,
  fsevents_monitor_type,
  kqueue_monitor_type,
  inotify_monitor_type,
  windows_monitor_type,
  poll_monitor_type,
  fen_monitor_type,
  fanotify_monitor_type
};

enum fsw_event_flag
{
  NoOp = 0,
  PlatformSpecific = (1 << 0),
  Created = (1 << 1),
  Updated = (1 << 2),
  Removed = (1 << 3),
  Renamed = (1 << 4),
  OwnerModified = (1 << 5),
  AttributeModified = (1 << 6),
  MovedFrom = (1 << 7),
  MovedTo = (1 << 8),
  IsFile = (1 << 9),
  IsDir = (1 << 10),
  IsSymLink = (1 << 11),
  Link = (1 << 12),
  Overflow = (1 << 13),
  CloseWrite = (1 << 14)
};

typedef struct fsw_cevent
{
  char * path;
  time_t evt_time;
  enum fsw_event_flag * flags;
  unsigned int flags_num;
} fsw_cevent;

typedef void (*FSW_CEVENT_CALLBACK)(fsw_cevent const *const events,
                                    const unsigned int event_num,
                                    void *data);

#define FSW_OK 0

/* ---------- 动态层接口 ---------- */
extern int fswatch_dyn_init(const char *);
extern int fswatch_libok(void);
extern void fswatch_dyn_cleanup(void);
extern int fswatch_dyn_has_symbol(const char *);

extern FSW_HANDLE fsw_init_session(const enum fsw_monitor_type type);
extern FSW_STATUS fsw_add_path(const FSW_HANDLE handle, const char *path);
extern FSW_STATUS fsw_set_latency(const FSW_HANDLE handle, const double latency);
extern FSW_STATUS fsw_set_callback(const FSW_HANDLE handle,
                                   const FSW_CEVENT_CALLBACK callback,
                                   void *data);
extern FSW_STATUS fsw_start_monitor(const FSW_HANDLE handle);
extern FSW_STATUS fsw_stop_monitor(const FSW_HANDLE handle);
extern bool fsw_is_running(const FSW_HANDLE handle);
extern FSW_STATUS fsw_destroy_session(const FSW_HANDLE handle);
extern FSW_STATUS fsw_last_error(void);
extern bool fsw_is_verbose(void);

/* ---------- 回调与触发线程 ---------- */
struct test_ctx {
    FSW_HANDLE handle;
    char *dir;
    volatile int event_count;
};

static void count_callback(fsw_cevent const *const events,
                           const unsigned int event_num,
                           void *data) {
    struct test_ctx *ctx = (struct test_ctx *)data;
    (void)events;
    ctx->event_count += (int)event_num;
}

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { }
}

static void *trigger_thread(void *arg) {
    struct test_ctx *ctx = (struct test_ctx *)arg;
    char path[512];
    FILE *fp;
    int waited = 0;

    /* 等 poll monitor 完成首次快照并进入扫描循环后再触发事件；
     * 若首个文件被并入基线则不产生事件，故等待足够久。 */
    sleep_ms(1500);
    snprintf(path, sizeof path, "%s/event.txt", ctx->dir);
    fp = fopen(path, "w");
    if (fp) {
        fputs("fswatch event\n", fp);
        fclose(fp);
    }
    /* poll monitor 最短扫描周期为 1 秒（MIN_POLL_LATENCY）：
     * 轮询等待回调收到事件（最多 4 秒），再停止监控。 */
    while (ctx->event_count == 0 && waited < 4000) {
        sleep_ms(100);
        waited += 100;
    }
    fsw_stop_monitor(ctx->handle);
    return NULL;
}

int main(void) {
    struct test_ctx ctx;
    char dir_template[] = "/tmp/opencode/fswatch_XXXXXX";
    char file_path[512];
    pthread_t tid;
    int rc = 0;

    if (fswatch_dyn_init(NULL) != 0) {
        TEST_SKIP("libfswatch not installed, skipping fswatch_dyn test\n");
    }
    rc |= CHECK(fswatch_libok());
    rc |= CHECK(fswatch_dyn_has_symbol("fsw_init_library"));
    rc |= CHECK(fswatch_dyn_has_symbol("fsw_start_monitor"));

    memset(&ctx, 0, sizeof ctx);
    if (mkdtemp(dir_template) == NULL) {
        fprintf(stderr, "FAIL: mkdtemp failed\n");
        return 1;
    }
    ctx.dir = dir_template;

    ctx.handle = fsw_init_session(poll_monitor_type);
    rc |= CHECK(ctx.handle != NULL);
    if (ctx.handle == NULL) {
        rmdir(ctx.dir);
        fswatch_dyn_cleanup();
        return 1;
    }

    rc |= CHECK(fsw_add_path(ctx.handle, ctx.dir) == FSW_OK);
    rc |= CHECK(fsw_set_latency(ctx.handle, 0.1) == FSW_OK);
    rc |= CHECK(fsw_set_callback(ctx.handle, count_callback, &ctx) == FSW_OK);
    rc |= CHECK(fsw_is_running(ctx.handle) == false);

    if (pthread_create(&tid, NULL, trigger_thread, &ctx) != 0) {
        fprintf(stderr, "FAIL: pthread_create failed\n");
        fsw_destroy_session(ctx.handle);
        rmdir(ctx.dir);
        fswatch_dyn_cleanup();
        return 1;
    }

    /* poll monitor 支持停止，start 会在 stop 后返回 */
    rc |= CHECK(fsw_start_monitor(ctx.handle) == FSW_OK);
    pthread_join(tid, NULL);

    rc |= CHECK(ctx.event_count > 0);
    rc |= CHECK(fsw_is_running(ctx.handle) == false);
    rc |= CHECK(fsw_destroy_session(ctx.handle) == FSW_OK);

    snprintf(file_path, sizeof file_path, "%s/event.txt", ctx.dir);
    unlink(file_path);
    rmdir(ctx.dir);

    /* 可选符号存在性只作观察，不作为失败条件（旧库可能没有） */
    printf("fswatch optional symbols: callback_v2=%d filter_mode=%d prune_filter=%d\n",
           fswatch_dyn_has_symbol("fsw_set_callback_v2"),
           fswatch_dyn_has_symbol("fsw_set_filter_mode"),
           fswatch_dyn_has_symbol("fsw_add_prune_filter"));

    fswatch_dyn_cleanup();

    if (rc == 0)
        printf("all checks passed\n");
    return rc;
}
