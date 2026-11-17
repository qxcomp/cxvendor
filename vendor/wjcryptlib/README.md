# vendor/wjcryptlib

WjCryptLib 的 RC4 单文件实现(公有领域),供 xiaohongshu 签名器的 b1 指纹 RC4 加解密。

- 来源:https://github.com/WaterJuice/WjCryptLib (master, lib/WjCryptLib_Rc4.{c,h})
- 许可:UNLICENSE(公有领域,仓库含 UNLICENSE 文件)
- 文件:`WjCryptLib_Rc4.c` / `WjCryptLib_Rc4.h`(保留原文件名以存证出处;API:
  `Rc4Initialise(Rc4Context*, key, keylen)` + `Rc4XorKeyStream(context, dst, src, len)`)
- 未引入该库其余件(AES/SHA 等,qlcomp 已有 aes/sha1/obsd_sha2)
- 用途:xhshow 移植中 b1 指纹的 RC4 口径(与 Python 参考实现对拍验证)
