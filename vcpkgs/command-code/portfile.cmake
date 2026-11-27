set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)

# npm installs platform-specific optional packages (ripgrep, clipboard), so
# the tool is built for the host OS and architecture. A different target
# triplet is fine when OS and architecture match.
set(_CC_HOST_ARCH "")
if(HOST_TRIPLET MATCHES "^arm64")
    set(_CC_HOST_ARCH "arm64")
elseif(HOST_TRIPLET MATCHES "^x64")
    set(_CC_HOST_ARCH "x64")
endif()
set(_CC_HOST_OS_MATCHES FALSE)
if(VCPKG_HOST_IS_LINUX AND VCPKG_TARGET_IS_LINUX)
    set(_CC_HOST_OS_MATCHES TRUE)
elseif(VCPKG_HOST_IS_OSX AND VCPKG_TARGET_IS_OSX)
    set(_CC_HOST_OS_MATCHES TRUE)
elseif(VCPKG_HOST_IS_WINDOWS AND VCPKG_TARGET_IS_WINDOWS)
    set(_CC_HOST_OS_MATCHES TRUE)
endif()
if(NOT _CC_HOST_OS_MATCHES OR (_CC_HOST_ARCH AND NOT _CC_HOST_ARCH STREQUAL VCPKG_TARGET_ARCHITECTURE))
    message(FATAL_ERROR
        "command-code installs host OS/architecture specific packages; building "
        "target '${TARGET_TRIPLET}' on '${HOST_TRIPLET}' would install unusable binaries."
    )
endif()

# Upstream artifact is the official npm tarball. command-code has no GitHub
# release/tag and its npm metadata declares license "UNLICENSED".
set(_CC_TARBALL_URL "https://registry.npmjs.org/command-code/-/command-code-1.93.4.tgz")
set(_CC_TARBALL_SHA512 "dd984777ff9941f64a83bec16c90a9287c64ce1ae194bf24acbfa1abe6602e20c09655d13aef2d3d87a65f355d115c8cc6e33ad40b87184ba7af6d009c43849e")

vcpkg_download_distfile(_CC_TARBALL
    URLS "${_CC_TARBALL_URL}"
    FILENAME "command-code-1.93.4.tgz"
    SHA512 "${_CC_TARBALL_SHA512}"
)

vcpkg_extract_source_archive_ex(
    OUT_SOURCE_PATH _CC_STAGING
    ARCHIVE "${_CC_TARBALL}"
)
if(NOT EXISTS "${_CC_STAGING}/dist/index.mjs")
    message(FATAL_ERROR "Unexpected npm tarball layout: ${_CC_STAGING}")
endif()

# Command Code requires Node >= 22.
function(_cc_node_supported _out _version)
    set(${_out} FALSE PARENT_SCOPE)
    if(_version MATCHES "^([0-9]+)")
        if(CMAKE_MATCH_1 GREATER_EQUAL 22)
            set(${_out} TRUE PARENT_SCOPE)
        endif()
    endif()
endfunction()

function(_cc_probe_node _out_ok _out_version _out_status _candidate)
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
    _cc_node_supported(_supported "${_ver}")
    if(NOT _supported)
        set(${_out_status} "unsupported version ${_ver}" PARENT_SCOPE)
        return()
    endif()
    set(${_out_ok} TRUE PARENT_SCOPE)
    set(${_out_status} "ok" PARENT_SCOPE)
endfunction()

set(_CC_NODE "")
set(_CC_NODE_VERSION "")
set(_CC_NODE_SOURCE "")
set(_CC_FALLBACK_ROOT "")
set(_CC_FALLBACK_PREFIX "")
set(_CC_NODEJS22_PREFIX "")

