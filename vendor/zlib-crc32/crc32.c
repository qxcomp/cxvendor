/* crc32.c -- zlib 1.2.11 crc32.c 的逐字节查表路径移植
 *
 * 原件 Copyright (C) 1995-2016 Mark Adler,zlib license。
 * 仅保留逐字节路径(去掉 zutil.h/SIMD/combine 依赖),供无 zlib 平台兜底。
 * 语义与系统 zlib 的 crc32() 一致:增量计算,crc 首次传 0。
 */
#include "crc32.h"
#include "crc32_table.h"

uint32_t qxv_crc32(uint32_t crc, const uint8_t* buf, size_t size) {
    const uint8_t* p = buf;
    crc = crc ^ 0xffffffffU;
    while (size--)
        crc = qxv_crc_table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffU;
}
