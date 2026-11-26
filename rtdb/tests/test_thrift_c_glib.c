/* thrift_c_glib_dyn 测试：只链 -ldl，不链 libthrift_c_glib / glib
 * 覆盖：动态加载、控制函数、符号探测，以及纯函数（hash/equal）、
 *       get_type() 与 error_quark() 的真实调用。
 * 需要真实 GObject 实例的 I/O 函数只做符号探测，避免空指针崩溃。
 * 库缺失时按 TEST_SKIP 正常退出。
 */
#include "test_common.h"
#include <stdio.h>
#include <string.h>

typedef unsigned long GType;
typedef unsigned int  GQuark;
typedef unsigned int  guint;
typedef int           gboolean;

extern int thrift_c_glib_dyn_init(const char *);
extern int thrift_c_glib_libok(void);
extern void thrift_c_glib_dyn_cleanup(void);
extern int thrift_c_glib_dyn_has_symbol(const char *);

extern guint    thrift_boolean_hash(const void *);
extern guint    thrift_int8_hash(const void *);
extern guint    thrift_int16_hash(const void *);
extern gboolean thrift_boolean_equal(const void *, const void *);
extern gboolean thrift_int8_equal(const void *, const void *);
extern gboolean thrift_int16_equal(const void *, const void *);
extern GType    thrift_transport_get_type(void);
extern GType    thrift_protocol_get_type(void);
extern GType    thrift_binary_protocol_get_type(void);
extern GType    thrift_server_get_type(void);
extern GType    thrift_zlib_transport_get_type(void);   /* 本机未导出，走 fallback */
extern GQuark   thrift_transport_error_quark(void);
extern GQuark   thrift_protocol_error_quark(void);

