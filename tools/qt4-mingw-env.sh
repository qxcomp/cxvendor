#!/usr/bin/env bash
# tools/qt4-mingw-env.sh - 在当前 bash 终端初始化 / 精确还原
#                          Linux/macOS 上用 mingw 交叉构建 Qt4 库本身的环境
#
# 用法（必须 source；直接执行无法修改当前终端的环境）：
#   source tools/qt4-mingw-env.sh init [别名|完整triplet]   # 默认 x86_64-w64-mingw32
#   source tools/qt4-mingw-env.sh reset                    # 还原到 init 之前
#   source tools/qt4-mingw-env.sh status
#   source tools/qt4-mingw-env.sh list
#   source tools/qt4-mingw-env.sh help
#
# 别名: x64/x86_64/win64/amd64 -> x86_64-w64-mingw32
#       x86/win32/i686         -> i686-w64-mingw32
#       arm64/aarch64          -> aarch64-w64-mingw32
#       arm/armv7              -> armv7-w64-mingw32
#
# 目标为 Qt 4.8.7 源码（qt-everywhere-src-4.8.7）。
# Qt4.8.7 自带 win32-g++ mkspec 已支持 CROSS_COMPILE，无需自定义 mkspec。
# configure 自动用 host 编译器 bootstrap qmake/moc/uic/rcc，
# 再用交叉编译器构建 Qt 库；host spec 由 uname 判定，可用 QT4_HOST_SPEC 覆盖。
# 本脚本只准备环境变量与命令模板，不生成文件、不执行编译。

# ---------- 只允许 source ----------
( return 0 2>/dev/null ) || {
    printf 'qt4-mingw-env: 请用 source 加载本脚本，例如: source %s init\n' "${BASH_SOURCE[0]}" >&2
    exit 1
}

# ---------- 所有受管变量（全部进入快照 / 还原） ----------
__qt4mingw_define_managed() {
    declare -ga __QT4MINGW_ENV_MANAGED=(
        CC CXX AR RANLIB STRIP DLLTOOL WINDRES RC
        CROSS_COMPILE CHOST MINGW_HOST SYSROOT PATH
        QMAKESPEC QT4_HOST_SPEC QT4_TARGET_SPEC QT4_CROSS
        QMAKE_LFLAGS QMAKE_CFLAGS QMAKE_CXXFLAGS
        PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
        MINGW_ROOT MINGW_TRIPLET
        QT4_CONFIGURE_ARGS
    )
}

# ---------- 快照：记录 值 / 是否原本未设置 / 是否原本已导出 ----------
__qt4mingw_snapshot() {
    local v
    declare -gA __QT4MINGW_ENV_SAVED_VALUE=() __QT4MINGW_ENV_SAVED_EXPORTED=()
    declare -ga __QT4MINGW_ENV_SAVED_NAMES=()
    for v in "${__QT4MINGW_ENV_MANAGED[@]}"; do
        if [[ -v $v ]]; then
            __QT4MINGW_ENV_SAVED_VALUE[$v]=${!v}
            if [[ ${!v@a} == *x* ]]; then
                __QT4MINGW_ENV_SAVED_EXPORTED[$v]=1
            else
                __QT4MINGW_ENV_SAVED_EXPORTED[$v]=0
            fi
        else
            __QT4MINGW_ENV_SAVED_EXPORTED[$v]=-1
        fi
        __QT4MINGW_ENV_SAVED_NAMES+=("$v")
    done
}

# ---------- 单变量还原：-1 未设置 / 0 普通变量 / 1 已导出 ----------
__qt4mingw_restore_one() {
    local v=$1 state val
    state=${__QT4MINGW_ENV_SAVED_EXPORTED[$v]:--1}
    if [[ $state == -1 ]]; then
        unset -v "$v"
        return 0
    fi
    val=${__QT4MINGW_ENV_SAVED_VALUE[$v]}
    unset -v "$v"
    if [[ $state == 1 ]]; then
        export "$v=$val"
    else
        printf -v "$v" '%s' "$val"
    fi
}

__qt4mingw_do_reset() {
    local i
    if [[ ${__QT4MINGW_ENV_ACTIVE:-0} == 1 ]]; then
        for (( i=${#__QT4MINGW_ENV_SAVED_NAMES[@]} - 1; i >= 0; i-- )); do
            __qt4mingw_restore_one "${__QT4MINGW_ENV_SAVED_NAMES[$i]}"
        done
    fi
    # 即使未激活也清理内部变量，避免 status/list/reset 留下残留
    unset -v __QT4MINGW_ENV_SAVED_NAMES __QT4MINGW_ENV_SAVED_VALUE \
             __QT4MINGW_ENV_SAVED_EXPORTED __QT4MINGW_ENV_MANAGED \
             __QT4MINGW_ENV_ACTIVE __QT4MINGW_ENV_TRIPLET \
             __QT4MINGW_ENV_ROOT __QT4MINGW_ENV_HOST_SPEC
    return 0
}

# ---------- 设置并导出 ----------
__qt4mingw_set() {
    local v=$1 val=$2
    printf -v "$v" '%s' "$val"
    export "$v"
}

# ---------- triplet 别名 ----------
__qt4mingw_normalize() {
    case $1 in
        x64|x86_64|win64|amd64) printf '%s\n' x86_64-w64-mingw32 ;;
        x86|win32|i686)         printf '%s\n' i686-w64-mingw32 ;;
        arm64|aarch64)          printf '%s\n' aarch64-w64-mingw32 ;;
        arm|armv7)              printf '%s\n' armv7-w64-mingw32 ;;
        *)                      printf '%s\n' "$1" ;;
    esac
}

