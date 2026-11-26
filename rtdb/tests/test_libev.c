/* libev_dyn 测试：只链 -ldl，不链 libev
 * 类型块由 ../libev_dyn.c 机械复制，保证 ABI 完全一致。
 * ev.h 的 ev_init/ev_timer_set 等是宏，这里直接赋值结构字段（等价语义）。
 */
#include "test_common.h"
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>

/* ------------------------- 类型（自动复制） ------------------------- */
struct sigaction;
struct sigcontext;
struct sigstack;
struct stat;
struct stat64;
struct statx;
struct timespec;
union sigval;

typedef double ev_tstamp;
struct ev_loop;
enum { EV_UNDEF = (int)0xFFFFFFFF, EV_NONE = 0x00, EV_READ = 0x01, EV_WRITE = 0x02, EV__IOFDSET = 0x80, EV_IO = EV_READ, EV_TIMER = 0x00000100, EV_TIMEOUT = EV_TIMER, EV_PERIODIC = 0x00000200, EV_SIGNAL = 0x00000400, EV_CHILD = 0x00000800, EV_STAT = 0x00001000, EV_IDLE = 0x00002000, EV_PREPARE = 0x00004000, EV_CHECK = 0x00008000, EV_EMBED = 0x00010000, EV_FORK = 0x00020000, EV_CLEANUP = 0x00040000, EV_ASYNC = 0x00080000, EV_CUSTOM = 0x01000000, EV_ERROR = (int)0x80000000 };
typedef struct ev_watcher { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_watcher *w, int revents); } ev_watcher;
typedef struct ev_watcher_list { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_watcher_list *w, int revents); struct ev_watcher_list *next; } ev_watcher_list;
typedef struct ev_watcher_time { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_watcher_time *w, int revents); ev_tstamp at; } ev_watcher_time;
typedef struct ev_io { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_io *w, int revents); struct ev_watcher_list *next; int fd; int events; } ev_io;
typedef struct ev_timer { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_timer *w, int revents); ev_tstamp at; ev_tstamp repeat; } ev_timer;
typedef struct ev_periodic { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_periodic *w, int revents); ev_tstamp at; ev_tstamp offset; ev_tstamp interval; ev_tstamp (*reschedule_cb)(struct ev_periodic *w, ev_tstamp now) ; } ev_periodic;
typedef struct ev_signal { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_signal *w, int revents); struct ev_watcher_list *next; int signum; } ev_signal;
typedef struct ev_child { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_child *w, int revents); struct ev_watcher_list *next; int flags; int pid; int rpid; int rstatus; } ev_child;
typedef struct stat ev_statdata;
typedef struct ev_stat { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_stat *w, int revents); struct ev_watcher_list *next; ev_timer timer; ev_tstamp interval; const char *path; ev_statdata prev; ev_statdata attr; int wd; } ev_stat;
typedef struct ev_idle { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_idle *w, int revents); } ev_idle;
typedef struct ev_prepare { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_prepare *w, int revents); } ev_prepare;
typedef struct ev_check { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_check *w, int revents); } ev_check;
typedef struct ev_fork { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_fork *w, int revents); } ev_fork;
typedef struct ev_cleanup { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_cleanup *w, int revents); } ev_cleanup;
typedef struct ev_embed { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_embed *w, int revents); struct ev_loop *other; ev_io io; ev_prepare prepare; ev_check check; ev_timer timer; ev_periodic periodic; ev_idle idle; ev_fork fork; ev_cleanup cleanup; } ev_embed;
typedef struct ev_async { int active; int pending; int priority; void *data; void (*cb)(struct ev_loop *loop, struct ev_async *w, int revents); sig_atomic_t volatile sent; } ev_async;
union ev_any_watcher { struct ev_watcher w; struct ev_watcher_list wl; struct ev_io io; struct ev_timer timer; struct ev_periodic periodic; struct ev_signal signal; struct ev_child child; struct ev_stat stat; struct ev_idle idle; struct ev_prepare prepare; struct ev_check check; struct ev_fork fork; struct ev_cleanup cleanup; struct ev_embed embed; struct ev_async async; };
typedef void (*ev_loop_callback)(struct ev_loop *loop);

/* ------------------------- 测试用常量 ------------------------- */
#define EVFLAG_AUTO  0x00000000U
#define EVRUN_NOWAIT 0x01
#define EVRUN_ONCE   0x02
#define EVBREAK_ALL  0x02
#define EV_TIMER     0x00000100
#define EV_READ      0x01
#define EV_WRITE     0x02

/* ------------------------- 封装函数声明 ------------------------- */
int libev_dyn_init(const char *library_path);
int libev_libok(void);
void libev_dyn_cleanup(void);

