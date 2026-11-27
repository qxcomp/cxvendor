# Standalone host tool: Node.js 22.23.5 from official prebuilt binaries.
# Node 22 supports macOS 11.0+, unlike the builtin vcpkg-tool-nodejs (Node 24),
# which is built for macOS 13.5+ and cannot start on older systems.
# Every SHA512 below was verified against nodejs.org's official SHA256 manifest.
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

set(_NODEJS22_PLATFORM "")
set(_NODEJS22_EXT "")
set(_NODEJS22_HASH "")

if(VCPKG_TARGET_IS_WINDOWS AND VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(_NODEJS22_PLATFORM "win-x64")
    set(_NODEJS22_EXT ".zip")
    set(_NODEJS22_HASH "d2599b1e31274a1573db944d1860642ee433121b8ed368582133825ae9cf977c99b138b12d54f2e439f4b100aa79dc587ae927afa1efc02fbc3bde047e4f239f")
elseif(VCPKG_TARGET_IS_WINDOWS AND VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    set(_NODEJS22_PLATFORM "win-arm64")
    set(_NODEJS22_EXT ".zip")
    set(_NODEJS22_HASH "0a806286b552cfc02129ed571a92c46cc00f69eca896ca5364ca2e8767c61e74648c99dc5a38ca87dbcf11a4b5755c18aafa6682833af533e0516453504ccffe")
elseif(VCPKG_TARGET_IS_OSX AND VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(_NODEJS22_PLATFORM "darwin-x64")
    set(_NODEJS22_EXT ".tar.gz")
    set(_NODEJS22_HASH "08dde8aa57ac62e3da4b54701631f1cff199c345a19478ef523023501b1c207b768d1eb371e17a2d607c2b41755c080a0a11240312dd9cdc008b9319192a98dc")
elseif(VCPKG_TARGET_IS_OSX AND VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    set(_NODEJS22_PLATFORM "darwin-arm64")
    set(_NODEJS22_EXT ".tar.gz")
    set(_NODEJS22_HASH "2c669966adb30c708ff77a4e08aae83b1a7f867e0da5e6e61b6b9eb5500130a6315bec3a5d5c224f779597e94a0a598c8d18d77ae4ac24876617a73c54cb26ee")
elseif(VCPKG_TARGET_IS_LINUX AND VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(_NODEJS22_PLATFORM "linux-x64")
    set(_NODEJS22_EXT ".tar.xz")
    set(_NODEJS22_HASH "5c79b4662c43fbb27999348e2db82bc008bfc9c1ae34358dae6b0e0f4bc74f3ef42bf812780a0a408ec590eeffd9c5236b8a7c2caf1bc49a639712e21551c78b")
elseif(VCPKG_TARGET_IS_LINUX AND VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    set(_NODEJS22_PLATFORM "linux-arm64")
    set(_NODEJS22_EXT ".tar.xz")
    set(_NODEJS22_HASH "a130da1abf74c9581b43994ab52bf0cbbc6335ed2c214ac3a3c95de2cfafeb5adf47e8c8d516eff58534fe0962226aaf84479244f7071ad8ac8f148b14952e52")
else()
    message(FATAL_ERROR "Target not yet supported by '${PORT}'")
endif()

# A prebuilt Node binary only runs on its own OS/architecture. Same OS and
# architecture with a different linkage triplet (e.g. x64-osx-dynamic) is fine;
# a genuine mismatch is reported instead of silently installing a broken tool.
set(_NODEJS22_TARGET_OS "")
if(VCPKG_TARGET_IS_LINUX)
    set(_NODEJS22_TARGET_OS "linux")
elseif(VCPKG_TARGET_IS_OSX)
    set(_NODEJS22_TARGET_OS "osx")
elseif(VCPKG_TARGET_IS_WINDOWS)
    set(_NODEJS22_TARGET_OS "windows")
endif()
set(_NODEJS22_HOST_OS "")
if(VCPKG_HOST_IS_LINUX)
    set(_NODEJS22_HOST_OS "linux")
elseif(VCPKG_HOST_IS_OSX)
    set(_NODEJS22_HOST_OS "osx")
elseif(VCPKG_HOST_IS_WINDOWS)
    set(_NODEJS22_HOST_OS "windows")
endif()
set(_NODEJS22_HOST_ARCH "")
if(HOST_TRIPLET MATCHES "^arm64")
    set(_NODEJS22_HOST_ARCH "arm64")
elseif(HOST_TRIPLET MATCHES "^x64")
    set(_NODEJS22_HOST_ARCH "x64")
endif()
if(_NODEJS22_HOST_OS AND NOT _NODEJS22_HOST_OS STREQUAL _NODEJS22_TARGET_OS)
    message(WARNING
        "Installing a ${_NODEJS22_TARGET_OS} Node binary on a ${_NODEJS22_HOST_OS} "
        "host; the tool will not be runnable on this machine."
    )
elseif(_NODEJS22_HOST_ARCH AND NOT _NODEJS22_HOST_ARCH STREQUAL VCPKG_TARGET_ARCHITECTURE)
    message(WARNING
        "Installing a ${VCPKG_TARGET_ARCHITECTURE} Node binary on a "
        "${_NODEJS22_HOST_ARCH} host; the tool will not be runnable on this machine."
    )
endif()

set(_NODEJS22_ARCHIVE "node-v${VERSION}-${_NODEJS22_PLATFORM}${_NODEJS22_EXT}")
vcpkg_download_distfile(ARCHIVE_PATH
    URLS "https://nodejs.org/dist/v${VERSION}/${_NODEJS22_ARCHIVE}"
    FILENAME "${_NODEJS22_ARCHIVE}"
    SHA512 "${_NODEJS22_HASH}"
)

set(_NODEJS22_EXTRACT_ROOT "${CURRENT_BUILDTREES_DIR}/nodejs22-${TARGET_TRIPLET}")
file(REMOVE_RECURSE "${_NODEJS22_EXTRACT_ROOT}")
file(MAKE_DIRECTORY "${_NODEJS22_EXTRACT_ROOT}")
vcpkg_execute_in_download_mode(
    COMMAND "${CMAKE_COMMAND}" -E tar xf "${ARCHIVE_PATH}"
    WORKING_DIRECTORY "${_NODEJS22_EXTRACT_ROOT}"
)
set(_NODEJS22_SOURCE "${_NODEJS22_EXTRACT_ROOT}/node-v${VERSION}-${_NODEJS22_PLATFORM}")
if(VCPKG_TARGET_IS_WINDOWS)
    set(_NODEJS22_BINARY "${_NODEJS22_SOURCE}/node.exe")
else()
    set(_NODEJS22_BINARY "${_NODEJS22_SOURCE}/bin/node")
endif()
if(NOT EXISTS "${_NODEJS22_BINARY}")
    message(FATAL_ERROR "Unexpected Node.js archive layout: ${_NODEJS22_SOURCE}")
endif()

# A dedicated directory avoids overwriting the builtin vcpkg-tool-nodejs
# installation in the same vcpkg root.
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/tools")
file(COPY "${_NODEJS22_SOURCE}/" DESTINATION "${CURRENT_PACKAGES_DIR}/tools/nodejs22")

# Do not break code signatures on macOS.
if(VCPKG_TARGET_IS_OSX)
    set(VCPKG_FIXUP_MACHO_RPATH OFF)
endif()