int main(void) {
    int errs = 0;
    gboolean b1 = 1, b2 = 1, b3 = 0;
    signed char i8a = 7, i8b = 7;
    short i16a = 9, i16b = 9;

    if (thrift_c_glib_dyn_init(NULL) != 0)
        TEST_SKIP("libthrift_c_glib not installed, skipping thrift_c_glib_dyn test\n");

    errs += CHECK(thrift_c_glib_libok());
    errs += CHECK(thrift_c_glib_dyn_has_symbol("thrift_transport_get_type"));
    errs += CHECK(thrift_c_glib_dyn_has_symbol("thrift_protocol_write_i32"));
    errs += CHECK(thrift_c_glib_dyn_has_symbol("thrift_protocol_read_i32"));
    errs += CHECK(thrift_c_glib_dyn_has_symbol("thrift_boolean_hash"));
    errs += CHECK(!thrift_c_glib_dyn_has_symbol("thrift_no_such_symbol_xyz"));

    /* 本机库导出计数（133 声明中 6 个 zlib/multiplexed 符号未导出） */
    {
        const char *names[] = {
            "thrift_application_exception_error_quark", "thrift_application_exception_get_type",
            "thrift_binary_protocol_factory_get_type", "thrift_binary_protocol_get_min_serialized_size",
            "thrift_binary_protocol_get_type", "thrift_boolean_equal", "thrift_boolean_hash",
            "thrift_buffered_transport_factory_get_transport", "thrift_buffered_transport_factory_get_type",
            "thrift_buffered_transport_get_type", "thrift_compact_protocol_factory_get_type",
            "thrift_compact_protocol_get_type", "thrift_configuration_get_type",
            "thrift_dispatch_processor_get_type", "thrift_dispatch_processor_process",
            "thrift_fd_transport_get_type", "thrift_framed_transport_factory_get_transport",
            "thrift_framed_transport_factory_get_type", "thrift_framed_transport_get_type",
            "thrift_hash_table_get_keys", "thrift_int16_equal", "thrift_int16_hash",
            "thrift_int8_equal", "thrift_int8_hash", "thrift_memory_buffer_get_type",
            "thrift_multiplexed_processor_error_quark", "thrift_multiplexed_processor_get_type",
            "thrift_multiplexed_protocol_get_type", "thrift_processor_get_type",
            "thrift_processor_process", "thrift_protocol_decorator_get_type",
            "thrift_protocol_decorator_write_message_begin", "thrift_protocol_error_quark",
            "thrift_protocol_factory_get_protocol", "thrift_protocol_factory_get_type",
            "thrift_protocol_get_min_serialized_size", "thrift_protocol_get_type",
            "thrift_protocol_skip", "thrift_safe_hash_table_destroy", "thrift_server_get_type",
            "thrift_server_serve", "thrift_server_socket_error_quark",
            "thrift_server_socket_get_type", "thrift_server_stop",
            "thrift_server_transport_accept", "thrift_server_transport_close",
            "thrift_server_transport_get_type", "thrift_server_transport_listen",
            "thrift_simple_server_get_type", "thrift_socket_close", "thrift_socket_get_type",
            "thrift_socket_is_open", "thrift_socket_open", "thrift_ssl_load_cert_from_buffer",
            "thrift_ssl_load_cert_from_file", "thrift_ssl_socket_authorize",
            "thrift_ssl_socket_close", "thrift_ssl_socket_context_initialize",
            "thrift_ssl_socket_finalize_openssl", "thrift_ssl_socket_get_type",
            "thrift_ssl_socket_initialize_openssl", "thrift_ssl_socket_is_open",
            "thrift_ssl_socket_new", "thrift_ssl_socket_new_with_host", "thrift_ssl_socket_open",
            "thrift_ssl_socket_set_manager", "thrift_stored_message_protocol_get_type",
            "thrift_string_free", "thrift_struct_get_type", "thrift_struct_read",
            "thrift_struct_write", "thrift_transport_close", "thrift_transport_error_quark",
            "thrift_transport_factory_get_transport", "thrift_transport_factory_get_type",
            "thrift_transport_flush", "thrift_transport_get_type", "thrift_transport_is_open",
            "thrift_transport_open", "thrift_transport_peek", "thrift_transport_read",
            "thrift_transport_read_all", "thrift_transport_read_end", "thrift_transport_write",
            "thrift_transport_write_end"
        };
        unsigned int i, found = 0;
        for (i = 0; i < sizeof names / sizeof names[0]; i++)
            if (thrift_c_glib_dyn_has_symbol(names[i])) found++;
        printf("info: thrift_c_glib known exports found = %u/%u\n",
               found, (unsigned int)(sizeof names / sizeof names[0]));
        errs += CHECK(found == sizeof names / sizeof names[0]);
    }

    /* 纯函数：hash / equal（不依赖 GObject 实例） */
    errs += CHECK(thrift_boolean_hash(&b1) != 0);
    errs += CHECK(thrift_int8_hash(&i8a) != 0);
    errs += CHECK(thrift_int16_hash(&i16a) != 0);
    errs += CHECK(thrift_boolean_equal(&b1, &b2) != 0);
    errs += CHECK(thrift_boolean_equal(&b1, &b3) == 0);
    errs += CHECK(thrift_int8_equal(&i8a, &i8b) != 0);
    errs += CHECK(thrift_int16_equal(&i16a, &i16b) != 0);

    /* get_type()：GType 注册后返回非 0 */
    errs += CHECK(thrift_transport_get_type() != 0);
    errs += CHECK(thrift_protocol_get_type() != 0);
    errs += CHECK(thrift_binary_protocol_get_type() != 0);
    errs += CHECK(thrift_server_get_type() != 0);

    /* error_quark()：返回非 0 */
    errs += CHECK(thrift_transport_error_quark() != 0);
    errs += CHECK(thrift_protocol_error_quark() != 0);

    /* 本机未导出的符号：调用走 fallback（0），不崩溃 */
    printf("info: thrift_zlib_transport_get_type() = %lu (fallback expected 0)\n",
           thrift_zlib_transport_get_type());

    thrift_c_glib_dyn_cleanup();
    if (errs) { fprintf(stderr, "thrift_c_glib_dyn: %d failure(s)\n", errs); return 1; }
    printf("thrift_c_glib_dyn: all checks passed\n");
    return 0;
}
