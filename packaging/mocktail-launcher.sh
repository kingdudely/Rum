#!/usr/bin/env bash
# Copyright 2026 Mocktail Project Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0

set -Eeuo pipefail

ResolveSelf() {
  local path="${BASH_SOURCE[0]}"
  while [[ -L "${path}" ]]; do
    local directory
    directory="$(cd -P -- "$(dirname -- "${path}")" && pwd)"
    path="$(readlink -- "${path}")"
    [[ "${path}" == /* ]] || path="${directory}/${path}"
  done
  cd -P -- "$(dirname -- "${path}")" && pwd
}

LAUNCHER_DIR="$(ResolveSelf)"
if [[ -n "${MOCKTAIL_BUNDLE_ROOT:-}" ]]; then
  BUNDLE_ROOT="$(cd -P -- "${MOCKTAIL_BUNDLE_ROOT}" && pwd)"
else
  BUNDLE_ROOT="$(cd -P -- "${LAUNCHER_DIR}/../.." && pwd)"
fi
RUNTIME_ROOT="${BUNDLE_ROOT}/mocktail"
BUNDLED_BIN_DIR="${RUNTIME_ROOT}/bin"
ANYLINUX_BIN_DIR="${MOCKTAIL_ANYLINUX_BIN_DIR:-}"
if [[ -n "${ANYLINUX_BIN_DIR}" ]]; then
  [[ "${ANYLINUX_BIN_DIR}" == /* && -d "${ANYLINUX_BIN_DIR}" ]] || {
    printf 'mocktail: invalid AnyLinux binary directory: %s\n' \
      "${ANYLINUX_BIN_DIR}" >&2
    exit 1
  }
  BIN_DIR="${ANYLINUX_BIN_DIR}"
else
  BIN_DIR="${BUNDLED_BIN_DIR}"
fi
MAIN_BINARY="${BIN_DIR}/mocktail"
WEBVIEW_HELPER="${BIN_DIR}/mocktail_webview_helper"
FREEBSD_SOCKET_HELPER="${BUNDLED_BIN_DIR}/mocktail_freebsd_socket_helper"
METADATA_DIR="${RUNTIME_ROOT}/metadata"
ABI_MANIFEST="${METADATA_DIR}/ABI.txt"
DEPENDENCY_MANIFEST="${METADATA_DIR}/DEPENDENCIES.txt"
CHECKSUM_MANIFEST="${METADATA_DIR}/SHA256SUMS.txt"
SUPPORT_ROOT="${RUNTIME_ROOT}/runtime"
if [[ -n "${ANYLINUX_BIN_DIR}" ]]; then
  SUPPORT_BIN="${ANYLINUX_BIN_DIR}"
else
  SUPPORT_BIN="${SUPPORT_ROOT}/bin"
fi
export PATH="${BIN_DIR}:${SUPPORT_BIN}:${PATH}"
export LD_LIBRARY_PATH="${RUNTIME_ROOT}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export MOCKTAIL_FREEBSD_SOCKET_HELPER="${FREEBSD_SOCKET_HELPER}"

Die() {
  printf 'mocktail: %s\n' "$*" >&2
  exit 1
}

ABI_SCHEMA=""
ABI_ARCHITECTURE=""
ABI_LIBC=""
ABI_MODE=""
ABI_INTERPRETER=""
ABI_ANDROID_TOOLS_LIBC=""

LoadAbiManifest() {
  [[ -f "${ABI_MANIFEST}" && ! -L "${ABI_MANIFEST}" ]] ||
    Die "missing regular ABI manifest: ${ABI_MANIFEST}"

  local key value
  local seen_schema=false seen_architecture=false seen_libc=false
  local seen_mode=false seen_interpreter=false
  local seen_android_tools_libc=false
  while IFS='=' read -r key value; do
    case "${key}" in
      schema)
        [[ "${seen_schema}" == false ]] ||
          Die "duplicate ABI manifest field: schema"
        seen_schema=true
        ABI_SCHEMA="${value}"
        ;;
      architecture)
        [[ "${seen_architecture}" == false ]] ||
          Die "duplicate ABI manifest field: architecture"
        seen_architecture=true
        ABI_ARCHITECTURE="${value}"
        ;;
      libc)
        [[ "${seen_libc}" == false ]] ||
          Die "duplicate ABI manifest field: libc"
        seen_libc=true
        ABI_LIBC="${value}"
        ;;
      mode)
        [[ "${seen_mode}" == false ]] ||
          Die "duplicate ABI manifest field: mode"
        seen_mode=true
        ABI_MODE="${value}"
        ;;
      interpreter)
        [[ "${seen_interpreter}" == false ]] ||
          Die "duplicate ABI manifest field: interpreter"
        seen_interpreter=true
        ABI_INTERPRETER="${value}"
        ;;
      android_tools_libc)
        [[ "${seen_android_tools_libc}" == false ]] ||
          Die "duplicate ABI manifest field: android_tools_libc"
        seen_android_tools_libc=true
        ABI_ANDROID_TOOLS_LIBC="${value}"
        ;;
      '') ;;
      *) Die "unknown ABI manifest field: ${key}" ;;
    esac
  done <"${ABI_MANIFEST}"

  [[ "${ABI_SCHEMA}" == 2 ]] || Die "unsupported ABI manifest schema"
  [[ "${ABI_ARCHITECTURE}" == x86_64 ]] ||
    Die "unsupported bundle architecture: ${ABI_ARCHITECTURE:-missing}"
  [[ "${ABI_LIBC}" == glibc || "${ABI_LIBC}" == musl ]] ||
    Die "unsupported bundle libc ABI: ${ABI_LIBC:-missing}"
  [[ "${ABI_MODE}" == standalone || "${ABI_MODE}" == thin ]] ||
    Die "unsupported bundle packaging mode: ${ABI_MODE:-missing}"
  [[ -n "${ABI_INTERPRETER}" ]] || Die "ABI manifest has no ELF interpreter"
  [[ "${ABI_ANDROID_TOOLS_LIBC}" == java ||
     "${ABI_ANDROID_TOOLS_LIBC}" == glibc ]] ||
    Die "unsupported Android build-tools ABI: ${ABI_ANDROID_TOOLS_LIBC:-missing}"
}

CheckBundleChecksums() {
  [[ -f "${CHECKSUM_MANIFEST}" && ! -L "${CHECKSUM_MANIFEST}" ]] ||
    Die "missing regular checksum manifest: ${CHECKSUM_MANIFEST}"
  command -v sha256sum >/dev/null 2>&1 ||
    Die "sha256sum is required to verify the portable bundle"
  (cd -- "${BUNDLE_ROOT}" &&
    sha256sum --quiet -c "mocktail/metadata/SHA256SUMS.txt") ||
    Die "portable bundle checksum verification failed"
}

CheckDependencyManifest() {
  [[ -f "${DEPENDENCY_MANIFEST}" && ! -L "${DEPENDENCY_MANIFEST}" ]] ||
    Die "missing regular dependency manifest: ${DEPENDENCY_MANIFEST}"
  [[ -d "${RUNTIME_ROOT}/lib" && ! -L "${RUNTIME_ROOT}/lib" ]] ||
    Die "missing regular dependency directory: ${RUNTIME_ROOT}/lib"
  local dependency_mode
  dependency_mode="$(sed -n 's/^mode=//p' "${DEPENDENCY_MANIFEST}")"
  [[ -n "${dependency_mode}" && "${dependency_mode}" != *$'\n'* ]] ||
    Die "dependency manifest has an invalid packaging mode"
  [[ "${dependency_mode}" == "${ABI_MODE}" ]] ||
    Die "ABI mode ${ABI_MODE} does not match DEPENDENCIES.txt mode ${dependency_mode}"

  if [[ "${ABI_MODE}" == thin ]] &&
      find "${RUNTIME_ROOT}/lib" -mindepth 1 -maxdepth 1 -print -quit \
        2>/dev/null | grep -q .; then
    Die "thin bundle unexpectedly contains bundled dependency libraries"
  fi
}

ValidateRelativeRuntimePath() {
  local value="$1"
  [[ -z "${value}" || ( "${value}" != /* && "${value}" != .. &&
     "${value}" != ../* && "${value}" != */../* &&
     "${value}" != */.. && "${value}" != *$'\n'* &&
     "${value}" != *$'\r'* ) ]] ||
    Die "standalone environment contains an unsafe relative path"
}

