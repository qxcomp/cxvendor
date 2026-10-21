# gumbo.pri — HTML5 解析器（vendor 源码，全平台统一，不使用系统 libgumbo）
# Usage: include(../vendor/gumbo.pri)

GUMBO_CPP = $$PWD/gumbo/attribute.c $$PWD/gumbo/char_ref.c \
            $$PWD/gumbo/char_ref_gperf.c $$PWD/gumbo/error.c \
            $$PWD/gumbo/parser.c $$PWD/gumbo/string_buffer.c \
            $$PWD/gumbo/string_piece.c $$PWD/gumbo/tag.c \
            $$PWD/gumbo/tokenizer.c $$PWD/gumbo/utf8.c \
            $$PWD/gumbo/util.c $$PWD/gumbo/vector.c

GUMBO_HDR = $$PWD/gumbo/gumbo.h $$PWD/gumbo/tag_enum.h

SOURCES += $$GUMBO_CPP
HEADERS += $$GUMBO_HDR
INCLUDEPATH += $$PWD/gumbo
