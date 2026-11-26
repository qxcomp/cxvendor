set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_ONLY_RELEASE_CRT enabled)

include(CMakePackageConfigHelpers)

# SHA512 of the v2.4.0 tag tarball downloaded through the gh-proxy GITHUB_HOST
# below. To refresh it, temporarily set this to 0 and let vcpkg print the hash.
set(_OAUTH2_SOURCE_SHA512 "73a536edaadf1d7f62bf3080ef5e543be0b01b3ee700b9188c62d10d6cb2d965b00b1ad349d057aed4a35ac96ce0aefb8dd2af0f798c58685aec8d79da1f0172")

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO OpenIDC/liboauth2
    REF v2.4.0
    SHA512 "${_OAUTH2_SOURCE_SHA512}"
    HEAD_REF master
    GITHUB_HOST "https://gh-proxy.com/https://github.com"
    PATCHES
        fix-out-of-tree-build.patch
)

# The optional Apache binding is built against the system Apache development
# environment: vcpkg has no httpd port, and configure.ac discovers APR through
# pkg-config plus the apxs binary. Fail early instead of silently producing a
# package without liboauth2_apache.
find_program(_OAUTH2_APXS NAMES apxs2 apxs REQUIRED)
find_program(_OAUTH2_PKG_CONFIG NAMES pkg-config pkgconf REQUIRED)
execute_process(
    COMMAND "${_OAUTH2_PKG_CONFIG}" --exists apr-1 apr-util-1
    RESULT_VARIABLE _OAUTH2_APR_RESULT
)
if(NOT _OAUTH2_APR_RESULT EQUAL 0)
    message(FATAL_ERROR "oauth2 Apache support requires system apr-1/apr-util-1 pkg-config modules")
endif()

set(_OAUTH2_OPTIONS
    --with-apache
    --with-apxs=${_OAUTH2_APXS}
    --without-memcache
    --without-redis
    --without-jq
)
# Do not pass --without-nginx: configure.ac uses AC_ARG_WITH's "given" branch
# for both --with- and --without-, so that would enable NGINX and then fail
# because no NGINX source tree is available. Omitting the option disables it.

vcpkg_configure_make(
    SOURCE_PATH "${SOURCE_PATH}"
    AUTOCONFIG
    OPTIONS ${_OAUTH2_OPTIONS}
)
vcpkg_install_make()

# Upstream installs libtool archives that embed absolute build-tree paths.
file(GLOB_RECURSE _OAUTH2_LIBTOOL_FILES "${CURRENT_PACKAGES_DIR}/lib/*.la")
if(_OAUTH2_LIBTOOL_FILES)
    file(REMOVE ${_OAUTH2_LIBTOOL_FILES})
endif()

vcpkg_fixup_pkgconfig()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    set(_OAUTH2_LIBRARY_FILE "liboauth2.a")
    set(_OAUTH2_INTERFACE_LINK_LIBRARIES "OpenSSL::SSL;OpenSSL::Crypto;CURL::libcurl;jansson::jansson;cjose::cjose;PCRE2::8BIT")
else()
    set(_OAUTH2_LIBRARY_FILE "liboauth2.so")
    set(_OAUTH2_INTERFACE_LINK_LIBRARIES "")
endif()

set(_OAUTH2_SHARE_DIR "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(MAKE_DIRECTORY "${_OAUTH2_SHARE_DIR}")

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/unofficial-oauth2-targets.cmake.in"
    "${_OAUTH2_SHARE_DIR}/unofficial-oauth2-targets.cmake"
    @ONLY
)
# 本 port 自行提供 CMake config；文件名必须是 find_package(oauth2) 可发现的
# oauth2-config.cmake，而不是 unofficial-oauth2-config.cmake。
configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/unofficial-oauth2-config.cmake.in"
    "${_OAUTH2_SHARE_DIR}/oauth2-config.cmake"
    INSTALL_DESTINATION "${_OAUTH2_SHARE_DIR}"
)

file(INSTALL
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    DESTINATION "${_OAUTH2_SHARE_DIR}"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
