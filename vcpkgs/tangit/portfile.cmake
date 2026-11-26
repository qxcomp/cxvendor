set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

# Upstream tag, not HEAD. No release date is recorded in this port.
set(_TANGIT_REF "v1.26.1")
set(_TANGIT_GITHUB_HOST "https://gh-proxy.com/https://github.com")

# SHA512 of archives fetched through the GITHUB_HOST / raw proxy above.
set(_TANGIT_SOURCE_SHA512 "047f286763aa5849a5365e4b06fce2e5125b5dcea3b59bf215baede8f15853733a88c74166d24f30ccafac3c4b1c36ccd27e02ce4d5e64dd5bcd04ddbd73df5d")
set(_TANGIT_LICENSE_SHA512 "4e82ed83ba26f90fb7c22b1139dad96c17dfad1ab111276a454a28a90813c51d3938d0d98d647ea34290cca7c5efa8ee4b75d37928864c254adc23566cb03024")
set(_TANGIT_LINUX_AMD64_SHA512 "b5d5cc9755814782ccd88105f2629aeef827a2d5bb066784a4342c708fc24744a5a3f0efbd4374947ce76c339761efb404cfaf943d9fb5a066657abb423c9322")
set(_TANGIT_LINUX_ARM64_SHA512 "060eef217f96666e5af5f3319dc0f79cb97f61d589a787b510736b9c8e0b6b11ff54fe57442febd06632b67963c504fecad7ceb1056c8f24391e827233c5b9e6")
set(_TANGIT_WINDOWS_AMD64_SHA512 "561c75e86c8a34f8d4e20f386515700f1dc1bdcbe5ed45860cae86c9b54ea411e3c38576ceb1cf9e52ad2197893015cd1700a6935ae58c8bca39964007a663ae")
set(_TANGIT_WINDOWS_ARM64_SHA512 "c768a6c2e534d3dfbdc680cc30dead81984007695870ef70d5b110939584881b86f440db1e6e779084cd86e11bbe3e31d66553945974186ff574240cc3006b8b")

set(_TANGIT_TOOLS "${CURRENT_PACKAGES_DIR}/tools/tangit")
file(MAKE_DIRECTORY "${_TANGIT_TOOLS}")

