#!/usr/bin/env bash
# tools/mingw-env.sh - 在当前 bash 终端初始化 / 精确还原 mingw 交叉编译环境
#
# 用法（必须 source；直接执行无法修改当前终端的环境）：
#   source tools/mingw-env.sh init [triplet|别名]   # 默认 x86_64-w64-mingw32
#   source tools/mingw-env.sh reset                # 还原到 init 之前
#   source tools/mingw-env.sh status
#   source tools/mingw-env.sh list
#   source tools/mingw-env.sh help
#
# 别名: x64/x86_64/win64/amd64 -> x86_64-w64-mingw32
#       x86/win32/i686         -> i686-w64-mingw32
#       arm/armv7              -> armv7-w64-mingw32
#       arm64/aarch64          -> aarch64-w64-mingw32
#       arm64ec                -> arm64ec-w64-mingw32
#       带 uwp 的 triplet 原样使用
#
# ROOT 探测顺序: MINGW_ROOT > /opt/mingw > /opt/local > /usr > PATH
# autotools 仍需显式传 ./configure --host="$MINGW_HOST"
# CMake 需新建 build 目录；CMAKE_TOOLCHAIN_FILE 环境变量仅 CMake >= 3.21 支持，
# 且只在首次创建 build 树时生效。

# ---------- 只允许 source ----------
( return 0 2>/dev/null ) || {
    printf 'mingw-env: 请用 source 加载本脚本，例如: source %s init\n' "${BASH_SOURCE[0]}" >&2
    exit 1
}

# ---------- 所有受管变量（全部进入快照 / 还原） ----------
__mingw_define_managed() {
    declare -ga __MINGW_ENV_MANAGED=(
        CC CXX AR RANLIB STRIP LD AS NM OBJCOPY OBJDUMP READELF SIZE STRINGS ADDR2LINE
        DLLTOOL WINDRES WIDL RC
        CROSS_COMPILE CHOST MINGW_HOST SYSROOT PATH
        PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
        CMAKE_PREFIX_PATH CMAKE_TOOLCHAIN_FILE MINGW_CMAKE_ARGS
        MINGW_ROOT MINGW_TRIPLET MINGW_TOOLCHAIN_FILE
    )
}
__mingw_define_managed

# ---------- 快照：记录 值 / 是否原本未设置 / 是否原本已导出 ----------
__mingw_snapshot() {
    local v
    declare -gA __MINGW_ENV_SAVED_VALUE=() __MINGW_ENV_SAVED_EXPORTED=()
    declare -ga __MINGW_ENV_SAVED_NAMES=()
    for v in "${__MINGW_ENV_MANAGED[@]}"; do
        if [[ -v $v ]]; then
            __MINGW_ENV_SAVED_VALUE[$v]=${!v}
            if [[ ${!v@a} == *x* ]]; then
                __MINGW_ENV_SAVED_EXPORTED[$v]=1
            else
                __MINGW_ENV_SAVED_EXPORTED[$v]=0
            fi
        else
            __MINGW_ENV_SAVED_EXPORTED[$v]=-1
        fi
        __MINGW_ENV_SAVED_NAMES+=("$v")
    done
}

# ---------- 单变量还原：-1 未设置 / 0 普通变量 / 1 已导出 ----------
__mingw_restore_one() {
    local v=$1 state val
    state=${__MINGW_ENV_SAVED_EXPORTED[$v]:--1}
    if [[ $state == -1 ]]; then
        unset -v "$v"
        return 0
    fi
    val=${__MINGW_ENV_SAVED_VALUE[$v]}
    unset -v "$v"
    if [[ $state == 1 ]]; then
        export "$v=$val"
    else
        printf -v "$v" '%s' "$val"
    fi
}

__mingw_do_reset() {
    local i
    if [[ ${__MINGW_ENV_ACTIVE:-0} == 1 ]]; then
        for (( i=${#__MINGW_ENV_SAVED_NAMES[@]} - 1; i >= 0; i-- )); do
            __mingw_restore_one "${__MINGW_ENV_SAVED_NAMES[$i]}"
        done
    fi
    # 即使未激活也要清理：脚本被 source 时顶层会重建受管清单
    unset -v __MINGW_ENV_SAVED_NAMES __MINGW_ENV_SAVED_VALUE __MINGW_ENV_SAVED_EXPORTED \
             __MINGW_ENV_MANAGED __MINGW_ENV_ACTIVE __MINGW_ENV_TRIPLET \
             __MINGW_ENV_ROOT __MINGW_ENV_BIN __MINGW_ENV_SYSROOT
    return 0
}

# ---------- 设置并导出 ----------
__mingw_set() {
    local v=$1 val=$2
    printf -v "$v" '%s' "$val"
    export "$v"
}

# ---------- triplet 别名 / CMake CPU 名称 ----------
__mingw_normalize() {
    case $1 in
        x64|x86_64|win64|amd64) printf '%s\n' x86_64-w64-mingw32 ;;
        x86|win32|i686)         printf '%s\n' i686-w64-mingw32 ;;
        arm|armv7)              printf '%s\n' armv7-w64-mingw32 ;;
        arm64|aarch64)          printf '%s\n' aarch64-w64-mingw32 ;;
        arm64ec)                printf '%s\n' arm64ec-w64-mingw32 ;;
        *)                      printf '%s\n' "$1" ;;
    esac
}

