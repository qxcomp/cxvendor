# Vendor Libraries

## uneif (QQ .eif 解包 CFB 实现)

- **Name**: statementreply/uneif
- **Upstream**: https://github.com/statementreply/uneif
- **License**: MIT（`uneif/LICENSE`）
- **Files**:
  - `uneif/cfbf.h`（CFB 头结构 + `EifFile` 类声明，134 行）
  - `uneif/cfbf.cpp`（DIFAT/FAT/MiniFAT/目录/MiniStream 解析 + `unpack` 按目录树落盘，193 行）
- **限界**: 仅 v3 OLE（`SectSize=512` 硬编码、`checkHeader` 拒 `log2SectorSize!=9`）；
  QQ 表情包 .eif 均为 v3，实用无碍。不解 Face.dat（组名/排序），按 CFB 树序导出。
- **C++23 适配**: 上游为 C++17 代码，入库已打补丁（`cfbf.cpp`）：
  `#include <cstring>`（memcmp）+ `u8string` 拼接改为
  `std::string(u8string().begin(), u8string().end())`（C++20 起 `u8string()` 返回
  `basic_string<char8_t>`，`const char* + u8string` 不再编译）。`g++ -std=c++23 -Wall` 通过。
- **用途**: 应用内 .eif 解析的 CFB 底座候选之一（与 `include/compoundfilereader.h` 并行保留，
  实施时以真实样本验证钦点一个为主）

## microsoft/compoundfilereader（CFB 单头读器）

- **Name**: microsoft/compoundfilereader
- **Upstream**: https://github.com/microsoft/compoundfilereader
- **License**: MIT
- **Files**:
  - `include/compoundfilereader.h`（470 行，buffer 原地读，`CFB::CompoundFileReader`）
- **能力**: 支持 v3(512B) + v4(4096B) sector；`EnumFiles`/`ReadFile`/`GetRootEntry`；
  异常 `WrongFormat`/`FileCorrupted`；仅 little-endian；需整体读入内存。
- **用途**: 应用内 .eif 解析的 CFB 底座候选之一（按需读取每个流，便于 Face.dat 驱动命名/排序）

## libobfuscate

- Name: libobfuscate (adamyaxley/Obfuscate)
- Version: upstream (C++14 compile-time string obfuscation, header-only)
- Upstream: https://github.com/adamyaxley/Obfuscate
- License: Unlicense
- Files:
  - `include/libobfuscate/obfuscate.h` (271 lines, XOR compile-time encryption via `AY_OBFUSCATE`)
  - 来源：vcpkg `libobfuscate` port（x64-linux-dynamic triplet 已装同版本）
- 用途：`src/davobfus.cpp` 用 `AY_OBFUSCATE` 编译期混淆 dav 鉴权 key（占位 `AUTHKEY_PLACEHOLDER`）
- 注意：单字节 XOR 为缓解非绝对安全；运行时解码后内存仍可被逆向提取。

## ADVobfuscator（已 vendor，Release 构建实际使用）

- **Name**: ADVobfuscator (andrivet/ADVobfuscator)，**v2.1.2**
- **Upstream**: https://github.com/andrivet/ADVobfuscator
- **License**: BSD-3-Clause-Clear（与 libobfuscate 的 Unlicense 不同）
- **结构**: header-only、**多文件**——`include/advobfuscator/` 下 9 个头（含相互 include）：
  `aes.h`, `aes_string.h`, `bytes.h`, `call.h`, `format.h`, `fsm.h`, `obf.h`, `random.h`, `string.h`
- **加密**: 编译期加密 + FSM 混淆运行期解码调用（`call.h`/`fsm.h` 的 `ObfuscatedMethodCall`、
  `ObfuscatedCall`）；`string.h` 的多算法链（XOR/CAESAR/ROTATE/SUBSTITUTE）+ `aes_string.h` 的
  AES-128-CTR，比 libobfuscate 的单字节 XOR 强度高
- **C++ 标准**: **必须 C++20**（`consteval`、class-NTTP UDL `template<ObfuscatedString str> operator""_obf`、
  constexpr 析构）。**项目已 `CMAKE_CXX_STANDARD 23`（CMakeLists.txt:4），满足**
- **Files** (SHA256):
  - `include/advobfuscator/aes.h` (`6c1efe45a4b1b0e9162e8ffc2c24c73259d9e5b67f360799e5575db5c59b9155`)
  - `include/advobfuscator/aes_string.h` (`3d495c17dcfaaae1a958cfb4e201b0d8ae8da8c79d8471c5759e39362b323d47`)
  - `include/advobfuscator/bytes.h` (`6c4eca412b422689f834cd01ed39cfcdabe9bb34511cd9f0f50a0057e80fbbf5`)
  - `include/advobfuscator/call.h` (`5e11f8d7639475d0284ab05e7263cb6d770799bd78ac3a098fefe31e78ce69c7`)
  - `include/advobfuscator/format.h` (`46174781ae38b370f75d6b549721fb0249bacafd9c0469914318b94e00217451`)
  - `include/advobfuscator/fsm.h` (`0be1d216ba50693b26d7a7ff1e015bd290283c884a36e925a7f9ddeb37759492`)
  - `include/advobfuscator/obf.h` (`07b99a8a2859576e4fecb8756ae43f3f97dc79ec2a0e1182d3a7474133769118`)
  - `include/advobfuscator/random.h` (`a50caffb578daa7070ebb9bffc28adc03ef543ef984d6275da0d191531275ac6`)
  - `include/advobfuscator/string.h` (`ec7838c939663fd70bea72a548ef8ede445fca8c4ddc9382e67a66aa3e838ec6`)
  - 来源：GitHub tag v2.1.2 `include/advobfuscator/`（经 gh-proxy.org 逐文件下载）
- **用途**: dav 鉴权 key 混淆方案，**双轨实现**——构建期按信号自动选择：
  - Debug/等同调试型（`_DEBUG` 或 `!NDEBUG` 或 `QT_DEBUG` 任一命中）→ libobfuscate `AY_OBFUSCATE`
  - Release（三信号均不命中，含 Android `RelWithDebInfo`）→ ADVobfuscator `""_obf`
  - 见 `src/davobfus.cpp` 与 `src/davobfus.cpp.tmpl`
- **Debug 构建限制**: upstream README 明确「Obfuscation works only for Release builds」，
  Debug 下明文照常进二进制。本项目 x64 构建用 `-DCMAKE_BUILD_TYPE=Debug`（+`-O1`）
  → 走 libobfuscate 分支；Android 构建 `RelWithDebInfo`（含 `-DNDEBUG -DQT_NO_DEBUG`）
  → 走 ADVobfuscator 分支
- **必须用 `_obf` UDL，勿用显式构造**: 实测 `andrivet::advobfuscator::ObfuscatedString("...")`
  会把字面量当运行时参数留在 `.rodata` 中**泄露明文**（-O1/-O2/-O3 均复现）；
  `"..."_obf` 把 ObfuscatedString 整体作为 class-NTTP 模板实参，编译期加密后仅存编码字节
  （g++/clang++ -std=c++23 -O0~-O3 全组合验证 `strings` 无明文）。需 `using namespace andrivet::advobfuscator;`
  使 UDL 可见。`DAVOBFUS_KEY""_obf`（宏展开后邻接字面量接 UDL）可用，key 保持单源宏定义
- **决策记录**: x64 Debug 不生效 → 采用**双轨**：Debug 用 libobfuscate（全构建生效），
  Release/RelWithDebInfo 用 ADVobfuscator（混淆更强）。检测条件取三信号 OR，宁保守不露明文。
  勿把 UDL 对象赋给 `const char*`（语句结束析构置零导致悬垂指针），
  须经隐式转换 `const char*` 后 `QString::fromUtf8(key)`

## cJSON