# ---------- host spec 判定 ----------
__qt4mingw_host_spec() {
    local h
    if [[ -n ${QT4_HOST_SPEC:-} ]]; then
        printf '%s\n' "$QT4_HOST_SPEC"
        return 0
    fi
    h=$(uname -s 2>/dev/null)
    case $h in
        Darwin) printf '%s\n' macx-g++ ;;
        Linux)  printf '%s\n' linux-g++ ;;
        MINGW*|MSYS*|CYGWIN*) printf '%s\n' win32-g++ ;;
        *)      printf '%s\n' linux-g++ ;;
    esac
}

# ---------- 工具链根目录探测（含 macOS Homebrew 布局） ----------
__qt4mingw_find_root() {
    local t=$1 p
    if [[ -n ${MINGW_ROOT:-} && -x ${MINGW_ROOT}/bin/${t}-gcc ]]; then
        printf '%s\n' "$MINGW_ROOT"; return 0
    fi
    if [[ -x /opt/mingw/bin/${t}-gcc ]]; then
        printf '%s\n' /opt/mingw; return 0
    fi
    if [[ -x /opt/local/bin/${t}-gcc ]]; then
        printf '%s\n' /opt/local; return 0
    fi
    # macOS: Homebrew 的 triplet-gcc 可能在 <prefix>/bin 或 <prefix>/<cellar>/bin
    for p in /opt/homebrew/opt/mingw-w64 /usr/local/opt/mingw-w64; do
        if [[ -x $p/bin/${t}-gcc ]]; then
            printf '%s\n' "$p"; return 0
        fi
    done
    if [[ -x /usr/bin/${t}-gcc ]]; then
        printf '%s\n' /usr; return 0
    fi
    local IFS=:
    for p in $PATH; do
        if [[ -n $p && -x $p/${t}-gcc ]]; then
            printf '%s\n' "${p%/bin}"; return 0
        fi
    done
    return 1
}

__qt4mingw_list() {
    local root bin f t
    for root in "${MINGW_ROOT:-}" /opt/mingw /opt/local \
                /opt/homebrew/opt/mingw-w64 /usr/local/opt/mingw-w64 /usr; do
        [[ -n $root ]] || continue
        bin=$root/bin
        [[ -d $bin ]] || continue
        for f in "$bin"/*-gcc; do
            [[ -x $f ]] || continue
            t=${f##*/}; t=${t%-gcc}
            printf '%-24s %s\n' "$t" "$root"
        done
    done | sort -u
}

