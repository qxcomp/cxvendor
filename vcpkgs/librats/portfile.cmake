# librats 3.0.6:
#   x64-osx / x64-osx-dynamic  -> built from source (upstream has no macos-x64 archive)
#   every other triplet        -> upstream release archive (static library)
set(_RATS_VERSION "3.0.6")
set(_RATS_GITHUB_HOST "https://gh-proxy.com/https://github.com")

# The release archives do not contain license files, so fetch them separately.
vcpkg_download_distfile(_RATS_LICENSE
    URLS "${_RATS_GITHUB_HOST}/librats/librats/raw/${_RATS_VERSION}/LICENSE"
    FILENAME "librats-${_RATS_VERSION}-LICENSE"
    SHA512 9e3ec6864045082a1550ee6db65c2b993fc4a0930599d107fa0639aadd79200ccaccc212794e23b6260ec465d8c82c77c52ab9ffaa6ff2ddf5d399050c5ae241
)
vcpkg_download_distfile(_RATS_THIRD_PARTY_NOTICES
    URLS "${_RATS_GITHUB_HOST}/librats/librats/raw/${_RATS_VERSION}/THIRD_PARTY_NOTICES.md"
    FILENAME "librats-${_RATS_VERSION}-THIRD_PARTY_NOTICES.md"
    SHA512 4081fed1f2a825ec3e5c4314bbac5caf9bc0bbbe4a6093766cec997a64ec78fdfc2ecca4a1b7bea6c552b9647398e1017178aff9e21fe731d0c7e674f90ced44
)
vcpkg_download_distfile(_RATS_LICENSE_CURVE25519
    URLS "${_RATS_GITHUB_HOST}/librats/librats/raw/${_RATS_VERSION}/licenses/curve25519-donna.LICENSE"
    FILENAME "librats-${_RATS_VERSION}-curve25519-donna.LICENSE"
    SHA512 d9a996207c9e751a071f8a2972177e82c8487ba0bd7fdd8efc9b6e8606d4e0abbfd0dd86bbccde0196ac8a98be7d091e2a08e4ee8093067d72f73db15f38138c
)
vcpkg_download_distfile(_RATS_LICENSE_POLY1305
    URLS "${_RATS_GITHUB_HOST}/librats/librats/raw/${_RATS_VERSION}/licenses/poly1305-donna.LICENSE"
    FILENAME "librats-${_RATS_VERSION}-poly1305-donna.LICENSE"
    SHA512 099e411a48b447ce5bfa6ab3abdf6224066c02007636676b53316a33fee97cf2a8f0f425bfbb5f94eceb018875c449f448b70da7c29d322bb2c4abb45bf2dedf
)
vcpkg_download_distfile(_RATS_LICENSE_NOISE_C
    URLS "${_RATS_GITHUB_HOST}/librats/librats/raw/${_RATS_VERSION}/licenses/noise-c.LICENSE"
    FILENAME "librats-${_RATS_VERSION}-noise-c.LICENSE"
    SHA512 02d3c15842b61ff230937f1743aa0c0b07d19ce333f468a4c644efdab173ee0f915afab1efbf77b39f2db84c7875a91d0833c0f35e7a972eaf4e8c2ea9d9670f
)
vcpkg_download_distfile(_RATS_LICENSE_IFADDRS
    URLS "${_RATS_GITHUB_HOST}/librats/librats/raw/${_RATS_VERSION}/licenses/ifaddrs-android.LICENSE"
    FILENAME "librats-${_RATS_VERSION}-ifaddrs-android.LICENSE"
    SHA512 0915ca88aecdf7f5882f19d51b13e82e17698d1d21159343b583cc8209bd54fda3d86067aa176ee132f6e872a1cf12280ab81af2e56b2e0e0795df7e20f3ea85
)

if(VCPKG_TARGET_IS_OSX AND VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO librats/librats
        REF "${_RATS_VERSION}"
        SHA512 0eb61f5b8f4622ef10792426d6fcde12c0348cadfb5f3605bfd33d9250138740ce34e93178967a1f3d45ba410f41a1cf1df93dbeb510fb11d3334fca4f5febef
        HEAD_REF master
        GITHUB_HOST "${_RATS_GITHUB_HOST}"
    )

    # Keep the same feature set as the upstream release archives: C bindings
    # enabled, BitTorrent/search and storage disabled.
    string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" _RATS_SHARED)
    string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "static" _RATS_STATIC)

    vcpkg_cmake_configure(
        SOURCE_PATH "${SOURCE_PATH}"
        OPTIONS
            -DRATS_BINDINGS=ON
            -DRATS_SEARCH_FEATURES=OFF
            -DRATS_STORAGE=OFF
            -DRATS_SHARED_LIBRARY=${_RATS_SHARED}
            -DRATS_STATIC_LIBRARY=${_RATS_STATIC}
            -DRATS_BUILD_TESTS=OFF
            -DRATS_BUILD_CLIENT=OFF
            -DRATS_BUILD_EXAMPLES=OFF
            -DRATS_INSTALL=ON
            -DRATS_VERSION_OVERRIDE=${_RATS_VERSION}
    )
    vcpkg_cmake_install()
    vcpkg_cmake_config_fixup(PACKAGE_NAME rats CONFIG_PATH lib/cmake/rats)

    file(REMOVE_RECURSE
        "${CURRENT_PACKAGES_DIR}/debug/include"
        "${CURRENT_PACKAGES_DIR}/debug/share"
    )
