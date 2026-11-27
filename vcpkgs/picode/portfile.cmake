set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)

# npm installs platform-specific optional packages, so the tool is built for
# the host OS and architecture. A different target triplet is fine when OS and
# architecture match; a genuine mismatch would install unusable binaries.
set(_PICODE_HOST_ARCH "")
if(HOST_TRIPLET MATCHES "^arm64")
    set(_PICODE_HOST_ARCH "arm64")
elseif(HOST_TRIPLET MATCHES "^x64")
    set(_PICODE_HOST_ARCH "x64")
endif()
set(_PICODE_HOST_OS_MATCHES FALSE)
if(VCPKG_HOST_IS_LINUX AND VCPKG_TARGET_IS_LINUX)
    set(_PICODE_HOST_OS_MATCHES TRUE)
elseif(VCPKG_HOST_IS_OSX AND VCPKG_TARGET_IS_OSX)
    set(_PICODE_HOST_OS_MATCHES TRUE)
elseif(VCPKG_HOST_IS_WINDOWS AND VCPKG_TARGET_IS_WINDOWS)
    set(_PICODE_HOST_OS_MATCHES TRUE)
endif()
if(NOT _PICODE_HOST_OS_MATCHES OR (_PICODE_HOST_ARCH AND NOT _PICODE_HOST_ARCH STREQUAL VCPKG_TARGET_ARCHITECTURE))
    message(FATAL_ERROR
        "picode installs host OS/architecture specific packages; building target "
        "'${TARGET_TRIPLET}' on '${HOST_TRIPLET}' would install unusable binaries."
    )
endif()

# Upstream artifact: official npm tarball. The GitHub release binaries require
# macOS 13+, so the npm bundle is used to keep osx11 support via Node 22.
set(_PICODE_TARBALL_URL "https://registry.npmjs.org/@earendil-works/pi-coding-agent/-/pi-coding-agent-1.6.1.tgz")
set(_PICODE_TARBALL_SHA512 "f9549bf793783c1f86ed32dc7a0ef091b253df5044788067513f79d3483d6b5aceb02c7bf02530d58146b4b711852ea38fca5d7e6d1f170ada92b30bbba95501")
set(_PICODE_GITHUB_HOST "https://gh-proxy.com/https://github.com")
set(_PICODE_LICENSE_SHA512 "f091a4be837de5cff430f71ca1e9692f85c0f57d51220054f4ba12e62ed6953bb75ba7c9b35a2b2a20719876c1a17d1a3f6ec976ccedca4962e58ec5978cbc29")

vcpkg_download_distfile(_PICODE_TARBALL
    URLS "${_PICODE_TARBALL_URL}"
    FILENAME "pi-coding-agent-1.6.1.tgz"
    SHA512 "${_PICODE_TARBALL_SHA512}"
)
vcpkg_download_distfile(_PICODE_LICENSE
    URLS "${_PICODE_GITHUB_HOST}/earendil-works/pi/raw/v1.6.1/LICENSE"
    FILENAME "picode-v1.6.1-LICENSE"
    SHA512 "${_PICODE_LICENSE_SHA512}"
)

# The npm tarball has a single "package/" root, stripped by this helper.
vcpkg_extract_source_archive_ex(
    OUT_SOURCE_PATH _PICODE_STAGING
    ARCHIVE "${_PICODE_TARBALL}"
)
if(NOT EXISTS "${_PICODE_STAGING}/dist/bundle/cli.js")
    message(FATAL_ERROR "Unexpected npm tarball layout: ${_PICODE_STAGING}")
endif()