ConfigureStandaloneEnvironment() {
  [[ "${ABI_MODE}" == standalone ]] || return 0
  [[ -n "${SUPPORT_ROOT}" && -d "${SUPPORT_ROOT}" ]] || return 0
  export SSL_CERT_FILE="${SUPPORT_ROOT}/share/ca-certificates/ca-bundle.crt"
}

EnterStandaloneNamespace() {
  [[ "${ABI_MODE}" == standalone &&
     -z "${ANYLINUX_BIN_DIR}" &&
     "${MOCKTAIL_STANDALONE_NAMESPACE:-0}" != 1 &&
     "${MOCKTAIL_SKIP_NAMESPACE_CHECK:-0}" != 1 ]] || return 0
  local namespace_usr="${RUNTIME_ROOT}/namespace/usr"
  local bubblewrap="${SUPPORT_BIN}/bwrap"
  [[ -d "${namespace_usr}" && ! -L "${namespace_usr}" ]] ||
    Die "standalone /usr overlay is unavailable"
  [[ -x "${bubblewrap}" && -x "${SUPPORT_BIN}/xdg-dbus-proxy" ]] ||
    Die "standalone WebKit namespace tools are unavailable"

  local -a command=(
    "${bubblewrap}"
    --die-with-parent
    --ro-bind / /
    --dev-bind /dev /dev
    --proc /proc
    --overlay-src /usr
    --overlay-src "${namespace_usr}"
    --ro-overlay /usr
    --bind /tmp /tmp
  )
  if [[ -d /var/tmp ]]; then
    command+=(--bind /var/tmp /var/tmp)
  fi
  if [[ -n "${HOME:-}" && -d "${HOME}" ]]; then
    command+=(--bind "${HOME}" "${HOME}")
  fi
  if [[ -n "${XDG_RUNTIME_DIR:-}" && -d "${XDG_RUNTIME_DIR}" ]]; then
    command+=(--bind "${XDG_RUNTIME_DIR}" "${XDG_RUNTIME_DIR}")
  fi
  command+=(
    --setenv MOCKTAIL_STANDALONE_NAMESPACE 1
    --chdir "${RUNTIME_ROOT}"
    "${SUPPORT_BIN}/bash"
    "${LAUNCHER_DIR}/portable_launcher.sh"
  )
  exec "${command[@]}" "$@"
  Die "cannot enter the standalone WebKit namespace"
}