# macOS is built from source; Linux and Windows install the upstream release
# archives for the target architecture.
if(VCPKG_TARGET_IS_OSX)
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO FleetingEcho/lazygitGo
        REF "${_TANGIT_REF}"
        SHA512 "${_TANGIT_SOURCE_SHA512}"
        HEAD_REF main
        GITHUB_HOST "${_TANGIT_GITHUB_HOST}"
    )

    find_program(_TANGIT_GO NAMES go REQUIRED)
    execute_process(
        COMMAND "${_TANGIT_GO}" version
        OUTPUT_VARIABLE _TANGIT_GO_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REGEX MATCH "go([0-9]+)\\.([0-9]+)\\.([0-9]+)" _TANGIT_GO_MATCH "${_TANGIT_GO_VERSION}")
    if(NOT _TANGIT_GO_MATCH)
        message(FATAL_ERROR "Unable to parse Go version: ${_TANGIT_GO_VERSION}")
    endif()
    if(CMAKE_MATCH_1 LESS 1
        OR (CMAKE_MATCH_1 EQUAL 1 AND CMAKE_MATCH_2 LESS 27)
        OR (CMAKE_MATCH_1 EQUAL 1 AND CMAKE_MATCH_2 EQUAL 27 AND CMAKE_MATCH_3 LESS 3))
        message(FATAL_ERROR "tangit requires Go >= 1.27.3, found ${_TANGIT_GO_VERSION}")
    endif()

    set(_TANGIT_GO_BUILD_DIR "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel")
    file(MAKE_DIRECTORY "${_TANGIT_GO_BUILD_DIR}")
    vcpkg_execute_required_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            CGO_ENABLED=0
            GOWORK=off
            GOTOOLCHAIN=local
            GOCACHE="${_TANGIT_GO_BUILD_DIR}/go-cache"
            GOMODCACHE="${CURRENT_BUILDTREES_DIR}/go-mod-cache"
            "${_TANGIT_GO}" tool mygo build
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME build-tangit-macos
    )

    file(GLOB _TANGIT_APPS
        "${SOURCE_PATH}/build/*.app"
        "${SOURCE_PATH}/build/*/*.app"
        "${SOURCE_PATH}/*.app"
    )
    list(LENGTH _TANGIT_APPS _TANGIT_APP_COUNT)
    if(NOT _TANGIT_APP_COUNT EQUAL 1)
        message(FATAL_ERROR "Expected exactly one built .app, found ${_TANGIT_APP_COUNT}: ${_TANGIT_APPS}")
    endif()
    list(GET _TANGIT_APPS 0 _TANGIT_APP)
    get_filename_component(_TANGIT_APP_NAME "${_TANGIT_APP}" NAME)
    file(COPY "${_TANGIT_APP}" DESTINATION "${_TANGIT_TOOLS}")
    file(INSTALL
        "${CMAKE_CURRENT_LIST_DIR}/tangit-auto-update.sh"
        DESTINATION "${_TANGIT_TOOLS}"
    )
    file(CHMOD
        "${_TANGIT_TOOLS}/tangit-auto-update.sh"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )

    file(GLOB _TANGIT_APP_BINARIES "${_TANGIT_TOOLS}/${_TANGIT_APP_NAME}/Contents/MacOS/*")
    list(GET _TANGIT_APP_BINARIES 0 _TANGIT_APP_BINARY)
    set(_TANGIT_LAUNCHER "${_TANGIT_TOOLS}/lazygitGo")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/lazygitGo-macos.in" "${_TANGIT_LAUNCHER}" @ONLY)
    configure_file("${CMAKE_CURRENT_LIST_DIR}/lazygitGo-macos.in" "${_TANGIT_TOOLS}/tangit" @ONLY)
    file(CHMOD
        "${_TANGIT_LAUNCHER}"
        "${_TANGIT_TOOLS}/tangit"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
    set(_TANGIT_COPYRIGHT "${SOURCE_PATH}/LICENSE")

elseif(VCPKG_TARGET_IS_LINUX OR VCPKG_TARGET_IS_WINDOWS)
    if(VCPKG_TARGET_IS_LINUX)
        if(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
            set(_TANGIT_ASSET "lazygitgo-1.26.1-linux-arm64.tar.gz")
            set(_TANGIT_ASSET_SHA512 "${_TANGIT_LINUX_ARM64_SHA512}")
        else()
            set(_TANGIT_ASSET "lazygitgo-1.26.1-linux-amd64.tar.gz")
            set(_TANGIT_ASSET_SHA512 "${_TANGIT_LINUX_AMD64_SHA512}")
        endif()
    else()
        if(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
            set(_TANGIT_ASSET "lazygitgo-1.26.1-windows-arm64.tar.gz")
            set(_TANGIT_ASSET_SHA512 "${_TANGIT_WINDOWS_ARM64_SHA512}")
        else()
            set(_TANGIT_ASSET "lazygitgo-1.26.1-windows-amd64.tar.gz")
            set(_TANGIT_ASSET_SHA512 "${_TANGIT_WINDOWS_AMD64_SHA512}")
        endif()
    endif()

    vcpkg_download_distfile(
        _TANGIT_ARCHIVE
        URLS "${_TANGIT_GITHUB_HOST}/FleetingEcho/lazygitGo/releases/download/${_TANGIT_REF}/${_TANGIT_ASSET}"
        FILENAME "${_TANGIT_ASSET}"
        SHA512 "${_TANGIT_ASSET_SHA512}"
    )
    vcpkg_extract_source_archive_ex(
        OUT_SOURCE_PATH _TANGIT_PAYLOAD
        ARCHIVE "${_TANGIT_ARCHIVE}"
        NO_REMOVE_ONE_LEVEL
    )
    vcpkg_download_distfile(
        _TANGIT_LICENSE
        URLS "${_TANGIT_GITHUB_HOST}/FleetingEcho/lazygitGo/raw/${_TANGIT_REF}/LICENSE"
        FILENAME "tangit-${_TANGIT_REF}-LICENSE"
        SHA512 "${_TANGIT_LICENSE_SHA512}"
    )
    set(_TANGIT_COPYRIGHT "${_TANGIT_LICENSE}")

    if(VCPKG_TARGET_IS_LINUX)
        file(COPY "${_TANGIT_PAYLOAD}/lazygitgo" DESTINATION "${_TANGIT_TOOLS}")
        file(RENAME "${_TANGIT_TOOLS}/lazygitgo" "${_TANGIT_TOOLS}/lazygitgo.bin")
        foreach(_TANGIT_EXTRA libghostty-vt.so icon.png lazygitgo.png .mygo-files)
            if(EXISTS "${_TANGIT_PAYLOAD}/${_TANGIT_EXTRA}")
                file(COPY "${_TANGIT_PAYLOAD}/${_TANGIT_EXTRA}" DESTINATION "${_TANGIT_TOOLS}")
            endif()
        endforeach()
        # Upstream only creates the tray when tray.png is readable; keeping it
        # disabled also keeps close-to-tray off. Users can rename it back.
        if(EXISTS "${_TANGIT_PAYLOAD}/tray.png")
            file(COPY "${_TANGIT_PAYLOAD}/tray.png" DESTINATION "${_TANGIT_TOOLS}")
            file(RENAME "${_TANGIT_TOOLS}/tray.png" "${_TANGIT_TOOLS}/tray.png.disabled")
        endif()
        file(INSTALL
            "${CMAKE_CURRENT_LIST_DIR}/tangit-auto-update.sh"
            DESTINATION "${_TANGIT_TOOLS}"
        )
        file(CHMOD
            "${_TANGIT_TOOLS}/lazygitgo.bin"
            "${_TANGIT_TOOLS}/tangit-auto-update.sh"
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        )

        # Default to the MyGo CPU renderer on Linux. MyGo's Linux surface uses
        # GtkDrawingArea instead of GtkGLArea when MYGO_GPU=0, so no GL context
        # is created. An explicit non-zero MYGO_GPU is normalized to 1; unset,
        # empty and 0 mean 0. TANGIT_DETACHED defaults to 0 (foreground); the
        # wrapper maps that to the upstream's inverted private marker
        # LAZYGITGO_DETACHED_LAUNCH=1 so the child is not detached and its
        # stdout/stderr stay with the caller. The launcher also seeds
        # updater.json with automaticChecks=false by default.
        set(_TANGIT_LAUNCHER "${_TANGIT_TOOLS}/lazygitGo")
        configure_file("${CMAKE_CURRENT_LIST_DIR}/lazygitGo.in" "${_TANGIT_LAUNCHER}" @ONLY)
        configure_file("${CMAKE_CURRENT_LIST_DIR}/lazygitGo.in" "${_TANGIT_TOOLS}/tangit" @ONLY)
        file(CHMOD
            "${_TANGIT_LAUNCHER}"
            "${_TANGIT_TOOLS}/tangit"
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        )

        set(_TANGIT_DESKTOP_EXEC "${_TANGIT_LAUNCHER}")
        set(_TANGIT_DESKTOP_ICON "${_TANGIT_TOOLS}/icon.png")
        file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/applications")
        configure_file(
            "${CMAKE_CURRENT_LIST_DIR}/tangit.desktop.in"
            "${CURRENT_PACKAGES_DIR}/share/applications/tangit.desktop"
            @ONLY
        )
    else()
        foreach(_TANGIT_PAYLOAD_FILE lazygitGo.exe TanGit.exe ghostty-vt.dll icon.png)
            if(EXISTS "${_TANGIT_PAYLOAD}/${_TANGIT_PAYLOAD_FILE}")
                file(COPY "${_TANGIT_PAYLOAD}/${_TANGIT_PAYLOAD_FILE}" DESTINATION "${_TANGIT_TOOLS}")
            endif()
        endforeach()

        # cmd shims keep every documented command on the auto-update default
        # path; the real executables are renamed so they cannot be bypassed.
        if(EXISTS "${_TANGIT_TOOLS}/lazygitGo.exe")
            file(RENAME "${_TANGIT_TOOLS}/lazygitGo.exe" "${_TANGIT_TOOLS}/lazygitgo.bin.exe")
        endif()
        if(EXISTS "${_TANGIT_TOOLS}/TanGit.exe")
            file(RENAME "${_TANGIT_TOOLS}/TanGit.exe" "${_TANGIT_TOOLS}/tangit.bin.exe")
        endif()
        file(INSTALL
            "${CMAKE_CURRENT_LIST_DIR}/tangit-auto-update.ps1"
            DESTINATION "${_TANGIT_TOOLS}"
        )

        set(_TANGIT_WINDOWS_BINARY "lazygitgo.bin.exe")
        configure_file(
            "${CMAKE_CURRENT_LIST_DIR}/lazygitGo.cmd.in"
            "${_TANGIT_TOOLS}/lazygitGo.cmd"
            @ONLY
            NEWLINE_STYLE CRLF
        )
        if(EXISTS "${_TANGIT_TOOLS}/tangit.bin.exe")
            set(_TANGIT_WINDOWS_BINARY "tangit.bin.exe")
        endif()
        configure_file(
            "${CMAKE_CURRENT_LIST_DIR}/lazygitGo.cmd.in"
            "${_TANGIT_TOOLS}/tangit.cmd"
            @ONLY
            NEWLINE_STYLE CRLF
        )
    endif()
else()
    message(FATAL_ERROR "tangit supports linux, windows and osx only")
endif()

vcpkg_install_copyright(FILE_LIST "${_TANGIT_COPYRIGHT}")
file(INSTALL
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
)
