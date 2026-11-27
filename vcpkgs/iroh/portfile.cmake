# iroh 1.5.0 is packaged as a tool-only port from the upstream prebuilt
# release assets. No Rust source build is performed.
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

set(_IROH_VERSION "1.5.0")
set(_IROH_GITHUB_HOST "https://gh-proxy.com/https://github.com")

set(_IROH_SHA_DNS_SERVER_x86_64_unknown_linux_gnu "c4fa84d1770b92147ab81b61f4d01d0c3d646f1d6d046f5edb1f0c37105d2625bd1b9dc73910fb1e76d267bb4e6864342a076e4192d575b63ff440dd6ffc86e3")
set(_IROH_SHA_DNS_SERVER_x86_64_unknown_linux_musl "99d33345cd345c7e754f2fb7063d04c1bfe4eaf80e2cf4f0c2e1f95601fabcdda4dda7c024e37b5c8f6d7c700f46a720eee61200881bfeaf864a870f6a40a991")
set(_IROH_SHA_DNS_SERVER_aarch64_unknown_linux_gnu "82396263f320515ae1e586d3b957175c341c1dad8462830db84bbf1cc23f0856b4fd29ced702ae8f11a971928ef2ceb5a3f07fce2e8224b83f030952ec2f4af4")
set(_IROH_SHA_DNS_SERVER_aarch64_unknown_linux_musl "dc8d00113ac4b97a973d0a6df1d3ca6c6ced994efa70e03ed2fb894da34ca57e3841bee82073f8c9c40a70ad68be8ca0c7d481098993cf4b19c2c4fc000a370d")
set(_IROH_SHA_DNS_SERVER_x86_64_apple_darwin "a444e4cc101eb1f80eebcfc43bc71039628ccad31de16a6d149d8f8b90e86d98ffac072a23e78271fc1bafdbc775c9ab2bac27981e80912e7bab03543288eb1b")
set(_IROH_SHA_DNS_SERVER_aarch64_apple_darwin "934ce4511934978c71b5605e6026e9eca20312afbecd4ae9065796dd5cce2d00ea066f1086a8a54a639cd95ef35f3f9fe901cee5c9cb8cdc66c1040ca9b962ec")
set(_IROH_SHA_DNS_SERVER_x86_64_pc_windows_msvc "d731ccbdaaf41df5fb910b81298989644a342759e480955ebf9d8e525ce4adbeb240956e9ce6dcc7c833f4df7136357ec15400213548f42e1b741ee811917057")

set(_IROH_SHA_RELAY_x86_64_unknown_linux_gnu "ae512368740ffc485f6aa6b35b8ac975060409cad314435be66d4b27d7ba3c93354be93e065ecb58edc91a9f8e99160988798c7595607dc2b24a3cba12144990")
set(_IROH_SHA_RELAY_x86_64_unknown_linux_musl "cc5ec6815b9b185dd9446a930a8f32a78b3bae773659d100cfe56cc58c0da153ff38c42e6964fe4c2361bccf1a1fb7557b95a40e8f62a0df26d759f80d3f70ed")
set(_IROH_SHA_RELAY_aarch64_unknown_linux_gnu "4f8bdf4eb6416905d20980f16ae5ac3d6f6add9fffabaf19439b27441793435e8582d1f0a0a824503042b2c11f9ed99449d04a20842b7b4fc8161c5a6a7d1c51")
set(_IROH_SHA_RELAY_aarch64_unknown_linux_musl "eb9bb3efd59e1322b42fdf2f3ea6dc92c2e6b31a1a92cbccac90ebab65180ab1e0726236780a85a7f8dfb5555005ec4f47d4b7c821ddb6a9ce19fe6b7f4e8a5d")
set(_IROH_SHA_RELAY_x86_64_apple_darwin "ead2b733efab1d15c075be7f94e6e88c1332ec54cc2fbd179f170f9e1a46deacccc1ce1e565e379dc3605583f07d8654295be46dc984f01372595f713f6feb52")
set(_IROH_SHA_RELAY_aarch64_apple_darwin "208dadfad3dad2b2009fa197452d9d5f1be91cf3c717b4215b525ec32c933ad789ad4053867383971d1417b7010499ec20b1a91f9fd321d9c2e79754bc1ba0de")
set(_IROH_SHA_RELAY_x86_64_pc_windows_msvc "96e77922a8deb3171e415eb05e9d28777394cc4c08e92e948224a55290543d77531eec3e58248eec3a4694de98572f21a51b4e6ca5c89eb1917cce4386b11d47")

vcpkg_download_distfile(_IROH_LICENSE_MIT
    URLS "${_IROH_GITHUB_HOST}/n0-computer/iroh/raw/v${_IROH_VERSION}/LICENSE-MIT"
    FILENAME "iroh-${_IROH_VERSION}-LICENSE-MIT"
    SHA512 f679dc1df60e94b91d489bb7d3102511e8f91c845d8d765f9b7985285253eb43a4e7b3187467391fe87e9cfb895fa660eb5b0619c935bc9be05d5e2bd5e81aa3
)
vcpkg_download_distfile(_IROH_LICENSE_APACHE
    URLS "${_IROH_GITHUB_HOST}/n0-computer/iroh/raw/v${_IROH_VERSION}/LICENSE-APACHE"
    FILENAME "iroh-${_IROH_VERSION}-LICENSE-APACHE"
    SHA512 58bf8a5d1b949535b2573c730685557b6d4e61320fda9286b430cfc567bb59fbafe143cfb7080d43b05fba61a623d5b800483481cc845f8979529ab624a36667
)

