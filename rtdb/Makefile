# rtdb 测试 Makefile
#
# 原则：测试程序只链接 -ldl（目标库在运行时由各 *_dyn.c dlopen 加载），
# 唯一例外 test_vte 需要链接 gtk+-3.0（窗口测试），但也不链 -lvte。
CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -Itests
LDLIBS  ?= -ldl
BUILD   := tests/build

GTK3_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)
GTK3_LIBS   := $(shell pkg-config --libs gtk+-3.0 2>/dev/null)

# mysql/pgsql 的封装层需要各自头文件路径（仅编译期，链接期仍不链目标库）
MYSQL_CFLAGS := $(shell pkg-config --cflags mariadb 2>/dev/null || \
                       pkg-config --cflags mysqlclient 2>/dev/null || \
                       echo -I/usr/include/mysql)
PGSQL_CFLAGS := $(shell pkg-config --cflags libpq 2>/dev/null)

TESTS := rclone python ruby guile vte notify dbus curl sqlite3 bdb \
         ffmpeg mpv clang mysql pgsql nghttp2 nghttp3 wasmtime wasmedge \
         libevent libuv libev \
         libxml2 expat tinyxml2 \
         gdbm lmdb leveldb \
         mxml opendal \
         md4c cmark \
         protobuf-c thrift_c_glib libcbor tinycbor msgpack avro fastipc lcm tachyon nng mosquitto zmq libgccjit luajit mir lightning reproc

# 无环境依赖、可直接跑的集合（mysql/pgsql 无服务时自身 SKIP 退出 0）
RUN_SAFE := $(filter-out vte,$(TESTS))

.PHONY: all clean $(TESTS:%=run-%) run-all

all: $(TESTS:%=$(BUILD)/test_%)

# 每个 run-<name> 显式规则（phony 目标不做模式规则匹配）
# 每个 run-<name> 显式规则（phony 目标不做模式规则匹配）
# 特殊运行环境：vte→xvfb-run；notify/dbus→dbus-run-session
VTE_RUNNER   = xvfb-run -a
DBUS_RUNNER  = dbus-run-session --
define RUN_rule
run-$(1): $(BUILD)/test_$(1)
	$(RUNNER_$(1)) ./$$<
endef
RUNNER_vte = $(VTE_RUNNER)
RUNNER_notify = $(DBUS_RUNNER)
RUNNER_dbus = $(DBUS_RUNNER)
$(foreach t,$(TESTS),$(eval $(call RUN_rule,$(t))))

# 通用规则：tests/test_<name>.c + <name>_dyn.c，链 -ldl
$(BUILD)/test_%: tests/test_%.c %_dyn.c tests/test_common.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(word 2,$^) $(LDLIBS)

# vte 例外：GTK3 头文件与链接库，但仍然不链 -lvte
$(BUILD)/test_vte: tests/test_vte.c vte_dyn.c tests/test_common.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(GTK3_CFLAGS) -o $@ $< vte_dyn.c $(GTK3_LIBS) $(LDLIBS)

# mysql/pgsql：仅编译期头文件路径
$(BUILD)/test_mysql: CFLAGS += $(MYSQL_CFLAGS)
$(BUILD)/test_pgsql: CFLAGS += $(PGSQL_CFLAGS)

clean:
	rm -rf $(BUILD)

# 全部安全集合；vte 需要 X，放最后单独跑
run-all:
	@set -e; for t in $(RUN_SAFE); do \
	    echo "===== run-$$t ====="; \
	    $(MAKE) --no-print-directory run-$$t; \
	done; \
	echo "===== run-vte (xvfb) ====="; \
	$(MAKE) --no-print-directory run-vte
