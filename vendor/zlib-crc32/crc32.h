/* crc32.h -- CRC-32(IEEE 802.3,多项式 0xEDB88320)接口
 *
 * 兜底实现,供无 zlib 的平台使用(有 zlib 的平台直接调 crc32(),见 zlib.h:
 * uLong ZEXPORT crc32(uLong crc, const Bytef *buf, uInt len),增量口径一致)。
 *
 * 移植自 zlib 1.2.11 (madler/zlib) crc32.c:仅保留逐字节查表路径
 * (Python zlib.crc32 等参考实现即此口径);SIMD/combine 路径未引入。
 * 原件许可:zlib license -- Copyright (C) 1995-2016 Mark Adler。
 */
#ifndef QXV_ZLIB_CRC32_H
#define QXV_ZLIB_CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 增量计算:crc 首次传 0,后续传上次返回值续传。
 * 校验:crc32(0, "123456789", 9) == 0xCBF43926 */
uint32_t qxv_crc32(uint32_t crc, const uint8_t* buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* QXV_ZLIB_CRC32_H */
