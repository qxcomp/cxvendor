/* stb_image v2.30 实现体唯一落点，与 stb_image.h 并列。
 * 从 qimagereader_shim.cpp 抽出，避免 qlvotube 复用时同链两份实现体导致重复符号。
 * 以 C 编译 → STBI_EXTERN=extern（C linkage）；C++ 侧只 include 头拿 extern "C" 声明。
 * 任何消费方只 include "stb_image.h"，不要再定义 STB_IMAGE_IMPLEMENTATION。 */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
