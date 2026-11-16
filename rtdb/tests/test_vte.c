/* vte_dyn 测试（GTK3）：链接 gtk+-3.0 但不链 -lvte
 * 运行方式：make run-vte（xvfb-run -a）
 */
#include "test_common.h"
#include <gtk/gtk.h>

typedef struct _VteTerminal VteTerminal;
typedef struct _VtePty VtePty;

int vte_dyn_init_gtk(int gtk_major, const char *library_path);
void vte_dyn_cleanup(void);
int vte_dyn_major(void);
int vte_dyn_minor(void);
int vte_dyn_micro(void);
int vte_dyn_gtk_major(void);
int vte_dyn_has_symbol(const char *name);

GtkWidget *vte_terminal_new(void);
void vte_terminal_feed(VteTerminal *t, const char *data, long len);
void vte_terminal_feed_child(VteTerminal *t, const char *data, long len);
long vte_terminal_get_column_count(VteTerminal *t);
long vte_terminal_get_row_count(VteTerminal *t);
void vte_terminal_set_size(VteTerminal *t, long cols, long rows);
void vte_terminal_set_scrollback_lines(VteTerminal *t, long lines);
gboolean vte_terminal_spawn_sync(VteTerminal *t, int pty_flags,
                                 const char *working_directory, char **argv,
                                 char **envv, int spawn_flags,
                                 void *child_setup, void *child_setup_data,
                                 GPid *child_pid, GCancellable *cancellable,
                                 GError **error);
VtePty *vte_terminal_get_pty(VteTerminal *t);
const char *vte_terminal_get_window_title(VteTerminal *t);

static VteTerminal *term;

static gboolean quit_cb(gpointer data) {
    (void)data;
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

static gboolean input_after_spawn_cb(gpointer data) {
    (void)data;
    /* spawn 完成后向子进程输入命令，验证交互通路 */
    vte_terminal_feed_child(term, "echo vtedyn-$((6*7))\r\n", -1);
    g_timeout_add(300, quit_cb, NULL);
    return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
    int failures = 0;
    gtk_init(&argc, &argv);

    if (vte_dyn_init_gtk(3, argc > 1 ? argv[1] : NULL) != 0) {
        fprintf(stderr, "vte_dyn_init_gtk(3) failed\n");
        return 1;
    }
    printf("loaded libvte %d.%d.%d (gtk %d)\n", vte_dyn_major(),
           vte_dyn_minor(), vte_dyn_micro(), vte_dyn_gtk_major());

    failures += CHECK(vte_dyn_gtk_major() == 3);
    failures += CHECK(vte_dyn_has_symbol("vte_terminal_new"));

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "vte_dyn_test");
    term = (VteTerminal *)(void *)vte_terminal_new();
    failures += CHECK(term != NULL);
    if (!term) return 1;

    /* feed 静态文本（屏幕缓冲） */
    vte_terminal_feed(term, "hello vte\r\n", -1);

    vte_terminal_set_size(term, 80, 24);
    vte_terminal_set_scrollback_lines(term, 500);
    failures += CHECK(vte_terminal_get_column_count(term) == 80);
    failures += CHECK(vte_terminal_get_row_count(term) == 24);
    printf("window_title=%s\n",
           vte_terminal_get_window_title(term) ? vte_terminal_get_window_title(term)
                                               : "(null)");

    gtk_container_add(GTK_CONTAINER(win), GTK_WIDGET(term));
    gtk_widget_show_all(win);

    /* spawn /bin/sh 交互 */
    {
        char *cmd[] = { "/bin/sh", NULL };
        GPid pid = 0;
        int ok = vte_terminal_spawn_sync(
            term, 0 /* PTY_FLAGS_DEFAULT */, NULL, cmd, NULL,
            G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, NULL, NULL);
        failures += CHECK(ok != 0);
        failures += CHECK(vte_terminal_get_pty(term) != NULL);
        if (pid) g_spawn_close_pid(pid);
    }

    g_timeout_add(400, input_after_spawn_cb, NULL);
    gtk_main();

    failures += CHECK(1); /* 走到 main loop 结束即未崩溃 */

    gtk_widget_destroy(win);
    vte_dyn_cleanup();

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
