/* clang_dyn 测试：只链 -ldl，不链 libclang */
#include "test_common.h"

/* CXString 是 libclang 公开布局的结构体 */
typedef struct CXStringImpl {
    const void *data;
    void (*dispose)(const void *);
} CXString;
typedef void *CXIndex;

int clang_dyn_init(const char *library_path);
void clang_dyn_cleanup(void);
int clang_libok(void);

CXIndex clang_createIndex(int excludeDeclarationsFromPCH,
                          int displayDiagnostics);
void clang_disposeIndex(CXIndex index);
CXString clang_getClangVersion(void);
void clang_disposeString(CXString string);

int main(int argc, char **argv) {
    int failures = 0;
    CXString ver;
    CXIndex idx;

    if (clang_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        fprintf(stderr, "clang_dyn_init failed\n");
        return 1;
    }
    failures += CHECK(clang_libok() != 0);

    ver = clang_getClangVersion();
    printf("version=%s\n", ver.data ? (const char *)ver.data : "(null)");
    failures += CHECK(ver.data != NULL &&
                      strstr((const char *)ver.data, "clang version") != NULL);
    clang_disposeString(ver);

    idx = clang_createIndex(0, 0);
    failures += CHECK(idx != NULL);
    if (idx) clang_disposeIndex(idx);

    /* 错误路径：dispose NULL 不崩溃 */
    clang_disposeIndex(NULL);
    failures += CHECK(1);

    clang_dyn_cleanup();

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