# The manifest feature declares the conditional dependency; the install script
# enables it only when no usable system Node exists.
if("nodejs22" IN_LIST FEATURES)
    foreach(_root
        "${CURRENT_HOST_INSTALLED_DIR}/tools/nodejs22"
        "${CURRENT_INSTALLED_DIR}/tools/nodejs22")
        if(VCPKG_HOST_IS_WINDOWS)
            set(_CC_CANDIDATES "${_root}/node.exe")
        else()
            set(_CC_CANDIDATES "${_root}/bin/node" "${_root}/node")
        endif()
        foreach(_candidate IN LISTS _CC_CANDIDATES)
            _cc_probe_node(_ok _ver _status "${_candidate}")
            if(_ok)
                set(_CC_NODE "${_candidate}")
                set(_CC_NODE_VERSION "${_ver}")
                set(_CC_NODE_SOURCE "nodejs22 host tool")
                set(_CC_NODEJS22_PREFIX "${_root}")
                break()
            endif()
        endforeach()
        if(_CC_NODE)
            break()
        endif()
    endforeach()
    if(NOT _CC_NODE)
        message(FATAL_ERROR
            "The 'nodejs22' feature is enabled but the nodejs22 host tool was "
            "not found or is unusable. Check 'vcpkg list nodejs22'."
        )
    endif()
endif()

if(NOT _CC_NODE)
    if(VCPKG_HOST_IS_WINDOWS)
        set(_CC_SEARCH_PATHS "$ENV{PATH}")
    else()
        string(REPLACE ":" ";" _CC_SEARCH_PATHS "$ENV{PATH}")
    endif()
    set(_CC_REJECTED "")
    foreach(_dir IN LISTS _CC_SEARCH_PATHS)
        if(_dir STREQUAL "" OR _dir STREQUAL ".")
            continue()
        endif()
        foreach(_name node node.exe)
            _cc_probe_node(_ok _ver _status "${_dir}/${_name}")
            if(_ok)
                set(_CC_NODE "${_dir}/${_name}")
                set(_CC_NODE_VERSION "${_ver}")
                set(_CC_NODE_SOURCE "system PATH")
                break()
            elseif(NOT _status STREQUAL "not found")
                list(APPEND _CC_REJECTED "${_dir}/${_name}: ${_status}")
            endif()
        endforeach()
        if(_CC_NODE)
            break()
        endif()
    endforeach()
    foreach(_msg IN LISTS _CC_REJECTED)
        message(STATUS "Skipping Node candidate: ${_msg}")
    endforeach()
endif()