- Version: 1.7.19
- Upstream: https://github.com/DaveGamble/cJSON
- Files:
  - `cJSON.c` (SHA256: `607e756460fa0de37d20a7a9181f2de29c97bfb7ce5a0e6c2f548243836cd852`)
  - `cJSON.h` (SHA256: `25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee`)

## SQLite

- Version: 3.53.3
- Android prebuilt .so（4 ABI，均为同一 release 的逐架构资产）:
  - Download: https://github.com/simolus3/sqlite3.dart/releases/download/sqlite3-3.4.0/
    （下载经 `https://gh-proxy.org/https://github.com/...` 前缀）
  - Built by: NDK r29 (14206865), Android API 24+
  - `lib/arm64-v8a/libsqlite3.so` ← `libsqlite3.arm64.android.so`
    (SHA256: `e99515af1d7119fb61843ae5e597344e7f258563de3a7e5a3869f627aab2887b`)
  - `lib/armeabi-v7a/libsqlite3.so` ← `libsqlite3.arm.android.so`（1,871,956 B）
    (SHA256: `6c1b8dffc1ddefaf02e771711491410bc3ab1db858d3d23d2925e0b2cd691b93`)
  - `lib/x86_64/libsqlite3.so` ← `libsqlite3.x64.android.so`（1,807,992 B）
    (SHA256: `e5a2d46ac5e11f471e1aaedfd364f54c7961a6900432d289ea3f5781bcaaf4cd`)
  - `lib/x86/libsqlite3.so` ← `libsqlite3.ia32.android.so`（1,751,304 B）
    (SHA256: `6d73649ad5896eb276aca304456b8664c0f9a82d6c7785c5975d43e0462d3777`)
  - 上游命名映射：arm64-v8a=arm64 / armeabi-v7a=arm / x86_64=x64 / x86=ia32
- Header (sqlite3.h):
  - Download: https://raw.githubusercontent.com/rhuijben/sqlite-amalgamation/master/sqlite3.h
  - File: `sqlite3.h` (SHA256: `4ff81af4849acabc76fc8349abb926814395072617ca18e08800abf734ab7612`)

## curl + OpenSSL

- curl: 8.19.0
- OpenSSL: 3.6.x
- Source: https://github.com/XDcobra/libcurl-ios-android-prebuilt-and-buildscripts/releases/tag/v8.19.0-1
  （下载经 `https://gh-proxy.org/https://github.com/...` 前缀）
- Android prebuilt .so（单一 `libcurl-android.zip` 全 ABI 包；zip sha256
  `21db554fe907df2fa44de314a6432e9390e75b241374c32d1398ef38682c0359`。
  上游 README 明列 4 ABI：arm64-v8a / armeabi-v7a / x86 / x86_64，API 24+）:
  - zip 内来源目录：`libcurl-openssl/jniLibs/{abi}/`（openssl 后端变体；
    包内可能并存其它 SSL 后端变体，取库须限定此目录）
  - `lib/arm64-v8a/libcurl.so` (SHA256: `5ceb34ff92d9f6cd6b28901cc220bc2917a53e2614e8c9f6764af18c89063b88`)
  - `lib/arm64-v8a/libssl.so` (SHA256: `96d844acd9b264face6529b3502269577c1c14843cef4b55031deb114db8f0a7`)
  - `lib/arm64-v8a/libcrypto.so` (SHA256: `953e2c534771b09e022bf4ef3d7d3ff4c18ba10241fb430036d1147399a28a90`)
  - `lib/armeabi-v7a/libcurl.so` (SHA256: `f464e7cf02fcfe5aca2be297dbdc2267766200b871460fd4eb14a08af43261a8`)
  - `lib/armeabi-v7a/libssl.so` (SHA256: `6d4d280c4d7a662f298b81b8876ff625586328b197851c8266ea43cd41399786`)
  - `lib/armeabi-v7a/libcrypto.so` (SHA256: `ba9cdc201710dbe00f32232db88007fd12f23de62900d0aeb7172601e082a889`)
  - `lib/x86_64/libcurl.so` (SHA256: `3cb2369dd50e934c89eb01612034e7efe7c507c9989e6f7bf7f337b729c896d8`)
  - `lib/x86_64/libssl.so` (SHA256: `08ffe59ca4699e5b897d386d6ea700ff6b611f373a51cdee4deb4296ccab959d`)
  - `lib/x86_64/libcrypto.so` (SHA256: `7d077de429e82061192a7bf2438aa6330f5f73e38b70beea1905227af91ffbfa`)
  - `lib/x86/libcurl.so` (SHA256: `a3447a3b8cfcda8bda6609d50657002840d009ae09332f88777ba0644e71b662`)
  - `lib/x86/libssl.so` (SHA256: `a31c38ea468a9d3cd880c7593663deb420ad3d55d2d9d0dccf354285daa02c32`)
  - `lib/x86/libcrypto.so` (SHA256: `2712a476ac7eceaa4eb794397c7a7b9f9c82f5e93604caabc9b45de36c097f79`)
  - 注：armeabi-v7a/x86_64/x86 的九项 SHA256 为 v8.19.0-1 zip 首装实测
    （zip 内各 ABI 目录限定 `libcurl-openssl` 变体后取出，经 `sha256sum -c` 复跑确认）
- Headers:
  - `include/curl/` (curl 头文件)
  - 注：v8.19.0-1 包内不含 OpenSSL 头，本地 `include/openssl/` 为另行放置；
    qsktox 无直接 `#include <openssl/>`，CI 缺此头不影响编译链接
- 此目录不入库：CI 于构建时经 `scripts/ci_prebuild_qsktox_vendor.sh` 自动下载归位并校验 SHA256

## UnifiedPush android-connector

- Version: 3.3.3
- Source: Maven Central (`org.unifiedpush.android:connector:3.3.3`)
- Upstream: https://github.com/UnifiedPush/android-connector
- License: Apache 2.0
- Min Android: 4.1 (API 16)，qsktox minSdkVersion 23 完全兼容
- 依赖：`com.google.crypto.tink:tink-android:1.20.0`（需 resolutionStrategy 解决冲突）
- 集成方式：Gradle 依赖（非预编译 .so）
- Java 层：`PushServiceImpl extends PushService`（`android/src/java/io/fedlet/mobutil/PushServiceImpl.java`）
- C++ 层：`pushhandler.h/cpp`，JNI 桥接
- AndroidManifest.xml：声明 `PushServiceImpl`（action: `org.unifiedpush.android.connector.PUSH_EVENT`）
- 注册流程：`UnifiedPush.tryUseCurrentOrDefaultDistributor()` → callback → `UnifiedPush.register(context, INSTANCE_DEFAULT, ...)`
- Go 服务端需实现 Web Push 发送（`webpush-go` 库），但当前不在 qsktox 范围内
- 推送测试（ntfy.sh）：
  - 简单推送：
    ```bash
    curl -d "Hello from qsktox" https://ntfy.sh/mytopic
    ```
  - 带标题和优先级：
    ```bash
    curl -H "Title: qsktox Push" -H "Priority: high" -d "New message" https://ntfy.sh/mytopic
    ```
  - 带 action broadcast（触发 BroadcastReceiver）：
    ```bash
    curl -H "Title: New Message" \
         -H "Actions: broadcast, Open qsktox, intent=io.fedlet.qsktox.PUSH_RECEIVED, extras.cmd=open" \
         -d "You have a new message" \
         https://ntfy.sh/mytopic
    ```
   - JSON 格式：
    ```bash
    curl -H "Content-Type: application/json" \
         -d '{"topic":"mytopic","message":"New message","title":"qsktox","priority":4}' \
         https://ntfy.sh
    ```

## UnifiedPush 字符串说明

### 三个关键字符串