ReadElfInterpreter() {
  LC_ALL=C readelf -l "$1" 2>/dev/null |
    sed -n 's/.*Requesting program interpreter: \([^]]*\).*/\1/p'
}

DetectElfLibc() {
  local path="$1"
  local interpreter needed interpreter_libc="" needed_libc=""
  interpreter="$(ReadElfInterpreter "${path}")"
  case "${interpreter}" in
    *ld-musl-*.so.1) interpreter_libc=musl ;;
    *ld-linux*.so.*) interpreter_libc=glibc ;;
  esac
  needed="$(LC_ALL=C readelf -d "${path}" 2>/dev/null |
    sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')"
  if grep -Eq '^(libc\.so\.6|ld-linux[^[:space:]]*\.so(\.[0-9]+)*)$' \
      <<<"${needed}"; then
    needed_libc=glibc
  fi
  if grep -Eq '^(libc\.so|ld-musl-[^[:space:]]*\.so\.1)$' \
      <<<"${needed}"; then
    if [[ -n "${needed_libc}" && "${needed_libc}" != musl ]]; then
      printf mixed
      return 0
    fi
    needed_libc=musl
  fi
  if [[ -n "${interpreter_libc}" && -n "${needed_libc}" &&
        "${interpreter_libc}" != "${needed_libc}" ]]; then
    printf mixed
  elif [[ -n "${interpreter_libc}" ]]; then
    printf '%s' "${interpreter_libc}"
  elif [[ -n "${needed_libc}" ]]; then
    printf '%s' "${needed_libc}"
  else
    printf unknown
  fi
}

