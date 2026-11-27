set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)

# Native addons are built for the host OS and architecture. A different target
# triplet is fine when OS/architecture match (e.g. x64-osx-dynamic on an
# x64-osx host); only a genuinely different host OS or architecture would put
# unusable addons into the target package.
# CMAKE_HOST_SYSTEM_PROCESSOR is empty in vcpkg's script-mode portfiles, so
# derive the host architecture from HOST_TRIPLET (vcpkg triplets encode it as
# a name prefix). An unrecognised host triplet is left unknown rather than
# rejected, to avoid false positives on custom triplets.
set(_JOPLIN_HOST_ARCH "")
if(HOST_TRIPLET MATCHES "^arm64")
    set(_JOPLIN_HOST_ARCH "arm64")
elseif(HOST_TRIPLET MATCHES "^x64")
    set(_JOPLIN_HOST_ARCH "x64")
endif()

set(_JOPLIN_HOST_OS_MATCHES FALSE)
if(VCPKG_HOST_IS_LINUX AND VCPKG_TARGET_IS_LINUX)
    set(_JOPLIN_HOST_OS_MATCHES TRUE)
elseif(VCPKG_HOST_IS_OSX AND VCPKG_TARGET_IS_OSX)
    set(_JOPLIN_HOST_OS_MATCHES TRUE)
elseif(VCPKG_HOST_IS_WINDOWS AND VCPKG_TARGET_IS_WINDOWS)
    set(_JOPLIN_HOST_OS_MATCHES TRUE)
endif()

if(NOT _JOPLIN_HOST_OS_MATCHES OR (_JOPLIN_HOST_ARCH AND NOT _JOPLIN_HOST_ARCH STREQUAL VCPKG_TARGET_ARCHITECTURE))
    message(FATAL_ERROR
        "joplin-terminal native addons are built for the host OS and architecture; "
        "building target '${TARGET_TRIPLET}' (${VCPKG_TARGET_ARCHITECTURE}) on "
        "'${HOST_TRIPLET}' (${CMAKE_HOST_SYSTEM_PROCESSOR}) would install unusable addons."
    )
endif()

# Upstream is the official npm package joplin@3.7.1 (Joplin CLI Client).
# No release date is recorded in this port; the version is pinned by the tarball.
set(_JOPLIN_TARBALL_URL "https://registry.npmjs.org/joplin/-/joplin-3.7.1.tgz")
set(_JOPLIN_TARBALL_SHA512 "f386a34d9efa5b205bac262166a71136f7322b78ca4ce341649859310b1418231d4e94520916a78761b9bbcdb34a1ee2af8aa8d9220441d6b759d2d9eeff8765")
set(_JOPLIN_GITHUB_HOST "https://gh-proxy.com/https://github.com")
set(_JOPLIN_LICENSE_SHA512 "a2b8015971b53608801eda8fe14f67906363c20610ec4a677d3cddae56a5200d04ed49138497b23ce9aedc342943a36bf92f3c8a2ba7554e5d27a4aeea2314b7")

vcpkg_download_distfile(
    _JOPLIN_TARBALL
    URLS "${_JOPLIN_TARBALL_URL}"
    FILENAME "joplin-3.7.1.tgz"
    SHA512 "${_JOPLIN_TARBALL_SHA512}"
)
vcpkg_download_distfile(
    _JOPLIN_LICENSE
    URLS "${_JOPLIN_GITHUB_HOST}/laurent22/joplin/raw/v3.7.1/LICENSE"
    FILENAME "joplin-terminal-v3.7.1-LICENSE"
    SHA512 "${_JOPLIN_LICENSE_SHA512}"
)

# The npm tarball has a single top-level "package/" directory, which this
# helper strips by default, leaving the application root in _JOPLIN_STAGING.
vcpkg_extract_source_archive_ex(
    OUT_SOURCE_PATH _JOPLIN_STAGING
    ARCHIVE "${_JOPLIN_TARBALL}"
)
if(NOT EXISTS "${_JOPLIN_STAGING}/main.js")
    message(FATAL_ERROR "Unexpected npm tarball layout: ${_JOPLIN_STAGING}")
endif()

