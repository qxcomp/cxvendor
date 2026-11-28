set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

# Updainium is a Tauri GUI application, not a C/C++ library. This port installs
# it as a tool under tools/updainium/; there are no headers or CMake targets.
set(_UPDAINIUM_VERSION "1.5.2")
set(_UPDAINIUM_RELEASE "https://gh-proxy.com/https://github.com/livrasand/Updainium/releases/download/v${_UPDAINIUM_VERSION}")
set(_UPDAINIUM_RAW "https://gh-proxy.com/https://raw.githubusercontent.com/livrasand/Updainium/v${_UPDAINIUM_VERSION}")

# LICENSE is shared by all three branches. SHA512 measured from the raw URL.
set(_UPDAINIUM_LICENSE_SHA512 "d361e5e8201481c6346ee6a886592c51265112be550d5224f1a7a6e116255c2f1ab8788df579d9b8372ed7bfd19bac4b6e70e00b472642966ab5b319b99a2686")
vcpkg_download_distfile(
    UPDAINIUM_LICENSE
    URLS "${_UPDAINIUM_RAW}/LICENSE"
    FILENAME "Updainium-${_UPDAINIUM_VERSION}-LICENSE"
    SHA512 "${_UPDAINIUM_LICENSE_SHA512}"
)

if(VCPKG_TARGET_IS_LINUX)
    if(NOT VCPKG_HOST_IS_LINUX OR NOT VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        message(FATAL_ERROR "updainium: native x64-linux only; cross-compilation is not supported")
    endif()

    # vcpkg_download_distfile only accepts SHA512; the release page publishes
    # SHA256, so skip the built-in SHA512 check and verify against SHA256SUMS.
    set(_UPDAINIUM_APPIMAGE_SHA256 "bb06b54ec9483a0d0df332e9ea512d56deb977721fa4158315ac97eb38043eb7")
    vcpkg_download_distfile(
        APPIMAGE
        URLS "${_UPDAINIUM_RELEASE}/Updainium_1.5.2_amd64.AppImage"
        FILENAME "Updainium-1.5.2-amd64.AppImage"
        SKIP_SHA512
    )
    file(SHA256 "${APPIMAGE}" _appimage_sha256)
    if(NOT _appimage_sha256 STREQUAL "${_UPDAINIUM_APPIMAGE_SHA256}")
        message(FATAL_ERROR "updainium: AppImage SHA256 mismatch: ${_appimage_sha256}")
    endif()
    file(INSTALL
        "${APPIMAGE}"
        DESTINATION "${CURRENT_PACKAGES_DIR}/tools/updainium"
        RENAME "Updainium.AppImage"
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )

elseif(VCPKG_TARGET_IS_WINDOWS)
    if(NOT VCPKG_HOST_IS_WINDOWS OR NOT VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        message(FATAL_ERROR "updainium: native x64-windows only; cross-compilation is not supported")
    endif()

    # Verify the prebuilt MSI against the SHA256 published in SHA256SUMS.
    set(_UPDAINIUM_MSI_SHA256 "f93d793ada675fd1fca7694163636e09680170a7b3afdd8ef0d777543f5be65a")
    vcpkg_download_distfile(
        MSI
        URLS "${_UPDAINIUM_RELEASE}/Updainium_1.5.2_x64_en-US.msi"
        FILENAME "Updainium-1.5.2-x64-en-US.msi"
        SKIP_SHA512
    )
    file(SHA256 "${MSI}" _msi_sha256)
    if(NOT _msi_sha256 STREQUAL "${_UPDAINIUM_MSI_SHA256}")
        message(FATAL_ERROR "updainium: MSI SHA256 mismatch: ${_msi_sha256}")
    endif()

    # Administrative install extracts the payload without installing the app.
    set(_msi_root "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/msi-root")
    file(MAKE_DIRECTORY "${_msi_root}")
    execute_process(
        COMMAND msiexec /a "${MSI}" /qn TARGETDIR="${_msi_root}"
        RESULT_VARIABLE _msi_result
    )
    if(NOT _msi_result EQUAL 0)
        message(FATAL_ERROR "updainium: msiexec administrative install failed")
    endif()

    file(GLOB_RECURSE _exe_list "${_msi_root}/*.exe")
    set(_payload_dir "")
    foreach(_candidate IN LISTS _exe_list)
        if(_candidate MATCHES "Updainium\\.exe$")
            get_filename_component(_payload_dir "${_candidate}" DIRECTORY)
        endif()
    endforeach()
    if(_payload_dir STREQUAL "")
        message(FATAL_ERROR "updainium: Updainium.exe not found in extracted MSI")
    endif()

    file(INSTALL
        "${_payload_dir}/"
        DESTINATION "${CURRENT_PACKAGES_DIR}/tools/updainium"
        USE_SOURCE_PERMISSIONS
    )

elseif(VCPKG_TARGET_IS_OSX)
    # CMAKE_HOST_SYSTEM_PROCESSOR is empty in vcpkg's script mode (cmake -P),
    # so compare the arch prefix of the host/target triplets instead.
    string(REGEX MATCH "^[^-]+" _host_arch "${HOST_TRIPLET}")
    string(REGEX MATCH "^[^-]+" _target_arch "${TARGET_TRIPLET}")
    if(NOT VCPKG_HOST_IS_OSX OR NOT "${_host_arch}" STREQUAL "${_target_arch}")
        message(FATAL_ERROR "updainium: macOS builds must be native; cross-compilation is not supported")
    endif()

    # Vite 8 needs Node 20.19+ / 22.12+, but Node 24+ binaries require
    # macOS 13.5. Big Sur (11) therefore needs Node 22.x
    find_program(NODE_EXECUTABLE NAMES node REQUIRED)
    execute_process(
        COMMAND "${NODE_EXECUTABLE}" -p "process.versions.node"
        OUTPUT_VARIABLE _node_version
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_node_version VERSION_LESS "20.19.0" OR _node_version VERSION_GREATER_EQUAL "24.0.0")
        message(FATAL_ERROR
            "updainium: need Node 20.19+ but <24 (use Node 22.x); "
            "Node 24+ binaries require macOS 13.5"
        )
    endif()

    find_program(NPM_EXECUTABLE NAMES npm REQUIRED)
    find_program(CARGO_EXECUTABLE NAMES cargo REQUIRED)

    # The source tarball has no SHA256 on the release page; use the SHA512
    # measured from the exact gh-proxy URL below.
    set(_UPDAINIUM_SOURCE_URL
        "https://gh-proxy.com/https://github.com/livrasand/Updainium/archive/refs/tags/v${_UPDAINIUM_VERSION}.tar.gz"
    )
    set(_UPDAINIUM_SOURCE_SHA512 "167268c602191a5f645a1a5198e85d98d242de0a2f79705b8185b2b7789be6cd54e8580784d2041ea387cb79393c554233ff39d10ac2c99dd98fb00ff9f86140")
    vcpkg_download_distfile(
        SOURCE_ARCHIVE
        URLS "${_UPDAINIUM_SOURCE_URL}"
        FILENAME "Updainium-${_UPDAINIUM_VERSION}.tar.gz"
        SHA512 "${_UPDAINIUM_SOURCE_SHA512}"
    )
    vcpkg_extract_source_archive_ex(
        OUT_SOURCE_PATH SOURCE_PATH
        ARCHIVE "${SOURCE_ARCHIVE}"
        REF v${_UPDAINIUM_VERSION}
    )

    set(_npm_cache "${CURRENT_BUILDTREES_DIR}/npm-cache")
    set(_cargo_home "${CURRENT_BUILDTREES_DIR}/cargo-home")
    file(MAKE_DIRECTORY "${_npm_cache}" "${_cargo_home}")
    set(ENV{npm_config_cache} "${_npm_cache}")
    set(ENV{CARGO_HOME} "${_cargo_home}")

    # Cargo.toml has a global [patch.crates-io] on jcfs/gtk-rs-core (git).
    # Route github.com through gh-proxy so the patch can be fetched.
    set(_git_config "${_cargo_home}/gitconfig")
    file(WRITE "${_git_config}"
        "[url \"https://gh-proxy.com/https://github.com/\"]\n\tinsteadOf = https://github.com/\n"
    )
    set(ENV{GIT_CONFIG_GLOBAL} "${_git_config}")
    set(ENV{CARGO_NET_GIT_FETCH_WITH_CLI} "true")

    execute_process(
        COMMAND "${NPM_EXECUTABLE}" ci
        WORKING_DIRECTORY "${SOURCE_PATH}"
        RESULT_VARIABLE _npm_result
    )
    if(NOT _npm_result EQUAL 0)
        message(FATAL_ERROR "updainium: npm ci failed")
    endif()

    execute_process(
        COMMAND "${NPM_EXECUTABLE}" run tauri build -- --bundles app
        WORKING_DIRECTORY "${SOURCE_PATH}"
        RESULT_VARIABLE _tauri_result
    )
    if(NOT _tauri_result EQUAL 0)
        message(FATAL_ERROR "updainium: tauri build failed")
    endif()

    set(_installed_app "${CURRENT_PACKAGES_DIR}/tools/updainium/Updainium.app")
    file(INSTALL
        "${SOURCE_PATH}/src-tauri/target/release/bundle/macos/Updainium.app"
        DESTINATION "${CURRENT_PACKAGES_DIR}/tools/updainium"
        USE_SOURCE_PERMISSIONS
    )

    # CMake's file(INSTALL) drops metadata sealed by the ad-hoc signature;
    # the copied app is then SIGKILLed at launch. Re-sign the installed copy.
    execute_process(
        COMMAND /usr/bin/codesign --force --deep --sign - "${_installed_app}"
        RESULT_VARIABLE _codesign_result
    )
    if(NOT _codesign_result EQUAL 0)
        message(FATAL_ERROR "updainium: codesign of installed app failed")
    endif()
    execute_process(
        COMMAND /usr/bin/codesign --verify --deep --strict "${_installed_app}"
        RESULT_VARIABLE _verify_result
    )
    if(NOT _verify_result EQUAL 0)
        message(FATAL_ERROR "updainium: installed app signature verification failed")
    endif()

    unset(ENV{npm_config_cache})
    unset(ENV{CARGO_HOME})
    unset(ENV{GIT_CONFIG_GLOBAL})
    unset(ENV{CARGO_NET_GIT_FETCH_WITH_CLI})
else()
    message(FATAL_ERROR "updainium: unsupported target platform")
endif()

file(INSTALL
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/updainium"
)

vcpkg_install_copyright(FILE_LIST "${UPDAINIUM_LICENSE}")
