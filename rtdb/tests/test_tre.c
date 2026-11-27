/* tre_dyn 测试：只链 -ldl，不链 libtre */
#define TRE_APPROX 1
#include <tre/tre.h>
#include "test_common.h"

int tre_dyn_init(const char *library_path);
void tre_dyn_cleanup(void);
int tre_libok(void);
int tre_dyn_has_symbol(const char *name);

int main(int argc, char **argv) {
    int failures = 0;
    int cfg = 0;
    int rc = 0;
    char errbuf[128];
    regex_t preg;
    regex_t bad;
    regmatch_t pm[2];
    regaparams_t params;
    const char *ver = NULL;

    if (tre_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        TEST_SKIP("TRE 动态库不可用\n");
    }

    failures += CHECK(tre_libok() != 0);
    failures += CHECK(tre_dyn_has_symbol("tre_regcomp"));
    failures += CHECK(tre_dyn_has_symbol("tre_version"));

    ver = tre_version();
    failures += CHECK(ver != NULL && ver[0] != '\0');
    if (ver != NULL && ver[0] != '\0') {
        printf("tre version=%s\n", ver);
    }

    rc = tre_config(TRE_CONFIG_APPROX, &cfg);
    failures += CHECK(rc == 0);
    printf("tre approx support=%d\n", cfg);

    memset(&params, 0, sizeof(params));
    tre_regaparams_default(&params);
    failures += CHECK(params.cost_ins > 0 && params.max_err >= 0);

    memset(&preg, 0, sizeof(preg));
    failures += CHECK(tre_regcomp(&preg, "a([0-9]+)", REG_EXTENDED) == 0);
    if (tre_have_backrefs(&preg) != 0) {
        /* 该模式没有反向引用，遇到非零值说明 ABI 参数顺序可能不符。 */
        failures += CHECK(tre_have_backrefs(&preg) == 0);
    }
    failures += CHECK(tre_have_approx(&preg) == 0);
    failures += CHECK(tre_regexec(&preg, "a123", 2, pm, 0) == 0);
    failures += CHECK(pm[1].rm_eo - pm[1].rm_so == 3);
    tre_regfree(&preg);

    memset(&bad, 0, sizeof(bad));
    rc = tre_regcomp(&bad, "(", REG_EXTENDED);
    failures += CHECK(rc != 0);
    if (rc != 0) {
        size_t n = tre_regerror(rc, &bad, errbuf, sizeof(errbuf));
        failures += CHECK(n > 0 && n < sizeof(errbuf));
        if (n > 0 && n < sizeof(errbuf)) {
            printf("tre regerror=%s\n", errbuf);
        }
    }

    tre_dyn_cleanup();
    printf("tre: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
