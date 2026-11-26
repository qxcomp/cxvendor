/* reproc_dyn 测试：只链 -ldl，不链 libreproc
 * 本机无 reproc 时 dyn_init 失败 → TEST_SKIP 退出 0。
 * 有上游头文件时 include 它，测试里声明的函数原型也参与类型检查。
 */
#include "test_common.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifndef REPROC_EXPORT
#define REPROC_EXPORT
#endif

#if defined(__has_include)
#  if __has_include(<reproc/reproc.h>)
#    include <reproc/reproc.h>
#    include <reproc/run.h>
#    include <reproc/drain.h>
#    define REPROC_TEST_HAVE_HEADERS 1
#  endif
#endif

#ifndef REPROC_TEST_HAVE_HEADERS
typedef struct reproc_t reproc_t;
typedef enum {
  REPROC_STREAM_IN = 0, REPROC_STREAM_OUT = 1, REPROC_STREAM_ERR = 2,
} REPROC_STREAM;
typedef enum {
  REPROC_REDIRECT_DEFAULT = 0, REPROC_REDIRECT_PIPE = 1,
  REPROC_REDIRECT_PARENT = 2, REPROC_REDIRECT_DISCARD = 3,
  REPROC_REDIRECT_STDOUT = 4, REPROC_REDIRECT_HANDLE = 5,
  REPROC_REDIRECT_FILE = 6, REPROC_REDIRECT_PATH = 7,
} REPROC_REDIRECT;
typedef enum {
  REPROC_STOP_NOOP = 0, REPROC_STOP_WAIT = 1,
  REPROC_STOP_TERMINATE = 2, REPROC_STOP_KILL = 3,
} REPROC_STOP;
typedef struct { REPROC_STOP action; int timeout; } reproc_stop_action;
typedef struct {
  reproc_stop_action first, second, third;
} reproc_stop_actions;
#ifdef _WIN32
typedef void *reproc_handle;
#else
typedef int reproc_handle;
#endif
typedef struct {
  REPROC_REDIRECT type; reproc_handle handle; FILE *file; const char *path;
} reproc_redirect;
typedef enum { REPROC_ENV_EXTEND = 0, REPROC_ENV_EMPTY = 1 } REPROC_ENV;
typedef struct {
  const char *working_directory;
  struct { REPROC_ENV behavior; const char *const *extra; } env;
  struct {
    reproc_redirect in, out, err;
    bool parent, discard;
    FILE *file;
    const char *path;
  } redirect;
  reproc_stop_actions stop;
  int deadline;
  struct { const uint8_t *data; size_t size; } input;
  bool fork;
  bool nonblocking;
} reproc_options;
#endif /* REPROC_TEST_HAVE_HEADERS */

int reproc_dyn_init(const char *);
int reproc_libok(void);
void reproc_dyn_cleanup(void);
int reproc_dyn_has_symbol(const char *);

reproc_t *reproc_new(void);
int reproc_start(reproc_t *, const char *const *, reproc_options);
int reproc_pid(reproc_t *);
int reproc_read(reproc_t *, REPROC_STREAM, uint8_t *, size_t);
int reproc_wait(reproc_t *, int);
reproc_t *reproc_destroy(reproc_t *);
const char *reproc_strerror(int);
int reproc_run(const char *const *, reproc_options);

int main(int argc, char **argv) {
    int errs = 0;
    reproc_t *p;
    uint8_t buf[128];
    int n;
    const char *a1[] = { "sh", "-c", "printf reproc-ok", NULL };
    const char *a2[] = { "sh", "-c", "exit 7", NULL };
    reproc_options options;

    if (reproc_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("libreproc not installed, skipping reproc_dyn test\n");

    errs += CHECK(reproc_libok());
    errs += CHECK(reproc_dyn_has_symbol("reproc_new"));
    errs += CHECK(reproc_dyn_has_symbol("reproc_start"));
    errs += CHECK(reproc_strerror(-22) != NULL);
    printf("info: strerror(-22)=%s\n", reproc_strerror(-22));

    p = reproc_new();
    errs += CHECK(p != NULL);
    if (!p) { reproc_dyn_cleanup(); return 1; }

    memset(&options, 0, sizeof options);
    errs += CHECK(reproc_start(p, a1, options) > 0);   /* 成功时返回子进程 pid */
    errs += CHECK(reproc_pid(p) > 0);
    n = reproc_read(p, REPROC_STREAM_OUT, buf, sizeof buf - 1);
    errs += CHECK(n > 0);
    if (n > 0) {
        buf[n] = 0;
        printf("info: output=%s\n", (char *)buf);
        errs += CHECK(strstr((char *)buf, "reproc-ok") != NULL);
    }
    errs += CHECK(reproc_wait(p, 5000) == 0);
    (void)reproc_destroy(p);

    /* 便捷接口：返回子进程退出码 7 */
    memset(&options, 0, sizeof options);
    errs += CHECK(reproc_run(a2, options) == 7);

    reproc_dyn_cleanup();
    if (errs) return 1;
    printf("all checks passed\n");
    return 0;
}