DetectHostLibc() {
  local version shell_interpreter
  version="$(LC_ALL=C getconf GNU_LIBC_VERSION 2>/dev/null || true)"
  if [[ "${version}" == glibc\ * ]]; then
    printf glibc
    return 0
  fi
  version="$(LC_ALL=C ldd --version 2>&1 || true)"
  if grep -qi 'musl' <<<"${version}"; then
    printf musl
    return 0
  fi
  shell_interpreter="$(ReadElfInterpreter "/proc/$$/exe")"
  case "${shell_interpreter}" in
    *ld-musl-*.so.1) printf musl ;;
    *ld-linux*.so.*) printf glibc ;;
    *) printf unknown ;;
  esac
}

ValidateBundledElfAbi() {
  local path="$1"
  local label="$2"
  local expected="${3:-${ABI_LIBC}}"
  local detected
  detected="$(DetectElfLibc "${path}")"
  [[ "${detected}" == "${expected}" ]] ||
    Die "${label} targets ${detected}, but ABI.txt requires ${expected}"
}

CheckBundleAbi() {
  command -v readelf >/dev/null 2>&1 ||
    Die "readelf is required to verify the portable ABI contract"
  CheckBundleChecksums
  LoadAbiManifest
  CheckDependencyManifest

  [[ "$(uname -m)" == "${ABI_ARCHITECTURE}" ]] ||
    Die "bundle requires ${ABI_ARCHITECTURE}, host is $(uname -m)"
  local main_interpreter host_libc
  main_interpreter="$(ReadElfInterpreter "${BUNDLED_BIN_DIR}/mocktail")"
  [[ "${main_interpreter}" == "${ABI_INTERPRETER}" ]] ||
    Die "mocktail ELF interpreter does not match ABI.txt (expected ${ABI_INTERPRETER}, found ${main_interpreter:-none})"
  ValidateBundledElfAbi "${BUNDLED_BIN_DIR}/mocktail" "mocktail"
  ValidateBundledElfAbi "${BUNDLED_BIN_DIR}/mocktail_webview_helper" \
    "WebView helper"
  if [[ -z "${ANYLINUX_BIN_DIR}" ]]; then
    host_libc="$(DetectHostLibc)"
    [[ "${host_libc}" != unknown ]] ||
      Die "cannot determine host libc ABI; install working getconf or ldd"
    [[ "${host_libc}" == "${ABI_LIBC}" ]] ||
      Die "bundle targets ${ABI_LIBC}, but host libc is ${host_libc}; use a Mocktail x86-64 ${host_libc} AppImage"
  fi
}

CheckCommand() {
  local name="$1"
  if command -v "${name}" >/dev/null 2>&1; then
    return 0
  fi
  printf '  missing command: %s\n' "${name}" >&2
  return 1
}

CheckElfDependencies() {
  local executable="$1"
  local label="$2"
  local missing output
  if ! output="$(LC_ALL=C ldd "${executable}" 2>&1)"; then
    printf '  cannot inspect %s libraries:\n%s\n' "${label}" \
      "${output}" >&2
    return 1
  fi
  missing="$(sed -n \
    's/^[[:space:]]*\([^[:space:]]\+\)[[:space:]]*=>[[:space:]]*not found.*$/\1/p' \
    <<<"${output}")"
  if [[ -z "${missing}" ]]; then
    return 0
  fi
  printf '  missing %s libraries:\n%s\n' "${label}" "${missing}" >&2
  return 1
}