else()
    # Upstream archives contain a single release static library.
    vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

    if(VCPKG_TARGET_IS_LINUX)
        set(_RATS_PLATFORM "linux")
        set(_RATS_LIBNAME "librats.a")
        set(_RATS_LINK_LIBS "Threads::Threads;atomic")
    elseif(VCPKG_TARGET_IS_OSX)
        set(_RATS_PLATFORM "macos")
        set(_RATS_LIBNAME "librats.a")
        set(_RATS_LINK_LIBS "Threads::Threads")
    elseif(VCPKG_TARGET_IS_WINDOWS)
        set(_RATS_PLATFORM "windows")
        set(_RATS_LIBNAME "rats.lib")
        set(_RATS_LINK_LIBS "Threads::Threads;ws2_32;iphlpapi;bcrypt;advapi32")
    else()
        message(FATAL_ERROR "librats has no prebuilt archive for this platform")
    endif()

    set(_RATS_ARCH "${VCPKG_TARGET_ARCHITECTURE}")
    set(_RATS_ARCHIVE "librats-${_RATS_PLATFORM}-${_RATS_ARCH}-${_RATS_VERSION}.zip")

    if(_RATS_PLATFORM STREQUAL "linux" AND _RATS_ARCH STREQUAL "x64")
        set(_RATS_ARCHIVE_SHA512 "628a553e1454f34c9e19247ca6b5acea528f6cd7c75410401fc52a34bbc11d3bd13f96b1c43313f2f23248ac3565367a9a1a55cd4c49014028671a5928b4cffd")
    elseif(_RATS_PLATFORM STREQUAL "linux" AND _RATS_ARCH STREQUAL "arm64")
        set(_RATS_ARCHIVE_SHA512 "14f97d2358cb15f4a89523cb6d9f2c553e36ac6e7db669458dddc4716f3cd5f684942ba1739d09f0a06c77b619f1e71b6acbc9b08c09b99123caf66eb0e4572b")
    elseif(_RATS_PLATFORM STREQUAL "macos" AND _RATS_ARCH STREQUAL "arm64")
        set(_RATS_ARCHIVE_SHA512 "97d4320582f7bfde4b6f80c63d8924ccf2b2f6ba94a507f5e5368cc9b3ee570f030b318e7486836da58e1eb5ae55065b3425d3542f4dc0ce2ef847fa16599439")
    elseif(_RATS_PLATFORM STREQUAL "windows" AND _RATS_ARCH STREQUAL "x64")
        set(_RATS_ARCHIVE_SHA512 "d629c4a1947f84174a1d0e818b93ae64164256d6194567266c4854698508120f639ad1849e9d3ae5b9988fbc297bee11592abce029864aa5f9ba8e0a45c01bd8")
    elseif(_RATS_PLATFORM STREQUAL "windows" AND _RATS_ARCH STREQUAL "arm64")
        set(_RATS_ARCHIVE_SHA512 "5c61115f6f70798361b9b077b6df85ca46874b51ce22f47f5618ab4b952b35aa96e1e3e3d5ba5a18027ff3981a5468dd309e25f074f7e8a3f71e3449332b6509")
    else()
        message(FATAL_ERROR "librats has no ${_RATS_PLATFORM}-${_RATS_ARCH} archive in release ${_RATS_VERSION}")
    endif()

    vcpkg_download_distfile(_RATS_ARCHIVE_PATH
        URLS "${_RATS_GITHUB_HOST}/librats/librats/releases/download/${_RATS_VERSION}/${_RATS_ARCHIVE}"
        FILENAME "${_RATS_ARCHIVE}"
        SHA512 "${_RATS_ARCHIVE_SHA512}"
    )

    set(_RATS_STAGING "${CURRENT_BUILDTREES_DIR}/prebuilt-${TARGET_TRIPLET}")
    file(REMOVE_RECURSE "${_RATS_STAGING}")
    file(MAKE_DIRECTORY "${_RATS_STAGING}")
    vcpkg_execute_in_download_mode(
        COMMAND "${CMAKE_COMMAND}" -E tar xf "${_RATS_ARCHIVE_PATH}"
        WORKING_DIRECTORY "${_RATS_STAGING}"
    )

    set(_RATS_EXTRACTED "${_RATS_STAGING}/${_RATS_PLATFORM}")
    if(NOT EXISTS "${_RATS_EXTRACTED}/include/librats" OR
       NOT EXISTS "${_RATS_EXTRACTED}/${_RATS_LIBNAME}")
        message(FATAL_ERROR "Unexpected librats release layout in ${_RATS_ARCHIVE}")
    endif()

    file(COPY "${_RATS_EXTRACTED}/include/librats" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
    file(MAKE_DIRECTORY
        "${CURRENT_PACKAGES_DIR}/lib"
        "${CURRENT_PACKAGES_DIR}/debug/lib"
    )
    # A static archive has no debug/release distinction; keep both layouts so
    # consumers of debug triplets can still find and link it.
    file(COPY "${_RATS_EXTRACTED}/${_RATS_LIBNAME}" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    file(COPY "${_RATS_EXTRACTED}/${_RATS_LIBNAME}" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")

    # The upstream zip omits CMake-generated headers. rats_export.h includes
    # features.h, so the installed tree would not compile without this file.
    set(_RATS_UTIL_DIR "${CURRENT_PACKAGES_DIR}/include/librats/util")
    file(WRITE "${_RATS_UTIL_DIR}/features.h" [=[
#pragma once

// Generated by the vcpkg port. The upstream release archive omits this
// CMake-generated header, but librats/util/rats_export.h includes it.
// The upstream release build has C bindings enabled, search/bittorrent and
// storage disabled, and is a static library.
/* #undef RATS_SEARCH_FEATURES */
/* #undef RATS_STORAGE */
/* #undef RATS_IMPORT_DLL */
]=])

    file(WRITE "${_RATS_UTIL_DIR}/version.h" [=[
#pragma once

#include "librats/util/rats_export.h"

// Generated by the vcpkg port for the upstream 3.0.6 release archive.
#define LIBRATS_VERSION_MAJOR 3
#define LIBRATS_VERSION_MINOR 0
#define LIBRATS_VERSION_PATCH 6
#define LIBRATS_VERSION_BUILD 0
#define LIBRATS_VERSION_STRING "3.0.6.0"
#define LIBRATS_GIT_DESCRIBE "3.0.6"

#define LIBRATS_ASCII_HEADER \
"        #######    #####   ########  #######       \n" \
"        ##    ##  ##   ##     ##     ##            \n" \
"        ##    ##  ##   ##     ##     ##            \n" \
"        #######   #######     ##     #######       \n" \
"        ##  ##    ##   ##     ##           ##      \n" \
"        ##   ##   ##   ##     ##           ##      \n" \
"        ##    ##  ##   ##     ##           ##      \n" \
"           P2P Network Communication Library       \n" \
"        ======================================      \n"

namespace librats {
    namespace version {
        const int MAJOR = LIBRATS_VERSION_MAJOR;
        const int MINOR = LIBRATS_VERSION_MINOR;
        const int PATCH = LIBRATS_VERSION_PATCH;
        const int BUILD = LIBRATS_VERSION_BUILD;
        const char* const STRING = LIBRATS_VERSION_STRING;
        const char* const GIT_DESCRIBE = LIBRATS_GIT_DESCRIBE;
        const char* const ASCII_HEADER = LIBRATS_ASCII_HEADER;

        RATS_API void rats_print_version_info();
        RATS_API void rats_print_header();
    }
}
]=])

    # Provide the same find_package target as the source build.
    set(_RATS_SHARE_CMAKE "${CURRENT_PACKAGES_DIR}/share/rats")
    file(MAKE_DIRECTORY "${_RATS_SHARE_CMAKE}")
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/ratsConfig.cmake.in"
        "${_RATS_SHARE_CMAKE}/ratsConfig.cmake"
        @ONLY
    )
    file(WRITE "${_RATS_SHARE_CMAKE}/ratsConfigVersion.cmake" [=[
set(PACKAGE_VERSION "3.0.6")
if(PACKAGE_FIND_VERSION VERSION_LESS_EQUAL PACKAGE_VERSION)
    set(PACKAGE_VERSION_COMPATIBLE TRUE)
else()
    set(PACKAGE_VERSION_COMPATIBLE FALSE)
endif()
if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)
    set(PACKAGE_VERSION_EXACT TRUE)
endif()
]=])
endif()

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage"
    @ONLY
)

vcpkg_install_copyright(
    COMMENT [=[
librats is licensed under the MIT license. It additionally embeds adapted
third-party sources that carry their own notices, reproduced below:

  * src/librats/crypto/curve25519.*  curve25519-donna     BSD-3-Clause
  * src/librats/crypto/poly1305.*    poly1305-donna       MIT
  * src/librats/crypto/{chacha,sha256,sha512,blake2*}.*
                                   noise-c              MIT
  * 3rdparty/android/ifaddrs-*     ifaddrs-android      BSD-2-Clause AND
                                   (Android API < 24)   BSD-1-Clause
]=]
    FILE_LIST
        "${_RATS_LICENSE}"
        "${_RATS_THIRD_PARTY_NOTICES}"
        "${_RATS_LICENSE_CURVE25519}"
        "${_RATS_LICENSE_POLY1305}"
        "${_RATS_LICENSE_NOISE_C}"
        "${_RATS_LICENSE_IFADDRS}"
)
