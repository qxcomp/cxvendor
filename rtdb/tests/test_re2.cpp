/* re2_dyn 测试：只显式链 -ldl，不链 libre2 */
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_common.h"

extern "C" {
int re2_dyn_init(const char *library_path);
void re2_dyn_cleanup(void);
int re2_libok(void);
int re2_dyn_has_symbol(const char *name);
}

/* 与 libstdc++ 的 std::basic_string_view / std::string ABI 对齐。 */
struct re2_sv {
    std::size_t len;
    const char *ptr;
};

struct re2_std_string {
    void *ptr;
    std::size_t len;
    union {
        char bytes[16];
        void *align;
    } tail;
};

static_assert(sizeof(re2_sv) == sizeof(std::string_view), "string_view ABI mismatch");
static_assert(sizeof(re2_std_string) == sizeof(std::string), "std::string ABI mismatch");
static_assert(alignof(re2_std_string) == alignof(std::string), "std::string alignment mismatch");

static re2_sv make_sv(const char *s) {
    re2_sv v;
    v.len = std::strlen(s);
    v.ptr = s;
    return v;
}

/* wrapper 函数名就是原始 mangled symbol；用 extern "C" 保证链接名不被再次改写。 */
extern "C" {
void *_ZN3re23RE2C1EPKc(void *self, const char *pattern);
void _ZN3re23RE2D1Ev(void *self);
unsigned char _ZN3re23RE210FullMatchNESt17basic_string_viewIcSt11char_traitsIcEERKS0_PKPKNS0_3ArgEi(
    re2_sv text, void *re, const void *const *args, int nargs);
unsigned char _ZN3re23RE213PartialMatchNESt17basic_string_viewIcSt11char_traitsIcEERKS0_PKPKNS0_3ArgEi(
    re2_sv text, void *re, const void *const *args, int nargs);
re2_std_string _ZN3re23RE29QuoteMetaB5cxx11ESt17basic_string_viewIcSt11char_traitsIcEE(re2_sv text);
void *_ZN3re23RE27OptionsC1ENS0_13CannedOptionsE(void *self, int canned);
void *_ZN3re23RE23SetC1ERKNS0_7OptionsENS0_6AnchorE(void *self, const void *options, int anchor);
void _ZN3re23RE23SetD1Ev(void *self);
int _ZN3re23RE23Set3AddESt17basic_string_viewIcSt11char_traitsIcEEPNSt7__cxx1112basic_stringIcS4_SaIcEEE(
    void *self, re2_sv pattern, void *error);
unsigned char _ZN3re23RE23Set7CompileEv(void *self);
unsigned char _ZNK3re23RE23Set5MatchESt17basic_string_viewIcSt11char_traitsIcEEPSt6vectorIiSaIiEE(
    const void *self, re2_sv text, void *matches);
void *_ZN3re211FilteredRE2C1Ev(void *self);
void _ZN3re211FilteredRE2D1Ev(void *self);
int _ZN3re211FilteredRE23AddESt17basic_string_viewIcSt11char_traitsIcEERKNS_3RE27OptionsEPi(
    void *self, re2_sv pattern, const void *options, int *id);
void _ZN3re211FilteredRE27CompileEPSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS7_EE(
    void *self, void *patterns);
int _ZNK3re211FilteredRE214SlowFirstMatchESt17basic_string_viewIcSt11char_traitsIcEE(
    const void *self, re2_sv text);
}

