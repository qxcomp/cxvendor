# LibreTV v2.24.0 is packaged from the official release standalone bundle.
# It is architecture independent: the bundle ships server.js, the Next.js
# output and its runtime node_modules, so no npm install is needed at build
# time. Runtime requires Node.js 18+ and the PASSWORD environment variable.
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

set(_LIBRETV_GITHUB_HOST "https://gh-proxy.com/https://github.com")
set(_LIBRETV_ARCHIVE "libretv-2.24.0-standalone.tar.gz")
set(_LIBRETV_ARCHIVE_SHA512 "5a2d9f699e52c94d7fc22950e8c43618a76d0ca656625ca9792e5776659946f29a61f40f6a973957ab81cdb03483115a36c046b8e5e4c623d0b80ca02d0c3c70")
set(_LIBRETV_LICENSE_SHA512 "3568a76677f3d55dbbc7fc33c4073b4714cc6f68d92cd842a9ca2b230e8bd2140e59c3fc3cce34be34ffed78d75ab6e9d991ba59bf7c4addde7fb96648d3375d")

vcpkg_download_distfile(_LIBRETV_ARCHIVE_PATH
    URLS "${_LIBRETV_GITHUB_HOST}/LibreSpark/LibreTV/releases/download/v2.24.0/${_LIBRETV_ARCHIVE}"
    FILENAME "${_LIBRETV_ARCHIVE}"
    SHA512 "${_LIBRETV_ARCHIVE_SHA512}"
)
vcpkg_download_distfile(_LIBRETV_LICENSE
    URLS "${_LIBRETV_GITHUB_HOST}/LibreSpark/LibreTV/raw/v2.24.0/LICENSE"
    FILENAME "libretv-v2.24.0-LICENSE"
    SHA512 "${_LIBRETV_LICENSE_SHA512}"
)

# The release tarball has no top-level directory, so create the target first.
set(_LIBRETV_STAGING "${CURRENT_BUILDTREES_DIR}/standalone-${TARGET_TRIPLET}")
file(REMOVE_RECURSE "${_LIBRETV_STAGING}")
file(MAKE_DIRECTORY "${_LIBRETV_STAGING}")
vcpkg_execute_in_download_mode(
    COMMAND "${CMAKE_COMMAND}" -E tar xzf "${_LIBRETV_ARCHIVE_PATH}"
    WORKING_DIRECTORY "${_LIBRETV_STAGING}"
)
if(NOT EXISTS "${_LIBRETV_STAGING}/server.js")
    message(FATAL_ERROR "Unexpected LibreTV standalone layout: ${_LIBRETV_STAGING}")
endif()

set(_LIBRETV_TOOLS "${CURRENT_PACKAGES_DIR}/tools/libretv")
file(MAKE_DIRECTORY "${_LIBRETV_TOOLS}")
file(COPY "${_LIBRETV_STAGING}/" DESTINATION "${_LIBRETV_TOOLS}")

# The launchers prefer a system Node, but fall back to the nodejs22 host tool
# when the 'nodejs22' feature was enabled by the install script.
set(_LIBRETV_NODEJS22_UNIX_PATHS "")
set(_LIBRETV_NODEJS22_WIN_PATHS "")
foreach(_root
    "${CURRENT_HOST_INSTALLED_DIR}/tools/nodejs22"
    "${CURRENT_INSTALLED_DIR}/tools/nodejs22")
    set(_unix_path "${_root}/bin/node")
    if(_LIBRETV_NODEJS22_UNIX_PATHS)
        set(_LIBRETV_NODEJS22_UNIX_PATHS "${_LIBRETV_NODEJS22_UNIX_PATHS}:${_unix_path}")
    else()
        set(_LIBRETV_NODEJS22_UNIX_PATHS "${_unix_path}")
    endif()
    file(TO_NATIVE_PATH "${_root}/node.exe" _win_path)
    if(_LIBRETV_NODEJS22_WIN_PATHS)
        set(_LIBRETV_NODEJS22_WIN_PATHS "${_LIBRETV_NODEJS22_WIN_PATHS} \"${_win_path}\"")
    else()
        set(_LIBRETV_NODEJS22_WIN_PATHS "\"${_win_path}\"")
    endif()
endforeach()

if(VCPKG_TARGET_IS_WINDOWS)
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/libretv.cmd.in"
        "${_LIBRETV_TOOLS}/libretv.cmd"
        @ONLY
        NEWLINE_STYLE CRLF
    )
else()
    set(_LIBRETV_LAUNCHER "${_LIBRETV_TOOLS}/libretv")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/libretv.in" "${_LIBRETV_LAUNCHER}" @ONLY)
    file(CHMOD
        "${_LIBRETV_LAUNCHER}"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
endif()

vcpkg_install_copyright(FILE_LIST "${_LIBRETV_LICENSE}")
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage"
    @ONLY
)