| 字符串 | 代码位置 | 含义 | 示例 |
|--------|----------|------|------|
| **connection token** | `UnifiedPush.register(context, token)` 第二参数 | App 标识符，distributor 用它匹配回调到对应 app | UUIDv4（如 `a1b2c3d4-e5f6-...`） |
| **instance** | `PushServiceImpl.onNewEndpoint(endpoint, instance)` 的 instance 参数 | 同 connection token，回调中原样返回 | 同上 |
| **topic** | ntfy 服务端生成，包含在 endpoint URL 路径中 | ntfy 上的频道名，推送方 POST 到此 topic | `upAbCdEfGh1234` |

### 单注册模式（per-device UUID）

每个设备注册一次，使用随机生成的 UUIDv4 作为 token：

| instance | topic | 用途 | 谁推送 |
|----------|-------|------|--------|
| UUIDv4 | `upXyZaBcDeF5678`（随机） | per-device 定向推送 | Go 服务端知道此 endpoint |

**安全设计**：
- Token 由 `QUuid::createUuid()` 生成，符合 UnifiedPush spec 推荐的 UUIDv4 格式
- 每设备独立 topic，endpoint URL 不可猜测（capability URL 模型）
- 开源代码中不暴露任何硬编码 token，避免 DDoS/消息伪造风险

### 调用链路

```
启动 → 读取/生成 UUID → 保存到 QSettings("pushDeviceToken")
      ↓
register(context, UUID)               → 分配 topic → 回调 onNewEndpoint(endpoint, UUID)
      ↓
onNewEndpoint: 保存 pushDeviceEndpoint = endpoint
      ↓
推送方 POST 到 endpoint URL:
  curl -d "targeted" https://ntfy.sh/upXyZaBcDeF5678?up=1
```

### QSettings 存储

| key | 值 | 说明 |
|-----|-----|------|
| `pushDeviceToken` | UUID 如 `a1b2c3d4-e5f6-...` | per-device 注册 token，重启复用 |
| `pushDeviceEndpoint` | `https://ntfy.sh/upXyZaBcDeF5678?up=1` | per-device 的 endpoint URL |

### ntfy 服务端对 UnifiedPush topic 的硬编码约束

```go
// ntfy/server/server.go
unifiedPushTopicPrefix = "up"   // 必须以 "up" 开头
unifiedPushTopicLength = 14     // 总长度必须 14 字符（含 "up"）
```

- topic 格式：`up` + 12字符随机串（如 `upAbCdEfGh1234`）
- **不可自定义** topic 名称（如 `io.fedlet.pushto.user9` 不满足约束）
- 此限制来自 ntfy 公共服务器，自建 ntfy 也可能继承

### 总结

| 概念 | 谁控制 | 可自定义？ |
|------|--------|-----------|
| connection token / instance | App 端 | **是**，per-device UUIDv4（随机生成） |
| topic | ntfy 服务端 | **否**，随机生成 |
| endpoint URL | ntfy 服务端 | **否**，格式为 `{server}/{topic}?up=1` |

## pugixml（DAV 207 解析的 XML 后端）

- **Name**: zeux/pugixml
- **Version**: 1.15（`PUGIXML_VERSION 1150`，已核对 `pugixml.hpp:17`）
- **Upstream**: https://pugixml.org/ （release 包 `pugixml-1.15.tar.gz`）
- **License**: MIT（`pugixml/LICENSE.md`，可商用，仅需保留声明）
- **Files**:
  - `pugixml/pugixml.hpp`（1585 行）
  - `pugixml/pugixml.cpp`（13553 行）
  - `pugixml/pugiconfig.hpp`（80 行）
  - `pugixml/LICENSE.md`
- **引入原因**: Qt 自带 XML API（QDom/QString）在 Qt3 与 Qt5/6 之间语义不一致，
  实测踩到的坑都是**静默产生错误数据**而非报错：①`QString::utf8()` 与
  `latin1()` 共用同一静态转换缓冲区，同一表达式内先后调用互相覆盖；
  ②`QString(const char*)` 在 Qt3 按 Latin-1 解释源文件 UTF-8 字节，"笔记"
  （6 字节）会变成 6 个 U+00xx 字符；③`QDom::elementsByTagName()` 只按本地名
  匹配、忽略命名空间，第三方命名空间同名元素会被误判。pugixml 不经 Qt，
  接口内统一用 `std::string`，Qt3/Qt5/6 行为一致。
- **实测**: 在本项目 Qt3 编译 flags（`-DQT3_BUILD -fPIC -O1`）下零错误通过，
  18s，`.o` 375KB；`load_buffer` 的 `encoding_auto` 使 UTF-8/UTF-16 自动识别，
  中文 href 无损。
- **C++ 标准**: pugixml **本体**支持 C++98 起（已实测 `-std=c++98 -fsyntax-only` 零错误；
  上游称在 VC++6.0~2026 / GCC 3.4~16 上测试，覆盖率 >99%）。不依赖异常
  （可 `PUGIXML_NO_EXCEPTIONS`）、不依赖 RTTI。
  **但 `stikcommon/dav207pugi.cpp` 用了 lambda，实测需 C++11**（`-std=c++98` 报
  "uses local type" 错）。因 anystik 走 CMake 且 `CMAKE_CXX_STANDARD 23`，
  C++11 无压力；真正的收益是**不依赖 Qt API**，与 C++ 标准版本无关。
- **已知限制**: 1.15 **不提供 `namespace_uri()`**（已 grep 确认，全头无 namespace 成员），
  故 `stikcommon/dav207pugi.cpp` 改为「剥前缀后比对本地名 + DAV 前缀白名单」判定命名空间。
- **配套（均在本目录之外，属自研代码，故放 stikcommon 不放 vendor）**:
  `stikcommon/dav207iface.h` 为统一解析契约（`Resource` 结构 + `parseMultiStatus`），
  `stikcommon/dav207pugi.cpp` 为唯一 pugixml 后端实现。
  原先的 `DAV207_USE_NEON` 后端开关宏**已删除**（neon 已否决，见下方条目），不再有第二后端。
- **自测**: 对 `/tmp/opencode/probe.xml`（11 个 response 的真实 207 样本）`fails=0`，
  覆盖：%20 解码、`+` 保留字面量、非法 `%ZZ`/截断 `%2` 整条拒绝（对齐
  `ne_path_unescape` 返回 NULL）、剥 query/fragment/authority、%2F、
  非 DAV 命名空间 collection 不判目录、嵌套 collection 不判目录、
  零 propstat 与全非 2xx 段剔除、中文 href 码点无损。

## neon 0.37.1 —— 已评估并放弃（仅保留行为基准，未入库任何代码）

- **Upstream**: http://www.webdav.org/neon/ ；GitHub 镜像 notroj/neon
- **取用版本**: tag **0.37.1**（commit `170c36704bfc`）。已逐文件 diff 确认
  `ne_207.c` / `ne_207.h` / `ne_xml.h` / `ne_uri.c` / `ne_string.c` /
  `ne_alloc.c` / `ne_utils.c` 在 0.37.1 与 master 分支**逐字节一致**。
- **License**: LGPL v2.1
- **放弃原因（两条约束与 neon 架构互斥，已实测确认）**:
  1. **neon 自己没有 XML 解析器。** `ne_xml.c` 只是 SAX 包装层
     （首行注释即 "Wrapper interface to XML parser"），第 41/56 行分别
     `#if defined(HAVE_EXPAT)` / `#elif defined(HAVE_LIBXML)`，第 66-67 行是
     `#else` + **`# error need an XML parser`**。即：**不给 expat 或 libxml2
     就编译不过**，无兜底实现。
  2. 因此要满足「neon 不依赖外部 XML 库」，唯一出路是自己实现
     `ne_xml.h` 的解析层；而该层要正确处理实体引用、CDATA、DOCTYPE、
     命名空间作用域、良构性检查与增量喂入 —— 这已属于自研 XML 解析器。
  实测：不定义任何 HAVE_* 时 `ne_xml.c:67: #error need an XML parser`；
  定义 `-DHAVE_EXPAT` 后可继续，但仍需 autoconf 生成的 `NE_FMT_SIZE_T`
  等宏（`ne_xml.c:598/607/638`），且构成外部库依赖。
