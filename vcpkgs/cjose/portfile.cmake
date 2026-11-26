set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_ONLY_RELEASE_CRT enabled)

# SHA512 of the 0.8.1 tag tarball downloaded through the gh-proxy GITHUB_HOST
# below. To refresh it, temporarily set this to 0 and let vcpkg print the hash.
set(_CJOSE_SOURCE_SHA512 "d3956d37e7e81e5b3353a3daea4b6fcd7969940069626c4f851000f8b6a314e3df6343077815a8470c6587d66f485fc7fdc74463ce397ab464f524173fe59cf5")

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO cisco/cjose
    REF 0.8.1
    SHA512 "${_CJOSE_SOURCE_SHA512}"
    HEAD_REF main
    GITHUB_HOST "https://gh-proxy.com/https://github.com"
)

# Upstream builds both library variants by default; keep only the one matching
# the triplet so vcpkg does not install mixed/static-and-shared artifacts.
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    set(_CJOSE_BUILD_SHARED OFF)
    set(_CJOSE_BUILD_STATIC ON)
else()
    set(_CJOSE_BUILD_SHARED ON)
    set(_CJOSE_BUILD_STATIC OFF)
endif()

set(_CJOSE_OPTIONS
    -DCJOSE_BUILD_SHARED=${_CJOSE_BUILD_SHARED}
    -DCJOSE_BUILD_STATIC=${_CJOSE_BUILD_STATIC}
    -DCJOSE_BUILD_TESTS=OFF
    -DCJOSE_ENABLE_RSA1_5=OFF
)
if(VCPKG_TARGET_IS_OSX)
    # vcpkg expects a dylib in lib, not an Apple framework bundle.
    list(APPEND _CJOSE_OPTIONS -DCJOSE_MACOS_DYLIB=ON)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS ${_CJOSE_OPTIONS}
)
vcpkg_cmake_install()

# Upstream installs its package config into lib/cmake/cjose. Move it to the
# vcpkg-standard share/cjose together with the exported targets, the version
# file and the bundled FindJansson.cmake module.
vcpkg_cmake_config_fixup(
    PACKAGE_NAME cjose
    CONFIG_PATH lib/cmake/cjose
)
vcpkg_fixup_pkgconfig()

file(INSTALL
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
