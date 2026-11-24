#!/usr/bin/env bash
# tools/zigcc-env.sh - 在当前 bash 终端初始化 / 精确还原 Zig cc 编译环境
#
# 用法（必须 source；直接执行无法修改当前终端的环境）：
#   source tools/zigcc-env.sh init [别名|完整triple]   # 无参 = 本地原生
#   source tools/zigcc-env.sh reset                   # 还原到 init 之前
#   source tools/zigcc-env.sh status
#   source tools/zigcc-env.sh list
#   source tools/zigcc-env.sh help
#
# 别名: (无参)/local/native/host            -> 本地原生（不加 -target）
#       win64/x64       -> x86_64-windows-gnu
#       win32/x86       -> x86-windows-gnu
#       winarm64        -> aarch64-windows-gnu
#       linux64/linux32 -> x86_64-linux-gnu / x86-linux-gnu
#       linux64-musl/linux32-musl -> x86_64-linux-musl / x86-linux-musl
#       linuxarm64/linuxarm -> aarch64-linux-gnu / arm-linux-gnueabihf
#       mac64/macarm64  -> x86_64-macos-none / aarch64-macos-none
#       wasm32          -> wasm32-wasi-musl
#
# Zig 定位顺序: ZIG_EXE > PATH 中的 zig > /opt/zig/zig > /usr/local/bin/zig > /usr/bin/zig
# 交叉模式会清空 pkg-config 搜索路径；需要时先设 ZIGCC_PKG_CONFIG_LIBDIR。
# CMake 交叉仍建议使用专用 toolchain 文件；ZIGCC_CMAKE_ARGS 仅作提示。

# ---------- 只允许 source ----------
( return 0 2>/dev/null ) || {
    printf 'zigcc-env: 请用 source 加载本脚本，例如: source %s init\n' "${BASH_SOURCE[0]}" >&2
    exit 1
}

# ---------- 所有受管变量（全部进入快照 / 还原） ----------
__zigcc_define_managed() {
    declare -ga __ZIGCC_ENV_MANAGED=(
        CC CXX AR RANLIB OBJCOPY OBJDUMP DLLTOOL LIB WINDRES RC
        ZIG_EXE ZIGCC_TARGET ZIGCC_MODE ZIGCC_HOST CHOST
        PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
        ZIGCC_CMAKE_ARGS
    )
}

# ---------- 快照：记录 值 / 是否原本未设置 / 是否原本已导出 ----------
__zigcc_snapshot() {
    local v
    declare -gA __ZIGCC_ENV_SAVED_VALUE=() __ZIGCC_ENV_SAVED_EXPORTED=()
    declare -ga __ZIGCC_ENV_SAVED_NAMES=()
    for v in "${__ZIGCC_ENV_MANAGED[@]}"; do
        if [[ -v $v ]]; then
            __ZIGCC_ENV_SAVED_VALUE[$v]=${!v}
            if [[ ${!v@a} == *x* ]]; then
                __ZIGCC_ENV_SAVED_EXPORTED[$v]=1
            else
                __ZIGCC_ENV_SAVED_EXPORTED[$v]=0
            fi
        else
            __ZIGCC_ENV_SAVED_EXPORTED[$v]=-1
        fi
        __ZIGCC_ENV_SAVED_NAMES+=("$v")
    done
}

# ---------- 单变量还原：-1 未设置 / 0 普通变量 / 1 已导出 ----------
__zigcc_restore_one() {
    local v=$1 state val
    state=${__ZIGCC_ENV_SAVED_EXPORTED[$v]:--1}
    if [[ $state == -1 ]]; then
        unset -v "$v"
        return 0
    fi
    val=${__ZIGCC_ENV_SAVED_VALUE[$v]}
    unset -v "$v"
    if [[ $state == 1 ]]; then
        export "$v=$val"
    else
        printf -v "$v" '%s' "$val"
    fi
}

