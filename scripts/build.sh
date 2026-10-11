#!/usr/bin/env bash
# Copyright 2026 Roblox Project Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

info()    { echo -e "${CYAN}[info]${RESET}  $*"; }
success() { echo -e "${GREEN}[ok]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[warn]${RESET}  $*"; }
die()     { echo -e "${RED}[error]${RESET} $*" >&2; exit 1; }
step()    { echo; echo -e "${BOLD}[$*]${RESET}"; }

Usage() {
  cat <<'EOF'
Usage: scripts/build.sh [OPTIONS]

Builds the native runtime. Place libroblox.so and assets/content next
to the resulting executable, or pass --roblox-lib/--assets at launch.

Options:
  --build-dir PATH    CMake build directory (default: build).
  --cmake-toolchain FILE
                      Explicit CMake toolchain file.
  --cmake-sysroot DIR Explicit compiler/CMake sysroot.
  --build-type TYPE   Debug, Release, or RelWithDebInfo (default: Release).
  --jobs N            Parallel build jobs (default: nproc).
  --clean             Remove the selected build directory first.
  -h, --help          Show this help.

Examples:
  ./scripts/build.sh
  ./scripts/build.sh --build-type Debug
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
BUILD_TYPE="Release"
JOBS="$(nproc)"
CLEAN=false
CMAKE_TOOLCHAIN_FILE=""
CMAKE_SYSROOT=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      [[ $# -ge 2 ]] || die "--build-dir requires a path"
      BUILD_DIR="$2"
      shift 2
      ;;
    --cmake-toolchain)
      [[ $# -ge 2 ]] || die "--cmake-toolchain requires a file"
      CMAKE_TOOLCHAIN_FILE="$2"
      shift 2
      ;;
    --cmake-sysroot)
      [[ $# -ge 2 ]] || die "--cmake-sysroot requires a directory"
      CMAKE_SYSROOT="$2"
      shift 2
      ;;
    --build-type)  BUILD_TYPE="$2"; shift 2 ;;
    --jobs)        JOBS="$2"; shift 2 ;;
    --clean)       CLEAN=true; shift ;;
    --help|-h)
      Usage
      exit 0
      ;;
    *)
      die "Unknown option: $1  (try --help)"
      ;;
  esac
done

if [[ "${BUILD_DIR}" != /* ]]; then
  BUILD_DIR="${PROJECT_ROOT}/${BUILD_DIR}"
fi
BUILD_DIR="$(readlink -m -- "${BUILD_DIR}")"
[[ "${BUILD_DIR}" != / && "${BUILD_DIR}" != "${PROJECT_ROOT}" ]] ||
  die "Unsafe build directory: ${BUILD_DIR}"
if [[ -n "${CMAKE_TOOLCHAIN_FILE}" ]]; then
  [[ -f "${CMAKE_TOOLCHAIN_FILE}" ]] ||
    die "CMake toolchain file not found: ${CMAKE_TOOLCHAIN_FILE}"
  CMAKE_TOOLCHAIN_FILE="$(readlink -f -- "${CMAKE_TOOLCHAIN_FILE}")"
fi
if [[ -n "${CMAKE_SYSROOT}" ]]; then
  [[ -d "${CMAKE_SYSROOT}" ]] ||
    die "CMake sysroot not found: ${CMAKE_SYSROOT}"
  CMAKE_SYSROOT="$(cd -P -- "${CMAKE_SYSROOT}" && pwd)"
fi

info "Roblox build: ${BUILD_TYPE}, -j${JOBS}"
info "Project root: ${PROJECT_ROOT}"
info "Build directory: ${BUILD_DIR}"

step "1/3 checking dependencies"

check_tool() {
  local tool="$1"
  local hint="${2:-install ${tool}}"
  if command -v "${tool}" &>/dev/null; then
    success "${tool} found: $(command -v "${tool}")"
  else
    die "${tool} not found. Hint: ${hint}"
  fi
}

check_tool cmake   "install CMake 3.20 or newer"
if [[ -z "${CMAKE_TOOLCHAIN_FILE}" ]]; then
  check_tool c++ "install GCC or Clang with C++17 support"
fi
check_tool git     "sudo apt install git"

step "2/3 initialising git submodules"
cd "${PROJECT_ROOT}"
if [[ -f ".gitmodules" ]]; then
  git submodule update --init --recursive
  success "Submodules ready."
else
  warn "No .gitmodules found — skipping submodule init."
fi

step "3/3 configuring CMake"

if [[ "${CLEAN}" == true && -d "${BUILD_DIR}" ]]; then
  info "Removing old build directory…"
  rm -rf "${BUILD_DIR}"
fi

cmake_arguments=(
  -S "${PROJECT_ROOT}"
  -B "${BUILD_DIR}"
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)
if [[ -n "${CMAKE_TOOLCHAIN_FILE}" ]]; then
  cmake_arguments+=("-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
fi
if [[ -n "${CMAKE_SYSROOT}" ]]; then
  cmake_arguments+=("-DCMAKE_SYSROOT=${CMAKE_SYSROOT}")
fi
cmake "${cmake_arguments[@]}"

success "CMake configuration complete."

step "building Roblox"

cmake --build "${BUILD_DIR}" -j"${JOBS}"

success "Build complete → ${BUILD_DIR}/roblox"

echo
success "Build successful."
echo "Run:"
echo -e "  ${CYAN}${BUILD_DIR}/roblox${RESET}"
info "Place libroblox.so and assets/content next to the resulting executable."
echo
