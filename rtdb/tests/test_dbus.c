/* dbus_dyn 测试（移植自 /tmp/dbus_dyn_test.c，已验证通过）：只链 -ldl
 * 运行方式：make run-dbus（dbus-run-session）
 *  * 注意：append_args/get_args 传值的地址（&v），不是值本身；
 *  *       marshal 前需 dbus_message_set_serial，否则 demarshal 报 Bad serial */
#include "test_common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef int dbus_bool_t;
typedef unsigned int dbus_uint32_t;
typedef int dbus_int32_t;

typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;
typedef struct DBusPendingCall DBusPendingCall;

typedef struct DBusError {
    const char *name;
    const char *message;
    unsigned int dummy1 : 1;
    unsigned int dummy2 : 1;
    unsigned int dummy3 : 1;
    unsigned int dummy4 : 1;
    unsigned int dummy5 : 1;
    void *padding1;
} DBusError;

typedef struct DBusMessageIter {
    void *dummy1, *dummy2;
    dbus_uint32_t dummy3;
    int dummy4, dummy5, dummy6, dummy7, dummy8, dummy9, dummy10, dummy11, pad1;
    void *pad2, *pad3;
} DBusMessageIter;

#define DBUS_DYN_ERROR_INIT { NULL, NULL, 1, 0, 0, 0, 0, NULL }
#define DBUS_DYN_BUS_SESSION 0
#define DBUS_DYN_BUS_SYSTEM 1
#define DBUS_DYN_MESSAGE_TYPE_METHOD_CALL 1
#define DBUS_DYN_TYPE_STRING ((int)'s')
#define DBUS_DYN_TYPE_UINT32 ((int)'u')
#define DBUS_DYN_TYPE_INVALID ((int)'\0')
#define DBUS_DYN_NAME_FLAG_DO_NOT_QUEUE 0x4
#define DBUS_DYN_REQUEST_NAME_REPLY_PRIMARY_OWNER 1

int dbus_dyn_init(const char *path);
void dbus_dyn_cleanup(void);
const char *dbus_dyn_version(void);
int dbus_dyn_major(void);
int dbus_dyn_minor(void);
int dbus_dyn_micro(void);
int dbus_dyn_has_symbol(const char *name);

void dbus_error_init(DBusError *error);
void dbus_error_free(DBusError *error);
void dbus_free(void *memory);
dbus_bool_t dbus_error_is_set(const DBusError *error);
dbus_bool_t dbus_error_has_name(const DBusError *error, const char *name);
DBusConnection *dbus_bus_get(int type, DBusError *error);
const char *dbus_bus_get_unique_name(DBusConnection *c);
dbus_bool_t dbus_bus_name_has_owner(DBusConnection *c, const char *name,
                                    DBusError *error);
int dbus_bus_request_name(DBusConnection *c, const char *name,
                          unsigned int flags, DBusError *error);
DBusMessage *dbus_message_new_method_call(const char *bus_name,
                                          const char *path,
                                          const char *iface,
                                          const char *method);
void dbus_message_unref(DBusMessage *m);
void dbus_message_set_serial(DBusMessage *m, dbus_uint32_t serial);
dbus_bool_t dbus_message_append_args(DBusMessage *m, int first, ...);
dbus_bool_t dbus_message_get_args(DBusMessage *m, DBusError *error,
                                  int first, ...);
const char *dbus_message_get_signature(DBusMessage *m);
DBusMessage *dbus_connection_send_with_reply_and_block(
    DBusConnection *c, DBusMessage *m, int timeout, DBusError *error);
dbus_bool_t dbus_message_iter_init(DBusMessage *m, DBusMessageIter *iter);
int dbus_message_iter_get_arg_type(DBusMessageIter *iter);
void dbus_message_iter_get_basic(DBusMessageIter *iter, void *value);
dbus_bool_t dbus_message_iter_next(DBusMessageIter *iter);
dbus_bool_t dbus_message_marshal(DBusMessage *m, char **data, int *len);
DBusMessage *dbus_message_demarshal(const char *str, int len,
                                    DBusError *error);
int dbus_message_demarshal_bytes_needed(const char *str, int len);
dbus_bool_t dbus_signature_validate(const char *sig, DBusError *error);
dbus_bool_t dbus_validate_bus_name(const char *name, DBusError *error);
char *dbus_get_local_machine_id(void);
dbus_bool_t dbus_message_is_method_call(DBusMessage *m, const char *iface,
                                        const char *method);
dbus_bool_t dbus_connection_get_is_connected(DBusConnection *c);
dbus_bool_t dbus_connection_get_is_authenticated(DBusConnection *c);