__zigcc_do_reset() {
    local i
    if [[ ${__ZIGCC_ENV_ACTIVE:-0} == 1 ]]; then
        for (( i=${#__ZIGCC_ENV_SAVED_NAMES[@]} - 1; i >= 0; i-- )); do
            __zigcc_restore_one "${__ZIGCC_ENV_SAVED_NAMES[$i]}"
        done
    fi
    # 即使未激活也清理内部变量，避免 status/list/reset 留下残留
    unset -v __ZIGCC_ENV_SAVED_NAMES __ZIGCC_ENV_SAVED_VALUE __ZIGCC_ENV_SAVED_EXPORTED \
             __ZIGCC_ENV_MANAGED __ZIGCC_ENV_ACTIVE __ZIGCC_ENV_TARGET
    return 0
}

# ---------- 设置并导出 ----------
__zigcc_set() {
    local v=$1 val=$2
    printf -v "$v" '%s' "$val"
    export "$v"
}

# ---------- 别名解析；原生返回空串 ----------
__zigcc_normalize() {
    case $1 in
        ''|local|native|host)          printf '%s\n' '' ;;
        win64|x64)                     printf '%s\n' x86_64-windows-gnu ;;
        win32|x86)                     printf '%s\n' x86-windows-gnu ;;
        winarm64)                      printf '%s\n' aarch64-windows-gnu ;;
        linux64)                       printf '%s\n' x86_64-linux-gnu ;;
        linux32)                       printf '%s\n' x86-linux-gnu ;;
        linux64-musl)                  printf '%s\n' x86_64-linux-musl ;;
        linux32-musl)                  printf '%s\n' x86-linux-musl ;;
        linuxarm64)                    printf '%s\n' aarch64-linux-gnu ;;
        linuxarm)                      printf '%s\n' arm-linux-gnueabihf ;;
        mac64)                         printf '%s\n' x86_64-macos-none ;;
        macarm64)                      printf '%s\n' aarch64-macos-none ;;
        wasm32)                        printf '%s\n' wasm32-wasi-musl ;;
        *)                             printf '%s\n' "$1" ;;
    esac
}

# ---------- Zig 定位 ----------
__zigcc_find_zig() {
    local z
    if [[ -n ${ZIG_EXE:-} && -x ${ZIG_EXE} ]]; then
        printf '%s\n' "$ZIG_EXE"
        return 0
    fi
    if z=$(command -v zig 2>/dev/null) && [[ -x $z ]]; then
        printf '%s\n' "$z"
        return 0
    fi
    if [[ -x /opt/zig/zig ]]; then
        printf '%s\n' /opt/zig/zig
        return 0
    fi
    for z in /usr/local/bin/zig /usr/bin/zig; do
        if [[ -x $z ]]; then
            printf '%s\n' "$z"
            return 0
        fi
    done
    return 1
}

# ---------- CMake 名称推导 ----------
__zigcc_cmake_system() {
    case $1 in
        *-windows-*)   printf '%s\n' Windows ;;
        *-linux-*)     printf '%s\n' Linux ;;
        *-macos-*)     printf '%s\n' Darwin ;;
        wasm32-*)      printf '%s\n' None ;;
        *)             printf '%s\n' "${1#*-}" ;;
    esac
}

__zigcc_cmake_cpu() {
    case $1 in
        x86_64-*)          printf '%s\n' x86_64 ;;
        x86-*)             printf '%s\n' x86 ;;
        aarch64-*|arm64-*) printf '%s\n' aarch64 ;;
        arm-*)             printf '%s\n' arm ;;
        wasm32-*)          printf '%s\n' wasm32 ;;
        *)                 printf '%s\n' "${1%%-*}" ;;
    esac
}

__zigcc_list() {
    cat <<'EOF'
别名                  triple
  local/native/host     (本地原生)
  win64/x64            x86_64-windows-gnu
  win32/x86            x86-windows-gnu
  winarm64             aarch64-windows-gnu
  linux64              x86_64-linux-gnu
  linux32              x86-linux-gnu
  linux64-musl         x86_64-linux-musl
  linux32-musl         x86-linux-musl
  linuxarm64           aarch64-linux-gnu
  linuxarm             arm-linux-gnueabihf
  mac64                x86_64-macos-none
  macarm64             aarch64-macos-none
  wasm32               wasm32-wasi-musl
也可直接传 zig targets 中的完整 triple。
EOF
}

