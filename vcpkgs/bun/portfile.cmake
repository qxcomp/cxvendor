set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
set(VCPKG_FIXUP_MACHO_RPATH OFF)

set(_BUN_VERSION "1.4.5")
set(_BUN_WEBKIT_REF "d6375c72affe0f70222cdcb45012b10f0bf3a9d5")
set(_BUN_GITHUB_HOST "https://gh-proxy.com/https://github.com")
set(_BUN_OSX_DEPLOYMENT_TARGET "11.0")
set(_BUN_MIN_OSX_SDK "13.0")

# Bun is built natively on each host. Cross-compiling a macOS binary from
# another OS is intentionally rejected: the shipped binary must have a
# verified LC_BUILD_VERSION, which requires otool on the build host.
set(_BUN_HOST_OS "")
if(VCPKG_HOST_IS_LINUX)
    set(_BUN_HOST_OS "linux")
elseif(VCPKG_HOST_IS_OSX)
    set(_BUN_HOST_OS "osx")
elseif(VCPKG_HOST_IS_WINDOWS)
    set(_BUN_HOST_OS "windows")
endif()

set(_BUN_TARGET_OS "")
if(VCPKG_TARGET_IS_LINUX)
    set(_BUN_TARGET_OS "linux")
elseif(VCPKG_TARGET_IS_OSX)
    set(_BUN_TARGET_OS "osx")
elseif(VCPKG_TARGET_IS_WINDOWS)
    set(_BUN_TARGET_OS "windows")
endif()

set(_BUN_HOST_ARCH "")
if(HOST_TRIPLET MATCHES "^arm64")
    set(_BUN_HOST_ARCH "arm64")
elseif(HOST_TRIPLET MATCHES "^x64")
    set(_BUN_HOST_ARCH "x64")
endif()

if(NOT _BUN_HOST_OS STREQUAL _BUN_TARGET_OS)
    message(FATAL_ERROR
        "bun must be built natively: host '${HOST_TRIPLET}' (${_BUN_HOST_OS}) "
        "cannot build target '${TARGET_TRIPLET}' (${_BUN_TARGET_OS})."
    )
endif()
if(_BUN_HOST_ARCH AND NOT _BUN_HOST_ARCH STREQUAL VCPKG_TARGET_ARCHITECTURE)
    message(FATAL_ERROR
        "bun must be built natively: host arch '${_BUN_HOST_ARCH}' differs "
        "from target arch '${VCPKG_TARGET_ARCHITECTURE}'."
    )
