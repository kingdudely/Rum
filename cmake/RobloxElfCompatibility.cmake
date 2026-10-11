# Copyright 2026 Roblox Project Authors
# Licensed under the Apache License, Version 2.0.

include_guard(GLOBAL)

get_filename_component(ROBLOX_ELF_COMPAT_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBELF REQUIRED IMPORTED_TARGET libelf)
if(NOT TARGET nlohmann_json::nlohmann_json)
  find_package(nlohmann_json CONFIG REQUIRED)
endif()

add_library(roblox_compat STATIC
  ${ROBLOX_ELF_COMPAT_ROOT}/src/compat/bionic_atfork_runtime.cc
  ${ROBLOX_ELF_COMPAT_ROOT}/src/compat/bionic_dns_runtime.cc
  ${ROBLOX_ELF_COMPAT_ROOT}/src/compat/bionic_host_libc_runtime.cc
  ${ROBLOX_ELF_COMPAT_ROOT}/src/compat/bionic_large_file_runtime.cc
  ${ROBLOX_ELF_COMPAT_ROOT}/src/compat/bionic_prctl_runtime.cc
  ${ROBLOX_ELF_COMPAT_ROOT}/src/compat/bionic_rwlock_runtime.cc
  ${ROBLOX_ELF_COMPAT_ROOT}/src/compat/bionic_signal_runtime.cc
  ${ROBLOX_ELF_COMPAT_ROOT}/src/compat/bionic_socket_runtime.cc
  ${ROBLOX_ELF_COMPAT_ROOT}/src/compat/bionic_semaphore_runtime.cc
  ${ROBLOX_ELF_COMPAT_ROOT}/src/compat/bionic_pthread_create_runtime.cc
  ${ROBLOX_ELF_COMPAT_ROOT}/src/compat/bionic_pthread_key_runtime.cc
  ${ROBLOX_ELF_COMPAT_ROOT}/src/compat/bionic_sysconf.cc
)
add_library(Roblox::Compat ALIAS roblox_compat)
target_include_directories(roblox_compat PUBLIC
  ${ROBLOX_ELF_COMPAT_ROOT}/include
)
target_compile_definitions(roblox_compat PRIVATE
  "ROBLOX_INSTALL_LIBDIR=\"${CMAKE_INSTALL_LIBDIR}\""
)
target_link_libraries(roblox_compat PUBLIC
  PkgConfig::LIBELF
  nlohmann_json::nlohmann_json
  Threads::Threads
)
target_link_libraries(roblox_compat PRIVATE OpenSSL::Crypto)
target_compile_features(roblox_compat PUBLIC cxx_std_17)

if(COMMAND roblox_apply_compile_options)
  roblox_apply_compile_options(roblox_compat)
endif()