# ---------- init ----------
__zigcc_init() {
    local arg=${1:-} zig t mode zigdir sys cpu

    __zigcc_do_reset

    if ! zig=$(__zigcc_find_zig); then
        printf 'zigcc-env: 找不到 zig，可用 ZIG_EXE 指定\n' >&2
        return 1
    fi

    t=$(__zigcc_normalize "$arg")
    if [[ -z $t ]]; then
        mode=native
    else
        mode=cross
        if ! "$zig" targets 2>/dev/null | grep -q "\"$t\""; then
            printf 'zigcc-env: zig 不认识目标 %s，可用别名:\n' "$t" >&2
            __zigcc_list >&2
            return 1
        fi
    fi

    __zigcc_define_managed
    __zigcc_snapshot

    zigdir=${zig%/*}
    __zigcc_set ZIG_EXE "$zig"
    __zigcc_set ZIGCC_MODE "$mode"
    __zigcc_set PATH "$zigdir:$PATH"

    __zigcc_set AR "$zig ar"
    __zigcc_set RANLIB "$zig ranlib"
    __zigcc_set OBJCOPY "$zig objcopy"
    __zigcc_set OBJDUMP "$zig objdump"
    __zigcc_set DLLTOOL "$zig dlltool"
    __zigcc_set LIB "$zig lib"
    __zigcc_set WINDRES "$zig rc"
    __zigcc_set RC "$zig rc"

    if [[ $mode == native ]]; then
        unset -v ZIGCC_TARGET ZIGCC_HOST CHOST
        __zigcc_set CC "$zig cc"
        __zigcc_set CXX "$zig c++"
    else
        __zigcc_set ZIGCC_TARGET "$t"
        __zigcc_set ZIGCC_HOST "$t"
        __zigcc_set CHOST "$t"
        __zigcc_set CC "$zig cc -target $t"
        __zigcc_set CXX "$zig c++ -target $t"

        # 交叉模式隔离本机 .pc，防止误链宿主机库
        if [[ -n ${ZIGCC_PKG_CONFIG_LIBDIR:-} ]]; then
            __zigcc_set PKG_CONFIG_LIBDIR "$ZIGCC_PKG_CONFIG_LIBDIR"
            unset -v PKG_CONFIG_SYSROOT_DIR
        else
            unset -v PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
        fi
    fi

    if [[ $mode == native ]]; then
        __zigcc_set ZIGCC_CMAKE_ARGS \
            "-DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX -DCMAKE_AR=$AR -DCMAKE_RANLIB=$RANLIB"
    else
        sys=$(__zigcc_cmake_system "$t")
        cpu=$(__zigcc_cmake_cpu "$t")
        __zigcc_set ZIGCC_CMAKE_ARGS \
            "-DCMAKE_SYSTEM_NAME=$sys -DCMAKE_SYSTEM_PROCESSOR=$cpu -DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX -DCMAKE_AR=$AR -DCMAKE_RANLIB=$RANLIB"
    fi

    __ZIGCC_ENV_ACTIVE=1
    __ZIGCC_ENV_TARGET=$t

    printf 'zigcc-env: %s (%s)\n' "${t:-本地原生}" "$mode"
    printf '  zig:    %s\n  CC:     %s\n  CXX:    %s\n  AR:     %s\n' \
        "$zig" "$CC" "$CXX" "$AR"
    return 0
}

__zigcc_status() {
    if [[ ${__ZIGCC_ENV_ACTIVE:-0} != 1 ]]; then
        printf 'zigcc-env: 未激活（当前是本机环境）\n'
        return 0
    fi
    printf 'zigcc-env: 已激活\n  mode:   %s\n  target: %s\n  zig:    %s\n  CC:     %s\n  CXX:    %s\n' \
        "$ZIGCC_MODE" "${ZIGCC_TARGET:-<native>}" "$ZIG_EXE" "$CC" "$CXX"
}

__zigcc_help() {
    cat <<'EOF'
用法: source tools/zigcc-env.sh <命令> [参数]
  init [别名|完整triple]  初始化环境；无参 = 本地原生
  reset                   精确还原到 init 之前的当前终端环境
  status                  查看当前是否激活及目标
  list                    列出可用别名
  help                    显示本帮助

示例:
  source tools/zigcc-env.sh init          # 本地
  source tools/zigcc-env.sh init win64    # 交叉到 x86_64-windows-gnu
  $CC hello.c -o hello.exe
  source tools/zigcc-env.sh reset
EOF
}

case ${1:-help} in
    init)           shift; __zigcc_init "$@" ;;
    reset)          __zigcc_do_reset && printf 'zigcc-env: 已还原到 init 前的环境\n' ;;
    status)         __zigcc_status ;;
    list)           __zigcc_list ;;
    help|-h|--help) __zigcc_help ;;
    *)
        printf 'zigcc-env: 未知命令 %s\n\n' "$1" >&2
        __zigcc_help >&2
        return 1
        ;;
esac
