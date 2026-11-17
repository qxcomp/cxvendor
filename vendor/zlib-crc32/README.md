# vendor/zlib-crc32

zlib 1.2.11 `crc32.c` 的逐字节查表路径移植,**仅供无 zlib 的平台兜底**;
providers 依赖 zlib(`-lz` / `ZLIB::ZLIB`)的平台直接调系统 `crc32()`(`zlib.h`),语义一致。

- 来源:https://github.com/madler/zlib (tag v1.2.11, crc32.c/crc32.h)
- 许可:zlib license -- Copyright (C) 1995-2016 Mark Adler
- 文件:
  - `crc32.h` — 接口 `qxv_crc32(uint32_t crc, const uint8_t*, size_t)`(增量口径)
  - `crc32_table.h` — 原件生成的逐字节查表(仅第一张表)
  - `crc32.c` — 实现(`crc ^ 0xffffffff` 前后处理 + 查表)
- 裁剪:去掉 zutil.h/zlib.h 依赖、SIMD/combine 路径
- 校验:`crc32(0, "123456789", 9) == 0xCBF43926`,与系统 zlib crc32() 增量对拍一致(2026-11-17 实测)
- 用法:代码里 `#if defined(QXV_NO_ZLIB)` 走本兜底,否则直接 `#include <zlib.h>` 调 `crc32()`
- 用途:xiaohongshu 签名器 x-s-common 的 x9=crc32(x8);参考实现 Python `zlib.crc32` 即此口径