- **另一处结构性障碍**: `ne_207.c` 并非独立单元。实测其 include 为
  `ne_xmlreq.h` / `ne_basic.h` / `ne_internal.h`，且第 300 行起是 HTTP 胶水
  （`ne_accept_207` / `ne_simple_request` / `ne_xml_dispatchif_request` /
  `ne_fill_server_uri`），依赖 `ne_request` / `ne_session`，与本项目已有的
  curl 传输层冲突。状态机本体只有 1-298 行。
  附带：`ne_buffer` 并无独立 .c，实现在 `ne_string.c:121-251`。
- **结论**: 保留 `dav207iface.h` 顶部契约 A–D 作为**行为基准**（下方已核实事实），
  实现只用 pugixml 一个后端。
- **已核实的源码事实**（tag 0.37.1 逐行核对）:
  - `ne_207.c:192-222` `in_response` 由 **href** 置位，非 propstat
  - `ne_207.c:255-265` `if (!p->in_response) break;` → 零 propstat 的 response
    **照样回调** end_response。「零 propstat 不回调」的说法是错的，过滤在 ne_props.c
  - `ne_uri.c:487` `ne_path_unescape()`: 非法 `%XX` → **整条 free 返回 NULL**
    （非原样保留）；纯字节 `strtol(buf,16)` 不校验 UTF-8；非 `%` 字符原样复制，
    故 URI path 中 `+` 是字面量、非空格
  - `ne_207.c:130-142` cdata 累积上限 2048 字节
  - `ne_xml.c:351-356` handler 栈语义：从**父元素的 handler** 起沿 `next`
    向下遍历，返回 `>0` 接受（值即该元素 state）、`0`=DECLINE（剪枝）、
    `<0` 中止解析；`<100` 为 `NE_XML_STATE_TOP` 保留

## doctest（单元测试框架）

- **Name**: doctest/doctest
- **Upstream**: https://github.com/doctest/doctest
- **License**: MIT（`doctest/LICENSE.txt`）
- **版本**: **2.4.11**（`DOCTEST_VERSION_MAJOR/MINOR/PATCH = 2/4/11` 已 grep 核实）
- **Files**:
  - `doctest/doctest.h`（单头，321644 字节，v2.4.11 发布版原文，未改一字）
- **为何是这个版本**：`qldox/run_tests`（2026-09 前遗留二进制，源码已丢）的调试信息里
  编译单元为 `test_main.cpp` + `test_md5.cpp` + `test_emojiutil.cpp` +
  `test_translate_util.cpp` + `test_compat34_time.cpp`，二进制内嵌版本串 `2.4.11`；
  同串 `2.2.5` 经核实是 `GLIBC_2.2.5` 符号版本，可排除。**按同一版本 vendor 以保持一致。**
- **能力**: header-only；`TEST_CASE`/`SUBCASE`/`CHECK_EQ`/`REQUIRE_EQ`；Console 与 JUnit
  XML reporter；命令行过滤（长短双别名，如 `--test-case=`/`-tc=`/`--dt-test-case=`，
  短名定义见 `doctest.h:6614-6630`）
- **已验证与本仓 Qt3 的兼容**：`qldox/run_tests` 链接 `libqt-mt.so.3`（即曾对着 Qt 3.5
  真实编译运行），且本次已用 `-DQT3_BUILD -lqt-mt` 实测编译运行通过 —— Qt 3.5 的
  `signals`/`slots`/`emit` 宏与 doctest 无冲突
- **用途**: `stikcommon/` 与 `qlstik/` 两处单元测试套件的框架。include 路径为
  `anystik/vendor` 根（与 `pugixml` 同一手法），写作 `#include "doctest/doctest.h"`

## uc_apng_loader + stb_image（APNG 解码，批次 5 Qt3 侧实际使用）

- **Name**: uctakeoff/uc_apng_loader（主用） + nothings/stb（仅取 `stb_image.h`）
- **Upstream**: https://github.com/uctakeoff/uc_apng_loader
- **License**: uc_apng_loader 为 MIT（`uc_apng_loader/LICENSE`）；`stb_image.h` 为 public domain
  （v2.30，头部自述 `stb_image - v2.30 - public domain image loader`）
- **Files** (SHA256):
  - `uc_apng_loader/uc_apng_loader.h`（17027 字节，头文件第 4-5 行自述 MIT）
    `126257eeb00cab85b0c685ac20fc8c572309d351c637da90a4c058e1b6278c6b`
  - `uc_apng_loader/stb_image.h`（283010 字节，v2.30）
    `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`
  - `uc_apng_loader/LICENSE`（1057 字节）
    `afb11426e09da40a1ae4f8fa17ddcc6b6a52d14df04c29bc5bcd06eb8730624d`
- **能力**: 解析 `acTL`/`fcTL`/`fdAT`；`create_file_loader()`/`create_memory_loader(buf,len)`
  （返回**对象本身**，不是指针）；`width()`/`height()`/`num_frames()`/`num_plays()`；
  `has_frame()`/`next_frame()` → `frame{index, image}`。`frame.image` 恒为**全画布尺寸**
  的已合成结果，合成代码在头文件内：`blend_frame()`（L329，按 `blend_op_t::SOURCE`/OVER 混合）
  与 `next_frame()` 的 `dispose_op_t::NONE`/`BACKGROUND`/`PREVIOUS` 三分支（L413-427）；
  L485-487 实现了规范细节「首帧 `dispose_op==PREVIOUS` 视为 `BACKGROUND`」
- **已实测**（`~/ztprobe/img/apngtest.cpp` / `apngscale.cpp`，PIL 生成 3 帧 40x40 APNG，
  duration 120/80/200ms，loop=0）：`canvas=40x40 frames=3 plays=0` 与 PIL `n_frames=3` 一致；
  每帧输出全画布 40x40；首帧左上 `ff0000ff`、后两帧 `00000000`，与帧内容相符；
  缩放到 20x20 后用 PIL 复核三张 PNG 尺寸/颜色均正确。`-std=c++11` 与 `-std=c++23`
  （项目 `CMAKE_CXX_STANDARD 23`）均编译通过
- **⚠ 上游缺陷：不能喂静态 PNG**。`uc_apng_loader.h:241-248` 的 `image_t(std::vector<uint8_t>&)`
  先调 `stbi_load_from_memory(..., STBI_rgb_alpha)`，再 `UC_APNG_ASSERT(d == BPP)`；
  但 stb 在指定 `req_comp` 强制转换时**不改写** `d`，故 RGB 静态 PNG（`d==3`）必然断言失败，
  抛 `uc::apng::exception: image_t : d == BPP failed.`。**本项目只把它用于 APNG 分支**，
  静态 PNG 仍走 Qt 自己的 `QImageReader`，两者不混用
- **⚠ 不可定义 `UC_APNG_LOADER_NO_EXCEPTION`**：实测该宏下 APNG 静默解析失败
  （`canvas=0x0 frames=0 plays=0`、`decoded=0`，不抛异常也不报警），错误被完全吞掉。
  保持默认异常模式，调用侧用 try/catch 转成错误返回
- **落地三个坑（均实测）**:
  1. 头文件**自身不 include `<cstdint>`**，调用方须先 `#include <cstdint>`，
     否则 `uint8_t`/`uint16_t`/`uint32_t` 全部报错
  2. 需在某 TU 里 `#define STB_IMAGE_IMPLEMENTATION` + `#include "stb_image.h"`，
     否则链接期报 `undefined reference to stbi_image_free`
  3. `create_memory_loader` 返回对象，写 `loader->` 编译失败，须 `loader.`