int main(int argc, char **argv) {
    int failures = 0;
    alignas(16) unsigned char re_obj[2048] = {0};
    alignas(16) unsigned char options_obj[1024] = {0};
    alignas(16) unsigned char set_obj[2048] = {0};
    alignas(16) unsigned char filtered_obj[4096] = {0};
    std::string error;
    std::string quoted;
    std::vector<int> matches;
    std::vector<std::string> patterns;
    int id = -1;

    if (re2_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        TEST_SKIP("RE2 动态库不可用\n");
    }

    failures += CHECK(re2_libok() != 0);
    failures += CHECK(re2_dyn_has_symbol("_ZNK3re23RE211ProgramSizeEv"));

    _ZN3re23RE2C1EPKc(re_obj, "a[0-9]+");
    failures += CHECK(_ZN3re23RE210FullMatchNESt17basic_string_viewIcSt11char_traitsIcEERKS0_PKPKNS0_3ArgEi(
                          make_sv("a123"), re_obj, NULL, 0) != 0);
    failures += CHECK(_ZN3re23RE210FullMatchNESt17basic_string_viewIcSt11char_traitsIcEERKS0_PKPKNS0_3ArgEi(
                          make_sv("xa123"), re_obj, NULL, 0) == 0);
    failures += CHECK(_ZN3re23RE213PartialMatchNESt17basic_string_viewIcSt11char_traitsIcEERKS0_PKPKNS0_3ArgEi(
                          make_sv("xxa123yy"), re_obj, NULL, 0) != 0);
    _ZN3re23RE2D1Ev(re_obj);

    quoted.clear();
    {
        re2_std_string raw = _ZN3re23RE29QuoteMetaB5cxx11ESt17basic_string_viewIcSt11char_traitsIcEE(make_sv("a.b"));
        if (raw.ptr != NULL && raw.len > 0) {
            quoted.assign(static_cast<const char *>(raw.ptr), raw.len);
        }
        reinterpret_cast<std::string *>(&raw)->~basic_string();
    }
    failures += CHECK(quoted == "a\\.b");

    _ZN3re23RE27OptionsC1ENS0_13CannedOptionsE(options_obj, 0);
    _ZN3re23RE23SetC1ERKNS0_7OptionsENS0_6AnchorE(set_obj, options_obj, 0);
    failures += CHECK(_ZN3re23RE23Set3AddESt17basic_string_viewIcSt11char_traitsIcEEPNSt7__cxx1112basic_stringIcS4_SaIcEEE(
                          set_obj, make_sv("a[0-9]+"), &error) == 0);
    failures += CHECK(_ZN3re23RE23Set3AddESt17basic_string_viewIcSt11char_traitsIcEEPNSt7__cxx1112basic_stringIcS4_SaIcEEE(
                          set_obj, make_sv("b+"), &error) == 1);
    failures += CHECK(_ZN3re23RE23Set7CompileEv(set_obj) != 0);
    matches.clear();
    failures += CHECK(_ZNK3re23RE23Set5MatchESt17basic_string_viewIcSt11char_traitsIcEEPSt6vectorIiSaIiEE(
                          set_obj, make_sv("a456"), &matches) != 0);
    failures += CHECK(matches.size() == 1 && matches[0] == 0);
    _ZN3re23RE23SetD1Ev(set_obj);

    _ZN3re211FilteredRE2C1Ev(filtered_obj);
    id = -1;
    failures += CHECK(_ZN3re211FilteredRE23AddESt17basic_string_viewIcSt11char_traitsIcEERKNS_3RE27OptionsEPi(
                          filtered_obj, make_sv("foo"), options_obj, &id) == 0);
    failures += CHECK(id == 0);
    _ZN3re211FilteredRE27CompileEPSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS7_EE(
        filtered_obj, &patterns);
    failures += CHECK(_ZNK3re211FilteredRE214SlowFirstMatchESt17basic_string_viewIcSt11char_traitsIcEE(
                          filtered_obj, make_sv("xx foo xx")) == 0);
    failures += CHECK(_ZNK3re211FilteredRE214SlowFirstMatchESt17basic_string_viewIcSt11char_traitsIcEE(
                          filtered_obj, make_sv("xx bar xx")) < 0);
    _ZN3re211FilteredRE2D1Ev(filtered_obj);

    re2_dyn_cleanup();
    printf("re2: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
