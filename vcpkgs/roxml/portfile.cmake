set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_ONLY_RELEASE_CRT enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)

# SHA512 of the 3.0.2 tag tarball downloaded through the gh-proxy GITHUB_HOST
# below. To refresh it, temporarily set this to 0 and let vcpkg print the hash.
set(_ROXML_SOURCE_SHA512 "c07d7af35110c9fb2b15fb56a5c68554e35c9bd10620e8eb9532084b42789408ead3fcda8e13e09f3642e5858cf7d3115ffa27fb1afd078f90de146ac28c5135")

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO blunderer/libroxml
    REF 3.0.2
    SHA512 "${_ROXML_SOURCE_SHA512}"
    HEAD_REF master
    GITHUB_HOST "https://gh-proxy.com/https://github.com"
    PATCHES fix-vcpkg-build.patch
)

# The patch raises cmake_minimum_required from 2.6 to 3.5; BUILD_TESTING would
# otherwise add the upstream unittest subdirectory.
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS -DBUILD_TESTING=OFF
)
vcpkg_cmake_install()

# Upstream installs neither a CMake package config nor a pkg-config file.
set(ROXML_PREFIX "${CURRENT_PACKAGES_DIR}")
file(MAKE_DIRECTORY
    "${CURRENT_PACKAGES_DIR}/lib/pkgconfig"
    "${CURRENT_PACKAGES_DIR}/share/unofficial-roxml"
)
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/libroxml.pc.in"
    "${CURRENT_PACKAGES_DIR}/lib/pkgconfig/libroxml.pc"
    @ONLY
)
vcpkg_fixup_pkgconfig()

# Export a target matching the triplet's library linkage.
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    set(ROXML_LIBRARY_TYPE STATIC)
    set(ROXML_IS_DYNAMIC FALSE)
else()
    set(ROXML_LIBRARY_TYPE SHARED)
    set(ROXML_IS_DYNAMIC TRUE)
endif()
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/unofficial-roxml-config.cmake.in"
    "${CURRENT_PACKAGES_DIR}/share/unofficial-roxml/unofficial-roxml-config.cmake"
    @ONLY
)
file(INSTALL
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/License.txt")