if(NOT _CC_NODE)
    set(_CC_NODE_VERSION "22.23.5")
    if(VCPKG_HOST_IS_WINDOWS AND _CC_HOST_ARCH STREQUAL "x64")
        set(_CC_FALLBACK_PLATFORM "win-x64")
        set(_CC_ARCHIVE "node-v22.23.5-win-x64.zip")
        set(_CC_HASH "d2599b1e31274a1573db944d1860642ee433121b8ed368582133825ae9cf977c99b138b12d54f2e439f4b100aa79dc587ae927afa1efc02fbc3bde047e4f239f")
    elseif(VCPKG_HOST_IS_WINDOWS AND _CC_HOST_ARCH STREQUAL "arm64")
        set(_CC_FALLBACK_PLATFORM "win-arm64")
        set(_CC_ARCHIVE "node-v22.23.5-win-arm64.zip")
        set(_CC_HASH "0a806286b552cfc02129ed571a92c46cc00f69eca896ca5364ca2e8767c61e74648c99dc5a38ca87dbcf11a4b5755c18aafa6682833af533e0516453504ccffe")
    elseif(VCPKG_HOST_IS_OSX AND _CC_HOST_ARCH STREQUAL "x64")
        set(_CC_FALLBACK_PLATFORM "darwin-x64")
        set(_CC_ARCHIVE "node-v22.23.5-darwin-x64.tar.gz")
        set(_CC_HASH "08dde8aa57ac62e3da4b54701631f1cff199c345a19478ef523023501b1c207b768d1eb371e17a2d607c2b41755c080a0a11240312dd9cdc008b9319192a98dc")
    elseif(VCPKG_HOST_IS_OSX AND _CC_HOST_ARCH STREQUAL "arm64")
        set(_CC_FALLBACK_PLATFORM "darwin-arm64")
        set(_CC_ARCHIVE "node-v22.23.5-darwin-arm64.tar.gz")
        set(_CC_HASH "2c669966adb30c708ff77a4e08aae83b1a7f867e0da5e6e61b6b9eb5500130a6315bec3a5d5c224f779597e94a0a598c8d18d77ae4ac24876617a73c54cb26ee")
    elseif(VCPKG_HOST_IS_LINUX AND _CC_HOST_ARCH STREQUAL "x64")
        set(_CC_FALLBACK_PLATFORM "linux-x64")
        set(_CC_ARCHIVE "node-v22.23.5-linux-x64.tar.xz")
        set(_CC_HASH "5c79b4662c43fbb27999348e2db82bc008bfc9c1ae34358dae6b0e0f4bc74f3ef42bf812780a0a408ec590eeffd9c5236b8a7c2caf1bc49a639712e21551c78b")
    elseif(VCPKG_HOST_IS_LINUX AND _CC_HOST_ARCH STREQUAL "arm64")
        set(_CC_FALLBACK_PLATFORM "linux-arm64")
        set(_CC_ARCHIVE "node-v22.23.5-linux-arm64.tar.xz")
        set(_CC_HASH "a130da1abf74c9581b43994ab52bf0cbbc6335ed2c214ac3a3c95de2cfafeb5adf47e8c8d516eff58534fe0962226aaf84479244f7071ad8ac8f148b14952e52")
    else()
        message(FATAL_ERROR "Cannot select a fallback Node archive for host '${HOST_TRIPLET}'")
    endif()
    vcpkg_download_distfile(_CC_NODE_ARCHIVE
        URLS "https://nodejs.org/dist/v22.23.5/${_CC_ARCHIVE}"
        FILENAME "${_CC_ARCHIVE}"
        SHA512 "${_CC_HASH}"
    )
    set(_CC_FALLBACK_ROOT "${CURRENT_BUILDTREES_DIR}/node-22.23.5-${TARGET_TRIPLET}")
    file(MAKE_DIRECTORY "${_CC_FALLBACK_ROOT}")
    vcpkg_execute_in_download_mode(
        COMMAND "${CMAKE_COMMAND}" -E tar xf "${_CC_NODE_ARCHIVE}"
        WORKING_DIRECTORY "${_CC_FALLBACK_ROOT}"
    )
    set(_CC_FALLBACK_PREFIX "${_CC_FALLBACK_ROOT}/node-v22.23.5-${_CC_FALLBACK_PLATFORM}")
    if(VCPKG_HOST_IS_WINDOWS)
        set(_CC_CANDIDATE "${_CC_FALLBACK_PREFIX}/node.exe")
    else()
        set(_CC_CANDIDATE "${_CC_FALLBACK_PREFIX}/bin/node")
    endif()
    _cc_probe_node(_ok _ver _status "${_CC_CANDIDATE}")
    if(NOT _ok)
        message(FATAL_ERROR "Downloaded Node failed validation (${_status}): ${_CC_CANDIDATE}")
    endif()
    set(_CC_NODE "${_CC_CANDIDATE}")
    set(_CC_NODE_VERSION "${_ver}")
    set(_CC_NODE_SOURCE "downloaded Node 22.23.5")
endif()

message(STATUS "Using ${_CC_NODE_SOURCE} Node ${_CC_NODE_VERSION}: ${_CC_NODE}")

get_filename_component(_CC_NODE_DIR "${_CC_NODE}" DIRECTORY)
get_filename_component(_CC_NODE_PREFIX "${_CC_NODE_DIR}/.." ABSOLUTE)
set(_CC_SEARCH_ROOTS "${_CC_NODE_PREFIX}")
if(_CC_NODEJS22_PREFIX)
    list(APPEND _CC_SEARCH_ROOTS "${_CC_NODEJS22_PREFIX}")
endif()
if(_CC_FALLBACK_PREFIX)
    list(APPEND _CC_SEARCH_ROOTS "${_CC_FALLBACK_PREFIX}")
endif()
find_file(_CC_NPM_CLI
    NAMES npm-cli.js
    PATHS ${_CC_SEARCH_ROOTS}
    PATH_SUFFIXES lib/node_modules/npm/bin node_modules/npm/bin
    NO_DEFAULT_PATH
    REQUIRED
)
vcpkg_add_to_path(PREPEND "${_CC_NODE_DIR}")

set(_CC_NPM_ENV
    "npm_config_cache=${CURRENT_BUILDTREES_DIR}/npm-cache-${TARGET_TRIPLET}"
    npm_config_update_notifier=false
    npm_config_audit=false
    npm_config_fund=false
)
find_file(_CC_NODE_H
    NAMES node.h
    PATHS ${_CC_SEARCH_ROOTS}
    PATH_SUFFIXES include/node
    NO_DEFAULT_PATH
)
if(_CC_NODE_H)
    get_filename_component(_CC_INCLUDE_NODE "${_CC_NODE_H}" DIRECTORY)
    get_filename_component(_CC_INCLUDE_DIR "${_CC_INCLUDE_NODE}" DIRECTORY)
    get_filename_component(_CC_NODE_ROOT "${_CC_INCLUDE_DIR}" DIRECTORY)
    list(APPEND _CC_NPM_ENV "npm_config_nodedir=${_CC_NODE_ROOT}")