# Pi requires Node >= 22.19.0. Skip binaries that cannot run at all.
function(_picode_node_supported _out _version)
    set(${_out} FALSE PARENT_SCOPE)
    if(NOT _version MATCHES "^([0-9]+)\\.([0-9]+)")
        return()
    endif()
    set(_major "${CMAKE_MATCH_1}")
    set(_minor "${CMAKE_MATCH_2}")
    if(_major GREATER 22 OR (_major EQUAL 22 AND _minor GREATER_EQUAL 19))
        set(${_out} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(_picode_probe_node _out_ok _out_version _out_status _candidate)
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
    _picode_node_supported(_supported "${_ver}")
    if(NOT _supported)
        set(${_out_status} "unsupported version ${_ver}" PARENT_SCOPE)
        return()
    endif()
    set(${_out_ok} TRUE PARENT_SCOPE)
    set(${_out_status} "ok" PARENT_SCOPE)
endfunction()

set(_PICODE_NODE "")
set(_PICODE_NODE_VERSION "")
set(_PICODE_NODE_SOURCE "")
set(_PICODE_FALLBACK_ROOT "")
set(_PICODE_FALLBACK_PREFIX "")
set(_PICODE_NODEJS22_PREFIX "")

# The manifest feature declares the conditional dependency; the install script
# enables it only when no usable system Node exists.
if("nodejs22" IN_LIST FEATURES)
    foreach(_root
        "${CURRENT_HOST_INSTALLED_DIR}/tools/nodejs22"
        "${CURRENT_INSTALLED_DIR}/tools/nodejs22")
        if(VCPKG_HOST_IS_WINDOWS)
            set(_PICODE_CANDIDATES "${_root}/node.exe")
        else()
            set(_PICODE_CANDIDATES "${_root}/bin/node" "${_root}/node")
        endif()
        foreach(_candidate IN LISTS _PICODE_CANDIDATES)
            _picode_probe_node(_ok _ver _status "${_candidate}")
            if(_ok)
                set(_PICODE_NODE "${_candidate}")
                set(_PICODE_NODE_VERSION "${_ver}")
                set(_PICODE_NODE_SOURCE "nodejs22 host tool")
                set(_PICODE_NODEJS22_PREFIX "${_root}")
                break()
            endif()
        endforeach()
        if(_PICODE_NODE)
            break()
        endif()
    endforeach()
    if(NOT _PICODE_NODE)
        message(FATAL_ERROR
            "The 'nodejs22' feature is enabled but the nodejs22 host tool was "
            "not found or is unusable. Check 'vcpkg list nodejs22'."
        )
    endif()
endif()

# Search every PATH entry: a broken binary must not hide a working Node later.
if(NOT _PICODE_NODE)
    if(VCPKG_HOST_IS_WINDOWS)
        set(_PICODE_SEARCH_PATHS "$ENV{PATH}")
    else()
        string(REPLACE ":" ";" _PICODE_SEARCH_PATHS "$ENV{PATH}")
    endif()
    set(_PICODE_REJECTED "")
    foreach(_dir IN LISTS _PICODE_SEARCH_PATHS)
        if(_dir STREQUAL "" OR _dir STREQUAL ".")
            continue()
        endif()
        foreach(_name node node.exe)
            _picode_probe_node(_ok _ver _status "${_dir}/${_name}")
            if(_ok)
                set(_PICODE_NODE "${_dir}/${_name}")
                set(_PICODE_NODE_VERSION "${_ver}")
                set(_PICODE_NODE_SOURCE "system PATH")
                break()
            elseif(NOT _status STREQUAL "not found")
                list(APPEND _PICODE_REJECTED "${_dir}/${_name}: ${_status}")
            endif()
        endforeach()
        if(_PICODE_NODE)
            break()
        endif()
    endforeach()
    foreach(_msg IN LISTS _PICODE_REJECTED)
        message(STATUS "Skipping Node candidate: ${_msg}")
    endforeach()
endif()

# Last-resort build-time fallback. The normal install script enables the
# nodejs22 feature instead; this keeps plain 'vcpkg install picode' working.
if(NOT _PICODE_NODE)
    set(_PICODE_NODE_VERSION "22.23.5")
    if(VCPKG_HOST_IS_WINDOWS AND _PICODE_HOST_ARCH STREQUAL "x64")
        set(_PICODE_FALLBACK_PLATFORM "win-x64")
        set(_PICODE_ARCHIVE "node-v22.23.5-win-x64.zip")
        set(_PICODE_HASH "d2599b1e31274a1573db944d1860642ee433121b8ed368582133825ae9cf977c99b138b12d54f2e439f4b100aa79dc587ae927afa1efc02fbc3bde047e4f239f")
    elseif(VCPKG_HOST_IS_WINDOWS AND _PICODE_HOST_ARCH STREQUAL "arm64")
        set(_PICODE_FALLBACK_PLATFORM "win-arm64")
        set(_PICODE_ARCHIVE "node-v22.23.5-win-arm64.zip")
        set(_PICODE_HASH "0a806286b552cfc02129ed571a92c46cc00f69eca896ca5364ca2e8767c61e74648c99dc5a38ca87dbcf11a4b5755c18aafa6682833af533e0516453504ccffe")
    elseif(VCPKG_HOST_IS_OSX AND _PICODE_HOST_ARCH STREQUAL "x64")
        set(_PICODE_FALLBACK_PLATFORM "darwin-x64")
        set(_PICODE_ARCHIVE "node-v22.23.5-darwin-x64.tar.gz")
        set(_PICODE_HASH "08dde8aa57ac62e3da4b54701631f1cff199c345a19478ef523023501b1c207b768d1eb371e17a2d607c2b41755c080a0a11240312dd9cdc008b9319192a98dc")
    elseif(VCPKG_HOST_IS_OSX AND _PICODE_HOST_ARCH STREQUAL "arm64")
        set(_PICODE_FALLBACK_PLATFORM "darwin-arm64")
        set(_PICODE_ARCHIVE "node-v22.23.5-darwin-arm64.tar.gz")
        set(_PICODE_HASH "2c669966adb30c708ff77a4e08aae83b1a7f867e0da5e6e61b6b9eb5500130a6315bec3a5d5c224f779597e94a0a598c8d18d77ae4ac24876617a73c54cb26ee")
    elseif(VCPKG_HOST_IS_LINUX AND _PICODE_HOST_ARCH STREQUAL "x64")
        set(_PICODE_FALLBACK_PLATFORM "linux-x64")
        set(_PICODE_ARCHIVE "node-v22.23.5-linux-x64.tar.xz")
        set(_PICODE_HASH "5c79b4662c43fbb27999348e2db82bc008bfc9c1ae34358dae6b0e0f4bc74f3ef42bf812780a0a408ec590eeffd9c5236b8a7c2caf1bc49a639712e21551c78b")
    elseif(VCPKG_HOST_IS_LINUX AND _PICODE_HOST_ARCH STREQUAL "arm64")
        set(_PICODE_FALLBACK_PLATFORM "linux-arm64")
        set(_PICODE_ARCHIVE "node-v22.23.5-linux-arm64.tar.xz")
        set(_PICODE_HASH "a130da1abf74c9581b43994ab52bf0cbbc6335ed2c214ac3a3c95de2cfafeb5adf47e8c8d516eff58534fe0962226aaf84479244f7071ad8ac8f148b14952e52")
    else()
        message(FATAL_ERROR "Cannot select a fallback Node archive for host '${HOST_TRIPLET}'")
    endif()
    vcpkg_download_distfile(_PICODE_NODE_ARCHIVE
        URLS "https://nodejs.org/dist/v22.23.5/${_PICODE_ARCHIVE}"
        FILENAME "${_PICODE_ARCHIVE}"
        SHA512 "${_PICODE_HASH}"
    )
    set(_PICODE_FALLBACK_ROOT "${CURRENT_BUILDTREES_DIR}/node-22.23.5-${TARGET_TRIPLET}")
    file(MAKE_DIRECTORY "${_PICODE_FALLBACK_ROOT}")
    vcpkg_execute_in_download_mode(
        COMMAND "${CMAKE_COMMAND}" -E tar xf "${_PICODE_NODE_ARCHIVE}"
        WORKING_DIRECTORY "${_PICODE_FALLBACK_ROOT}"
    )
    set(_PICODE_FALLBACK_PREFIX "${_PICODE_FALLBACK_ROOT}/node-v22.23.5-${_PICODE_FALLBACK_PLATFORM}")
    if(VCPKG_HOST_IS_WINDOWS)
        set(_PICODE_CANDIDATE "${_PICODE_FALLBACK_PREFIX}/node.exe")
    else()
        set(_PICODE_CANDIDATE "${_PICODE_FALLBACK_PREFIX}/bin/node")
    endif()
    _picode_probe_node(_ok _ver _status "${_PICODE_CANDIDATE}")
    if(NOT _ok)
        message(FATAL_ERROR "Downloaded Node failed validation (${_status}): ${_PICODE_CANDIDATE}")
    endif()
    set(_PICODE_NODE "${_PICODE_CANDIDATE}")
    set(_PICODE_NODE_VERSION "${_ver}")
    set(_PICODE_NODE_SOURCE "downloaded Node 22.23.5")
endif()

message(STATUS "Using ${_PICODE_NODE_SOURCE} Node ${_PICODE_NODE_VERSION}: ${_PICODE_NODE}")

get_filename_component(_PICODE_NODE_DIR "${_PICODE_NODE}" DIRECTORY)
get_filename_component(_PICODE_NODE_PREFIX "${_PICODE_NODE_DIR}/.." ABSOLUTE)
set(_PICODE_SEARCH_ROOTS "${_PICODE_NODE_PREFIX}")
if(_PICODE_NODEJS22_PREFIX)
    list(APPEND _PICODE_SEARCH_ROOTS "${_PICODE_NODEJS22_PREFIX}")
endif()
if(_PICODE_FALLBACK_PREFIX)
    list(APPEND _PICODE_SEARCH_ROOTS "${_PICODE_FALLBACK_PREFIX}")
endif()
find_file(_PICODE_NPM_CLI
    NAMES npm-cli.js
    PATHS ${_PICODE_SEARCH_ROOTS}
    PATH_SUFFIXES lib/node_modules/npm/bin node_modules/npm/bin
    NO_DEFAULT_PATH
    REQUIRED
)
vcpkg_add_to_path(PREPEND "${_PICODE_NODE_DIR}")

set(_PICODE_NPM_ENV
    "npm_config_cache=${CURRENT_BUILDTREES_DIR}/npm-cache-${TARGET_TRIPLET}"
    npm_config_update_notifier=false
    npm_config_audit=false
    npm_config_fund=false
)
find_file(_PICODE_NODE_H
    NAMES node.h
    PATHS ${_PICODE_SEARCH_ROOTS}
    PATH_SUFFIXES include/node
    NO_DEFAULT_PATH
)
if(_PICODE_NODE_H)
    get_filename_component(_PICODE_INCLUDE_NODE "${_PICODE_NODE_H}" DIRECTORY)
    get_filename_component(_PICODE_INCLUDE_DIR "${_PICODE_INCLUDE_NODE}" DIRECTORY)
    get_filename_component(_PICODE_NODE_ROOT "${_PICODE_INCLUDE_DIR}" DIRECTORY)
    list(APPEND _PICODE_NPM_ENV "npm_config_nodedir=${_PICODE_NODE_ROOT}")
endif()

# npm resolves devDependencies even with --omit=dev. Upstream lists vitest as
# a devDependency, whose peer graph breaks npm 10 (bundled with nodejs22) with
# "Cannot read properties of null (reading 'edgesOut')". Strip that section
# from the staging copy only; published runtime dependencies are kept.
set(_PICODE_PKG_JSON "${_PICODE_STAGING}/package.json")
file(READ "${_PICODE_PKG_JSON}" _PICODE_PKG_JSON_TEXT)
string(JSON _PICODE_DEV_DEPS ERROR_VARIABLE _PICODE_JSON_ERR GET "${_PICODE_PKG_JSON_TEXT}" devDependencies)
if(NOT _PICODE_JSON_ERR)
    string(JSON _PICODE_PKG_JSON_TEXT ERROR_VARIABLE _PICODE_JSON_ERR REMOVE "${_PICODE_PKG_JSON_TEXT}" devDependencies)
    if(_PICODE_JSON_ERR)
        message(FATAL_ERROR "Failed to strip devDependencies from package.json: ${_PICODE_JSON_ERR}")
    endif()
    file(WRITE "${_PICODE_PKG_JSON}" "${_PICODE_PKG_JSON_TEXT}")
endif()

vcpkg_execute_required_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${_PICODE_NPM_ENV}
        "${_PICODE_NODE}" "${_PICODE_NPM_CLI}" install --omit=dev
    WORKING_DIRECTORY "${_PICODE_STAGING}"
    LOGNAME install-picode
)

