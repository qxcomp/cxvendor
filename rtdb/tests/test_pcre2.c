/* pcre2_dyn 测试：只链 -ldl，不链 libpcre2-* */
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <pcre2posix.h>
#include "test_common.h"

int pcre2_dyn_init(const char *library_path);
void pcre2_dyn_cleanup(void);
int pcre2_libok(void);
int pcre2_dyn_has_symbol(const char *name);

/* pcre2.h 即使只定义 CODE_UNIT_WIDTH=8，也声明了 16/32 位接口，直接使用即可。 */

int main(int argc, char **argv) {
    int failures = 0;
    int errcode = 0;
    int n = 0;
    size_t erroffset = 0;
    PCRE2_UCHAR8 ver8[32] = {0};
    PCRE2_UCHAR16 ver16[32] = {0};
    PCRE2_UCHAR32 ver32[32] = {0};
    pcre2_code *code = NULL;
    pcre2_match_data *match_data = NULL;
    pcre2_code_16 *code16 = NULL;
    pcre2_code_32 *code32 = NULL;
    regex_t preg;
    regmatch_t pm[2];
    const uint16_t pat16[] = { 'a', '(', '\\', 'd', '+', ')' };
    const uint32_t pat32[] = { 'a', '(', '\\', 'd', '+', ')' };

    if (pcre2_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        TEST_SKIP("PCRE2 动态库不可用\n");
    }

    failures += CHECK(pcre2_libok() != 0);
    failures += CHECK(pcre2_dyn_has_symbol("pcre2_config_8"));
    failures += CHECK(pcre2_dyn_has_symbol("pcre2_config_16"));
    failures += CHECK(pcre2_dyn_has_symbol("pcre2_config_32"));
    failures += CHECK(pcre2_dyn_has_symbol("pcre2_regcomp"));

    n = pcre2_config_8(PCRE2_CONFIG_VERSION, ver8);
    failures += CHECK(n > 0 && n < (int)sizeof(ver8));
    if (n > 0 && n < (int)sizeof(ver8)) {
        printf("pcre2 version=%s\n", (const char *)ver8);
    }

    code = pcre2_compile((PCRE2_SPTR)"a(\\d+)", PCRE2_ZERO_TERMINATED, 0,
                         &errcode, &erroffset, NULL);
    failures += CHECK(code != NULL);
    if (code != NULL) {
        match_data = pcre2_match_data_create_from_pattern(code, NULL);
        failures += CHECK(match_data != NULL);
        if (match_data != NULL) {
            int rc = pcre2_match(code, (PCRE2_SPTR)"a123", 4, 0, 0, match_data, NULL);
            PCRE2_SIZE *ov = pcre2_get_ovector_pointer(match_data);
            failures += CHECK(rc == 2);
            failures += CHECK(ov[3] - ov[2] == 3);
            pcre2_match_data_free(match_data);
        }
        pcre2_code_free(code);
    }

    n = pcre2_config_16(PCRE2_CONFIG_VERSION, ver16);
    failures += CHECK(n > 0 && n < (int)sizeof(ver16));
    code16 = pcre2_compile_16(pat16, 6, 0, &errcode, &erroffset, NULL);
    failures += CHECK(code16 != NULL);
    if (code16 != NULL) {
        pcre2_code_free_16(code16);
    }

    n = pcre2_config_32(PCRE2_CONFIG_VERSION, ver32);
    failures += CHECK(n > 0 && n < (int)sizeof(ver32));
    code32 = pcre2_compile_32(pat32, 6, 0, &errcode, &erroffset, NULL);
    failures += CHECK(code32 != NULL);
    if (code32 != NULL) {
        pcre2_code_free_32(code32);
    }

    memset(&preg, 0, sizeof(preg));
    failures += CHECK(pcre2_regcomp(&preg, "a([0-9]+)", REG_EXTENDED) == 0);
    failures += CHECK(pcre2_regexec(&preg, "a123", 2, pm, 0) == 0);
    failures += CHECK(pm[1].rm_eo - pm[1].rm_so == 3);
    pcre2_regfree(&preg);

    pcre2_dyn_cleanup();
    printf("pcre2: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