- **成熟度风险（选型时已知并接受）**: 仅 **11★ / 3 forks**，created 2017-04-08，
  pushed **2024-07-24**，无 OSS-Fuzz 等安全审计、无公开发行版。选它是因为功能完整
  （已含合成，不必自写 150-200 行）且 header-only 体量小（17KB+283KB）
- **为何不选 Firefox `media/libpng/apng.patch`**: 该 patch 只提供帧流 API
  （`png_get_acTL`/`png_read_frame_head`/`png_write_frame_head`/`png_write_frame_tail`），
  `pnginfo.h` 新增字段全是元数据（`num_frames`/`next_frame_dispose_op`/`next_frame_blend_op` 等）、
  **不含已合成画布**，合成仍要自写；且 patch 是针对 Mozilla 内 **libpng 1.6.59** 打的，
  本机系统是 1.6.50，不能直接 apply。上游 `pnggroup/libpng` PR #706 中维护者明确要求
  「disabling APNG handling by default」，长期游离于上游之外。详见 `qlstik/移植计划.md` §6.3k
- **用途**: 批次 5 `stickerstore` 在 Qt3 侧的 APNG 多帧解码（Qt3 无 APNG 动图支持）；
  GIF 走 libnsgif、WebP 走系统 libwebp，见 `qlstik/移植计划.md` §6.3k K.1

## libnsgif（GIF 动图解码，批次 5 Qt3 侧实际使用）

- **Name**: NetSurf libnsgif
- **Upstream**: 官方 git 在 `source.netsurf-browser.org/libnsgif.git`（**不在 GitHub**）。
  入库取自镜像 `netsurf-plan9/libnsgif` 的 `master` 分支 commit `e97bc7b86f`
- **License**: MIT（`libnsgif/COPYING`；© 2004 Richard Wilson / © 2008 Sean Fox /
  © 2013-2021 Michael Drake）
- **为何这样取**：官方源是 git 而非 tarball 快照，GitHub 镜像 `netsurf-plan9/libnsgif`
  仅 9★ 且 push 停在 2024-03-29，**未跟进官方 master**。镜像仓库的文件路径与官方不同
  （镜像用 `src/gif.c`、`include/nsgif.h`，而非常见的 `libnsgif.c`），故按镜像实际布局入库。
  文件名以镜像为准，源码是上游 NetSurf 原件。GitHub 上 `NetSurf/libnsgif`、
  `giflib/giflib` 均返回 404
- **Files** (SHA256):
  - `libnsgif/gif.c`（50874 字节，**注意不叫 `libnsgif.c`**）
    `e9d5fe0e63b10d9be603e1ad2f1fede0a5acf0909bc17c8c7911b776f0a0a1af`
  - `libnsgif/lzw.c`（16711 字节）
    `55ee58985717ad622c42ac853d241470ed48fbf9d78820ac497a29c83a5bf794`
  - `libnsgif/lzw.h`（4878 字节）
    `6191b797a275e9c250aad591853bc2825593bb182b5002b7ae1cd9e50329a6f2`
  - `libnsgif/nsgif.h`（15318 字节）
    `586ed21a3e12d6b0cd423af262fb6beee4f0e45b848672040505cb5d395d71fa`
  - `libnsgif/COPYING`（1133 字节）
    `1469b759cf18e43c6e1b4ff892307d3962cbbb337ac497620d6690a219fad10c`
- **零外部依赖（已核对全部 include）**：`gif.c`/`lzw.c` 只 include
  `<assert.h> <stdint.h> <stdlib.h> <string.h> <stdbool.h>` + 本目录 `lzw.h`/`nsgif.h`；
  `nsgif.h` 只 include `<stdint.h> <stdbool.h> <inttypes.h>`。C99 即可编译
- **API 范式（与官方 README 一致，两步式）**:
  `nsgif_create(&vt, fmt, &gif)` → `nsgif_data_scan(gif, size, data)`（可多次喂增量数据）
  → `nsgif_data_complete(gif)` → `nsgif_get_info(gif)` 取 `const nsgif_info_t*`
  （`width/height/frame_count/loop_max/background/global_palette`）
  → 循环 `nsgif_frame_prepare(gif,&area,&delay,&frame)` + `nsgif_frame_decode(gif,frame,bitmap)`
- **⚠ 三个 API 陷阱（实测踩过，务必先读）**:
  1. **`nsgif_frame_decode` 的 bitmap 参数是二级指针** `nsgif_bitmap_t **bitmap`
     （`gif.c:1947`），库会**自己创建并回填** bitmap。传单层指针会让库写进调用方的
     栈上临时变量，随后读另一个 buffer 就得到 `905c1aef` 之类垃圾值——极易误判成
     库有 bug。正确写法：`nsgif_bitmap_t* bmp=NULL; nsgif_frame_decode(gif,frame,&bmp);`
     之后用 `bmp`（库持有，勿 free）
  2. **`delay` 是 GIF 原始单位（1/100 秒），不是毫秒**。`gif.c:774`
     `frame->info.delay = data[3] | (data[4] << 8)` 直接取 GCE 两字节小端。
     实测对照：`t3.gif` 文件 GCE 字节为 `0c`/`08`/`14`，libnsgif 报 12/8/20，
     而 stb_image 报 120/80/200（它内部已 ×10）。`stickerstore` 现有 delay 语义是
     **毫秒**，接 libnsgif 必须 **×10**
  3. **`nsgif_frame_prepare()` 不会自己停**：`loop_max == 0`（永久循环）时永远返回
     `NSGIF_OK` 并从第 0 帧重来，实测 3 帧 GIF 无限吐帧。须在拿到
     `NSGIF_ERR_ANIMATION_END`（仅有限循环结束时出现）**或自行按 `loop_max` 计数退出**
- **与其他解码器的实测对照**（同一批文件，40x40）：
  `t3.gif`（无 disposal）与 `d2.gif`（`disposal=2`）两者的**像素与 stb_image v2.30、
  PIL 完全一致**，含 disposal=2 的透明处理（帧1 `(0,0)=000000ff` 透明、`(15,15)=00ff00ff` 绿）。
  唯一差异就是上面第 2 条的 delay 单位
- **stb_image 也能解 GIF 动画**（`stb_image.h:433` 有 `stbi_load_gif_from_memory`，
  返回紧凑排列的 `frames×w×h×4` RGBA + `int** delays` 毫秒数组，两个 buffer 都要
  `stbi_image_free`，链接需 `-lm`）。本项目仍选 libnsgif 而非 stb_image 解 GIF，
  原因见下条 stb 维护者立场
- **已实测**（`/tmp/vtest/nsgiftest.c`，`-std=c99 -O2 -Wall`，PIL 生成 3 帧 40x40 GIF，
  duration 120/80/200ms、loop=0）：`canvas=40x40 frames=3 loop=0` 与 PIL `n_frames=3` 一致；
  三帧 `delay` 分别回读为 **12/8/20ms**（GIF 时间单位是 10ms，120ms→12 正确）；
  第 3 帧 `redraw` 区域为 **32x32**，正确反映了该帧的局部尺寸（前两帧为全画布 40x40）
- **成熟度**: GitHub star 数不具参考性（官方源不在 GitHub）。真实采用证据：
  **libvips**（11697★）、**OBS Studio**（76826★）、**GEGL**、**NetSurf 浏览器** 均内置。
  维护者 Michael Drake 是 NetSurf 核心开发者
- **用途**: 批次 5 `stickerstore` 在 Qt3 侧的 GIF 多帧解码（Qt 3.5.0 只有 `libqmng.so`
  + `libqjpeg.so`，实测 gif 解码失败）。GIF **编码**侧沿用现有 `vendor/tangora_gif.h`
  （纯编码器，`GifBegin`/`GifWriteFrame`/`GifEnd`）。见 `qlstik/移植计划.md` §6.3k

