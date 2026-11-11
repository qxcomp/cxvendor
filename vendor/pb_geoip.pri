# pb_geoip.pri — geoip.dat 离线解码（内置 nanopb 0.4.9.1，全平台统一，不使用系统 protobuf）
# Usage: include(../../vendor/pb_geoip.pri)
#
# 版本与生成方式见 vendor/pb_geoip/README.md

PB_GEOIP_DIR = $$PWD/pb_geoip

INCLUDEPATH += $$PB_GEOIP_DIR

SOURCES += $$PB_GEOIP_DIR/pb_common.c \
           $$PB_GEOIP_DIR/pb_decode.c \
           $$PB_GEOIP_DIR/pb_encode.c \
           $$PB_GEOIP_DIR/geoip.pb.c

HEADERS += $$PB_GEOIP_DIR/geoip.pb.h
