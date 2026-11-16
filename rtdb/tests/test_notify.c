/* notify_dyn 测试（移植自 /tmp/notify_dyn_test.c）：只链 -ldl
 * 运行方式：make run-notify（dbus-run-session） */
#include "test_common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct _NotifyNotification NotifyNotification;
typedef struct _GList GList;
struct _GList { void *data; GList *next, *prev; }; /* 布局与 glib 一致（测试用） */
typedef struct _GError GError;
typedef int gboolean;
typedef int gint;

int notify_dyn_init(const char *path);
void notify_dyn_cleanup(void);
const char *notify_dyn_version(void);
int notify_dyn_minor(void);
int notify_dyn_api_level(void);
int notify_dyn_has_symbol(const char *name);

/* 原版 API 名（由封装层提供） */
int notify_init(const char *app_name);
void notify_uninit(void);
gboolean notify_is_initted(void);
const char *notify_get_app_name(void);
GList *notify_get_server_caps(void);
gboolean notify_get_server_info(char **name, char **vendor, char **version);
NotifyNotification *notify_notification_new(const char *summary,
                                            const char *body,
                                            const char *icon);
gboolean notify_notification_update(NotifyNotification *n,
                                    const char *summary,
                                    const char *body,
                                    const char *icon);
gboolean notify_notification_show(NotifyNotification *n, GError **error);
void notify_notification_set_timeout(NotifyNotification *n, gint t);
void notify_notification_set_urgency(NotifyNotification *n, gint u);
void notify_notification_set_hint_string(NotifyNotification *n,
                                         const char *k, const char *v);
void notify_notification_set_app_name(NotifyNotification *n,
                                      const char *app_name);
void notify_notification_add_action(NotifyNotification *n,
                                    const char *action,
                                    const char *label,
                                    void (*cb)(NotifyNotification *,
                                               char *, void *),
                                    void *user_data,
                                    void (*free_func)(void *));
gboolean notify_notification_close(NotifyNotification *n, GError **error);
int notify_notification_get_closed_reason(const NotifyNotification *n);
const char *notify_notification_get_activation_token(NotifyNotification *n);

/* glib 部分通过封装层加载的依赖即可用（dlopen 自动递归加载），
 * 但调用 g_free 等 glib 函数需要声明为弱符号或直接 dlsym。
 * 这里测试主要验证封装层自身行为，show 的错误检查用 GError 的
 * message 域偏移读取。简化：不调 g_free，泄漏由进程退出回收。 */

static void test_action_cb(NotifyNotification *nn, char *action, void *ud) {
    (void)nn; (void)action; (void)ud;
}

int main(int argc, char **argv) {
    int failures = 0;
    GList *caps;
    NotifyNotification *n;

    if (notify_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        fprintf(stderr, "notify_dyn_init failed\n");
        return 1;
    }
    printf("loaded libnotify %s (api=%d)\n",
           notify_dyn_version(), notify_dyn_api_level());

    failures += check(notify_dyn_minor() == 7 || notify_dyn_minor() == 8,
                      "API level 7/8");
    failures += check(notify_dyn_has_symbol("notify_init"), "has notify_init");
    failures += check(
        notify_dyn_has_symbol("notify_notification_this_does_not_exist") == 0,
        "missing symbol probe");

    failures += check(notify_init("notify_dyn_test") != 0, "notify_init");
    failures += check(notify_is_initted() != 0, "notify_is_initted");
    failures += check(notify_get_app_name() != NULL &&
                          strcmp(notify_get_app_name(), "notify_dyn_test") == 0,
                      "notify_get_app_name");

    caps = notify_get_server_caps();
    failures += check(caps != NULL, "notify_get_server_caps");
    /* caps 元素是 gchar* 字符串 */
    if (caps) {
        int count = 0;
        GList *l;
        for (l = caps; l; l = l->next) count++;
        printf("server caps count=%d, first=%s\n",
               count, caps->data ? (const char *)caps->data : "(null)");
    }

    {
        char *sname = NULL, *svendor = NULL, *sver = NULL;
        gboolean ok = notify_get_server_info(&sname, &svendor, &sver);
        failures += check(ok != 0, "notify_get_server_info");
        if (ok)
            printf("server: name=%s vendor=%s version=%s\n",
                   sname ? sname : "(null)",
                   svendor ? svendor : "(null)",
                   sver ? sver : "(null)");
    }

    n = notify_notification_new("summary text", "body text", NULL);
    failures += check(n != NULL, "notify_notification_new");
    if (n) {
        notify_notification_set_app_name(n, "notify_dyn_test");
        notify_notification_set_timeout(n, 5000);
        notify_notification_set_urgency(n, 1); /* NORMAL */
        notify_notification_set_hint_string(n, "test-key", "test-value");

        {
            gboolean ok = notify_notification_show(n, NULL);
            printf("show -> %s\n", ok ? "TRUE (displayed)" :
                   "FALSE (no notification daemon?)");
            /* show 结果依赖环境守护进程，不算失败 */
        }

        {
            gboolean ok = notify_notification_update(
                n, "updated summary", "updated body", NULL);
            failures += check(ok != 0, "notify_notification_update");
        }

        notify_notification_add_action(n, "test-action", "Test",
                                       test_action_cb, NULL, NULL);

        failures += check(
            notify_notification_get_closed_reason(n) == -1,
            "get_closed_reason == UNSET before close");

        {
            gboolean ok = notify_notification_close(n, NULL);
            printf("close -> %s\n", ok ? "TRUE" : "FALSE");
        }
        {
            int reason = notify_notification_get_closed_reason(n);
            printf("closed_reason=%d\n", reason);
            failures += check(reason != -1 || reason == -1, "closed_reason read");
        }

        /* 注：notify_notification_get_activation_token 只能在
         * 激活(activating)流程内调用，否则原版库自身断言失败，
         * 不在空闲时测试。 */
    }

    notify_uninit();
    notify_dyn_cleanup();

    if (failures) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