## stb_image（含 GIF 动画，评估参考）

- **Name**: nothings/stb
- **File**: `uc_apng_loader/stb_image.h` 已入库（v2.30，public domain）
- **API（GIF 动画）**: `stbi_load_gif_from_memory(buf, len, int **delays, int *x, int *y,
  int *z, int *comp, int req_comp)`，其中 `*z` 是帧数，`**delays` 返回每帧时长
  数组（**毫秒**）。返回值是紧凑排列的 `frames × w × h × 4` RGBA，两个 buffer
  均须用 `stbi_image_free()` 释放，链接需 `-lm`
- **实测对照**（与 libnsgif/PIL）：像素数据完全一致（包括 `disposal=2` 的透明处理）。
  **唯一差异**：delay 单位 —— libnsgif 返回 GIF 原始 10ms 单位（GCE 两字节小端÷10），
  stb_image 返回**毫秒**
- **stb 维护者立场（重要）**：`nothings/stb` issue #1568（作者本人）明确表示
  _"The plan is to remove all support for animated gifs from stb_image and fork them
  into a separate library (which I personally will not be maintaining), because this
  has just been a source of headaches and it's really outside the original intent of
  stb_image."_ 此外 issue #1688（2024-09）还报告 disposal method 2/3 的处理有 PR
  未合并，说明动画 GIF 路径并非 stb 的长期维护重心
- **项目选型结论**：GIF 动图解码选用 **libnsgif**（NetSurf 官方、MIT、被 libvips/OBS/GEGL
  广泛采用，API 明确、维护活跃于 NetSurf 生态）。stb_image 的 GIF 动画实现**不选作主路线**，
  仅在此作为对照验证之用（已实测可用）

## openssh-sha1（OpenSSH openbsd-compat SHA-1，双实现并存备胎）

- **Name**: openssh/openssh-portable → `openbsd-compat/sha1.c`（`$OpenBSD: sha1.c,v 1.27
  2019/06/07 22:56:36 dtucker Exp $`，OpenBSD libc / BIND / libreSSL 同一血统）
- **Upstream**: https://github.com/openssh/openssh-portable/blob/master/openbsd-compat/sha1.c
  （2026-10-07 抓取）
- **License**: Steve Reid 100% Public Domain（原头注释）；OpenBSD 后续修改同公版声明
- **Files**:
  - `openssh-sha1/sha1_ossh.c`（去 OpenSSH 脚手架：删 `includes.h`/`WITH_OPENSSL` 门控/
    4 处 `DEF_WEAK`；字节序用编译器内建 `__BYTE_ORDER__`（不依赖 glibc `endian.h`，
    macOS/Android/BSD 通用）；`explicit_bzero` 改为自带 `sha1_explicit_bzero`（volatile
    写清零，不依赖 libc 声明——macOS 严格 C 模式下 `<string.h>` 不声明它））
  - `openssh-sha1/sha1_ossh.h`（裁剪：仅 `SHA1Init/Pad/Transform/Update/Final` 声明，
    删 End/File/FileChunk/Data 声明与 `__bounded__` 属性、htonl 宏；守卫 `__SHA1_OSSH_H`）
- **API 不同名、与现实现零符号冲突**: BSD 风格 `SHA1Init/SHA1Update/SHA1Final`（Final
  参数序 = digest 在前、`SHA1_CTX{state[5]; u_int64_t count; buffer[64]}`）v.s. 现仓
  `stikcommon/sha1.c`/`qlcomp/sha1.c` 的 `SHA1_Init/Update/Final`（ctx 在前、`count[2]`）。
  两套可同时编进同一产物不被调用、互不干扰。
- **改名原因**: `sha1.{c,h} -> sha1_ossh.{c,h}`。否则 qmake 对 `vendor/.../sha1.c`
  生成的 `./sha1.o` 与现存 `stikcommon`/`qlcomp` 两个 `sha1.o` 目标冲突
  （`qlstik/build-qt3/Makefile:1019`、`build-qt6/Makefile:20686`）；头文件同名 + 守卫
  不同会在同一 TU 双 include 时因 `struct SHA1_CTX` tag 重名而编译失败。
- **行为对比（本实现默认，无需任何宏）**:
  - OpenSSH 版：`SHA1Transform` **无条件**把输入 block `memcpy` 进栈 workspace 再算，
    **默认不会覆盖传入的内存**；无 static 存储，线程安全
  - qlcomp/sha1.c（与 stikcommon/sha1.c 逐字节相同，`diff -q` 已验；Steve Reid
    sea-to-sky 变体，用户指认源自 curl 的 vendored 副本）：未定义 `SHA1HANDSOFF` 时
    `SHA1_Transform` 把 const 输入强转**就地改写**——**默认会覆盖传入的内存**
- **用途**: 作为双实现并存的备胎**只入库并挂进 qlstik.pro 构建**（全 Qt3/Qt6 目标编译，
  不被任何源码调用），供后续切换/对照使用。现用实现仍是 `stikcommon`/`qlcomp` sha1
  （构建设 `-DSHA1HANDSOFF` 兜底覆盖行为）。
- **已验证**: `-std=gnu99/-std=gnu17 -Wall -Wextra -fsyntax-only` 零警告；独立驱动
  `/tmp/opencode/sha1ossh_probe.c`：FIPS 三向量（abc / "abcdb…" 56B / 1000000×"a"）全过、
  1000B 多块输入 hash 后逐字节回读不回写断言通过（2026-10-07）

## gumbo（HTML5 解析器，链接预览用）

- **Name**: gumbo-parser/gumbo-parser（Google gumbo-parser 的现代维护分支）
- **Version**: **0.14.1**（2026-09-27 hotfix；`CHANGES.md` 确认，兼容 WPT tree construction 测试）
- **Upstream**: https://codeberg.org/gumbo-parser/gumbo-parser
  （官方源在 Codeberg；GitHub mirror `gumbo-parser/gumbo-parser`）
- **License**: Apache-2.0（`gumbo/COPYING`）
- **来源**: 经 `gh-proxy.org` 前缀从 GitHub mirror 逐文件下载（GitHub 直连被阻断）
- **Files**（SHA256）:
  - 编译单元（12，清单取自上游 `meson.build` 的 `gumbo_src`）:
    - `gumbo/attribute.c` `8ef119b956d2f1cc89eddff49f3445aff3e066615c66bb1bf686a2106b215bd6`
    - `gumbo/char_ref.c` `dc336f658f2b879039e788e3f025cd1612300d5d75e8a15cfb76a620eeefcf96`
    - `gumbo/char_ref_gperf.c` `5ae87a5b94094a31442664fc7527106a545a4a014d8822b5b3de114abc8cf647`
    - `gumbo/error.c` `e4d777af9372483d9053e8722558acd9edec7c840087e7cd6d8ba8652e866a22`
    - `gumbo/parser.c` `ddf3050c3957ad40911c9f6d1f32369a3e4bf7766aafd1aff908696adcfd18b6`
    - `gumbo/string_buffer.c` `aa221dafc32d91aa8b367c819965a816c61e8884a6148f03372b445d2ed99b1b`
    - `gumbo/string_piece.c` `08a92fb76160f82e5b6a0a2229892057903ec4b76dbce08c8723b6479de1b0e9`
    - `gumbo/tag.c` `f3f432f6f1b29d16262fff6e65f5aef2f7933b6918b170a27d5bfe7121ae374d`
    - `gumbo/tokenizer.c` `fbac96299cadc1009f96528168829635a945566699870be0273a06f4d6698243`
    - `gumbo/utf8.c` `721d317237d4cf1be279b8037d7e6941fb90980c61a20771758ed6942e5af914`
    - `gumbo/util.c` `a475451b46362b9059a7a1c7322f110b165a34c17dd7603119151bc936b5a371`
    - `gumbo/vector.c` `5a41a60acc66e107cdd309749af556de06c0d22fb5a750e084f0e4c8cbd31177`
  - 公开头: `gumbo/gumbo.h` `50632920ec1a6efba811ecd88a19dedad1edf7056f665727100e68c8b93ad0b1`
    `gumbo/tag_enum.h` `b3477365ac929de00135fb244e02446a6b1e67dfc07537aa39242494b32be18b`
  - 内部头: `attribute.h` `char_ref.h` `error.h` `insertion_mode.h` `parser.h`
    `string_buffer.h` `string_piece.h` `tag_gperf.h` `tag_sizes.h` `tag_strings.h`
    `tokenizer.h` `tokenizer_states.h` `token_type.h` `utf8.h` `util.h` `vector.h`
  - `gumbo/COPYING`（取自上游 `doc/COPYING`）
    `c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4`
  - `gumbo/README.md` `35c435cd20b29c09ba03bf1b14a15e68b52842c9d4d3b44121795fd677945506`
  - 注：v0.14.1 已无 `replacement.h`/`macros.h`/`ascii.c`/`nokogiri_gumbo.h`（旧版遗留，无源码引用）
