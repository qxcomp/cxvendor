set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)

# Native addons are built for the host architecture. Cross-compiling would put
# addons for the wrong architecture into the target package.
if(VCPKG_CROSSCOMPILING)
    message(FATAL_ERROR
        "joplin-terminal must be built on a host matching the target triplet; "
        "cross-compiling would produce native Node addons for the wrong architecture."
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

# node/npm come from the host-only vcpkg-tool-nodejs port. They are build
# tools only: the installed launchers use the system Node from PATH.
find_program(_JOPLIN_NODE
    NAMES node
    PATHS
        "${CURRENT_HOST_INSTALLED_DIR}/tools/node"
        "${CURRENT_HOST_INSTALLED_DIR}/tools/node/bin"
    NO_DEFAULT_PATH
    REQUIRED
)
find_file(_JOPLIN_NPM_CLI
    NAMES npm-cli.js
    PATHS "${CURRENT_HOST_INSTALLED_DIR}/tools/node"
    PATH_SUFFIXES lib/node_modules/npm/bin node_modules/npm/bin
    NO_DEFAULT_PATH
    REQUIRED
)
get_filename_component(_JOPLIN_NODE_DIR "${_JOPLIN_NODE}" DIRECTORY)
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
    PATHS "${CURRENT_HOST_INSTALLED_DIR}/tools/node"
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