endif()

# npm resolves devDependencies even with --omit=dev. Upstream 1.93.4 lists
# @byokkit/cmd-provider-anthropic@0.1.5 as a devDependency but npm only has
# 0.1.4, which makes 'npm install --omit=dev' fail with ETARGET. Strip that
# section from the staging copy only; published runtime dependencies are kept.
set(_CC_PKG_JSON "${_CC_STAGING}/package.json")
file(READ "${_CC_PKG_JSON}" _CC_PKG_JSON_TEXT)
string(JSON _CC_DEV_DEPS ERROR_VARIABLE _CC_JSON_ERR GET "${_CC_PKG_JSON_TEXT}" devDependencies)
if(NOT _CC_JSON_ERR)
    string(JSON _CC_PKG_JSON_TEXT ERROR_VARIABLE _CC_JSON_ERR REMOVE "${_CC_PKG_JSON_TEXT}" devDependencies)
    if(_CC_JSON_ERR)
        message(FATAL_ERROR "Failed to strip devDependencies from package.json: ${_CC_JSON_ERR}")
    endif()
    file(WRITE "${_CC_PKG_JSON}" "${_CC_PKG_JSON_TEXT}")
endif()

vcpkg_execute_required_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${_CC_NPM_ENV}
        "${_CC_NODE}" "${_CC_NPM_CLI}" install --omit=dev
    WORKING_DIRECTORY "${_CC_STAGING}"
    LOGNAME install-command-code
)

set(_CC_TOOLS "${CURRENT_PACKAGES_DIR}/tools/command-code")
file(MAKE_DIRECTORY "${_CC_TOOLS}")
file(COPY "${_CC_STAGING}/" DESTINATION "${_CC_TOOLS}")

set(_CC_NODEJS22_UNIX_PATHS "")
set(_CC_NODEJS22_WIN_PATHS "")
foreach(_root
    "${CURRENT_HOST_INSTALLED_DIR}/tools/nodejs22"
    "${CURRENT_INSTALLED_DIR}/tools/nodejs22")
    set(_unix_path "${_root}/bin/node")
    if(_CC_NODEJS22_UNIX_PATHS)
        set(_CC_NODEJS22_UNIX_PATHS "${_CC_NODEJS22_UNIX_PATHS} ${_unix_path}")
    else()
        set(_CC_NODEJS22_UNIX_PATHS "${_unix_path}")
    endif()
    file(TO_NATIVE_PATH "${_root}/node.exe" _win_path)
    if(_CC_NODEJS22_WIN_PATHS)
        set(_CC_NODEJS22_WIN_PATHS "${_CC_NODEJS22_WIN_PATHS} \"${_win_path}\"")
    else()
        set(_CC_NODEJS22_WIN_PATHS "\"${_win_path}\"")
    endif()
endforeach()

# npm declares four bin names; install all of them.
set(_CC_LAUNCHERS cmd cmdc commandcode command-code)
foreach(_launcher IN LISTS _CC_LAUNCHERS)
    set(_CC_LAUNCHER_NAME "${_launcher}")
    if(VCPKG_TARGET_IS_WINDOWS)
        configure_file(
            "${CMAKE_CURRENT_LIST_DIR}/command-code.cmd.in"
            "${_CC_TOOLS}/${_launcher}.cmd"
            @ONLY
            NEWLINE_STYLE CRLF
        )
    else()
        set(_CC_LAUNCHER "${_CC_TOOLS}/${_launcher}")
        configure_file("${CMAKE_CURRENT_LIST_DIR}/command-code.in" "${_CC_LAUNCHER}" @ONLY)
        file(CHMOD
            "${_CC_LAUNCHER}"
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        )
    endif()
endforeach()

vcpkg_install_copyright(FILE_LIST "${CMAKE_CURRENT_LIST_DIR}/copyright")
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage"
    @ONLY
)