- **特性**: 纯 C99，**零外部依赖**（无 iconv/libxml2/expat）；不建 DOM 的流式 parse tree；
  全平台（Linux/macOS/Android）均可直接编译（已 `gcc -std=c99 -fsyntax-only` 12 单元全过），
  不依赖系统 libgumbo
- **为何用此版本**: Google 原 `google/gumbo-parser` 2016 起停维护；本分支为社区续维护，
  含 `<selectedcontent>`/处理指令等新规范支持与多处 UAF 修复
- **用途**: `qlappcom/linkpreview.cpp` 的 HTML5 链接预览解析（`og:`/`twitter:` meta、`<title>`）
- **集成方式**: **源码内嵌编译**（非预编译 .so、**不使用系统 libgumbo**）。
  qsktox 经 `qsktox/CMakeLists.txt` 的 `add_library(gumbo STATIC ...)`；qltox 经
  `include(../vendor/gumbo.pri)` 编入。三平台行为一致
- **限制**: 仅支持 UTF-8 输入（调用方已先用 `linkPreviewDecodeToUtf8` 转码）；
  不支持 DOM 可变操作（本项目只读遍历）

## cpp-httplib

- **Name**: yhirose/cpp-httplib
- **Version**: 0.64.0（Release tag `v0.64.0`，文件内 `CPPHTTPLIB_VERSION "0.64.0"` 与之核对一致）
- **Upstream**: https://github.com/yhirose/cpp-httplib
- **License**: MIT（见 `httplib.h` 文件头版权声明）
- **Files**:
  - `httplib.h` (SHA256: `805169049e6a6e9ba7729d7e01d14ced825029c88a277586602a6b38df4d5357`)
    来源：`https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.64.0/httplib.h`（经 gh-proxy.com 下载）
- **类型**: header-only 单文件（HTTP/HTTPS 客户端 + 服务端，C++11+）
- **验证**: `g++ -fsyntax-only -std=c++14 -I vendor` 包含单元通过
- **用途**: 预留（轻量 HTTP 客户端/服务端候选，不接入当前构建）
- **注意**:
  - HTTPS 需 OpenSSL/BoringSSL/wolfSSL，启用压缩需 zlib
  - 按需 `#include`（单文件约 860KB，编译期较重）
  - 与 Qt 线程/事件循环配合需留意（自带线程池）

## base58

- **Name**: luke-jr/libbase58（**当前采用**）
- **Version**: master @ 2023-05-06（`pushed_at: 2023-05-06T05:07:57Z`）
- **Upstream**: https://github.com/luke-jr/libbase58
- **License**: MIT（见上游 `COPYING`）
- **Files**:
  - `base58.c` (SHA256: `32df54c79fbc0d49ad998efef5bbf55fa0d236c615af097d4554fb4b50e3ec54`)
    来源：`https://raw.githubusercontent.com/luke-jr/libbase58/master/base58.c`
  - `libbase58.h` (SHA256: `2ec80c5190660207fef0ecdb72ebf9911a849c60732f6ccd3c342acc8971b6b6`)
    来源：`https://raw.githubusercontent.com/luke-jr/libbase58/master/libbase58.h`
  - 按上游原文件名存放（base58.c + libbase58.h 本就不同名），不建子目录
- **API**（C，`extern "C"` 兼容）:
  - `bool b58enc(char *b58, size_t *b58sz, const void *bin, size_t binsz)` —— 纯 Base58 编码
  - `bool b58tobin(void *bin, size_t *binsz, const char *b58, size_t b58sz)` —— 纯 Base58 解码（`*binsz` 传入缓冲大小，返回时为实际长度）
  - `int b58check(...)` / `bool b58check_enc(...)` —— Base58Check（仅此二者需要 SHA256）
  - `bool (*b58_sha256_impl)(void*, const void*, size_t)` —— 调用方必须提供 SHA256 实现（仅用 Encode/Decode 可不设）
- **验证**: `gcc -std=c99` 编译 + 回环测试通过：`"Hello World!"` → `2NEpo7TZRRrLZSi2U`（确定性，
  与已知正确值一致），5 次重复结果全同
- **用途**: 预留（如节点 ID/密钥类字符串编码），不接入当前构建
- **注意**:
  - 上游 `libbase58.h` 用到 `uint8_t` 但**未包含 `<stdint.h>`**（上游原貌缺陷）——调用方需在
    `#include "libbase58.h"` **之前**自行 `#include <stdint.h>`
  - 纯 Encode/Decode 不依赖任何外部库；Base58Check 需自备 `b58_sha256_impl`（未提供时不得调用 b58check）
  - `b58tobin` 的 `*binsz` 语义：输入为目标缓冲大小，输出为实际字节长（结果在缓冲“右对齐”，偏移 = 缓冲大小 - 实际长）

### 曾评估弃用：miguelmota/cpp-base58（记录备查）

- Upstream: https://github.com/miguelmota/cpp-base58 @ `72635ec1`（MIT）
- **弃用原因（实测缺陷）**: `base58encode` 内部 `unsigned char digits[len * 137 / 100]` 为
  **未初始化 VLA**，`digits[0]` 从未清零，导致**同输入多次运行输出不同**（实测 5 次运行 5 种结果，
  非确定性、不可复现）；`base58decode` 还存在额外前导 `0x00` 字节未裁剪问题
- 结论：不可作为可靠实现使用，已从 vendor 移除，留此记录避免将来误引

## pb_geoip（用 nanopb 解码 geoip.dat 的离线国家查询实现）

- **Name**: nanopb
- **Version**: **0.4.9.1**（`pb.h` 中 `NANOPB_VERSION`）
- **Upstream**: https://github.com/nanopb/nanopb/releases/tag/0.4.9.1
- **License**: zlib（`vendor/nanopb/LICENSE.txt`，另附 `AUTHORS.txt`）
- **Files**（上游原样，勿改）:
  - `pb_geoip/pb.h`、`pb_geoip/pb_common.c/.h`、`pb_geoip/pb_decode.c/.h`、`pb_geoip/pb_encode.c/.h`
- **本项目新增文件**:
  - `pb_geoip/geoip.proto`：与 **XTLS/Xray-core v26.11.9** 一致的 geodata schema
    （`GeoIPList{ repeated GeoIP entry=1 }` / `GeoIP{ string country_code=1; repeated CIDR cidr=2 }` /
    `CIDR{ bytes ip=1; uint32 prefix=2 }`），字段编号不可改
  - `pb_geoip/geoip.options`：`GeoIPList.entry`、`GeoIP.cidr` 设为 `FT_CALLBACK`（条目数上万，不能写死 max_count）；
    `GeoIP.country_code max_size: 8`、`CIDR.ip max_size: 16` 保持静态，便于回调内直接取值
  - `pb_geoip/geoip.pb.c/.h`：由 **nanopb_generator 0.4.9.1** 生成（文件头带版本号）
  - `pb_geoip/README.md`：版本/生成命令/更新步骤
  - `pb_geoip.pri`：`include(../../vendor/pb_geoip.pri)` 引入（注意要放在 `.pro` 的 `SOURCES =` **之后**，否则被覆盖）
