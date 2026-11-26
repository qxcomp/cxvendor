#!/bin/sh
# Keep lazygitGo's updater preference off by default. MyGo stores the choice
# in updater.json under PathUserData; an absent automaticChecks field means
# automatic checks are enabled, so wrappers seed it before starting the app.
tangit_auto_update_init() {
  case "${TANGIT_AUTO_UPDATE-}" in
    1) _tangit_want=true; _tangit_explicit=1 ;;
    0) _tangit_want=false; _tangit_explicit=1 ;;
    *) _tangit_want=false; _tangit_explicit=0 ;;
  esac

  case "$(uname -s 2>/dev/null)" in
    Darwin)
      _tangit_dir="${HOME:-}/Library/Application Support/lazygitGo"
      ;;
    *)
      case "${XDG_CONFIG_HOME:-}" in
        /*) _tangit_dir="${XDG_CONFIG_HOME}/lazygitGo" ;;
        *)  _tangit_dir="${HOME:-}/.config/lazygitGo" ;;
      esac
      ;;
  esac

  [ -n "${HOME:-}" ] || return 0
  [ -n "${_tangit_dir}" ] || return 0
  mkdir -p "${_tangit_dir}" 2>/dev/null || return 0
  _tangit_cfg="${_tangit_dir}/updater.json"

  if [ ! -f "${_tangit_cfg}" ]; then
    printf '{"automaticChecks":%s}\n' "${_tangit_want}" > "${_tangit_cfg}" 2>/dev/null || return 0
    return 0
  fi

  # A saved explicit choice wins unless TANGIT_AUTO_UPDATE was set to 0 or 1.
  if grep -q '"automaticChecks"' "${_tangit_cfg}" 2>/dev/null && [ "${_tangit_explicit}" -eq 0 ]; then
    return 0
  fi

  _tangit_tmp="${_tangit_cfg}.tangit.$$"
  _tangit_ok=0
  if grep -q '"automaticChecks"' "${_tangit_cfg}" 2>/dev/null; then
    if sed -E "s/(\"automaticChecks\"[[:space:]]*:[[:space:]]*)(true|false)/\\1${_tangit_want}/" \
        "${_tangit_cfg}" > "${_tangit_tmp}" 2>/dev/null && [ -s "${_tangit_tmp}" ]; then
      _tangit_ok=1
    fi
  elif sed -E "s/^([[:space:]]*\\{[[:space:]]*)/\\1\"automaticChecks\":${_tangit_want},/" \
        "${_tangit_cfg}" > "${_tangit_tmp}" 2>/dev/null && grep -q '"automaticChecks"' "${_tangit_tmp}" 2>/dev/null; then
    _tangit_ok=1
  fi

  if [ "${_tangit_ok}" -eq 1 ]; then
    mv -f "${_tangit_tmp}" "${_tangit_cfg}" 2>/dev/null || rm -f "${_tangit_tmp}" 2>/dev/null
  else
    rm -f "${_tangit_tmp}" 2>/dev/null
  fi
  return 0
}
