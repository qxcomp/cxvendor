/* rtdb/tests/test_mosquitto.c - libmosquitto 客户端 API 动态封装定向测试 */
#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---------- 类型（不引入目标库头文件） ---------- */
enum mosq_err_t {
    MOSQ_ERR_SUCCESS = 0,
    MOSQ_ERR_NOMEM = 1,
    MOSQ_ERR_PROTOCOL = 2,
    MOSQ_ERR_INVAL = 3,
    MOSQ_ERR_NO_CONN = 4,
    MOSQ_ERR_UNKNOWN = 13
};
enum mosq_opt_t { MOSQ_OPT_PROTOCOL_VERSION = 1 };
#define MQTT_PROTOCOL_V5 5
#define MQTT_PROP_PAYLOAD_FORMAT_INDICATOR 1
#define MQTT_PROP_CONTENT_TYPE 3
#define MQTT_CMD_CONNECT 0x10

struct mosquitto;
typedef struct mqtt5__property mosquitto_property;
struct mosquitto_message {
    int mid;
    char *topic;
    void *payload;
    int payloadlen;
    int qos;
    bool retain;
};

/* ---------- 动态层接口 ---------- */
extern int mosquitto_dyn_init(const char *);
extern int mosquitto_libok(void);
extern void mosquitto_dyn_cleanup(void);
extern int mosquitto_dyn_has_symbol(const char *);

/* ---------- 本测试直接调用的原生声明 ---------- */
extern int mosquitto_lib_init(void);
extern int mosquitto_lib_cleanup(void);
extern int mosquitto_lib_version(int *, int *, int *);
extern struct mosquitto *mosquitto_new(const char *, bool, void *);
extern void mosquitto_destroy(struct mosquitto *);
extern const char *mosquitto_strerror(int);
extern const char *mosquitto_connack_string(int);
extern const char *mosquitto_reason_string(int);
extern int mosquitto_topic_matches_sub(const char *, const char *, bool *);
extern int mosquitto_topic_matches_sub2(const char *, size_t, const char *, size_t, bool *);
extern int mosquitto_pub_topic_check(const char *);
extern int mosquitto_sub_topic_check(const char *);
extern int mosquitto_validate_utf8(const char *, int);
extern int mosquitto_sub_topic_tokenise(const char *, char ***, int *);
extern int mosquitto_sub_topic_tokens_free(char ***, int);
extern int mosquitto_string_to_command(const char *, int *);
extern int mosquitto_property_add_byte(mosquitto_property **, int, uint8_t);
extern int mosquitto_property_add_string(mosquitto_property **, int, const char *);
extern int mosquitto_property_identifier(const mosquitto_property *);
extern const mosquitto_property *mosquitto_property_next(const mosquitto_property *);
extern const mosquitto_property *mosquitto_property_read_byte(const mosquitto_property *, int, uint8_t *, bool);
extern const mosquitto_property *mosquitto_property_read_string(const mosquitto_property *, int, char **, bool);
extern void mosquitto_property_free_all(mosquitto_property **);
extern int mosquitto_property_copy_all(mosquitto_property **, const mosquitto_property *);
extern int mosquitto_message_copy(struct mosquitto_message *, const struct mosquitto_message *);
extern void mosquitto_message_free_contents(struct mosquitto_message *);
extern void mosquitto_message_free(struct mosquitto_message **);
extern int mosquitto_int_option(struct mosquitto *, enum mosq_opt_t, int);
extern int mosquitto_username_pw_set(struct mosquitto *, const char *, const char *);
extern int mosquitto_will_set(struct mosquitto *, const char *, int, const void *, int, bool);
extern int mosquitto_loop(struct mosquitto *, int, int);
extern void mosquitto_user_data_set(struct mosquitto *, void *);
extern void *mosquitto_userdata(struct mosquitto *);

