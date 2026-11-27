# veilid 0.5.8 is packaged as a tool-only port from the official Debian
# packages. No Rust source build is performed.
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

set(_VEILID_VERSION "0.5.8")
set(_VEILID_APT_BASE "https://packages.veilid.net/apt/pool/stable/main")

if(NOT VCPKG_TARGET_IS_LINUX)
    message(FATAL_ERROR "veilid only ships prebuilt Linux binary packages")
endif()
if(TARGET_TRIPLET MATCHES "musl")
    message(FATAL_ERROR "veilid prebuilt packages require glibc; musl triplets are not supported")
endif()

if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(_VEILID_DEB_ARCH "amd64")
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    set(_VEILID_DEB_ARCH "arm64")
else()
    message(FATAL_ERROR "veilid has no prebuilt package for linux-${VCPKG_TARGET_ARCHITECTURE}")
endif()

find_program(_VEILID_DPKG_DEB NAMES dpkg-deb)
if(NOT _VEILID_DPKG_DEB)
    message(FATAL_ERROR "veilid requires dpkg-deb to unpack the official Debian packages")
endif()

vcpkg_download_distfile(_VEILID_LICENSE
    URLS "https://gitlab.com/veilid/veilid/-/raw/v${_VEILID_VERSION}/LICENSE"
    FILENAME "veilid-${_VEILID_VERSION}-LICENSE"
    SHA512 c1e3a0c8f5267fb5c0b65733bda037c62d914ed989bee0f510d8196b1029eec00d40f415ce1514a4996d420ba02d856d04db0915b64573ef4a36033cc5efb94e
)

set(_VEILID_TOOLS "${CURRENT_PACKAGES_DIR}/tools/veilid")
file(MAKE_DIRECTORY "${_VEILID_TOOLS}")

foreach(_VEILID_TOOL server cli)
    set(_VEILID_DEB "veilid-${_VEILID_TOOL}-${_VEILID_VERSION}_${_VEILID_DEB_ARCH}.deb")

    if(_VEILID_TOOL STREQUAL "server")
        if(_VEILID_DEB_ARCH STREQUAL "amd64")
            set(_VEILID_DEB_SHA512 "7831528487dc8e22d2bc4e977db7e18c395fcfd8372544c8e20bf93cb5dbc10e3ae1b779882d1c9b59d04db3a2a1fb37617d68a2a4478e641fdee24072abed1c")
        else()
            set(_VEILID_DEB_SHA512 "cafd50aa21c75a57fd46676e6e428fb685538ff3fed4abc61b3c690050a594735a03a55b1e883d2d5a193937b15ad436808733cbd0b78858f19e07f2f2c89fa8")
        endif()
    else()
        if(_VEILID_DEB_ARCH STREQUAL "amd64")
            set(_VEILID_DEB_SHA512 "665dcb6e0ed9e3772e430a17d328cc8e7d10d24654d7394c0f0e352cf0ba2ab4620f50041744cd4b13f7751f542184780a3e279ea7b182317fbbe33f14c6c33e")
        else()
            set(_VEILID_DEB_SHA512 "a9902b0d6a9badfa21dd817cdc42f1265e63e6042871c4e72def7dd9b59dc0a3ef2898530b6f1d824dde5be059c972655bc448e7e03461dedf6d9cd46b63140b")
        endif()
    endif()

    vcpkg_download_distfile(_VEILID_DEB_PATH
        URLS "${_VEILID_APT_BASE}/${_VEILID_DEB}"
        FILENAME "${_VEILID_DEB}"
        SHA512 "${_VEILID_DEB_SHA512}"
    )

    set(_VEILID_STAGING "${CURRENT_BUILDTREES_DIR}/${_VEILID_TOOL}-${TARGET_TRIPLET}")
    file(REMOVE_RECURSE "${_VEILID_STAGING}")
    file(MAKE_DIRECTORY "${_VEILID_STAGING}")
    vcpkg_execute_in_download_mode(
        COMMAND "${_VEILID_DPKG_DEB}" -x "${_VEILID_DEB_PATH}" "${_VEILID_STAGING}"
    )

    set(_VEILID_BINARY "${_VEILID_STAGING}/usr/bin/veilid-${_VEILID_TOOL}")
    if(NOT EXISTS "${_VEILID_BINARY}")
        message(FATAL_ERROR "Unexpected veilid package layout in ${_VEILID_DEB}: ${_VEILID_BINARY} is missing")
    endif()

    file(INSTALL "${_VEILID_BINARY}"
        DESTINATION "${_VEILID_TOOLS}"
        USE_SOURCE_PERMISSIONS
    )
endforeach()

vcpkg_install_copyright(
    COMMENT [=[
veilid is licensed under the Mozilla Public License 2.0. The full license text is reproduced below.
]=]
    FILE_LIST "${_VEILID_LICENSE}"
)

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage"
    @ONLY
)
