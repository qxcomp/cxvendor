set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_ONLY_RELEASE_CRT enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)

# SHA512 of the 2.4.9 tag tarball downloaded through _interprocess_archive_url
# below. To refresh it, temporarily set this to 0 and let vcpkg
# print the expected hash.
set(_INTERPROCESS_SOURCE_SHA512 "790ca0df3a9223da80967e8fc1fc12c77a096160e485b04bd91697197aee3419f7186b561b87d136a7ca2b5dc535ee69465a7f3352c77c2a5e1f088c5c2d5170")

set(_interprocess_archive_url
    "https://gh-proxy.com/https://github.com/kotauskas/interprocess/archive/2.4.9.tar.gz"
)
vcpkg_download_distfile(
    ARCHIVE
    URLS "${_interprocess_archive_url}"
    FILENAME "interprocess-2.4.9.tar.gz"
    SHA512 "${_INTERPROCESS_SOURCE_SHA512}"
)
vcpkg_extract_source_archive_ex(
    OUT_SOURCE_PATH SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
    REF "2.4.9"
)

find_program(CARGO_EXECUTABLE NAMES cargo REQUIRED)
find_program(RUSTC_EXECUTABLE NAMES rustc REQUIRED)

execute_process(
    COMMAND "${RUSTC_EXECUTABLE}" --version
    OUTPUT_VARIABLE _rustc_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _rustc_result
)
if(NOT _rustc_result EQUAL 0)
    message(FATAL_ERROR "Failed to run rustc: ${RUSTC_EXECUTABLE}")
endif()
string(STRIP "${_rustc_version}" _rustc_version)
if(_rustc_version MATCHES "rustc ([0-9]+)\\.([0-9]+)")
    if(CMAKE_MATCH_1 LESS 1 OR (CMAKE_MATCH_1 EQUAL 1 AND CMAKE_MATCH_2 LESS 75))
        message(FATAL_ERROR "interprocess requires Rust >= 1.75, found ${_rustc_version}")
    endif()
else()
    message(WARNING "Could not parse rustc version: ${_rustc_version}")
endif()

execute_process(
    COMMAND "${CARGO_EXECUTABLE}" --version
    OUTPUT_VARIABLE _cargo_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _cargo_result
)
if(NOT _cargo_result EQUAL 0)
    message(FATAL_ERROR "Failed to run cargo: ${CARGO_EXECUTABLE}")
endif()