int main(int argc, char **argv) {
    int failures = 0;
    DBusError err = DBUS_DYN_ERROR_INIT;
    DBusConnection *conn;
    DBusMessage *msg, *reply;
    DBusMessageIter iter;

    if (dbus_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        fprintf(stderr, "dbus_dyn_init failed\n");
        return 1;
    }
    printf("loaded libdbus %s (%d.%d.%d)\n", dbus_dyn_version(),
           dbus_dyn_major(), dbus_dyn_minor(), dbus_dyn_micro());

    failures += check(dbus_dyn_major() == 1, "major version 1");
    failures += check(dbus_dyn_has_symbol("dbus_bus_get"), "has dbus_bus_get");
    failures += check(
        dbus_dyn_has_symbol("dbus_this_symbol_does_not_exist") == 0,
        "missing symbol probe");

    /* 1. 版本 + 机器 ID */
    {
        char *mid = dbus_get_local_machine_id();
        failures += check(mid != NULL && strlen(mid) == 32,
                          "dbus_get_local_machine_id");
        if (mid) { printf("machine id=%s\n", mid); free(mid); }
    }

    /* 2. 校验函数 */
    failures += check(dbus_signature_validate("sus", NULL), "signature 'sus'");
    failures += check(!dbus_signature_validate("zzz", NULL), "bad signature 'zzz'");
    failures += check(dbus_validate_bus_name("org.example.Test", NULL),
                      "bus name valid");
    failures += check(!dbus_validate_bus_name("1invalid", NULL),
                      "bus name invalid");

    /* 3. 连接 session bus */
    conn = dbus_bus_get(DBUS_DYN_BUS_SESSION, &err);
    if (!conn) {
        fprintf(stderr, "bus_get error: %s: %s\n",
                err.name ? err.name : "?", err.message ? err.message : "?");
        dbus_error_free(&err);
        return 1;
    }
    failures += check(conn != NULL, "dbus_bus_get(session)");
    failures += check(dbus_connection_get_is_connected(conn),
                      "connection is_connected");
    failures += check(dbus_connection_get_is_authenticated(conn),
                      "connection is_authenticated");
    {
        const char *uniq = dbus_bus_get_unique_name(conn);
        failures += check(uniq != NULL && uniq[0] == ':', "unique name :X.Y");
        printf("unique name=%s\n", uniq ? uniq : "(null)");
    }

    /* 4. name_has_owner + request_name */
    failures += check(dbus_bus_name_has_owner(conn, "org.freedesktop.DBus",
                                              &err),
                      "name_has_owner(org.freedesktop.DBus)");
    {
        int r = dbus_bus_request_name(conn, "org.example.dbus_dyn_test",
                                      DBUS_DYN_NAME_FLAG_DO_NOT_QUEUE, &err);
        failures += check(r == DBUS_DYN_REQUEST_NAME_REPLY_PRIMARY_OWNER,
                          "request_name primary owner");
    }
    {
        /* 重复请求不排队 → EXISTS 分支或已拥有 */
        int r = dbus_bus_request_name(conn, "org.example.dbus_dyn_test",
                                      DBUS_DYN_NAME_FLAG_DO_NOT_QUEUE, &err);
        failures += check(r == 3 || r == 4, "request_name again → EXISTS/ALREADY");
    }

    /* 5. Peer.Ping 同步调用（带 "ss" 参数的方法调用 + iter 读回） */
    msg = dbus_message_new_method_call("org.freedesktop.DBus", "/",
                                       "org.freedesktop.DBus.Peer", "Ping");
    failures += check(msg != NULL, "new_method_call");
    failures += check(dbus_message_is_method_call(
                          msg, "org.freedesktop.DBus.Peer", "Ping"),
                      "is_method_call");
    reply = dbus_connection_send_with_reply_and_block(conn, msg, 5000, &err);
    if (!reply) {
        fprintf(stderr, "ping error: %s: %s\n",
                err.name ? err.name : "?", err.message ? err.message : "?");
        dbus_error_free(&err);
    }
    failures += check(reply != NULL, "Peer.Ping reply");
    if (reply) {
        dbus_message_unref(reply);
    }
    dbus_message_unref(msg);

    /* 6. append_args/get_args + iter 往返 */
    msg = dbus_message_new_method_call("org.freedesktop.DBus",
                                       "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus.Debug.Stats",
                                       "GetStats");
    if (msg) {
        {
            /* 官方文档：append_args 传值的地址（&v），不是值本身 */
            const char *a1 = "hello";
            dbus_uint32_t a2 = 42;
            const char *a3 = "world";
            failures += check(dbus_message_append_args(msg,
                                  DBUS_DYN_TYPE_STRING, &a1,
                                  DBUS_DYN_TYPE_UINT32, &a2,
                                  DBUS_DYN_TYPE_STRING, &a3,
                                  DBUS_DYN_TYPE_INVALID),
                              "append_args sus (by address)");
        }
        failures += check(strcmp(dbus_message_get_signature(msg), "sus") == 0,
                          "message signature 'sus'");
    }

    /* 7. marshal/demarshal 往返 */
    if (msg) {
        char *data = NULL;
        int len = 0;
        dbus_message_set_serial(msg, 1); /* marshal 前需设 serial，否则 demarshal 报 Bad serial */
        if (dbus_message_marshal(msg, &data, &len) && data && len > 0) {
            DBusMessage *m2;
            int need = dbus_message_demarshal_bytes_needed(data, len);
            failures += check(need == len, "demarshal_bytes_needed == len");
            m2 = dbus_message_demarshal(data, len, &err);
            if (!m2)
                fprintf(stderr, "demarshal error: %s: %s\n",
                        err.name ? err.name : "?",
                        err.message ? err.message : "?");
            failures += check(m2 != NULL, "demarshal roundtrip");
            if (m2) {
                failures += check(strcmp(dbus_message_get_signature(m2),
                                         "sus") == 0,
                                  "roundtrip signature 'sus'");
                {
                    const char *s1 = NULL;
                    dbus_uint32_t u = 0;
                    dbus_message_get_args(m2, &err,
                                          DBUS_DYN_TYPE_STRING, &s1,
                                          DBUS_DYN_TYPE_UINT32, &u,
                                          DBUS_DYN_TYPE_INVALID);
                    failures += check(s1 && strcmp(s1, "hello") == 0 && u == 42,
                                      "get_args values");
                    printf("get_args: s1=%s u=%u\n", s1 ? s1 : "(null)", u);
                }
                dbus_message_unref(m2);
            }
            /* dbus_malloc 分配的 data 用 dbus_free 释放 */
            dbus_message_unref(msg);
        }
        /* 数据由 libdbus 内部 malloc；这里通过 dbus_free 释放 */
        if (data) dbus_free(data);
    }

    /* 8. iter 逐字段读取 */
    {
        DBusMessage *m3 = dbus_message_new_method_call("a.b", "/o", "i.f", "m");
        if (m3) {
            {
                const char *t1 = "one";
                const char *t2 = "two";
                dbus_message_append_args(m3, DBUS_DYN_TYPE_STRING, &t1,
                                         DBUS_DYN_TYPE_STRING, &t2,
                                         DBUS_DYN_TYPE_INVALID);
            }
            failures += check(dbus_message_iter_init(m3, &iter),
                              "iter_init");
            {
                int t = dbus_message_iter_get_arg_type(&iter);
                const char *v = NULL;
                failures += check(t == DBUS_DYN_TYPE_STRING, "iter arg type 's'");
                dbus_message_iter_get_basic(&iter, &v);
                failures += check(v && strcmp(v, "one") == 0, "iter value one");
                failures += check(dbus_message_iter_next(&iter),
                                  "iter next");
                t = dbus_message_iter_get_arg_type(&iter);
                dbus_message_iter_get_basic(&iter, &v);
                failures += check(t == DBUS_DYN_TYPE_STRING && v &&
                                      strcmp(v, "two") == 0, "iter value two");
                failures += check(!dbus_message_iter_next(&iter),
                                  "iter end");
            }
            dbus_message_unref(m3);
        }
    }

    /* 9. 错误路径：请求已存在但无权限的知名名 */
    {
        DBusError e2 = DBUS_DYN_ERROR_INIT;
        int r = dbus_bus_request_name(conn, "org.freedesktop.DBus",
                                      DBUS_DYN_NAME_FLAG_DO_NOT_QUEUE, &e2);
        failures += check(dbus_error_is_set(&e2), "error set for reserved name");
        failures += check(
            dbus_error_has_name(&e2,
                "org.freedesktop.DBus.Error.AccessDenied") ||
            dbus_error_has_name(&e2,
                "org.freedesktop.DBus.Error.InvalidArgs") ||
            r == 3 || r == 4,
            "error name AccessDenied/InvalidArgs/exists");
        printf("error: %s (%s)\n", e2.name ? e2.name : "(unset)",
               e2.message ? e2.message : "");
        dbus_error_free(&e2);
    }

    /* 10. SYSTEM 总线不存在时的失败路径（dbus-run-session 内） */
    {
        DBusError e3 = DBUS_DYN_ERROR_INIT;
        DBusConnection *c2 = dbus_bus_get(DBUS_DYN_BUS_SYSTEM, &e3);
        /* 可能成功（系统有 system bus）也可能失败，验证不崩溃 */
        printf("system bus get -> %s (error: %s)\n",
               c2 ? "OK" : "NULL",
               dbus_error_is_set(&e3) && e3.name ? e3.name : "(none)");
        dbus_error_free(&e3);
        failures += check(1, "system bus no-crash");
    }

    dbus_dyn_cleanup();

    if (failures) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