- **依赖**: **无运行时依赖**（纯 C 实现，不是 Google protobuf 库）；仅重新生成 `geoip.pb.*` 时需开发机装
  Python `protobuf` 包与同版本 `nanopb_generator.py`
- **用途**: v2ray_suber 离线解析内核自带 `geoip.dat`，做 IP→国家 查询（不依赖系统 `geoiplookup`/`geoip-bin`）
- **验证**: `8.8.8.8→US`、`114.114.114.114→CN`、`1.1.1.1→AU`、`202.50.253.45→NL`、
  IPv6 `2606:4700::1111→CA`（实测于 v2ray_suber + Qt3 构建）

## android-ffmpeg/arm64-v8a（FFmpeg Android arm64 预编译库）

- **Name**: husen-hn/ffmpeg-android-binary
- **Upstream**: https://github.com/husen-hn/ffmpeg-android-binary
- **Release tag**: **v2.0.0** —— ⚠️ tag 号是仓库自己的版本号，**tag v2.0.0 内含的是 FFmpeg 7.1**
  （`include/libavformat/version.h` → `LIBAVFORMAT_VERSION_INT = 61.7.100`；so 由 NDK r26b 构建、
  `min API 21`、`aarch64` 已用 `file` 核实、SONAME 无版本后缀如 `libavformat.so`，Android 友好）
- **License**: FFmpeg 本体 LGPL-2.1+（该构建未显式 enable-gpl；动态链接分发需遵守 LGPL 条款，
  注意保留版权声明并提供可重链途径）
- **Files**（入库布局，qskvotube CMake/androiddeployqt 消费）:
  - `lib/libavformat.so libavcodec.so libavutil.so libswscale.so libswresample.so`
    （另含 libavdevice/libavfilter/libpostproc，cxvideo 未链接但随包保留）
  - `include/libav{format,codec,util}/ include/libsw{scale,resample}/`（头文件与 so 同版本）
- **头文件来源**: ffmpeg.org `ffmpeg-7.1.tar.bz2` 解包 `libav*/libsw*/*.h`（so 包内不含头文件）
- **sha256**（`lib/`）:
  - libavcodec.so   `7a4e6126ccd997ded8cac850a6d0e2520fd313255bb05d46d4aaae5fe7cd9f4e`
  - libavdevice.so  `7d578d0e2240319710b0da1fcaa6721652ebfc6f3e899d17eeb5eba9646cfb33`
  - libavfilter.so  `cc544eaabc812856ac5673508c64cdc402604fe70bb35f2f992fe78c293f8be2`
  - libavformat.so  `5693bcb612f2f388a6d8f55f2cfed9ec6f7435fc329e1497d166d5aef5ac4a6d`
  - libavutil.so    `0bd26463e37655d8d4f6204cca67681cd5cc33135c5fec4c576bca2ef03eca41`
  - libpostproc.so  `084b13ba10d4b9c91d376a0daf9abc8c28492651885ff2ad94c34e7e0f383f15`
  - libswresample.so `30044f9fabcf896b2609f1cf7ca94ce1a7b58a7f97bc684d3d34199d94d3ee16`
  - libswscale.so   `f3be8f84659070b41c32c85fa884156febf505fd99ab28c32ae0061a8559aeba`
- **重取方法**（慢/失败时加 `https://gh-proxy.com/` 前缀）:
  1. `curl -L https://gh-proxy.com/https://github.com/husen-hn/ffmpeg-android-binary/releases/download/v2.0.0/arm64-v8a.zip`
  2. 解包 zip 内 `arm64-v8a/lib/*.so` → `vendor/android-ffmpeg/arm64-v8a/lib/`
  3. `curl -L https://ffmpeg.org/releases/ffmpeg-7.1.tar.bz2` 解包，拷 `libav*/libsw*/*.h`
     → `vendor/android-ffmpeg/arm64-v8a/include/<库名>/`
- **选型记录**（2026-11 调研）:
  - arthenica/ffmpeg-kit 已退役（2025），GitHub release 二进制已全部删除（API 实测 assets=0）
  - hzw1199/Android-FFmpeg-Prebuilt：单文件 `libffmpeg.so`（全部合链），cxvideo 需要分库链接不匹配
  - husen-hn/ffmpeg-android-binary：分库 .so + ffmpeg/ffprobe 可执行 + 全 codec/MediaCodec/NEON，
    与 cxvideo 的 `PkgConfig::CXV_FFMPEG` 分库链接需求吻合 → 入选
- **消费方**: `qskvotube/build-android.sh`（fail-fast 校验 lib 清单）+
  `qskvotube/CMakeLists.txt` ANDROID 分支（`$ENV{CXV_FFMPEG_ARM64_DIR}` 或本目录，IMPORTED 链接）
- **桌面无关**: 桌面构建走 pkg-config 系统库，不消费本目录
- **验证**: 头文件 `avformat_network_init()` g++ -fsyntax-only 通过；桌面 FFmpeg 为 n9.0.2（API 63），
  Android so 为 7.1（API 61/59），cxvideo 源码按 4.x+ 兼容 API 编写，交叉编译期如遇 API 差异按报错适配

## android-libmpv/libmpv.so（libmpv Android arm64 预编译库）

- **Name**: media-kit/libmpv-android-video-build
- **Upstream**: https://github.com/media-kit/libmpv-android-video-build
- **Release tag**: **v1.1.11**（asset `default-arm64-v8a.jar`,2026-01-27 发布;
  jar 内 `lib/arm64-v8a/libmpv.so` 解出）
- **License**: LGPL-2.1+（mpv 本体;构建未含 GPL 组件的 default 变体）
- **核实**（2026-11-13 `file`/`objdump`）: aarch64、min API 21、NDK r25c 构建、
  stripped;**NEDED 仅系统库**(libm/libandroid/libOpenSLES/libEGL/libdl/libc)——
  ffmpeg 静态内嵌于 libmpv.so,无第三方 so 依赖,eng_mpv dlopen 即用
- **sha256**: `4a77ebff52bec1bd06feefd588ea4c26f2ef604ad70c780fdc2279d6c9802e31`
- **重取方法**（慢/失败时加 `https://gh-proxy.com/` 前缀）:
  `curl -L https://gh-proxy.com/https://github.com/media-kit/libmpv-android-video-build/releases/download/v1.1.11/default-arm64-v8a.jar`
  → `unzip lib/arm64-v8a/libmpv.so` → `vendor/android-libmpv/`
- **消费方**: `qskvotube/build-android.sh`（`CXV_MPV_ARM64_SO` 缺省即本文件,
  自动拷进 APK `libs/arm64-v8a/`;eng_mpv 运行期 dlopen "libmpv.so"）
- **选型记录**: mpv-android 官方发布形态是 APK 非 so;media-kit 是 Flutter
  media_kit 生态的维护仓库,jar 直接含 libmpv.so,自包含 → 入选
- **桌面无关**: 桌面 mpv 引擎走系统 dlopen 的宿主 libmpv,不消费本目录
| wjcryptlib | https://github.com/WaterJuice/WjCryptLib (Rc4 单文件) | UNLICENSE(公有领域) | xhs 签名器 b1 指纹 RC4;详见 wjcryptlib/README.md |
| zlib-crc32 | https://github.com/madler/zlib v1.2.11 crc32 逐字节路径移植 | zlib license | 无 zlib 平台兜底(x9=crc32);有 zlib 直接调系统 crc32();详见 zlib-crc32/README.md |