# Prefer a Node.js already available on the build machine. Joplin itself only
# needs Node >= 10, but sharp@0.34.5 requires ^18.17.0 || ^20.3.0 || >=21.0.0,
# so that is the binding constraint. A Node that exists but cannot run (for
# example Node 24 built for macOS 13.5 on an older system) is skipped.
function(_joplin_node_supported _out _version)
    set(${_out} FALSE PARENT_SCOPE)
    if(NOT _version MATCHES "^([0-9]+)\\.([0-9]+)")
        return()
    endif()
    set(_major "${CMAKE_MATCH_1}")
    set(_minor "${CMAKE_MATCH_2}")
    if(_major EQUAL 18 AND _minor GREATER_EQUAL 17)
        set(${_out} TRUE PARENT_SCOPE)
    elseif(_major EQUAL 20 AND _minor GREATER_EQUAL 3)
        set(${_out} TRUE PARENT_SCOPE)
    elseif(_major GREATER_EQUAL 21)
        set(${_out} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(_joplin_probe_node _out_ok _out_version _out_status _candidate)
    set(${_out_ok} FALSE PARENT_SCOPE)
    set(${_out_version} "" PARENT_SCOPE)
    set(${_out_status} "not found" PARENT_SCOPE)
    if(NOT EXISTS "${_candidate}")
        return()
    endif()
    execute_process(
        COMMAND "${_candidate}" -p process.versions.node
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _ver
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT _rc EQUAL 0)
        set(${_out_status} "cannot run (exit ${_rc})" PARENT_SCOPE)
        return()
    endif()
    set(${_out_version} "${_ver}" PARENT_SCOPE)
    _joplin_node_supported(_supported "${_ver}")
    if(NOT _supported)
        set(${_out_status} "unsupported version ${_ver}" PARENT_SCOPE)
        return()
    endif()
    set(${_out_ok} TRUE PARENT_SCOPE)
    set(${_out_status} "ok" PARENT_SCOPE)
endfunction()

set(_JOPLIN_NODE "")
set(_JOPLIN_NODE_VERSION "")
set(_JOPLIN_NODE_SOURCE "")
set(_JOPLIN_FALLBACK_ROOT "")
set(_JOPLIN_FALLBACK_PREFIX "")

# Search every PATH entry: vcpkg may put its own tools first, and a broken
# binary there must not hide a working system Node.
if(VCPKG_HOST_IS_WINDOWS)
    set(_JOPLIN_SEARCH_PATHS "$ENV{PATH}")
else()
    string(REPLACE ":" ";" _JOPLIN_SEARCH_PATHS "$ENV{PATH}")
endif()
set(_JOPLIN_REJECTED "")
foreach(_dir IN LISTS _JOPLIN_SEARCH_PATHS)
    if(_dir STREQUAL "" OR _dir STREQUAL ".")
        continue()
    endif()
    foreach(_name node node.exe)
        _joplin_probe_node(_ok _ver _status "${_dir}/${_name}")
        if(_ok)
            set(_JOPLIN_NODE "${_dir}/${_name}")
            set(_JOPLIN_NODE_VERSION "${_ver}")
            set(_JOPLIN_NODE_SOURCE "system PATH")
            break()
        elseif(NOT _status STREQUAL "not found")
            list(APPEND _JOPLIN_REJECTED "${_dir}/${_name}: ${_status}")
        endif()
    endforeach()
    if(_JOPLIN_NODE)
        break()
    endif()
endforeach()
foreach(_msg IN LISTS _JOPLIN_REJECTED)
    message(STATUS "Skipping Node candidate: ${_msg}")
endforeach()

# On-demand fallback: download Node only when no usable system Node exists.
# A vcpkg dependency would be installed unconditionally, wasting a download on
# machines that already have a suitable Node, so fetch the archive here.
if(NOT _JOPLIN_NODE)
    set(_JOPLIN_NODE_VERSION "22.23.5")
    if(VCPKG_HOST_IS_WINDOWS AND _JOPLIN_HOST_ARCH STREQUAL "x64")
        set(_JOPLIN_FALLBACK_PLATFORM "win-x64")
        set(_JOPLIN_ARCHIVE "node-v22.23.5-win-x64.zip")
        set(_JOPLIN_HASH "d2599b1e31274a1573db944d1860642ee433121b8ed368582133825ae9cf977c99b138b12d54f2e439f4b100aa79dc587ae927afa1efc02fbc3bde047e4f239f")
    elseif(VCPKG_HOST_IS_WINDOWS AND _JOPLIN_HOST_ARCH STREQUAL "arm64")
        set(_JOPLIN_FALLBACK_PLATFORM "win-arm64")
        set(_JOPLIN_ARCHIVE "node-v22.23.5-win-arm64.zip")
        set(_JOPLIN_HASH "0a806286b552cfc02129ed571a92c46cc00f69eca896ca5364ca2e8767c61e74648c99dc5a38ca87dbcf11a4b5755c18aafa6682833af533e0516453504ccffe")
    elseif(VCPKG_HOST_IS_OSX AND _JOPLIN_HOST_ARCH STREQUAL "x64")
        set(_JOPLIN_FALLBACK_PLATFORM "darwin-x64")
        set(_JOPLIN_ARCHIVE "node-v22.23.5-darwin-x64.tar.gz")
        set(_JOPLIN_HASH "08dde8aa57ac62e3da4b54701631f1cff199c345a19478ef523023501b1c207b768d1eb371e17a2d607c2b41755c080a0a11240312dd9cdc008b9319192a98dc")
    elseif(VCPKG_HOST_IS_OSX AND _JOPLIN_HOST_ARCH STREQUAL "arm64")
        set(_JOPLIN_FALLBACK_PLATFORM "darwin-arm64")
        set(_JOPLIN_ARCHIVE "node-v22.23.5-darwin-arm64.tar.gz")
        set(_JOPLIN_HASH "2c669966adb30c708ff77a4e08aae83b1a7f867e0da5e6e61b6b9eb5500130a6315bec3a5d5c224f779597e94a0a598c8d18d77ae4ac24876617a73c54cb26ee")
    elseif(VCPKG_HOST_IS_LINUX AND _JOPLIN_HOST_ARCH STREQUAL "x64")
        set(_JOPLIN_FALLBACK_PLATFORM "linux-x64")
        set(_JOPLIN_ARCHIVE "node-v22.23.5-linux-x64.tar.xz")
        set(_JOPLIN_HASH "5c79b4662c43fbb27999348e2db82bc008bfc9c1ae34358dae6b0e0f4bc74f3ef42bf812780a0a408ec590eeffd9c5236b8a7c2caf1bc49a639712e21551c78b")
    elseif(VCPKG_HOST_IS_LINUX AND _JOPLIN_HOST_ARCH STREQUAL "arm64")
        set(_JOPLIN_FALLBACK_PLATFORM "linux-arm64")
        set(_JOPLIN_ARCHIVE "node-v22.23.5-linux-arm64.tar.xz")
        set(_JOPLIN_HASH "a130da1abf74c9581b43994ab52bf0cbbc6335ed2c214ac3a3c95de2cfafeb5adf47e8c8d516eff58534fe0962226aaf84479244f7071ad8ac8f148b14952e52")
    else()
        message(FATAL_ERROR "Cannot select a fallback Node archive for host '${HOST_TRIPLET}'")
    endif()

    vcpkg_download_distfile(_JOPLIN_NODE_ARCHIVE
        URLS "https://nodejs.org/dist/v22.23.5/${_JOPLIN_ARCHIVE}"
        FILENAME "${_JOPLIN_ARCHIVE}"
        SHA512 "${_JOPLIN_HASH}"
    )
    set(_JOPLIN_FALLBACK_ROOT "${CURRENT_BUILDTREES_DIR}/node-22.23.5-${TARGET_TRIPLET}")
    file(MAKE_DIRECTORY "${_JOPLIN_FALLBACK_ROOT}")
    vcpkg_execute_in_download_mode(
        COMMAND "${CMAKE_COMMAND}" -E tar xf "${_JOPLIN_NODE_ARCHIVE}"
        WORKING_DIRECTORY "${_JOPLIN_FALLBACK_ROOT}"
    )
    set(_JOPLIN_FALLBACK_PREFIX "${_JOPLIN_FALLBACK_ROOT}/node-v22.23.5-${_JOPLIN_FALLBACK_PLATFORM}")
    if(VCPKG_HOST_IS_WINDOWS)
        set(_JOPLIN_CANDIDATE "${_JOPLIN_FALLBACK_PREFIX}/node.exe")
    else()
        set(_JOPLIN_CANDIDATE "${_JOPLIN_FALLBACK_PREFIX}/bin/node")
    endif()
    _joplin_probe_node(_ok _ver _status "${_JOPLIN_CANDIDATE}")
    if(NOT _ok)
        message(FATAL_ERROR "Downloaded Node failed validation (${_status}): ${_JOPLIN_CANDIDATE}")
    endif()
    set(_JOPLIN_NODE "${_JOPLIN_CANDIDATE}")
    set(_JOPLIN_NODE_VERSION "${_ver}")
    set(_JOPLIN_NODE_SOURCE "downloaded Node 22.23.5")
endif()

message(STATUS "Using ${_JOPLIN_NODE_SOURCE} Node ${_JOPLIN_NODE_VERSION}: ${_JOPLIN_NODE}")

get_filename_component(_JOPLIN_NODE_DIR "${_JOPLIN_NODE}" DIRECTORY)
get_filename_component(_JOPLIN_NODE_PREFIX "${_JOPLIN_NODE_DIR}/.." ABSOLUTE)
set(_JOPLIN_SEARCH_ROOTS "${_JOPLIN_NODE_PREFIX}")
if(_JOPLIN_FALLBACK_PREFIX)
    list(APPEND _JOPLIN_SEARCH_ROOTS "${_JOPLIN_FALLBACK_PREFIX}")
endif()
find_file(_JOPLIN_NPM_CLI
    NAMES npm-cli.js
    PATHS ${_JOPLIN_SEARCH_ROOTS}
    PATH_SUFFIXES lib/node_modules/npm/bin node_modules/npm/bin
    NO_DEFAULT_PATH
    REQUIRED
)
vcpkg_add_to_path(PREPEND "${_JOPLIN_NODE_DIR}")

# keytar/sqlite3 ship NAPI prebuilds, but a failed GitHub download makes npm
# fall back to node-gyp. Point node-gyp at the headers shipped with the host
# Node when present, and keep npm's cache inside buildtrees.
set(_JOPLIN_NPM_ENV
    "npm_config_cache=${CURRENT_BUILDTREES_DIR}/npm-cache-${TARGET_TRIPLET}"
    npm_config_update_notifier=false
    npm_config_audit=false
    npm_config_fund=false
    # sharp must use its prebuilt addon/libvips packages. Otherwise a newer
    # system libvips makes it build from source, which fails against the old
    # node-addon-api hoisted here by keytar.
    SHARP_IGNORE_GLOBAL_LIBVIPS=1
)
find_file(_JOPLIN_NODE_H
    NAMES node.h
    PATHS ${_JOPLIN_SEARCH_ROOTS}
    PATH_SUFFIXES include/node
    NO_DEFAULT_PATH
)
if(_JOPLIN_NODE_H)
    get_filename_component(_JOPLIN_INCLUDE_NODE "${_JOPLIN_NODE_H}" DIRECTORY)
    get_filename_component(_JOPLIN_INCLUDE_DIR "${_JOPLIN_INCLUDE_NODE}" DIRECTORY)
    get_filename_component(_JOPLIN_NODE_ROOT "${_JOPLIN_INCLUDE_DIR}" DIRECTORY)
    list(APPEND _JOPLIN_NPM_ENV "npm_config_nodedir=${_JOPLIN_NODE_ROOT}")
endif()

vcpkg_execute_required_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${_JOPLIN_NPM_ENV}
        "${_JOPLIN_NODE}" "${_JOPLIN_NPM_CLI}" install --omit=dev
    WORKING_DIRECTORY "${_JOPLIN_STAGING}"
    LOGNAME install-joplin-terminal
)

set(_JOPLIN_TOOLS "${CURRENT_PACKAGES_DIR}/tools/joplin-terminal")
file(MAKE_DIRECTORY "${_JOPLIN_TOOLS}")
file(COPY "${_JOPLIN_STAGING}/" DESTINATION "${_JOPLIN_TOOLS}")

if(VCPKG_TARGET_IS_WINDOWS)
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/joplin.cmd.in"
        "${_JOPLIN_TOOLS}/joplin.cmd"
        @ONLY
        NEWLINE_STYLE CRLF
    )
else()
    set(_JOPLIN_LAUNCHER "${_JOPLIN_TOOLS}/joplin")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/joplin.in" "${_JOPLIN_LAUNCHER}" @ONLY)
    file(CHMOD
        "${_JOPLIN_LAUNCHER}"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
endif()

vcpkg_install_copyright(FILE_LIST "${_JOPLIN_LICENSE}")
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage"
    @ONLY
)
