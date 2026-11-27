#!/usr/bin/env bash
# Install picode, enabling the nodejs22 feature only when no usable system Node.js
# is available. Usage: ./install.sh [--vcpkg-root DIR] [--triplet TRIPLET]
# [--host-triplet TRIPLET]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OVERLAY_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
VCPKG_ROOT="${VCPKG_ROOT:-}"
TRIPLET=""
HOST_TRIPLET=""

usage() {
    cat <<EOF
Usage: $0 [options]
  --vcpkg-root DIR     vcpkg root containing the vcpkg executable
                       (default: VCPKG_ROOT, ./vcpkg, ~/.vcpkg, /opt/vcpkg)
  --triplet TRIPLET    target triplet (default: detected from uname)
  --host-triplet T     host triplet used for the nodejs22 host tool
                       (default: same as --triplet)
  -h, --help           show this help
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --vcpkg-root)   VCPKG_ROOT="$2"; shift 2 ;;
        --triplet)      TRIPLET="$2"; shift 2 ;;
        --host-triplet) HOST_TRIPLET="$2"; shift 2 ;;
        -h|--help)      usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [ -z "${VCPKG_ROOT}" ]; then
    for candidate in "${PWD}/vcpkg" "${HOME}/.vcpkg" "/opt/vcpkg"; do
        if [ -x "${candidate}/vcpkg" ]; then
            VCPKG_ROOT="${candidate}"
            break
        fi
    done
fi
if [ -z "${VCPKG_ROOT}" ] || [ ! -x "${VCPKG_ROOT}/vcpkg" ]; then
    echo "Cannot find the vcpkg executable; set VCPKG_ROOT or use --vcpkg-root." >&2
    exit 2
fi
VCPKG="${VCPKG_ROOT}/vcpkg"

if [ -z "${TRIPLET}" ]; then
    case "$(uname -s)-$(uname -m)" in
        Linux-x86_64)                  TRIPLET="x64-linux" ;;
        Linux-aarch64|Linux-arm64)     TRIPLET="arm64-linux" ;;
        Darwin-x86_64)                 TRIPLET="x64-osx" ;;
        Darwin-arm64)                  TRIPLET="arm64-osx" ;;
        *) echo "Cannot detect triplet; use --triplet." >&2; exit 2 ;;
    esac
fi
if [ -z "${HOST_TRIPLET}" ]; then
    HOST_TRIPLET="${TRIPLET}"
fi

# Pi requires Node.js 22.19+.
node_version_supported() {
    local v="$1" major minor rest
    [[ "${v}" =~ ^([0-9]+)\.([0-9]+) ]] || return 1
    major="${BASH_REMATCH[1]}"
    minor="${BASH_REMATCH[2]}"
    if [ "${major}" -gt 22 ]; then return 0; fi
    if [ "${major}" -eq 22 ] && [ "${minor}" -ge 19 ]; then return 0; fi
    return 1
}

# Probe every PATH entry: a broken binary must not hide a working Node later.
usable_system_node() {
    local IFS=':' dir name bin v
    for dir in ${PATH}; do
        [ -n "${dir}" ] || continue
        for name in node node.exe; do
            bin="${dir}/${name}"
            [ -x "${bin}" ] || continue
            v="$("${bin}" -p process.versions.node 2>/dev/null)" || continue
            if node_version_supported "${v}"; then
                echo "${v}"
                return 0
            fi
        done
    done
    return 1
}

if v="$(usable_system_node)"; then
    echo "System Node ${v} is usable; installing picode without nodejs22."
    SPEC="picode:${TRIPLET}"
else
    echo "No usable system Node found; enabling the nodejs22 feature."
    SPEC="picode[nodejs22]:${TRIPLET}"
fi

"${VCPKG}" install "${SPEC}" \
    --overlay-ports="${OVERLAY_DIR}" \
    --host-triplet="${HOST_TRIPLET}"