# ---------- init ----------
__qt4mingw_init() {
    local arg=${1:-} t root bin sysroot cc cxx hostspec v

    __qt4mingw_do_reset

    t=$(__qt4mingw_normalize "${arg:-${MINGW_TRIPLET:-x86_64-w64-mingw32}}")
    if ! root=$(__qt4mingw_find_root "$t"); then
        printf 'qt4-mingw-env: 找不到 triplet %s 的工具链，可用:\n' "$t" >&2
        __qt4mingw_list >&2
        return 1
    fi

    bin=$root/bin
    if [[ $root == /usr ]]; then
        sysroot=/usr/$t
    else
        sysroot=$root/$t
    fi
    [[ -d $sysroot ]] || sysroot=$root

    cc=$bin/$t-gcc
    [[ -x $cc ]] || cc=$bin/$t-clang
    cxx=$bin/$t-g++
    [[ -x $cxx ]] || cxx=$bin/$t-clang++
    [[ -x $cc ]] || { printf 'qt4-mingw-env: 缺 %s\n' "$cc" >&2; return 1; }

    hostspec=$(__qt4mingw_host_spec)

    __qt4mingw_define_managed
    __qt4mingw_snapshot

    __qt4mingw_set CC "$cc"
    __qt4mingw_set CXX "$cxx"
    for v in AR RANLIB STRIP DLLTOOL WINDRES RC; do
        local suffix
        case $v in
            AR) suffix=ar ;; RANLIB) suffix=ranlib ;; STRIP) suffix=strip ;;
            DLLTOOL) suffix=dlltool ;; WINDRES|RC) suffix=windres ;;
        esac
        if [[ -x $bin/$t-$suffix ]]; then
            __qt4mingw_set "$v" "$bin/$t-$suffix"
        else
            unset -v "$v"
        fi
    done

    __qt4mingw_set CROSS_COMPILE "$bin/$t-"
    __qt4mingw_set CHOST "$t"
    __qt4mingw_set MINGW_HOST "$t"
    __qt4mingw_set SYSROOT "$sysroot"
    __qt4mingw_set PATH "$bin:$PATH"
    __qt4mingw_set MINGW_ROOT "$root"
    __qt4mingw_set MINGW_TRIPLET "$t"

    # QMAKESPEC 给 host bootstrap 阶段用；target spec 仅作字符串保留
    __qt4mingw_set QMAKESPEC "$hostspec"
    __qt4mingw_set QT4_HOST_SPEC "$hostspec"
    __qt4mingw_set QT4_TARGET_SPEC win32-g++
    __qt4mingw_set QT4_CROSS "$bin/$t-"

    # 交叉时隔离本机 pkg-config，避免 .pc 误链宿主机库
    if [[ -d $sysroot/lib/pkgconfig || -d $sysroot/share/pkgconfig ]]; then
        local pcdirs=
        [[ -d $sysroot/lib/pkgconfig ]]   && pcdirs=$sysroot/lib/pkgconfig
        [[ -d $sysroot/share/pkgconfig ]] && pcdirs=${pcdirs:+$pcdirs:}$sysroot/share/pkgconfig
        __qt4mingw_set PKG_CONFIG_LIBDIR "$pcdirs"
        __qt4mingw_set PKG_CONFIG_SYSROOT_DIR "$sysroot"
    else
        unset -v PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
    fi

    # 不预设 QMAKE_*FLAGS：Qt4 的 qmake.conf 会按选定 mkspec 生成正确 flags，
    # 手工塞值反而破坏交叉配置。
    unset -v QMAKE_LFLAGS QMAKE_CFLAGS QMAKE_CXXFLAGS

    # configure 命令模板：Qt4.8.7 自带 win32-g++ mkspec 支持 CROSS_COMPILE，
    # 无需自定义 mkspec；-platform 为 host，-xplatform 为 target。
    __qt4mingw_set QT4_CONFIGURE_ARGS \
        "-platform $hostspec -xplatform win32-g++ -device-option CROSS_COMPILE=$bin/$t- -opensource -confirm-license -release -shared -no-webkit -no-phonon -no-multimedia -nomake demos -nomake examples -nomake tests"

    __QT4MINGW_ENV_ACTIVE=1
    __QT4MINGW_ENV_TRIPLET=$t
    __QT4MINGW_ENV_ROOT=$root
    __QT4MINGW_ENV_HOST_SPEC=$hostspec

    printf 'qt4-mingw-env: %s (%s), host spec %s\n' "$t" "$root" "$hostspec"
    printf '  CC=%s\n  CXX=%s\n  WINDRES=%s\n  SYSROOT=%s\n' "$cc" "$cxx" "${WINDRES:-<none>}" "$sysroot"
    printf '  在 Qt4.8.7 源码目录执行:\n    ./configure %s\n    make -j4 && make install\n' "$QT4_CONFIGURE_ARGS"
    return 0
}

__qt4mingw_status() {
    if [[ ${__QT4MINGW_ENV_ACTIVE:-0} != 1 ]]; then
        printf 'qt4-mingw-env: 未激活（当前是本机环境）\n'
        return 0
    fi
    printf 'qt4-mingw-env: 已激活\n  triplet:   %s\n  root:      %s\n  host spec: %s\n  target:    %s\n  CC:        %s\n  CXX:       %s\n  WINDRES:   %s\n' \
        "$__QT4MINGW_ENV_TRIPLET" "$__QT4MINGW_ENV_ROOT" \
        "$__QT4MINGW_ENV_HOST_SPEC" "$QT4_TARGET_SPEC" \
        "$CC" "$CXX" "${WINDRES:-<none>}"
}

__qt4mingw_help() {
    cat <<'EOF'
用法: source tools/qt4-mingw-env.sh <命令> [参数]
  init [别名|完整triplet]  初始化环境（默认 x86_64-w64-mingw32）
  reset                    精确还原到 init 之前的当前终端环境
  status                   查看当前是否激活及 triplet/host spec
  list                     列出探测到的可用 triplet
  help                     显示本帮助

流程（Qt 4.8.7 源码）:
  source tools/qt4-mingw-env.sh init x64
  cd qt-everywhere-src-4.8.7
  ./configure $QT4_CONFIGURE_ARGS
  make -j4 && make install
  source tools/qt4-mingw-env.sh reset

说明: 只准备环境变量与命令模板，不生成文件、不执行编译。
      host spec 由 uname 判定；如需覆盖，init 前先设 QT4_HOST_SPEC。
EOF
}

case ${1:-help} in
    init)           shift; __qt4mingw_init "$@" ;;
    reset)          __qt4mingw_do_reset && printf 'qt4-mingw-env: 已还原到 init 前的环境\n' ;;
    status)         __qt4mingw_status ;;
    list)           __qt4mingw_list ;;
    help|-h|--help) __qt4mingw_help ;;
    *)
        printf 'qt4-mingw-env: 未知命令 %s\n\n' "$1" >&2
        __qt4mingw_help >&2
        return 1
        ;;
esac