int main(int argc, char **argv) {
    int errs = 0;
    unsigned int i, found = 0;
    const char *names[] = {
        "mosquitto_connack_string", "mosquitto_connect", "mosquitto_connect_async",
        "mosquitto_connect_bind", "mosquitto_connect_bind_async", "mosquitto_connect_bind_v5",
        "mosquitto_connect_callback_set", "mosquitto_connect_srv", "mosquitto_connect_v5_callback_set",
        "mosquitto_connect_with_flags_callback_set", "mosquitto_destroy", "mosquitto_disconnect",
        "mosquitto_disconnect_callback_set", "mosquitto_disconnect_v5",
        "mosquitto_disconnect_v5_callback_set", "mosquitto_int_option", "mosquitto_lib_cleanup",
        "mosquitto_lib_init", "mosquitto_lib_version", "mosquitto_log_callback_set",
        "mosquitto_loop", "mosquitto_loop_forever", "mosquitto_loop_misc", "mosquitto_loop_read",
        "mosquitto_loop_start", "mosquitto_loop_stop", "mosquitto_loop_write",
        "mosquitto_max_inflight_messages_set", "mosquitto_message_callback_set",
        "mosquitto_message_copy", "mosquitto_message_free", "mosquitto_message_free_contents",
        "mosquitto_message_retry_set", "mosquitto_message_v5_callback_set", "mosquitto_new",
        "mosquitto_opts_set", "mosquitto_property_add_binary", "mosquitto_property_add_byte",
        "mosquitto_property_add_int16", "mosquitto_property_add_int32",
        "mosquitto_property_add_string", "mosquitto_property_add_string_pair",
        "mosquitto_property_add_varint", "mosquitto_property_check_all",
        "mosquitto_property_check_command", "mosquitto_property_copy_all",
        "mosquitto_property_free_all", "mosquitto_property_identifier",
        "mosquitto_property_identifier_to_string", "mosquitto_property_next",
        "mosquitto_property_read_binary", "mosquitto_property_read_byte",
        "mosquitto_property_read_int16", "mosquitto_property_read_int32",
        "mosquitto_property_read_string", "mosquitto_property_read_string_pair",
        "mosquitto_property_read_varint", "mosquitto_publish", "mosquitto_publish_callback_set",
        "mosquitto_publish_v5", "mosquitto_publish_v5_callback_set", "mosquitto_pub_topic_check",
        "mosquitto_pub_topic_check2", "mosquitto_reason_string", "mosquitto_reconnect",
        "mosquitto_reconnect_async", "mosquitto_reconnect_delay_set", "mosquitto_reinitialise",
        "mosquitto_socket", "mosquitto_socks5_set", "mosquitto_ssl_get", "mosquitto_strerror",
        "mosquitto_string_option", "mosquitto_string_to_command", "mosquitto_string_to_property_info",
        "mosquitto_subscribe", "mosquitto_subscribe_callback", "mosquitto_subscribe_callback_set",
        "mosquitto_subscribe_multiple", "mosquitto_subscribe_simple", "mosquitto_subscribe_v5",
        "mosquitto_subscribe_v5_callback_set", "mosquitto_sub_topic_check",
        "mosquitto_sub_topic_check2", "mosquitto_sub_topic_tokenise",
        "mosquitto_sub_topic_tokens_free", "mosquitto_threaded_set",
        "mosquitto_tls_insecure_set", "mosquitto_tls_opts_set", "mosquitto_tls_psk_set",
        "mosquitto_tls_set", "mosquitto_topic_matches_sub", "mosquitto_topic_matches_sub2",
        "mosquitto_unsubscribe", "mosquitto_unsubscribe_callback_set",
        "mosquitto_unsubscribe_multiple", "mosquitto_unsubscribe_v5",
        "mosquitto_unsubscribe_v5_callback_set", "mosquitto_userdata",
        "mosquitto_user_data_set", "mosquitto_username_pw_set", "mosquitto_validate_utf8",
        "mosquitto_void_option", "mosquitto_want_write", "mosquitto_will_clear",
        "mosquitto_will_set", "mosquitto_will_set_v5"
    };

    if (mosquitto_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("mosquitto not installed, skipping mosquitto_dyn test\n");

    errs += CHECK(mosquitto_libok());
    errs += CHECK(!mosquitto_dyn_has_symbol("mosquitto_no_such_symbol_xyz"));
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (mosquitto_dyn_has_symbol(names[i])) found++;
    }
    printf("info: mosquitto public exports found = %u/%u\n",
           found, (unsigned int)(sizeof names / sizeof names[0]));
    errs += CHECK(found == sizeof names / sizeof names[0]);

    errs += CHECK(MOSQ_ERR_SUCCESS == 0 && MOSQ_ERR_NO_CONN == 4 && MOSQ_ERR_UNKNOWN == 13);
    errs += CHECK(MOSQ_OPT_PROTOCOL_VERSION == 1 && MQTT_PROTOCOL_V5 == 5);

    errs += CHECK(mosquitto_lib_init() == MOSQ_ERR_SUCCESS);
    {
        int major = 0, minor = 0, revision = 0;
        printf("info: mosquitto_lib_version rc=%d\n", mosquitto_lib_version(&major, &minor, &revision));
        errs += CHECK(major == 2);
        printf("info: libmosquitto %d.%d.%d\n", major, minor, revision);
    }
    errs += CHECK(mosquitto_strerror(MOSQ_ERR_INVAL) != NULL);
    errs += CHECK(mosquitto_connack_string(0) != NULL);
    errs += CHECK(mosquitto_reason_string(0) != NULL);

    {
        bool result = false;
        errs += CHECK(mosquitto_topic_matches_sub("a/#", "a/b/c", &result) == MOSQ_ERR_SUCCESS && result);
        errs += CHECK(mosquitto_topic_matches_sub("a/b", "a/c", &result) == MOSQ_ERR_SUCCESS && !result);
        errs += CHECK(mosquitto_topic_matches_sub2("a/#", 3, "a/b", 3, &result) == MOSQ_ERR_SUCCESS && result);
    }
    errs += CHECK(mosquitto_pub_topic_check("a/b") == MOSQ_ERR_SUCCESS);
    errs += CHECK(mosquitto_pub_topic_check("a/+/b") != MOSQ_ERR_SUCCESS);
    errs += CHECK(mosquitto_sub_topic_check("a/#") == MOSQ_ERR_SUCCESS);
    errs += CHECK(mosquitto_sub_topic_check("a/#/b") != MOSQ_ERR_SUCCESS);
    errs += CHECK(mosquitto_validate_utf8("hello", 5) == MOSQ_ERR_SUCCESS);

    {
        char **topics = NULL;
        int count = 0;
        errs += CHECK(mosquitto_sub_topic_tokenise("a/b/c", &topics, &count) == MOSQ_ERR_SUCCESS);
        errs += CHECK(count == 3 && topics != NULL);
        if (topics) errs += CHECK(strcmp(topics[0], "a") == 0 && strcmp(topics[2], "c") == 0);
        errs += CHECK(mosquitto_sub_topic_tokens_free(&topics, count) == MOSQ_ERR_SUCCESS);
    }
    {
        int cmd = 0;
        errs += CHECK(mosquitto_string_to_command("CONNECT", &cmd) == MOSQ_ERR_SUCCESS);
        errs += CHECK(cmd == MQTT_CMD_CONNECT);
    }

    {
        mosquitto_property *props = NULL;
        uint8_t bval = 0;
        char *sval = NULL;
        mosquitto_property *copy = NULL;
        errs += CHECK(mosquitto_property_add_byte(&props, MQTT_PROP_PAYLOAD_FORMAT_INDICATOR, 7) == MOSQ_ERR_SUCCESS);
        errs += CHECK(mosquitto_property_add_string(&props, MQTT_PROP_CONTENT_TYPE, "text/plain") == MOSQ_ERR_SUCCESS);
        errs += CHECK(props != NULL);
        if (props) {
            errs += CHECK(mosquitto_property_identifier(props) == MQTT_PROP_PAYLOAD_FORMAT_INDICATOR);
            errs += CHECK(mosquitto_property_next(props) != NULL);
        }
        errs += CHECK(mosquitto_property_read_byte(props, MQTT_PROP_PAYLOAD_FORMAT_INDICATOR, &bval, false) != NULL);
        errs += CHECK(bval == 7);
        errs += CHECK(mosquitto_property_read_string(props, MQTT_PROP_CONTENT_TYPE, &sval, false) != NULL);
        errs += CHECK(sval != NULL && strcmp(sval, "text/plain") == 0);
        errs += CHECK(mosquitto_property_copy_all(&copy, props) == MOSQ_ERR_SUCCESS);
        errs += CHECK(copy != NULL);
        mosquitto_property_free_all(&copy);
        mosquitto_property_free_all(&props);
        errs += CHECK(props == NULL && copy == NULL);
    }

    {
        struct mosquitto_message src;
        struct mosquitto_message dst;
        memset(&src, 0, sizeof src);
        memset(&dst, 0, sizeof dst);
        src.mid = 9;
        src.topic = (char *)"a/b";
        src.payload = (void *)"hi";
        src.payloadlen = 2;
        src.qos = 1;
        src.retain = false;
        errs += CHECK(mosquitto_message_copy(&dst, &src) == MOSQ_ERR_SUCCESS);
        errs += CHECK(dst.mid == 9 && dst.topic != NULL && strcmp(dst.topic, "a/b") == 0);
        errs += CHECK(dst.payloadlen == 2 && dst.payload != NULL && memcmp(dst.payload, "hi", 2) == 0);
        mosquitto_message_free_contents(&dst);
    }
    {
        struct mosquitto_message *m = (struct mosquitto_message *)calloc(1, sizeof(struct mosquitto_message));
        errs += CHECK(m != NULL);
        if (m) {
            m->topic = (char *)malloc(8);
            if (m->topic) strcpy(m->topic, "a/b");
            m->payload = malloc(2);
            if (m->payload) memcpy(m->payload, "hi", 2);
            m->payloadlen = 2;
            mosquitto_message_free(&m);
            printf("ok: mosquitto_message_free() returned\n");
        }
    }

    {
        struct mosquitto *mosq = mosquitto_new(NULL, true, NULL);
        errs += CHECK(mosq != NULL);
        if (mosq) {
            int marker = 42;
            mosquitto_user_data_set(mosq, &marker);
            errs += CHECK(mosquitto_userdata(mosq) == &marker);
            errs += CHECK(mosquitto_int_option(mosq, MOSQ_OPT_PROTOCOL_VERSION, MQTT_PROTOCOL_V5) == MOSQ_ERR_SUCCESS);
            errs += CHECK(mosquitto_username_pw_set(mosq, "user", "pass") == MOSQ_ERR_SUCCESS);
            errs += CHECK(mosquitto_will_set(mosq, "a/lwt", 4, "gone", 0, false) == MOSQ_ERR_SUCCESS);
            printf("info: mosquitto_loop(no connection) rc=%d\n", mosquitto_loop(mosq, 0, 1));
            mosquitto_destroy(mosq);
        }
    }

    errs += CHECK(mosquitto_lib_cleanup() == MOSQ_ERR_SUCCESS);
    mosquitto_dyn_cleanup();

    if (errs == 0) printf("mosquitto_dyn: all checks passed\n");
    return errs ? 1 : 0;
}