__mingw_cmake_cpu() {
    case $1 in
        x86_64-*)             printf '%s\n' x86_64 ;;
        i686-*)               printf '%s\n' x86 ;;
        armv7-*)              printf '%s\n' arm ;;
        aarch64-*|arm64ec-*)  printf '%s\n' aarch64 ;;
        *)                    printf '%s\n' "${1%%-*}" ;;
    esac
}

# ---------- 工具链根目录探测 ----------
__mingw_find_root() {
    local t=$1 p
    if [[ -n ${MINGW_ROOT:-} && -x ${MINGW_ROOT}/bin/${t}-gcc ]]; then
        printf '%s\n' "$MINGW_ROOT"
        return 0
    fi
    if [[ -x /opt/mingw/bin/${t}-gcc ]]; then
        printf '%s\n' /opt/mingw
        return 0
    fi
    if [[ -x /opt/local/bin/${t}-gcc ]]; then
        printf '%s\n' /opt/local
        return 0
    fi
    if [[ -x /usr/bin/${t}-gcc ]]; then
        printf '%s\n' /usr
        return 0
    fi
    local IFS=:
    for p in $PATH; do
        if [[ -n $p && -x $p/${t}-gcc ]]; then
            printf '%s\n' "${p%/bin}"
            return 0
        fi
    done
    return 1
}

__mingw_list() {
    local root bin f t
    for root in "${MINGW_ROOT:-}" /opt/mingw /opt/local /usr; do
        [[ -n $root ]] || continue
        bin=$root/bin
        [[ -d $bin ]] || continue
        for f in "$bin"/*-gcc; do
            [[ -x $f ]] || continue
            t=${f##*/}
            t=${t%-gcc}
            printf '%-24s %s\n' "$t" "$root"
        done
    done | sort -u
}

__mingw_tool_suffix() {
    case $1 in
        AR)        printf '%s\n' ar ;;
        RANLIB)    printf '%s\n' ranlib ;;
        STRIP)     printf '%s\n' strip ;;
        LD)        printf '%s\n' ld ;;
        AS)        printf '%s\n' as ;;
        NM)        printf '%s\n' nm ;;
        OBJCOPY)   printf '%s\n' objcopy ;;
        OBJDUMP)   printf '%s\n' objdump ;;
        READELF)   printf '%s\n' readelf ;;
        SIZE)      printf '%s\n' size ;;
        STRINGS)   printf '%s\n' strings ;;
        ADDR2LINE) printf '%s\n' addr2line ;;
        DLLTOOL)   printf '%s\n' dlltool ;;
        WINDRES)   printf '%s\n' windres ;;
        WIDL)      printf '%s\n' widl ;;
    esac
}

