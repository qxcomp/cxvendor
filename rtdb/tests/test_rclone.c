/* rclone_dyn 测试：只链 -ldl，不链 librclone
 * 注意 RcloneRPC 按值返回 struct RcloneRPCResult（与 librclone 头一致）
 */
#include "test_common.h"

struct RcloneRPCResult {
    char *Output;
    int   Status;
};

int rclone_dyn_init(const char *library_path);
void rclone_dyn_cleanup(void);
int rclone_libok(void);

void RcloneInitialize(void);
void RcloneFinalize(void);
void RcloneFreeString(char *str);
struct RcloneRPCResult RcloneRPC(char *method, char *input);

int main(int argc, char **argv) {
    int failures = 0;

    if (rclone_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        fprintf(stderr, "rclone_dyn_init failed\n");
        return 1;
    }
    failures += CHECK(rclone_libok() != 0);

    RcloneInitialize();

    /* core/version：合法 HTTP 状态 + 非空 JSON 输出 */
    {
        struct RcloneRPCResult r = RcloneRPC("core/version", "{}");
        printf("status=%d output=%.120s\n", r.Status,
               r.Output ? r.Output : "(null)");
        failures += CHECK(r.Status == 200);
        failures += CHECK(r.Output != NULL && r.Output[0] == '{');
        if (r.Output) RcloneFreeString(r.Output);
    }

    /* 错误路径：不存在的 method → status 非 200 */
    {
        struct RcloneRPCResult r = RcloneRPC("no/such/method", "{}");
        printf("bad method status=%d\n", r.Status);
        failures += CHECK(r.Status != 200);
        if (r.Output) RcloneFreeString(r.Output);
    }

    /* 错误路径：NULL 参数 → 安全默认值不崩溃 */
    {
        struct RcloneRPCResult r = RcloneRPC(NULL, NULL);
        failures += CHECK(r.Status == 0 && r.Output == NULL);
    }

    RcloneFinalize();
    rclone_dyn_cleanup();

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
