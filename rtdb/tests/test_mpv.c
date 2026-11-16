/* mpv_dyn 测试：只链 -ldl，不链 libmpv */
#include "test_common.h"

typedef struct mpv_handle mpv_handle;

int mpv_dyn_init(const char *library_path);
void mpv_dyn_cleanup(void);
int mpv_libok(void);
unsigned long mpv_dyn_api_version(void);

mpv_handle *mpv_create(void);
int mpv_initialize(mpv_handle *ctx);
int mpv_set_option_string(mpv_handle *ctx, const char *name,
                          const char *data);
int mpv_set_property_string(mpv_handle *ctx, const char *name,
                            const char *data);
int mpv_command(mpv_handle *ctx, const char **args);
const char *mpv_error_string(int error);
void mpv_terminate_destroy(mpv_handle *ctx);

int main(int argc, char **argv) {
    int failures = 0;
    mpv_handle *h;

    if (mpv_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        fprintf(stderr, "mpv_dyn_init failed\n");
        return 1;
    }
    failures += CHECK(mpv_libok() != 0);

    failures += CHECK(mpv_dyn_api_version() > 0);
    printf("wrapper api version=%lu (error 0 = %s)\n",
           mpv_dyn_api_version(), mpv_error_string(0));

    h = mpv_create();
    failures += CHECK(h != NULL);
    if (!h) return 1;

    /* initialize 前设置 vo/ao=null，保证无显示/音频环境也可初始化 */
    failures += CHECK(mpv_set_option_string(h, "vo", "null") >= 0);
    failures += CHECK(mpv_set_option_string(h, "ao", "null") >= 0);
    failures += CHECK(mpv_initialize(h) >= 0);

    failures += CHECK(mpv_set_property_string(h, "pause", "yes") >= 0);
    {
        const char *cmd[] = { "set", "volume", "0", NULL };
        failures += CHECK(mpv_command(h, cmd) >= 0);
    }

    /* 错误路径：错误属性名返回负错误码 */
    {
        int r = mpv_set_property_string(h, "no-such-property-xyz", "1");
        printf("bad property -> %d (%s)\n", r, mpv_error_string(r));
        failures += CHECK(r < 0);
    }

    mpv_terminate_destroy(h);
    mpv_dyn_cleanup();

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