CheckSystem() {
  local status=0

  if [[ "${ABI_MODE}" == thin ]]; then
    CheckElfDependencies "${MAIN_BINARY}" runtime || status=1
  fi

  local command_name
  for command_name in bash jq unzip file flock \
      timeout readelf sha256sum; do
    CheckCommand "${command_name}" || status=1
  done
  local webview_dependencies icd_manifest
  if [[ "${ABI_MODE}" == thin ]]; then
    if ! ldd "${WEBVIEW_HELPER}" | grep -q 'libvulkan\.so\.1'; then
      printf '  missing host Vulkan loader: libvulkan.so.1\n' >&2
      status=1
    fi
  fi
  icd_manifest="$(find /usr/share/vulkan/icd.d /etc/vulkan/icd.d \
    -maxdepth 1 -type f -name '*.json' -print -quit 2>/dev/null || true)"
  if [[ -z "${VK_ICD_FILENAMES:-}" && -z "${icd_manifest}" ]]; then
    printf '  no Vulkan ICD manifest found; install the GPU vendor driver\n' >&2
    status=1
  fi

  if (( status != 0 )); then
    if [[ "${ABI_LIBC}" == glibc ]]; then
      printf '\nArch example:\n' >&2
      printf '  sudo pacman -S --needed vulkan-icd-loader jq unzip file binutils\n' >&2
      printf '\nVoid x86_64-glibc example:\n' >&2
    else
      printf '\nVoid x86_64-musl example:\n' >&2
    fi
    printf '  sudo xbps-install -S vulkan-loader jq unzip file binutils\n' >&2
    printf 'Install the Vulkan ICD matching the GPU (for example vulkan-radeon, nvidia-utils, or vulkan-intel).\n' >&2
    return "${status}"
  fi

  printf 'Mocktail portable host check passed.\n'
  printf 'Packaging mode: %s.\n' "${ABI_MODE}"
  printf 'GPU driver and Vulkan ICD remain host-provided by design.\n'
}

[[ -d "${RUNTIME_ROOT}" && ! -L "${RUNTIME_ROOT}" ]] ||
  Die "missing regular runtime directory: ${RUNTIME_ROOT}"
[[ -x "${MAIN_BINARY}" ]] || Die "missing executable: ${MAIN_BINARY}"
[[ -x "${WEBVIEW_HELPER}" ]] || Die "missing WebView helper: ${WEBVIEW_HELPER}"
[[ -r "${METADATA_DIR}/roblox_compatibility.json" ]] ||
  Die "missing compatibility manifest"
  Die "missing native HostAbi reference profile"

CheckBundleAbi
ConfigureStandaloneEnvironment
EnterStandaloneNamespace "$@"

if [[ "${1:-}" == --check-system ]]; then
  CheckSystem
  exit $?
fi

data_root="${MOCKTAIL_DATA_ROOT:-${XDG_DATA_HOME:-${HOME}/.local/share}/mocktail}"
information_only=false
case "${1:-}" in
  --help|-h) information_only=true ;;
esac
if [[ "${information_only}" == false && -z "${ROBLOX_LIB_PATH:-}" &&
      ! -f "${data_root}/current.json" &&
      "${MOCKTAIL_SKIP_HOST_CHECK:-0}" != 1 ]]; then
  printf 'Mocktail first-run host check...\n'
  CheckSystem || Die "host requirements are incomplete"
fi

export MOCKTAIL_PROJECT_ROOT="${RUNTIME_ROOT}"
export MOCKTAIL_COMPATIBILITY_MANIFEST="${METADATA_DIR}/roblox_compatibility.json"
export MOCKTAIL_UPDATE_HELPER="${UPDATE_HELPER}"
export MOCKTAIL_UPDATE_CANARY_BIN="${MAIN_BINARY}"
export MOCKTAIL_BIN="${MAIN_BINARY}"
export MOCKTAIL_PORTABLE_MODE="${ABI_MODE}"
# AnyLinux relocates executables into its own bin directory. The Bionic
# adapters stay in the original bundle and must be loaded by exact path.
export MOCKTAIL_RUNTIME_LIBRARY_DIR="${BUNDLED_BIN_DIR}"

cd -- "${RUNTIME_ROOT}"
exec "${MAIN_BINARY}" "$@"