endif()
if(NOT VCPKG_TARGET_ARCHITECTURE STREQUAL "x64" AND NOT VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    message(FATAL_ERROR "Unsupported bun architecture: ${VCPKG_TARGET_ARCHITECTURE}")
endif()

find_program(_BUN_GIT NAMES git git.cmd)
if(NOT _BUN_GIT)
    message(FATAL_ERROR "git is required to fetch the pinned bun and WebKit sources")
endif()
find_program(_BUN_CMAKE NAMES cmake)
if(NOT _BUN_CMAKE)
    message(FATAL_ERROR "cmake >= 3.24 is required to build bun")
endif()
execute_process(
    COMMAND "${_BUN_CMAKE}" --version
    OUTPUT_VARIABLE _BUN_CMAKE_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
string(REGEX MATCH "[0-9]+\\.[0-9]+" _BUN_CMAKE_VERSION_MAJOR_MINOR "${_BUN_CMAKE_VERSION}")
if(NOT _BUN_CMAKE_VERSION_MAJOR_MINOR OR _BUN_CMAKE_VERSION_MAJOR_MINOR VERSION_LESS 3.24)
    message(FATAL_ERROR "cmake >= 3.24 is required, found: ${_BUN_CMAKE_VERSION}")
endif()
find_program(_BUN_NINJA NAMES ninja)
if(NOT _BUN_NINJA)
    message(FATAL_ERROR "ninja is required to build bun")
endif()
find_program(_BUN_RUSTUP NAMES rustup)
if(NOT _BUN_RUSTUP)
    message(FATAL_ERROR "rustup is required; bun uses the pinned nightly from rust-toolchain.toml")
endif()
find_program(_BUN_CLANG NAMES clang-23 clang)
if(NOT _BUN_CLANG)
    message(FATAL_ERROR "LLVM/Clang 23 is required; install llvm@23 and put it on PATH")
endif()
execute_process(
    COMMAND "${_BUN_CLANG}" --version
    OUTPUT_VARIABLE _BUN_CLANG_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT _BUN_CLANG_VERSION MATCHES "version 23\\.")
    message(FATAL_ERROR "Clang 23 is required, found: ${_BUN_CLANG_VERSION}")
endif()

if(VCPKG_HOST_IS_OSX)
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-version
        OUTPUT_VARIABLE _BUN_OSX_SDK
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT _BUN_OSX_SDK OR _BUN_OSX_SDK VERSION_LESS _BUN_MIN_OSX_SDK)
        message(FATAL_ERROR
            "Building bun ${_BUN_VERSION} requires macOS SDK >= ${_BUN_MIN_OSX_SDK}; "
            "found '${_BUN_OSX_SDK}'. Update Command Line Tools / Xcode. "
            "The shipped binary still targets macOS ${_BUN_OSX_DEPLOYMENT_TARGET}."
        )
    endif()
endif()

# Bootstrap: a release build of bun runs the codegen/bundle steps. SHA256
# values come from the upstream SHASUMS256.txt for bun-v1.4.5.
set(_BUN_BOOT_FILE "")
set(_BUN_BOOT_SHA256 "")
if(_BUN_HOST_OS STREQUAL "osx" AND _BUN_HOST_ARCH STREQUAL "x64")
    set(_BUN_BOOT_FILE "bun-darwin-x64-baseline.zip")
    set(_BUN_BOOT_SHA256 "fb0121739a7e80e7511b6bfe4d2116422b48ba18a013acadef0c425348e95891")
elseif(_BUN_HOST_OS STREQUAL "osx" AND _BUN_HOST_ARCH STREQUAL "arm64")
    set(_BUN_BOOT_FILE "bun-darwin-aarch64.zip")
    set(_BUN_BOOT_SHA256 "b5d6bc2e13caf14e57c89661e89efd073ff16165b41bfe43882616410609cd10")
elseif(_BUN_HOST_OS STREQUAL "linux" AND _BUN_HOST_ARCH STREQUAL "x64")
    set(_BUN_BOOT_FILE "bun-linux-x64-baseline.zip")
    set(_BUN_BOOT_SHA256 "dcb23a18a1c2fff12d57bcef696dbbb42eb67fde29f43816ecb595459a24c463")
elseif(_BUN_HOST_OS STREQUAL "linux" AND _BUN_HOST_ARCH STREQUAL "arm64")
    set(_BUN_BOOT_FILE "bun-linux-aarch64.zip")
    set(_BUN_BOOT_SHA256 "18656a3bf0ea5641f5796ae4213b75255579a70a0fded2f337933785568cdf84")
elseif(_BUN_HOST_OS STREQUAL "windows" AND _BUN_HOST_ARCH STREQUAL "x64")
    set(_BUN_BOOT_FILE "bun-windows-x64-baseline.zip")
    set(_BUN_BOOT_SHA256 "3c081c73865e7a3453a9d514cc2ba3d699880621bcf67bb17241ff5194cb1d4b")
elseif(_BUN_HOST_OS STREQUAL "windows" AND _BUN_HOST_ARCH STREQUAL "arm64")
    set(_BUN_BOOT_FILE "bun-windows-aarch64.zip")
    set(_BUN_BOOT_SHA256 "41f57d49c3c6811888d9633c569e40b4eb1f7e92b5e5336e3abd3af03187d77a")
else()
    message(FATAL_ERROR "Cannot select bootstrap bun archive for ${HOST_TRIPLET}")
endif()

vcpkg_download_distfile(_BUN_BOOT_ARCHIVE
    URLS "${_BUN_GITHUB_HOST}/oven-sh/bun/releases/download/bun-v${_BUN_VERSION}/${_BUN_BOOT_FILE}"
    FILENAME "bun-v${_BUN_VERSION}-${_BUN_BOOT_FILE}"
    SKIP_SHA512
)
file(SHA256 "${_BUN_BOOT_ARCHIVE}" _BUN_BOOT_ACTUAL_SHA256)
if(NOT _BUN_BOOT_ACTUAL_SHA256 STREQUAL _BUN_BOOT_SHA256)
    message(FATAL_ERROR
        "Bootstrap bun checksum mismatch for ${_BUN_BOOT_FILE}: "
        "expected ${_BUN_BOOT_SHA256}, got ${_BUN_BOOT_ACTUAL_SHA256}"
    )
endif()

set(_BUN_BOOT_ROOT "${CURRENT_BUILDTREES_DIR}/bun-bootstrap-${TARGET_TRIPLET}")
file(REMOVE_RECURSE "${_BUN_BOOT_ROOT}")
file(MAKE_DIRECTORY "${_BUN_BOOT_ROOT}")
vcpkg_execute_in_download_mode(
    COMMAND "${_BUN_CMAKE}" -E tar xf "${_BUN_BOOT_ARCHIVE}"
    WORKING_DIRECTORY "${_BUN_BOOT_ROOT}"
)
if(VCPKG_HOST_IS_WINDOWS)
    file(GLOB _BUN_BOOT_EXE "${_BUN_BOOT_ROOT}/*/bun.exe")
else()
    file(GLOB _BUN_BOOT_EXE "${_BUN_BOOT_ROOT}/*/bun")
endif()
list(LENGTH _BUN_BOOT_EXE _BUN_BOOT_COUNT)
if(NOT _BUN_BOOT_COUNT EQUAL 1)
    message(FATAL_ERROR "Unexpected bootstrap bun archive layout: ${_BUN_BOOT_EXE}")
endif()
if(NOT VCPKG_HOST_IS_WINDOWS)
    file(CHMOD "${_BUN_BOOT_EXE}"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

# vcpkg_from_git fetches with --no-tags, so REF must be the commit SHA that
# the formal tag points to, not the tag name itself.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "${_BUN_GITHUB_HOST}/oven-sh/bun.git"
    REF "a7733435cf40429e3b35c36072769df2052b80f1" # bun-v1.4.5
)

# macOS must rebuild JavaScriptCore: a prebuilt WebKit archive is compiled
# with MACOS_DEPLOYMENT_TARGET 13.0 and would keep the final Mach-O at 13.0.
if(VCPKG_TARGET_IS_OSX)
    vcpkg_from_git(
        OUT_SOURCE_PATH WEBKIT_PATH
        URL "${_BUN_GITHUB_HOST}/oven-sh/WebKit.git"
        REF "${_BUN_WEBKIT_REF}"
    )
endif()

set(_BUN_PROFILE "release")
if(VCPKG_TARGET_IS_WINDOWS)
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        set(_BUN_PROFILE "windows-arm64-release")
    else()
        set(_BUN_PROFILE "windows-x64-release")
    endif()
endif()

set(_BUN_BUILD_CMD "${_BUN_BOOT_EXE}" "scripts/build.ts" "--profile=${_BUN_PROFILE}")
if(VCPKG_TARGET_IS_OSX)
    list(APPEND _BUN_BUILD_CMD
        "--webkit=local"
        "--osx-deployment-target=${_BUN_OSX_DEPLOYMENT_TARGET}"
    )
    set(_BUN_BUILD_CMD
        "${_BUN_CMAKE}" -E env "BUN_WEBKIT_PATH=${WEBKIT_PATH}" ${_BUN_BUILD_CMD}
    )
endif()

vcpkg_execute_required_process(
    COMMAND ${_BUN_BUILD_CMD}
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME "bun-build-${TARGET_TRIPLET}"
)

if(VCPKG_TARGET_IS_WINDOWS)
    set(_BUN_BUILT_EXE "${SOURCE_PATH}/build/release/bun.exe")
else()
    set(_BUN_BUILT_EXE "${SOURCE_PATH}/build/release/bun")
endif()
if(NOT EXISTS "${_BUN_BUILT_EXE}")
    message(FATAL_ERROR "bun build output not found: ${_BUN_BUILT_EXE}")
endif()

if(VCPKG_TARGET_IS_OSX)
    find_program(_BUN_OTOOL otool)
    if(NOT _BUN_OTOOL)
        message(FATAL_ERROR "otool is required to verify the macOS deployment target")
    endif()
    execute_process(
        COMMAND "${_BUN_OTOOL}" -l "${_BUN_BUILT_EXE}"
        OUTPUT_VARIABLE _BUN_MACHO_INFO
        ERROR_QUIET
    )
    # Check the minos of the first LC_BUILD_VERSION, not any minos in the
    # output: a binary can contain several load commands with different values.
    string(REPLACE "\r\n" "\n" _BUN_MACHO_INFO "${_BUN_MACHO_INFO}")
    string(REPLACE "\n" ";" _BUN_MACHO_LINES "${_BUN_MACHO_INFO}")
    set(_BUN_FOUND_BUILD_VERSION FALSE)
    set(_BUN_MINOS_OK FALSE)
    foreach(_line IN LISTS _BUN_MACHO_LINES)
        if(_line MATCHES "cmd LC_BUILD_VERSION")
            set(_BUN_FOUND_BUILD_VERSION TRUE)
            continue()
        endif()
        if(_BUN_FOUND_BUILD_VERSION AND _line MATCHES "minos ([0-9]+\\.[0-9]+)")
            if(CMAKE_MATCH_1 STREQUAL _BUN_OSX_DEPLOYMENT_TARGET)
                set(_BUN_MINOS_OK TRUE)
            endif()
            break()
        endif()
    endforeach()
    if(NOT _BUN_MINOS_OK)
        message(FATAL_ERROR
            "bun binary does not target macOS ${_BUN_OSX_DEPLOYMENT_TARGET} "
            "(found LC_BUILD_VERSION: ${_BUN_FOUND_BUILD_VERSION}). "
            "Check the local WebKit build and LC_BUILD_VERSION output."
        )
    endif()
endif()

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/tools/bun")
file(COPY "${_BUN_BUILT_EXE}" DESTINATION "${CURRENT_PACKAGES_DIR}/tools/bun")
if(NOT VCPKG_TARGET_IS_WINDOWS)
    file(CHMOD "${CURRENT_PACKAGES_DIR}/tools/bun/bun"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright" [[
Bun 1.4.5
Source: https://github.com/oven-sh/bun
License: MIT

Bun statically links JavaScriptCore from the oven-sh/WebKit fork.
JavaScriptCore/WebKit is distributed under the LGPL-2 license; see the
upstream LICENSE.md for the complete licensing notice.
]])
