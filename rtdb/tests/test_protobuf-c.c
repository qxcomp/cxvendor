/* protobuf-c_dyn 测试：只链 -ldl，不链 libprotobuf-c
 * 覆盖：动态加载、控制函数、符号探测，以及真实版本查询/描述符查询。
 * 库缺失时按 TEST_SKIP 正常退出。
 */
#include "test_common.h"
#include <stdio.h>
#include <string.h>

extern int protobuf_c_dyn_init(const char *);
extern int protobuf_c_dyn_libok(void);
extern void protobuf_c_dyn_cleanup(void);
extern int protobuf_c_dyn_has_symbol(const char *);

extern const char *protobuf_c_version(void);
extern unsigned int protobuf_c_version_number(void);
extern const void *protobuf_c_message_descriptor_get_field_by_name(const void *, const char *);
extern const void *protobuf_c_enum_descriptor_get_value_by_name(const void *, const char *);
extern size_t protobuf_c_message_get_packed_size(const void *);
extern void protobuf_c_message_free_unpacked(void *, void *);

int main(void) {
    int errs = 0;
    const char *ver;
    unsigned int vernum;

    if (protobuf_c_dyn_init(NULL) != 0)
        TEST_SKIP("libprotobuf-c not installed, skipping protobuf-c_dyn test\n");

    errs += CHECK(protobuf_c_dyn_libok());
    errs += CHECK(protobuf_c_dyn_has_symbol("protobuf_c_version"));
    errs += CHECK(protobuf_c_dyn_has_symbol("protobuf_c_message_unpack"));
    errs += CHECK(protobuf_c_dyn_has_symbol("protobuf_c_message_pack"));
    errs += CHECK(protobuf_c_dyn_has_symbol("protobuf_c_service_destroy"));
    errs += CHECK(!protobuf_c_dyn_has_symbol("protobuf_c_no_such_symbol_xyz"));

    /* 全部 18 个导出都应可 dlsym */
    {
        const char *names[] = {
            "protobuf_c_version", "protobuf_c_version_number",
            "protobuf_c_enum_descriptor_get_value_by_name",
            "protobuf_c_enum_descriptor_get_value",
            "protobuf_c_message_descriptor_get_field_by_name",
            "protobuf_c_message_descriptor_get_field",
            "protobuf_c_message_get_packed_size", "protobuf_c_message_pack",
            "protobuf_c_message_pack_to_buffer", "protobuf_c_message_unpack",
            "protobuf_c_message_free_unpacked", "protobuf_c_message_check",
            "protobuf_c_message_init", "protobuf_c_service_destroy",
            "protobuf_c_service_descriptor_get_method_by_name",
            "protobuf_c_buffer_simple_append", "protobuf_c_service_generated_init",
            "protobuf_c_service_invoke_internal"
        };
        unsigned int i, missing = 0;
        for (i = 0; i < sizeof names / sizeof names[0]; i++)
            if (!protobuf_c_dyn_has_symbol(names[i])) missing++;
        printf("info: protobuf-c exports found = %u/18\n",
               (unsigned int)(sizeof names / sizeof names[0]) - missing);
        errs += CHECK(missing == 0);
    }

    ver = protobuf_c_version();
    vernum = protobuf_c_version_number();
    errs += CHECK(ver != NULL);
    errs += CHECK(vernum > 0);
    printf("info: protobuf-c version = %s (number=%u)\n", ver ? ver : "(null)", vernum);

    /* 32 位版本号编码：高 16 位主版本，其低 8 位次版本 */
    errs += CHECK(((vernum >> 16) & 0xffff) >= 1);

    /* 描述符查询：传入非法指针应安全返回 NULL（fallback 路径） */
    errs += CHECK(protobuf_c_message_descriptor_get_field_by_name(NULL, "x") == NULL);
    errs += CHECK(protobuf_c_enum_descriptor_get_value_by_name(NULL, "x") == NULL);

    protobuf_c_dyn_cleanup();
    if (errs) { fprintf(stderr, "protobuf-c_dyn: %d failure(s)\n", errs); return 1; }
    printf("protobuf-c_dyn: all checks passed\n");
    return 0;
}
