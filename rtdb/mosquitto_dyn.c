/* ============================================================
 * mosquitto_dyn.c - dlopen 模拟链接：libmosquitto 客户端 C API
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照上游 mosquitto v2.0.20 公开头
 * include/mosquitto.h 的 107 个 libmosq_EXPORT 函数；broker/plugin
 * 专用头（mosquitto_broker.h / mosquitto_plugin.h）不纳入。
 * 类型按头文件原样内嵌，字段顺序即 ABI；导出函数名保持原生原样。
 * 链接期不需要 libmosquitto，仅需 -ldl。
 * 加载顺序：显式路径 > MOSQUITTO_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > Nix > 平台默认；成功后不卸载。
 * 加载日志照搬 opendal_dyn.c 当前 diff。
 * ============================================================ */

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---------- 平台适配层 ---------- */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE lib_handle_t;
  static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
  static void *get_sym(lib_handle_t h, const char *name) { return (void *)GetProcAddress(h, name); }
  static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *lib_error(void) { return "LoadLibrary failed"; }
  #define PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *lib_handle_t;
  static lib_handle_t load_lib(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
  static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
  static const char *lib_error(void) { return dlerror(); }
  #define PATH_SEP ':'
#endif

/* ---------- 类型（mosquitto.h 原样内嵌，字段顺序即 ABI） ---------- */
enum mosq_err_t {
    MOSQ_ERR_AUTH_CONTINUE = -4,
    MOSQ_ERR_NO_SUBSCRIBERS = -3,
    MOSQ_ERR_SUB_EXISTS = -2,
    MOSQ_ERR_CONN_PENDING = -1,
    MOSQ_ERR_SUCCESS = 0,
    MOSQ_ERR_NOMEM = 1,
    MOSQ_ERR_PROTOCOL = 2,
    MOSQ_ERR_INVAL = 3,
    MOSQ_ERR_NO_CONN = 4,
    MOSQ_ERR_CONN_REFUSED = 5,
    MOSQ_ERR_NOT_FOUND = 6,
    MOSQ_ERR_CONN_LOST = 7,
    MOSQ_ERR_TLS = 8,
    MOSQ_ERR_PAYLOAD_SIZE = 9,
    MOSQ_ERR_NOT_SUPPORTED = 10,
    MOSQ_ERR_AUTH = 11,
    MOSQ_ERR_ACL_DENIED = 12,
    MOSQ_ERR_UNKNOWN = 13,
    MOSQ_ERR_ERRNO = 14,
    MOSQ_ERR_EAI = 15,
    MOSQ_ERR_PROXY = 16,
    MOSQ_ERR_PLUGIN_DEFER = 17,
    MOSQ_ERR_MALFORMED_UTF8 = 18,
    MOSQ_ERR_KEEPALIVE = 19,
    MOSQ_ERR_LOOKUP = 20,
    MOSQ_ERR_MALFORMED_PACKET = 21,
    MOSQ_ERR_DUPLICATE_PROPERTY = 22,
    MOSQ_ERR_TLS_HANDSHAKE = 23,
    MOSQ_ERR_QOS_NOT_SUPPORTED = 24,
    MOSQ_ERR_OVERSIZE_PACKET = 25,
    MOSQ_ERR_OCSP = 26,
    MOSQ_ERR_TIMEOUT = 27,
    MOSQ_ERR_RETAIN_NOT_SUPPORTED = 28,
    MOSQ_ERR_TOPIC_ALIAS_INVALID = 29,
    MOSQ_ERR_ADMINISTRATIVE_ACTION = 30,
    MOSQ_ERR_ALREADY_EXISTS = 31
};

enum mosq_opt_t {
    MOSQ_OPT_PROTOCOL_VERSION = 1,
    MOSQ_OPT_SSL_CTX = 2,
    MOSQ_OPT_SSL_CTX_WITH_DEFAULTS = 3,
    MOSQ_OPT_RECEIVE_MAXIMUM = 4,
    MOSQ_OPT_SEND_MAXIMUM = 5,
    MOSQ_OPT_TLS_KEYFORM = 6,
    MOSQ_OPT_TLS_ENGINE = 7,
    MOSQ_OPT_TLS_ENGINE_KPASS_SHA1 = 8,
    MOSQ_OPT_TLS_OCSP_REQUIRED = 9,
    MOSQ_OPT_TLS_ALPN = 10,
    MOSQ_OPT_TCP_NODELAY = 11,
    MOSQ_OPT_BIND_ADDRESS = 12,
    MOSQ_OPT_TLS_USE_OS_CERTS = 13
};

#define MQTT_PROTOCOL_V31 3
#define MQTT_PROTOCOL_V311 4
#define MQTT_PROTOCOL_V5 5

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

struct libmosquitto_will {
    char *topic;
    void *payload;
    int payloadlen;
    int qos;
    bool retain;
};

struct libmosquitto_auth {
    char *username;
    char *password;
};

struct libmosquitto_tls {
    char *cafile;
    char *capath;
    char *certfile;
    char *keyfile;
    char *ciphers;
    char *tls_version;
    int (*pw_callback)(char *buf, int size, int rwflag, void *userdata);
    int cert_reqs;
};

/* ---------- 动态层状态 ---------- */
static lib_handle_t mosquitto_handle = NULL;
static int mosquitto_init_state = 0;

static void *mosquitto_sym(const char *name) {
    return mosquitto_handle ? get_sym(mosquitto_handle, name) : NULL;
}

static int mosquitto_do_load(const char *library_path) {
    static char bufs[64][1024];
    const char *candidates[64];
    const char *so_soname = "libmosquitto.so.1";
    const char *so_ver = "libmosquitto.so.1.6.1";
    const char *so_plain = "libmosquitto.so";
    int n = 0, i;
    int res = -1;

    if (mosquitto_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("MOSQUITTO_DYN_LIB");
        if (e && *e) {
            snprintf(bufs[n], sizeof bufs[0], "%s", e);
            candidates[n] = bufs[n]; n++;
        }
    }
    {
#ifdef _WIN32
        const char *env_path = getenv("PATH");
#elif defined(__APPLE__)
        const char *env_path = getenv("DYLD_LIBRARY_PATH");
#else
        const char *env_path = getenv("LD_LIBRARY_PATH");
#endif
        if (env_path && *env_path) {
            char *copy = strdup(env_path);
            if (copy) {
                char *q = copy;
                while (q && *q && n < 50) {
                    char *sep = strchr(q, PATH_SEP);
                    if (sep) *sep = 0;
                    if (*q && n + 3 < 64) {
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_soname);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_ver);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_plain);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = so_soname;
    candidates[n++] = so_ver;
    candidates[n++] = so_plain;
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/%s", home, so_soname);
            candidates[n] = bufs[n]; n++;
        }
        snprintf(bufs[n], sizeof bufs[0], "/nix/var/nix/profiles/default/lib/%s", so_soname);
        candidates[n] = bufs[n]; n++;
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libmosquitto.dylib";
    candidates[n++] = "/usr/local/lib/libmosquitto.dylib";
#elif defined(_WIN32)
    candidates[n++] = "mosquitto.dll";
    candidates[n++] = "libmosquitto.dll";
#else
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_soname);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/x86_64-linux-gnu/%s", so_soname);
    candidates[n] = bufs[n]; n++;
#endif

    for (i = 0; i < n; i++) {
        mosquitto_handle = load_lib(candidates[i]);
        if (mosquitto_handle) { res = i; break; }
    }
    if (!mosquitto_handle || !mosquitto_sym("mosquitto_lib_init")) {
        fprintf(stderr, "mosquitto_dyn: cannot load mosquitto (%s)\n", lib_error());
        if (mosquitto_handle) { free_lib(mosquitto_handle); mosquitto_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "mosquitto_dyn: loaded mosquitto %d %s\n", res, candidates[res]);
    return 0;
}

int mosquitto_dyn_libok(void) {
    if (mosquitto_init_state == 0)
        mosquitto_init_state = (mosquitto_do_load(NULL) == 0) ? 1 : -1;
    return (mosquitto_init_state == 1 && mosquitto_handle != NULL) ? 1 : 0;
}

int mosquitto_dyn_init(const char *library_path) {
    if (mosquitto_do_load(library_path) == 0) {
        mosquitto_init_state = 1;
        return 0;
    }
    if (mosquitto_init_state == 0) mosquitto_init_state = -1;
    return -1;
}

void mosquitto_dyn_cleanup(void) {
    if (mosquitto_handle) { free_lib(mosquitto_handle); mosquitto_handle = NULL; }
    mosquitto_init_state = 0;
}

int mosquitto_dyn_has_symbol(const char *name) {
    if (!mosquitto_dyn_libok()) return 0;
    return mosquitto_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                            \
ret name params {                                                              \
    static ret (*fn) params = NULL;                                            \
    if (!fn) {                                                                 \
        if (!mosquitto_dyn_libok()) return fallback;                           \
        *(void **)(&fn) = mosquitto_sym(#name);                                \
        if (!fn) {                                                             \
            fprintf(stderr, "mosquitto_dyn: dlsym failed: %s\n", #name);       \
            return fallback;                                                   \
        }                                                                      \
    }                                                                          \
    return fn args;                                                            \
}
#define WRAP_VOID(name, params, args)                                          \
void name params {                                                             \
    static void (*fn) params = NULL;                                           \
    if (!fn) {                                                                 \
        if (!mosquitto_dyn_libok()) return;                                    \
        *(void **)(&fn) = mosquitto_sym(#name);                                \
        if (!fn) {                                                             \
            fprintf(stderr, "mosquitto_dyn: dlsym failed: %s\n", #name);       \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn args;                                                                   \
}

/* ---------- 107 个 libmosq_EXPORT 公开函数（函数名原样） ---------- */
WRAP_RET(int, mosquitto_lib_version, (int *major, int *minor, int *revision), (major, minor, revision), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_lib_init, (void), (), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_lib_cleanup, (void), (), MOSQ_ERR_UNKNOWN)
WRAP_RET(struct mosquitto *, mosquitto_new, (const char *id, bool clean_session, void *obj), (id, clean_session, obj), NULL)
WRAP_VOID(mosquitto_destroy, (struct mosquitto *mosq), (mosq))
WRAP_RET(int, mosquitto_reinitialise, (struct mosquitto *mosq, const char *id, bool clean_session, void *obj), (mosq, id, clean_session, obj), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_will_set, (struct mosquitto *mosq, const char *topic, int payloadlen, const void *payload, int qos, bool retain), (mosq, topic, payloadlen, payload, qos, retain), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_will_set_v5, (struct mosquitto *mosq, const char *topic, int payloadlen, const void *payload, int qos, bool retain, mosquitto_property *properties), (mosq, topic, payloadlen, payload, qos, retain, properties), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_will_clear, (struct mosquitto *mosq), (mosq), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_username_pw_set, (struct mosquitto *mosq, const char *username, const char *password), (mosq, username, password), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_connect, (struct mosquitto *mosq, const char *host, int port, int keepalive), (mosq, host, port, keepalive), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_connect_bind, (struct mosquitto *mosq, const char *host, int port, int keepalive, const char *bind_address), (mosq, host, port, keepalive, bind_address), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_connect_bind_v5, (struct mosquitto *mosq, const char *host, int port, int keepalive, const char *bind_address, const mosquitto_property *properties), (mosq, host, port, keepalive, bind_address, properties), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_connect_async, (struct mosquitto *mosq, const char *host, int port, int keepalive), (mosq, host, port, keepalive), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_connect_bind_async, (struct mosquitto *mosq, const char *host, int port, int keepalive, const char *bind_address), (mosq, host, port, keepalive, bind_address), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_connect_srv, (struct mosquitto *mosq, const char *host, int keepalive, const char *bind_address), (mosq, host, keepalive, bind_address), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_reconnect, (struct mosquitto *mosq), (mosq), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_reconnect_async, (struct mosquitto *mosq), (mosq), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_disconnect, (struct mosquitto *mosq), (mosq), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_disconnect_v5, (struct mosquitto *mosq, int reason_code, const mosquitto_property *properties), (mosq, reason_code, properties), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_publish, (struct mosquitto *mosq, int *mid, const char *topic, int payloadlen, const void *payload, int qos, bool retain), (mosq, mid, topic, payloadlen, payload, qos, retain), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_publish_v5, (struct mosquitto *mosq, int *mid, const char *topic, int payloadlen, const void *payload, int qos, bool retain, const mosquitto_property *properties), (mosq, mid, topic, payloadlen, payload, qos, retain, properties), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_subscribe, (struct mosquitto *mosq, int *mid, const char *sub, int qos), (mosq, mid, sub, qos), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_subscribe_v5, (struct mosquitto *mosq, int *mid, const char *sub, int qos, int options, const mosquitto_property *properties), (mosq, mid, sub, qos, options, properties), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_subscribe_multiple, (struct mosquitto *mosq, int *mid, int sub_count, char *const *const sub, int qos, int options, const mosquitto_property *properties), (mosq, mid, sub_count, sub, qos, options, properties), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_unsubscribe, (struct mosquitto *mosq, int *mid, const char *sub), (mosq, mid, sub), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_unsubscribe_v5, (struct mosquitto *mosq, int *mid, const char *sub, const mosquitto_property *properties), (mosq, mid, sub, properties), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_unsubscribe_multiple, (struct mosquitto *mosq, int *mid, int sub_count, char *const *const sub, const mosquitto_property *properties), (mosq, mid, sub_count, sub, properties), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_message_copy, (struct mosquitto_message *dst, const struct mosquitto_message *src), (dst, src), MOSQ_ERR_UNKNOWN)
WRAP_VOID(mosquitto_message_free, (struct mosquitto_message **message), (message))
WRAP_VOID(mosquitto_message_free_contents, (struct mosquitto_message *message), (message))
WRAP_RET(int, mosquitto_loop_forever, (struct mosquitto *mosq, int timeout, int max_packets), (mosq, timeout, max_packets), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_loop_start, (struct mosquitto *mosq), (mosq), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_loop_stop, (struct mosquitto *mosq, bool force), (mosq, force), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_loop, (struct mosquitto *mosq, int timeout, int max_packets), (mosq, timeout, max_packets), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_loop_read, (struct mosquitto *mosq, int max_packets), (mosq, max_packets), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_loop_write, (struct mosquitto *mosq, int max_packets), (mosq, max_packets), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_loop_misc, (struct mosquitto *mosq), (mosq), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_socket, (struct mosquitto *mosq), (mosq), MOSQ_ERR_UNKNOWN)
WRAP_RET(bool, mosquitto_want_write, (struct mosquitto *mosq), (mosq), false)
WRAP_RET(int, mosquitto_threaded_set, (struct mosquitto *mosq, bool threaded), (mosq, threaded), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_opts_set, (struct mosquitto *mosq, enum mosq_opt_t option, void *value), (mosq, option, value), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_int_option, (struct mosquitto *mosq, enum mosq_opt_t option, int value), (mosq, option, value), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_string_option, (struct mosquitto *mosq, enum mosq_opt_t option, const char *value), (mosq, option, value), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_void_option, (struct mosquitto *mosq, enum mosq_opt_t option, void *value), (mosq, option, value), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_reconnect_delay_set, (struct mosquitto *mosq, unsigned int reconnect_delay, unsigned int reconnect_delay_max, bool reconnect_exponential_backoff), (mosq, reconnect_delay, reconnect_delay_max, reconnect_exponential_backoff), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_max_inflight_messages_set, (struct mosquitto *mosq, unsigned int max_inflight_messages), (mosq, max_inflight_messages), MOSQ_ERR_UNKNOWN)
WRAP_VOID(mosquitto_message_retry_set, (struct mosquitto *mosq, unsigned int message_retry), (mosq, message_retry))
WRAP_VOID(mosquitto_user_data_set, (struct mosquitto *mosq, void *obj), (mosq, obj))
WRAP_RET(void *, mosquitto_userdata, (struct mosquitto *mosq), (mosq), NULL)
WRAP_RET(int, mosquitto_tls_set, (struct mosquitto *mosq, const char *cafile, const char *capath, const char *certfile, const char *keyfile, int (*pw_callback)(char *buf, int size, int rwflag, void *userdata)), (mosq, cafile, capath, certfile, keyfile, pw_callback), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_tls_insecure_set, (struct mosquitto *mosq, bool value), (mosq, value), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_tls_opts_set, (struct mosquitto *mosq, int cert_reqs, const char *tls_version, const char *ciphers), (mosq, cert_reqs, tls_version, ciphers), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_tls_psk_set, (struct mosquitto *mosq, const char *psk, const char *identity, const char *ciphers), (mosq, psk, identity, ciphers), MOSQ_ERR_UNKNOWN)
WRAP_RET(void *, mosquitto_ssl_get, (struct mosquitto *mosq), (mosq), NULL)
WRAP_VOID(mosquitto_connect_callback_set, (struct mosquitto *mosq, void (*on_connect)(struct mosquitto *, void *, int)), (mosq, on_connect))
WRAP_VOID(mosquitto_connect_with_flags_callback_set, (struct mosquitto *mosq, void (*on_connect)(struct mosquitto *, void *, int, int)), (mosq, on_connect))
WRAP_VOID(mosquitto_connect_v5_callback_set, (struct mosquitto *mosq, void (*on_connect)(struct mosquitto *, void *, int, int, const mosquitto_property *props)), (mosq, on_connect))
WRAP_VOID(mosquitto_disconnect_callback_set, (struct mosquitto *mosq, void (*on_disconnect)(struct mosquitto *, void *, int)), (mosq, on_disconnect))
WRAP_VOID(mosquitto_disconnect_v5_callback_set, (struct mosquitto *mosq, void (*on_disconnect)(struct mosquitto *, void *, int, const mosquitto_property *props)), (mosq, on_disconnect))
WRAP_VOID(mosquitto_publish_callback_set, (struct mosquitto *mosq, void (*on_publish)(struct mosquitto *, void *, int)), (mosq, on_publish))
WRAP_VOID(mosquitto_publish_v5_callback_set, (struct mosquitto *mosq, void (*on_publish)(struct mosquitto *, void *, int, int, const mosquitto_property *props)), (mosq, on_publish))
WRAP_VOID(mosquitto_message_callback_set, (struct mosquitto *mosq, void (*on_message)(struct mosquitto *, void *, const struct mosquitto_message *)), (mosq, on_message))
WRAP_VOID(mosquitto_message_v5_callback_set, (struct mosquitto *mosq, void (*on_message)(struct mosquitto *, void *, const struct mosquitto_message *, const mosquitto_property *props)), (mosq, on_message))
WRAP_VOID(mosquitto_subscribe_callback_set, (struct mosquitto *mosq, void (*on_subscribe)(struct mosquitto *, void *, int, int, const int *)), (mosq, on_subscribe))
WRAP_VOID(mosquitto_subscribe_v5_callback_set, (struct mosquitto *mosq, void (*on_subscribe)(struct mosquitto *, void *, int, int, const int *, const mosquitto_property *props)), (mosq, on_subscribe))
WRAP_VOID(mosquitto_unsubscribe_callback_set, (struct mosquitto *mosq, void (*on_unsubscribe)(struct mosquitto *, void *, int)), (mosq, on_unsubscribe))
WRAP_VOID(mosquitto_unsubscribe_v5_callback_set, (struct mosquitto *mosq, void (*on_unsubscribe)(struct mosquitto *, void *, int, const mosquitto_property *props)), (mosq, on_unsubscribe))
WRAP_VOID(mosquitto_log_callback_set, (struct mosquitto *mosq, void (*on_log)(struct mosquitto *, void *, int, const char *)), (mosq, on_log))
WRAP_RET(int, mosquitto_socks5_set, (struct mosquitto *mosq, const char *host, int port, const char *username, const char *password), (mosq, host, port, username, password), MOSQ_ERR_UNKNOWN)
WRAP_RET(const char *, mosquitto_strerror, (int mosq_errno), (mosq_errno), NULL)
WRAP_RET(const char *, mosquitto_connack_string, (int connack_code), (connack_code), NULL)
WRAP_RET(const char *, mosquitto_reason_string, (int reason_code), (reason_code), NULL)
WRAP_RET(int, mosquitto_string_to_command, (const char *str, int *cmd), (str, cmd), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_sub_topic_tokenise, (const char *subtopic, char ***topics, int *count), (subtopic, topics, count), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_sub_topic_tokens_free, (char ***topics, int count), (topics, count), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_topic_matches_sub, (const char *sub, const char *topic, bool *result), (sub, topic, result), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_topic_matches_sub2, (const char *sub, size_t sublen, const char *topic, size_t topiclen, bool *result), (sub, sublen, topic, topiclen, result), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_pub_topic_check, (const char *topic), (topic), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_pub_topic_check2, (const char *topic, size_t topiclen), (topic, topiclen), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_sub_topic_check, (const char *topic), (topic), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_sub_topic_check2, (const char *topic, size_t topiclen), (topic, topiclen), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_validate_utf8, (const char *str, int len), (str, len), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_subscribe_simple, (struct mosquitto_message **messages, int msg_count, bool want_retained, const char *topic, int qos, const char *host, int port, const char *client_id, int keepalive, bool clean_session, const char *username, const char *password, const struct libmosquitto_will *will, const struct libmosquitto_tls *tls), (messages, msg_count, want_retained, topic, qos, host, port, client_id, keepalive, clean_session, username, password, will, tls), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_subscribe_callback, (int (*callback)(struct mosquitto *, void *, const struct mosquitto_message *), void *userdata, const char *topic, int qos, const char *host, int port, const char *client_id, int keepalive, bool clean_session, const char *username, const char *password, const struct libmosquitto_will *will, const struct libmosquitto_tls *tls), (callback, userdata, topic, qos, host, port, client_id, keepalive, clean_session, username, password, will, tls), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_property_add_byte, (mosquitto_property **proplist, int identifier, uint8_t value), (proplist, identifier, value), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_property_add_int16, (mosquitto_property **proplist, int identifier, uint16_t value), (proplist, identifier, value), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_property_add_int32, (mosquitto_property **proplist, int identifier, uint32_t value), (proplist, identifier, value), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_property_add_varint, (mosquitto_property **proplist, int identifier, uint32_t value), (proplist, identifier, value), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_property_add_binary, (mosquitto_property **proplist, int identifier, const void *value, uint16_t len), (proplist, identifier, value, len), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_property_add_string, (mosquitto_property **proplist, int identifier, const char *value), (proplist, identifier, value), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_property_add_string_pair, (mosquitto_property **proplist, int identifier, const char *name, const char *value), (proplist, identifier, name, value), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_property_identifier, (const mosquitto_property *property), (property), MOSQ_ERR_UNKNOWN)
WRAP_RET(const mosquitto_property *, mosquitto_property_next, (const mosquitto_property *proplist), (proplist), NULL)
WRAP_RET(const mosquitto_property *, mosquitto_property_read_byte, (const mosquitto_property *proplist, int identifier, uint8_t *value, bool skip_first), (proplist, identifier, value, skip_first), NULL)
WRAP_RET(const mosquitto_property *, mosquitto_property_read_int16, (const mosquitto_property *proplist, int identifier, uint16_t *value, bool skip_first), (proplist, identifier, value, skip_first), NULL)
WRAP_RET(const mosquitto_property *, mosquitto_property_read_int32, (const mosquitto_property *proplist, int identifier, uint32_t *value, bool skip_first), (proplist, identifier, value, skip_first), NULL)
WRAP_RET(const mosquitto_property *, mosquitto_property_read_varint, (const mosquitto_property *proplist, int identifier, uint32_t *value, bool skip_first), (proplist, identifier, value, skip_first), NULL)
WRAP_RET(const mosquitto_property *, mosquitto_property_read_binary, (const mosquitto_property *proplist, int identifier, void **value, uint16_t *len, bool skip_first), (proplist, identifier, value, len, skip_first), NULL)
WRAP_RET(const mosquitto_property *, mosquitto_property_read_string, (const mosquitto_property *proplist, int identifier, char **value, bool skip_first), (proplist, identifier, value, skip_first), NULL)
WRAP_RET(const mosquitto_property *, mosquitto_property_read_string_pair, (const mosquitto_property *proplist, int identifier, char **name, char **value, bool skip_first), (proplist, identifier, name, value, skip_first), NULL)
WRAP_VOID(mosquitto_property_free_all, (mosquitto_property **properties), (properties))
WRAP_RET(int, mosquitto_property_copy_all, (mosquitto_property **dest, const mosquitto_property *src), (dest, src), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_property_check_command, (int command, int identifier), (command, identifier), MOSQ_ERR_UNKNOWN)
WRAP_RET(int, mosquitto_property_check_all, (int command, const mosquitto_property *properties), (command, properties), MOSQ_ERR_UNKNOWN)
WRAP_RET(const char *, mosquitto_property_identifier_to_string, (int identifier), (identifier), NULL)
WRAP_RET(int, mosquitto_string_to_property_info, (const char *propname, int *identifier, int *type), (propname, identifier, type), MOSQ_ERR_UNKNOWN)
