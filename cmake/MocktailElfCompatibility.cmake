# Copyright 2026 Mocktail Project Authors
# Licensed under the Apache License, Version 2.0.

include_guard(GLOBAL)

get_filename_component(MOCKTAIL_ELF_COMPAT_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBELF REQUIRED IMPORTED_TARGET libelf)
if(NOT TARGET nlohmann_json::nlohmann_json)
  find_package(nlohmann_json CONFIG REQUIRED)
endif()

add_library(mocktail_compat STATIC
  ${MOCKTAIL_ELF_COMPAT_ROOT}/src/compat/bionic_atfork_runtime.cc
  ${MOCKTAIL_ELF_COMPAT_ROOT}/src/compat/bionic_dns_runtime.cc
  ${MOCKTAIL_ELF_COMPAT_ROOT}/src/compat/bionic_host_libc_runtime.cc
  ${MOCKTAIL_ELF_COMPAT_ROOT}/src/compat/bionic_large_file_runtime.cc
  ${MOCKTAIL_ELF_COMPAT_ROOT}/src/compat/bionic_prctl_runtime.cc
  ${MOCKTAIL_ELF_COMPAT_ROOT}/src/compat/bionic_rwlock_runtime.cc
  ${MOCKTAIL_ELF_COMPAT_ROOT}/src/compat/bionic_signal_runtime.cc
  ${MOCKTAIL_ELF_COMPAT_ROOT}/src/compat/bionic_socket_runtime.cc
  ${MOCKTAIL_ELF_COMPAT_ROOT}/src/compat/bionic_semaphore_runtime.cc
  ${MOCKTAIL_ELF_COMPAT_ROOT}/src/compat/bionic_pthread_create_runtime.cc
  ${MOCKTAIL_ELF_COMPAT_ROOT}/src/compat/bionic_pthread_key_runtime.cc
  ${MOCKTAIL_ELF_COMPAT_ROOT}/src/compat/bionic_sysconf.cc
)
add_library(Mocktail::Compat ALIAS mocktail_compat)
target_include_directories(mocktail_compat PUBLIC
  ${MOCKTAIL_ELF_COMPAT_ROOT}/include
)
target_compile_definitions(mocktail_compat PRIVATE
  "MOCKTAIL_INSTALL_LIBDIR=\"${CMAKE_INSTALL_LIBDIR}\""
)
target_link_libraries(mocktail_compat PUBLIC
  PkgConfig::LIBELF
  nlohmann_json::nlohmann_json
  Threads::Threads
)
target_link_libraries(mocktail_compat PRIVATE OpenSSL::Crypto)
target_compile_features(mocktail_compat PUBLIC cxx_std_17)

if(COMMAND mocktail_apply_compile_options)
  mocktail_apply_compile_options(mocktail_compat)
endif()
