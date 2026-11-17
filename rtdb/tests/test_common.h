/* rtdb 测试共享断言头：只依赖 libc，不引入任何目标库头文件 */
#ifndef RTDB_TEST_COMMON_H
#define RTDB_TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        return 1;
    }
    printf("ok: %s\n", what);
    return 0;
}

#define CHECK(cond) check((cond), #cond)
#define CHECK_MSG(cond, msg) check((cond), (msg))

/* 无服务可用时：打印 SKIP 并正常退出（run-all 视为通过） */
#define TEST_SKIP(...) \
    do { printf("SKIP: " __VA_ARGS__); return 0; } while (0)

#endif /* RTDB_TEST_COMMON_H */