set(_PICODE_TOOLS "${CURRENT_PACKAGES_DIR}/tools/picode")
file(MAKE_DIRECTORY "${_PICODE_TOOLS}")
file(COPY "${_PICODE_STAGING}/" DESTINATION "${_PICODE_TOOLS}")

# Runtime launchers prefer Node from PATH and fall back to the nodejs22 host
# tool when that optional feature was enabled by the install script.
set(_PICODE_NODEJS22_UNIX_PATHS "")
set(_PICODE_NODEJS22_WIN_PATHS "")
foreach(_root
    "${CURRENT_HOST_INSTALLED_DIR}/tools/nodejs22"
    "${CURRENT_INSTALLED_DIR}/tools/nodejs22")
    set(_unix_path "${_root}/bin/node")
    if(_PICODE_NODEJS22_UNIX_PATHS)
        set(_PICODE_NODEJS22_UNIX_PATHS "${_PICODE_NODEJS22_UNIX_PATHS} ${_unix_path}")
    else()
        set(_PICODE_NODEJS22_UNIX_PATHS "${_unix_path}")
    endif()
    file(TO_NATIVE_PATH "${_root}/node.exe" _win_path)
    if(_PICODE_NODEJS22_WIN_PATHS)
        set(_PICODE_NODEJS22_WIN_PATHS "${_PICODE_NODEJS22_WIN_PATHS} \"${_win_path}\"")
    else()
        set(_PICODE_NODEJS22_WIN_PATHS "\"${_win_path}\"")
    endif()
endforeach()

if(VCPKG_TARGET_IS_WINDOWS)
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/picode.cmd.in"
        "${_PICODE_TOOLS}/picode.cmd"
        @ONLY
        NEWLINE_STYLE CRLF
    )
else()
    set(_PICODE_LAUNCHER "${_PICODE_TOOLS}/picode")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/picode.in" "${_PICODE_LAUNCHER}" @ONLY)
    file(CHMOD
        "${_PICODE_LAUNCHER}"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
endif()

vcpkg_install_copyright(FILE_LIST "${_PICODE_LICENSE}")
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage"
    @ONLY
)
