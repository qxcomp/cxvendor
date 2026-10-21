# gumbo.cmake — HTML5 解析器（vendor 源码，全平台统一，不使用系统 libgumbo）
# Usage: include(${CMAKE_CURRENT_SOURCE_DIR}/../vendor/gumbo.cmake)
# 依赖调用方已定义目标 qsktox，并已 target_link_libraries(qsktox PRIVATE gumbo)。

set(gumbo_src_dir ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/gumbo)

add_library(gumbo STATIC
    ${gumbo_src_dir}/attribute.c
    ${gumbo_src_dir}/char_ref.c
    ${gumbo_src_dir}/char_ref_gperf.c
    ${gumbo_src_dir}/error.c
    ${gumbo_src_dir}/parser.c
    ${gumbo_src_dir}/string_buffer.c
    ${gumbo_src_dir}/string_piece.c
    ${gumbo_src_dir}/tag.c
    ${gumbo_src_dir}/tokenizer.c
    ${gumbo_src_dir}/utf8.c
    ${gumbo_src_dir}/util.c
    ${gumbo_src_dir}/vector.c)

target_include_directories(gumbo PUBLIC ${gumbo_src_dir})

target_link_libraries(qsktox PRIVATE gumbo)