if(VCPKG_TARGET_IS_LINUX)
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        set(_IROH_TARGET_ARCH "x86_64")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        set(_IROH_TARGET_ARCH "aarch64")
    else()
        message(FATAL_ERROR "iroh has no prebuilt release for linux-${VCPKG_TARGET_ARCHITECTURE}")
    endif()

    # vcpkg has no standard musl triplet variable, so use the triplet name.
    if(TARGET_TRIPLET MATCHES "musl")
        set(_IROH_TARGET "${_IROH_TARGET_ARCH}-unknown-linux-musl")
    else()
        set(_IROH_TARGET "${_IROH_TARGET_ARCH}-unknown-linux-gnu")
    endif()
elseif(VCPKG_TARGET_IS_OSX)
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        set(_IROH_TARGET "x86_64-apple-darwin")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        set(_IROH_TARGET "aarch64-apple-darwin")
    else()
        message(FATAL_ERROR "iroh has no prebuilt release for osx-${VCPKG_TARGET_ARCHITECTURE}")
    endif()
elseif(VCPKG_TARGET_IS_WINDOWS)
    if(NOT VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        message(FATAL_ERROR "iroh has no prebuilt release for windows-${VCPKG_TARGET_ARCHITECTURE}")
    endif()
    if(TARGET_TRIPLET MATCHES "mingw|uwp")
        message(FATAL_ERROR "iroh only ships an MSVC Windows binary")
    endif()
    set(_IROH_TARGET "x86_64-pc-windows-msvc")
else()
    message(FATAL_ERROR "iroh has no prebuilt release for ${TARGET_TRIPLET}")
endif()

if(VCPKG_TARGET_IS_WINDOWS)
    set(_IROH_ARCHIVE_EXT "zip")
    set(_IROH_EXE_SUFFIX ".exe")
    set(_IROH_TAR_FLAG "xf")
else()
    set(_IROH_ARCHIVE_EXT "tar.gz")
    set(_IROH_EXE_SUFFIX "")
    set(_IROH_TAR_FLAG "xzf")
endif()

set(_IROH_TOOLS "${CURRENT_PACKAGES_DIR}/tools/iroh")
file(MAKE_DIRECTORY "${_IROH_TOOLS}")

foreach(_IROH_TOOL dns-server relay)
    set(_IROH_ASSET "iroh-${_IROH_TOOL}-v${_IROH_VERSION}-${_IROH_TARGET}.${_IROH_ARCHIVE_EXT}")

    set(_IROH_TOOL_KEY "${_IROH_TOOL}")
    string(REPLACE "-" "_" _IROH_TOOL_KEY "${_IROH_TOOL_KEY}")
    string(TOUPPER "${_IROH_TOOL_KEY}" _IROH_TOOL_KEY)
    string(REPLACE "-" "_" _IROH_TARGET_KEY "${_IROH_TARGET}")
    set(_IROH_ARCHIVE_SHA512 "${_IROH_SHA_${_IROH_TOOL_KEY}_${_IROH_TARGET_KEY}}")
    if(NOT _IROH_ARCHIVE_SHA512)
        message(FATAL_ERROR "No SHA512 recorded for ${_IROH_ASSET}")
    endif()

    vcpkg_download_distfile(_IROH_ARCHIVE_PATH
        URLS "${_IROH_GITHUB_HOST}/n0-computer/iroh/releases/download/v${_IROH_VERSION}/${_IROH_ASSET}"
        FILENAME "${_IROH_ASSET}"
        SHA512 "${_IROH_ARCHIVE_SHA512}"
    )

    set(_IROH_STAGING "${CURRENT_BUILDTREES_DIR}/${_IROH_TOOL}-${TARGET_TRIPLET}")
    file(REMOVE_RECURSE "${_IROH_STAGING}")
    file(MAKE_DIRECTORY "${_IROH_STAGING}")
    vcpkg_execute_in_download_mode(
        COMMAND "${CMAKE_COMMAND}" -E tar "${_IROH_TAR_FLAG}" "${_IROH_ARCHIVE_PATH}"
        WORKING_DIRECTORY "${_IROH_STAGING}"
    )

    set(_IROH_BINARY "${_IROH_STAGING}/iroh-${_IROH_TOOL}${_IROH_EXE_SUFFIX}")
    if(NOT EXISTS "${_IROH_BINARY}")
        message(FATAL_ERROR "Unexpected iroh release layout in ${_IROH_ASSET}: ${_IROH_BINARY} is missing")
    endif()

    file(INSTALL "${_IROH_BINARY}"
        DESTINATION "${_IROH_TOOLS}"
        USE_SOURCE_PERMISSIONS
    )
endforeach()

vcpkg_install_copyright(
    COMMENT [=[
iroh is dual-licensed under the MIT and Apache-2.0 licenses. Both license texts are reproduced below.
]=]
    FILE_LIST
        "${_IROH_LICENSE_MIT}"
        "${_IROH_LICENSE_APACHE}"
)

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage"
    @ONLY
)
