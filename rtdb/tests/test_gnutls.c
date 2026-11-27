/* gnutls_dyn 测试：只链 -ldl，不链 libgnutls */
#include "test_common.h"

int gnutls_dyn_init(const char *library_path);
void gnutls_dyn_cleanup(void);
int gnutls_libok(void);
int gnutls_dyn_has_symbol(const char *name);
void *gnutls_sym(const char *name);

int gnutls_global_init(void);
void gnutls_global_deinit(void);
const char *gnutls_check_version(const char *required);
int gnutls_init(void **session, unsigned int flags);
void gnutls_deinit(void *session);

/* 数据符号：库加载时由 dyn 层同步的分配器函数指针 */
extern void *(*gnutls_malloc)(size_t);
extern void *gnutls_secure_malloc;              /* 无公开声明：void * 承载 */
extern char *(*gnutls_strdup)(const char *);     /* 公开头：函数指针 */

#define GNUTLS_CLIENT 1

int main(int argc, char **argv) {
    int failures = 0;
    const char *ver;
    void *session = NULL;

    if (gnutls_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("GnuTLS 动态库不可用\n");
    failures += CHECK(gnutls_libok() != 0);
    failures += CHECK(gnutls_dyn_has_symbol("gnutls_check_version"));
    failures += CHECK(gnutls_sym("gnutls_global_init") != NULL);

    failures += CHECK(gnutls_global_init() == 0);

    ver = gnutls_check_version(NULL);
    failures += CHECK(ver != NULL && ver[0] != '\0');
    failures += CHECK(gnutls_check_version("3.0") != NULL);
    printf("gnutls version=%s\n", ver ? ver : "(null)");

    failures += CHECK(gnutls_init(&session, GNUTLS_CLIENT) == 0);
    failures += CHECK(session != NULL);
    if (session != NULL) gnutls_deinit(session);

    failures += CHECK(gnutls_malloc != NULL);
    failures += CHECK(gnutls_strdup != NULL);
    failures += CHECK(gnutls_secure_malloc != NULL);

    gnutls_global_deinit();
    gnutls_dyn_cleanup();
    printf("gnutls: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