# ---------- init ----------
__mingw_init() {
    local arg=${1:-} t root bin sysroot cc cxx suffix v pcdirs cmake_cpu

    # 重复 init 先还原，保证 PATH 不叠加、快照不被覆盖
    __mingw_do_reset
    # reset 会清掉受管清单，这里重建后再快照
    __mingw_define_managed

    t=$(__mingw_normalize "${arg:-${MINGW_TRIPLET:-x86_64-w64-mingw32}}")
    if ! root=$(__mingw_find_root "$t"); then
        printf 'mingw-env: 找不到 triplet %s 的工具链，可用:\n' "$t" >&2
        __mingw_list >&2
        return 1
    fi

    bin=$root/bin
    if [[ $root == /usr ]]; then
        sysroot=/usr/$t
    else
        sysroot=$root/$t
    fi
    [[ -d $sysroot ]] || sysroot=$root

    # llvm-mingw 的 *-gcc / *-g++ 是 clang-target-wrapper.sh，必须带 triplet 使用
    cc=$bin/$t-gcc
    [[ -x $cc ]] || cc=$bin/$t-clang
    cxx=$bin/$t-g++
    [[ -x $cxx ]] || cxx=$bin/$t-clang++

    __mingw_snapshot

    __mingw_set CC "$cc"
    __mingw_set CXX "$cxx"

    for v in AR RANLIB STRIP LD AS NM OBJCOPY OBJDUMP READELF SIZE STRINGS \
             ADDR2LINE DLLTOOL WINDRES WIDL; do
        suffix=$(__mingw_tool_suffix "$v")
        if [[ -x $bin/$t-$suffix ]]; then
            __mingw_set "$v" "$bin/$t-$suffix"
        else
            unset -v "$v"
        fi
    done
    if [[ -x $bin/$t-windres ]]; then
        __mingw_set RC "$bin/$t-windres"
    else
        unset -v RC
    fi

    __mingw_set CROSS_COMPILE "$bin/$t-"
    __mingw_set CHOST "$t"
    __mingw_set MINGW_HOST "$t"
    __mingw_set SYSROOT "$sysroot"
    __mingw_set PATH "$bin:$PATH"
    __mingw_set MINGW_ROOT "$root"
    __mingw_set MINGW_TRIPLET "$t"

    # pkg-config：只在目标 sysroot 真有 .pc 目录时隔离，绝不回落到本机 .pc
    pcdirs=
    [[ -d $sysroot/lib/pkgconfig ]]   && pcdirs=$sysroot/lib/pkgconfig
    [[ -d $sysroot/share/pkgconfig ]] && pcdirs=${pcdirs:+$pcdirs:}$sysroot/share/pkgconfig
    if [[ -n $pcdirs ]]; then
        __mingw_set PKG_CONFIG_LIBDIR "$pcdirs"
        __mingw_set PKG_CONFIG_SYSROOT_DIR "$sysroot"
    else
        unset -v PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
    fi

    # CMake 提示：CMAKE_PREFIX_PATH / CMAKE_TOOLCHAIN_FILE 会被 CMake 从环境读取；
    # 没有用户提供的 toolchain 文件时显式清空，避免残留影响首次 configure。
    __mingw_set CMAKE_PREFIX_PATH "$sysroot"
    if [[ -n ${MINGW_TOOLCHAIN_FILE:-} && -f ${MINGW_TOOLCHAIN_FILE} ]]; then
        __mingw_set CMAKE_TOOLCHAIN_FILE "$MINGW_TOOLCHAIN_FILE"
    else
        unset -v CMAKE_TOOLCHAIN_FILE
    fi
    cmake_cpu=$(__mingw_cmake_cpu "$t")
    __mingw_set MINGW_CMAKE_ARGS \
        "-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=$cmake_cpu -DCMAKE_C_COMPILER=$cc -DCMAKE_CXX_COMPILER=$cxx"

    __MINGW_ENV_ACTIVE=1
    __MINGW_ENV_TRIPLET=$t
    __MINGW_ENV_ROOT=$root
    __MINGW_ENV_BIN=$bin
    __MINGW_ENV_SYSROOT=$sysroot

    printf 'mingw-env: %s (%s)\n' "$t" "$root"
    printf '  CC=%s\n  CXX=%s\n  AR=%s\n  WINDRES=%s\n  SYSROOT=%s\n' \
        "$CC" "$CXX" "${AR:-<none>}" "${WINDRES:-<none>}" "$SYSROOT"
    printf '  CMake: cmake -S . -B build-mingw %s\n' "$MINGW_CMAKE_ARGS"
    return 0
}

__mingw_status() {
    if [[ ${__MINGW_ENV_ACTIVE:-0} != 1 ]]; then
        printf 'mingw-env: 未激活（当前是本机环境）\n'
        return 0
    fi
    printf 'mingw-env: 已激活\n  triplet: %s\n  root:    %s\n  CC:      %s\n  CXX:     %s\n  AR:      %s\n  WINDRES: %s\n' \
        "$__MINGW_ENV_TRIPLET" "$__MINGW_ENV_ROOT" "$CC" "$CXX" \
        "${AR:-<none>}" "${WINDRES:-<none>}"
}

__mingw_help() {
    cat <<'EOF'
用法: source tools/mingw-env.sh <命令> [参数]
  init [triplet|别名]  初始化 mingw 环境（默认 x86_64-w64-mingw32）
  reset                精确还原到 init 之前的当前终端环境
  status               查看当前是否激活及其 triplet/根路径
  list                 列出探测到的可用 triplet
  help                 显示本帮助

示例:
  source tools/mingw-env.sh init x64
  ./configure --host="$MINGW_HOST" && make
  cmake -S . -B build-mingw $MINGW_CMAKE_ARGS
  source tools/mingw-env.sh reset
EOF
}

case ${1:-help} in
    init)           shift; __mingw_init "$@" ;;
    reset)          __mingw_do_reset && printf 'mingw-env: 已还原到 init 前的环境\n' ;;
    status)         __mingw_status ;;
    list)           __mingw_list ;;
    help|-h|--help) __mingw_help ;;
    *)
        printf 'mingw-env: 未知命令 %s\n\n' "$1" >&2
        __mingw_help >&2
        return 1
        ;;
esac
