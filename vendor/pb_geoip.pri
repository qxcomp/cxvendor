# pb_geoip.pri — geoip.dat 离线解码（内置 nanopb 0.4.9.1，全平台统一，不使用系统 protobuf）
# Usage: include(../../vendor/pb_geoip.pri)
#
# 版本与生成方式见 vendor/pb_geoip/README.md

PB_GEOIP_DIR = $$PWD/pb_geoip

INCLUDEPATH += $$PB_GEOIP_DIR

# nanopb 官方加速项：只支持内存缓冲流 + 去掉错误字符串（我们只用 pb_istream_from_buffer）
DEFINES += PB_BUFFER_ONLY PB_NO_ERRMSG

# 60 万条 CIDR 的 pb_decode 是热点：C 源以 -O2 编译（实测 decode 1823ms → 738ms）
QMAKE_CFLAGS += -O1

SOURCES += $$PB_GEOIP_DIR/pb_common.c \
           $$PB_GEOIP_DIR/pb_decode.c \
           $$PB_GEOIP_DIR/pb_encode.c \
           $$PB_GEOIP_DIR/geoip.pb.c

HEADERS += $$PB_GEOIP_DIR/geoip.pb.h
