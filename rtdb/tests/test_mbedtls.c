/* mbedtls_dyn 测试：只链 -ldl，不链 libmbedtls/libmbedcrypto/libmbedx509 */
#include "test_common.h"

int mbedtls_dyn_init(const char *library_path);
void mbedtls_dyn_cleanup(void);
int mbedtls_libok(void);
int mbedtls_dyn_has_symbol(const char *name);
void *mbedtls_sym(const char *name);

const char *mbedtls_version_get_string(void);
unsigned int mbedtls_version_get_number(void);
void mbedtls_strerror(int err, char *buf, size_t buf_size);

/* 数据符号：dyn 层用可写副本承载，大小由官方头文件实测为 16 字节 */
extern unsigned char mbedtls_x509_crt_profile_default[16];
extern void *mbedtls_ecdsa_info;                 /* 私有头声明：void * 承载 */
extern char *mbedtls_platform_dev_random;        /* 公开头：const char * */
extern void *mbedtls_ssl_iana_tls_group_info;    /* 公开头：数组 → 指针 */

int main(int argc, char **argv) {
    int failures = 0;
    char buf[256];
    const char *ver;
    unsigned int num;
    int i, nonzero;

    if (mbedtls_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("mbedTLS 动态库不可用\n");
    failures += CHECK(mbedtls_libok() != 0);
    failures += CHECK(mbedtls_dyn_has_symbol("mbedtls_version_get_string"));
    failures += CHECK(mbedtls_sym("mbedtls_version_get_string") != NULL);

    ver = mbedtls_version_get_string();
    num = mbedtls_version_get_number();
    failures += CHECK(ver != NULL && ver[0] != '\0');
    failures += CHECK(num != 0);
    printf("mbedtls version=%s number=0x%08x\n", ver ? ver : "(null)", num);

    memset(buf, 0, sizeof(buf));
    mbedtls_strerror(-82, buf, sizeof(buf));  /* -0x0052: NET_UNKNOWN_HOST */
    failures += CHECK(strstr(buf, "NET") != NULL);
    printf("mbedtls strerror(-82)=%s\n", buf);

    /* 数据符号同步：加载后副本应携带真实库中的非齐次值 */
    nonzero = 0;
    for (i = 0; i < 16; i++)
        if (mbedtls_x509_crt_profile_default[i] != 0) nonzero++;
    failures += CHECK(nonzero > 0);
    printf("mbedtls_x509_crt_profile_default nonzero_bytes=%d/16\n", nonzero);

    failures += CHECK(mbedtls_ecdsa_info != NULL);
    failures += CHECK(mbedtls_ssl_iana_tls_group_info != NULL);
    printf("mbedtls_platform_dev_random=%p\n", (void *)mbedtls_platform_dev_random);

    mbedtls_dyn_cleanup();
    printf("mbedtls: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