if(VCPKG_TARGET_IS_WINDOWS AND NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR "Windows interprocess builds must run on a Windows host")
endif()
if(VCPKG_TARGET_IS_OSX AND NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR "macOS interprocess builds must run on a macOS host")
endif()
if(VCPKG_TARGET_IS_LINUX AND
   NOT VCPKG_CMAKE_SYSTEM_NAME STREQUAL "Android" AND
   NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Linux interprocess builds must run on a Linux host")
endif()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    set(_interprocess_crate_type "staticlib")
    set(_interprocess_cmake_library_type "STATIC")
    set(_interprocess_is_dynamic FALSE)
else()
    set(_interprocess_crate_type "cdylib")
    set(_interprocess_cmake_library_type "SHARED")
    set(_interprocess_is_dynamic TRUE)
endif()

# Rust staticlib archives do not carry their native system dependencies;
# forward the target's native-static-libs through the CMake imported target.
if(_interprocess_is_dynamic)
    set(INTERPROCESS_NATIVE_LIBS "")
elseif(VCPKG_CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(INTERPROCESS_NATIVE_LIBS "dl;log;m;pthread;gcc_s")
elseif(VCPKG_TARGET_IS_WINDOWS)
    if(VCPKG_TARGET_IS_MINGW)
        set(INTERPROCESS_NATIVE_LIBS "advapi32;bcrypt;kernel32;userenv;ws2_32")
    else()
        set(INTERPROCESS_NATIVE_LIBS "advapi32;bcrypt;kernel32;msvcrt;ntdll;userenv;ws2_32")
    endif()
elseif(VCPKG_TARGET_IS_OSX)
    set(INTERPROCESS_NATIVE_LIBS "System;resolv;m;iconv")
else()
    set(INTERPROCESS_NATIVE_LIBS "gcc_s;util;rt;pthread;m;\${CMAKE_DL_LIBS}")
endif()

if(VCPKG_CMAKE_SYSTEM_NAME STREQUAL "Android")
    if(NOT VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        message(FATAL_ERROR "This port supports only arm64-android")
    endif()
    set(_rust_target "aarch64-linux-android")

    set(_android_ndk "$ENV{ANDROID_NDK_HOME}")
    if(NOT _android_ndk OR NOT EXISTS "${_android_ndk}/build/cmake/android.toolchain.cmake")
        message(FATAL_ERROR
            "Android interprocess builds require ANDROID_NDK_HOME pointing to an "
            "Android NDK with build/cmake/android.toolchain.cmake.")
    endif()

    set(_android_api_level "${VCPKG_CMAKE_SYSTEM_VERSION}")
    if(_android_api_level MATCHES "^android-([0-9]+)$")
        set(_android_api_level "${CMAKE_MATCH_1}")
    endif()
    if(NOT _android_api_level MATCHES "^[0-9]+$")
        set(_android_api_level "28")
    endif()

    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(_ndk_host "linux-x86_64")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(_ndk_host "darwin-x86_64")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(_ndk_host "windows-x86_64")
    else()
        message(FATAL_ERROR "Unsupported host system for Android NDK: ${CMAKE_HOST_SYSTEM_NAME}")
    endif()

    set(_android_prebuilt "${_android_ndk}/toolchains/llvm/prebuilt/${_ndk_host}")
    set(_android_bin "${_android_prebuilt}/bin")
    if(NOT EXISTS "${_android_bin}/aarch64-linux-android${_android_api_level}-clang")
        message(FATAL_ERROR
            "Android NDK is missing ${_android_bin}/aarch64-linux-android${_android_api_level}-clang")
    endif()

    set(ENV{CC_aarch64_linux_android}
        "${_android_bin}/aarch64-linux-android${_android_api_level}-clang")
    set(ENV{CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER}
        "${_android_bin}/aarch64-linux-android${_android_api_level}-clang")
    set(ENV{AR_aarch64_linux_android} "${_android_bin}/llvm-ar")
elseif(VCPKG_TARGET_IS_WINDOWS)
    if(NOT VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        message(FATAL_ERROR "This port supports only x64-windows")
    endif()
    if(VCPKG_TARGET_IS_MINGW)
        set(_rust_target "x86_64-pc-windows-gnu")
    else()
        set(_rust_target "x86_64-pc-windows-msvc")
    endif()
elseif(VCPKG_TARGET_IS_OSX)
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        set(_rust_target "x86_64-apple-darwin")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        set(_rust_target "aarch64-apple-darwin")
    else()
        message(FATAL_ERROR "Unsupported macOS architecture: ${VCPKG_TARGET_ARCHITECTURE}")
    endif()
elseif(VCPKG_TARGET_IS_LINUX)
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        set(_rust_target "x86_64-unknown-linux-gnu")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        set(_rust_target "aarch64-unknown-linux-gnu")
        find_program(_aarch64_linux_gcc NAMES aarch64-linux-gnu-gcc REQUIRED)
        find_program(_aarch64_linux_ar NAMES aarch64-linux-gnu-ar REQUIRED)
        set(ENV{CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER} "${_aarch64_linux_gcc}")
        set(ENV{AR_aarch64_unknown_linux_gnu} "${_aarch64_linux_ar}")
    else()
        message(FATAL_ERROR "Unsupported Linux architecture: ${VCPKG_TARGET_ARCHITECTURE}")
    endif()
else()
    message(FATAL_ERROR "Unsupported vcpkg platform for interprocess")
endif()

set(_cargo_home_backup "$ENV{CARGO_HOME}")
set(_cargo_target_backup "$ENV{CARGO_TARGET_DIR}")

set(_cargo_home "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-cargo-home")
set(_cargo_target_dir "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-cargo-target")
set(_ffi_dir "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-ffi")

file(REMOVE_RECURSE "${_cargo_home}" "${_cargo_target_dir}" "${_ffi_dir}")
file(MAKE_DIRECTORY "${_cargo_home}" "${_cargo_target_dir}")

set(ENV{CARGO_HOME} "${_cargo_home}")
set(ENV{CARGO_TARGET_DIR} "${_cargo_target_dir}")

file(MAKE_DIRECTORY "${_ffi_dir}/src")
set(INTERPROCESS_SOURCE_PATH "${SOURCE_PATH}")
set(INTERPROCESS_CRATE_TYPE "${_interprocess_crate_type}")
set(INTERPROCESS_LIBRARY_TYPE "${_interprocess_cmake_library_type}")
set(INTERPROCESS_IS_DYNAMIC "${_interprocess_is_dynamic}")

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/ffi/Cargo.toml.in"
    "${_ffi_dir}/Cargo.toml"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/unofficial-interprocess-config.cmake.in"
    "${_ffi_dir}/unofficial-interprocess-config.cmake"
    @ONLY
)
file(INSTALL
    "${CMAKE_CURRENT_LIST_DIR}/ffi/src/lib.rs"
    DESTINATION "${_ffi_dir}/src"
)

message(STATUS
    "Building interprocess ${_interprocess_crate_type} for Rust target ${_rust_target}")
vcpkg_execute_required_process(
    COMMAND
        "${CARGO_EXECUTABLE}"
        build
        --release
        --target "${_rust_target}"
    WORKING_DIRECTORY "${_ffi_dir}"
    LOGNAME "build-interprocess-${TARGET_TRIPLET}"
)

set(_build_lib_dir "${_cargo_target_dir}/${_rust_target}/release")

if(VCPKG_TARGET_IS_WINDOWS)
    if(_interprocess_is_dynamic)
        file(INSTALL
            "${_build_lib_dir}/interprocess_ffi.dll"
            DESTINATION "${CURRENT_PACKAGES_DIR}/bin"
        )
        if(VCPKG_TARGET_IS_MINGW)
            file(INSTALL
                "${_build_lib_dir}/libinterprocess_ffi.dll.a"
                DESTINATION "${CURRENT_PACKAGES_DIR}/lib"
            )
        else()
            file(INSTALL
                "${_build_lib_dir}/interprocess_ffi.dll.lib"
                DESTINATION "${CURRENT_PACKAGES_DIR}/lib"
                RENAME "interprocess_ffi.lib"
            )
        endif()
    elseif(VCPKG_TARGET_IS_MINGW)
        file(INSTALL
            "${_build_lib_dir}/libinterprocess_ffi.a"
            DESTINATION "${CURRENT_PACKAGES_DIR}/lib"
        )
    else()
        file(INSTALL
            "${_build_lib_dir}/interprocess_ffi.lib"
            DESTINATION "${CURRENT_PACKAGES_DIR}/lib"
        )
    endif()
elseif(VCPKG_TARGET_IS_OSX)
    if(_interprocess_is_dynamic)
        file(INSTALL
            "${_build_lib_dir}/libinterprocess_ffi.dylib"
            DESTINATION "${CURRENT_PACKAGES_DIR}/lib"
        )
    else()
        file(INSTALL
            "${_build_lib_dir}/libinterprocess_ffi.a"
            DESTINATION "${CURRENT_PACKAGES_DIR}/lib"
        )
    endif()
else()
    if(_interprocess_is_dynamic)
        file(INSTALL
            "${_build_lib_dir}/libinterprocess_ffi.so"
            DESTINATION "${CURRENT_PACKAGES_DIR}/lib"
        )
    else()
        file(INSTALL
            "${_build_lib_dir}/libinterprocess_ffi.a"
            DESTINATION "${CURRENT_PACKAGES_DIR}/lib"
        )
    endif()
endif()

file(INSTALL
    "${CMAKE_CURRENT_LIST_DIR}/include/interprocess.h"
    "${CMAKE_CURRENT_LIST_DIR}/include/interprocess.hpp"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include"
)

file(INSTALL
    "${_ffi_dir}/unofficial-interprocess-config.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/unofficial-interprocess"
    RENAME "unofficial-interprocess-config.cmake"
)

file(INSTALL
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/unofficial-interprocess"
)

vcpkg_install_copyright(
    FILE_LIST
        "${SOURCE_PATH}/LICENSE-0BSD.txt"
        "${SOURCE_PATH}/LICENSE-APACHE.txt"
)

file(REMOVE_RECURSE "${_cargo_home}" "${_cargo_target_dir}" "${_ffi_dir}")

if(_cargo_home_backup STREQUAL "")
    unset(ENV{CARGO_HOME})
else()
    set(ENV{CARGO_HOME} "${_cargo_home_backup}")
endif()
if(_cargo_target_backup STREQUAL "")
    unset(ENV{CARGO_TARGET_DIR})
else()
    set(ENV{CARGO_TARGET_DIR} "${_cargo_target_backup}")
endif()
unset(ENV{CC_aarch64_linux_android})
unset(ENV{CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER})
unset(ENV{AR_aarch64_linux_android})
unset(ENV{CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER})
unset(ENV{AR_aarch64_unknown_linux_gnu})
