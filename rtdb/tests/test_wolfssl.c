/* wolfssl_dyn 测试：只链 -ldl，不链 libwolfssl */
#include "test_common.h"

int wolfssl_dyn_init(const char *library_path);
void wolfssl_dyn_cleanup(void);
int wolfssl_libok(void);
int wolfssl_dyn_has_symbol(const char *name);
void *wolfssl_sym(const char *name);

int wolfSSL_Init(void);
int wolfSSL_Cleanup(void);
void *wolfTLS_client_method(void);
void *wolfSSL_CTX_new(void *method);
void wolfSSL_CTX_free(void *ctx);

int main(int argc, char **argv) {
    int failures = 0;
    void *method = NULL, *ctx = NULL;

    if (wolfssl_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("wolfSSL 动态库不可用\n");
    failures += CHECK(wolfssl_libok() != 0);
    failures += CHECK(wolfssl_dyn_has_symbol("wolfSSL_CTX_new"));
    failures += CHECK(wolfssl_dyn_has_symbol("wolfTLS_client_method"));
    failures += CHECK(wolfssl_sym("wolfSSL_Init") != NULL);

    failures += CHECK(wolfSSL_Init() == 1);  /* WOLFSSL_SUCCESS */

    method = wolfTLS_client_method();
    failures += CHECK(method != NULL);
    if (method != NULL) {
        ctx = wolfSSL_CTX_new(method);
        failures += CHECK(ctx != NULL);
        if (ctx != NULL) wolfSSL_CTX_free(ctx);
    }

    failures += CHECK(wolfSSL_Cleanup() == 1);  /* WOLFSSL_SUCCESS */

    wolfssl_dyn_cleanup();
    printf("wolfssl: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