int ev_version_major(void);
int ev_version_minor(void);
unsigned int ev_supported_backends(void);
unsigned int ev_recommended_backends(void);
ev_tstamp ev_time(void);
void ev_sleep(ev_tstamp);

struct ev_loop *ev_loop_new(unsigned int);
void ev_loop_destroy(struct ev_loop *);
unsigned int ev_backend(struct ev_loop *);
int ev_depth(struct ev_loop *);
unsigned int ev_pending_count(struct ev_loop *);
ev_tstamp ev_now(struct ev_loop *);
int ev_run(struct ev_loop *, int);
void ev_break(struct ev_loop *, int);
void ev_verify(struct ev_loop *);
void ev_set_userdata(struct ev_loop *, void *);
void *ev_userdata(struct ev_loop *);

void ev_timer_start(struct ev_loop *, ev_timer *);
void ev_timer_stop(struct ev_loop *, ev_timer *);
ev_tstamp ev_timer_remaining(struct ev_loop *, const ev_timer *);
void ev_io_start(struct ev_loop *, ev_io *);
void ev_io_stop(struct ev_loop *, ev_io *);
void ev_async_start(struct ev_loop *, ev_async *);
void ev_async_stop(struct ev_loop *, ev_async *);
void ev_async_send(struct ev_loop *, ev_async *);

static int timer_hits, io_hits, async_hits;

static void on_timer(struct ev_loop *loop, ev_timer *w, int revents) {
    (void)w;
    if (revents & EV_TIMER)
        timer_hits++;
    ev_break(loop, EVBREAK_ALL);
}

static void on_io(struct ev_loop *loop, ev_io *w, int revents) {
    (void)loop;
    if (revents & EV_READ)
        io_hits++;
    ev_io_stop(loop, w);
}

static void on_async(struct ev_loop *loop, ev_async *w, int revents) {
    (void)w; (void)revents;
    async_hits++;
    ev_async_stop(loop, w);
}

int main(int argc, char **argv) {
    int failures = 0;
    struct ev_loop *loop;
    int marker = 0;

    if (libev_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("libev 不可加载\n");
    failures += CHECK(libev_libok() != 0);

    printf("libev %d.%d, backends=0x%08x/0x%08x\n",
           ev_version_major(), ev_version_minor(),
           ev_supported_backends(), ev_recommended_backends());
    failures += CHECK(ev_version_major() == 4);
    failures += CHECK(ev_supported_backends() != 0);

    /* 1. ev_time 单调性 */
    {
        ev_tstamp t0 = ev_time();
        ev_sleep(0.001);
        failures += CHECK(ev_time() > t0);
    }

    /* 2. loop + timer + ev_break */
    loop = ev_loop_new(EVFLAG_AUTO);
    failures += CHECK(loop != NULL);
    failures += CHECK(ev_backend(loop) != 0);
    ev_set_userdata(loop, &marker);
    failures += CHECK(ev_userdata(loop) == &marker);
    failures += CHECK(ev_depth(loop) == 0);

    {
        ev_timer tw;
        memset(&tw, 0, sizeof tw);
        tw.cb = on_timer;
        tw.at = 0.002;
        failures += CHECK(tw.priority == 0);
        ev_timer_start(loop, &tw);
        failures += CHECK(ev_timer_remaining(loop, &tw) >= 0);
        ev_run(loop, 0);
        failures += CHECK(timer_hits == 1);
    }

    /* 3. ev_io：管道可读 */
    {
        int pfd[2];
        ev_io iw;
        char c = 'x';

        failures += CHECK(pipe(pfd) == 0);
        memset(&iw, 0, sizeof iw);
        iw.cb = on_io;
        iw.fd = pfd[0];
        iw.events = EV_READ;
        ev_io_start(loop, &iw);
        failures += CHECK(write(pfd[1], &c, 1) == 1);
        ev_run(loop, EVRUN_ONCE);
        failures += CHECK(io_hits == 1);
        close(pfd[0]);
        close(pfd[1]);
    }

    /* 4. ev_async */
    {
        ev_async aw;
        memset(&aw, 0, sizeof aw);
        aw.cb = on_async;
        ev_async_start(loop, &aw);
        ev_async_send(loop, &aw);
        ev_run(loop, EVRUN_ONCE);
        failures += CHECK(async_hits == 1);
    }

    /* 5. 状态查询与错误路径 */
    ev_verify(loop);   /* 返回 void；内部数据损坏会 abort */
    failures += CHECK(ev_now(loop) > 0);
    failures += CHECK(ev_pending_count(loop) == 0);

    ev_loop_destroy(loop);
    libev_dyn_cleanup();
    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
