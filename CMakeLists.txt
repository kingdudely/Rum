# Copyright 2026 Roblox Project Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0

cmake_minimum_required(VERSION 3.20)
project(Roblox
  VERSION 1.0.4
  DESCRIPTION "Android Roblox compatibility runtime for Linux"
  LANGUAGES CXX
)

include(GNUInstallDirs)
find_package(Threads REQUIRED)

set(ROBLOX_BUILD_GIT_COMMIT "unknown")
set(ROBLOX_BUILD_GIT_DIRTY "unknown")
find_package(Git QUIET)
if(Git_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE ROBLOX_GIT_REV_PARSE_RESULT
    OUTPUT_VARIABLE ROBLOX_GIT_REV_PARSE_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(ROBLOX_GIT_REV_PARSE_RESULT EQUAL 0 AND
     ROBLOX_GIT_REV_PARSE_OUTPUT MATCHES "^[0-9a-fA-F]+$")
    string(TOLOWER "${ROBLOX_GIT_REV_PARSE_OUTPUT}"
      ROBLOX_BUILD_GIT_COMMIT)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      RESULT_VARIABLE ROBLOX_GIT_STATUS_RESULT
      OUTPUT_VARIABLE ROBLOX_GIT_STATUS_OUTPUT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(ROBLOX_GIT_STATUS_RESULT EQUAL 0)
      if(ROBLOX_GIT_STATUS_OUTPUT STREQUAL "")
        set(ROBLOX_BUILD_GIT_DIRTY "false")
      else()
        set(ROBLOX_BUILD_GIT_DIRTY "true")
      endif()
    endif()
  endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# libjnivm uses C++ exceptions, so these cannot be directory-wide.
add_library(roblox_compile_options INTERFACE)
target_compile_options(roblox_compile_options INTERFACE
  "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wno-unused-parameter;-fno-exceptions;-g>"
)

function(roblox_apply_compile_options target_name)
  target_link_libraries(${target_name} PRIVATE roblox_compile_options)
endfunction()

add_subdirectory(third_party/mcpelauncher-linker)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(linker PRIVATE -w)
endif()

include(${CMAKE_SOURCE_DIR}/cmake/RobloxBundledDeps.cmake)

find_package(OpenSSL REQUIRED)

include(${CMAKE_SOURCE_DIR}/cmake/RobloxPlatformGraphics.cmake)
foreach(foundation_target IN ITEMS
    roblox_platform_sdl
    roblox_graphics_foundation
    roblox_sdl_vulkan_wsi)
  roblox_apply_compile_options(${foundation_target})
endforeach()

include(${CMAKE_SOURCE_DIR}/cmake/RobloxAudio.cmake)
foreach(audio_target IN ITEMS
    roblox_audio_core
    roblox_audio_sdl
    roblox_audio_fmod_java_runtime
    roblox_fmod_jni_audio_bridge
    roblox_opensles)
  roblox_apply_compile_options(${audio_target})
endforeach()

include(${CMAKE_SOURCE_DIR}/cmake/RobloxElfCompatibility.cmake)

# Bionic stdio symbols must exist before the linker target.
add_library(roblox_bionic_stdio_runtime STATIC
  src/compat/bionic_stdio_runtime.cc
)
add_library(Roblox::BionicStdio ALIAS roblox_bionic_stdio_runtime)
target_include_directories(roblox_bionic_stdio_runtime PUBLIC include)
roblox_apply_compile_options(roblox_bionic_stdio_runtime)

find_package(CURL REQUIRED)
if(NOT TARGET nlohmann_json::nlohmann_json)
  find_package(nlohmann_json CONFIG REQUIRED)
endif()
find_package(PkgConfig REQUIRED)

add_library(roblox_legacy_jnivm STATIC
  src/jnivm/jnivm.cc
  src/jnivm/jni_audio.cc
  src/jnivm/jni_function_tables.cc
  src/jnivm/jni_identity_cookies.cc
  src/jnivm/jni_message_bus.cc
  src/jnivm/jni_references.cc
  src/jnivm/jni_fields.cc
  src/jnivm/jni_roblox_text_input.cc
  src/jnivm/jni_strings.cc
  src/jnivm/jni_vm_lifecycle.cc
)
add_library(Roblox::LegacyJni ALIAS roblox_legacy_jnivm)
target_include_directories(roblox_legacy_jnivm PUBLIC include)
roblox_apply_compile_options(roblox_legacy_jnivm)
# A JDK is not required for these ABI-only headers.
set(ROBLOX_JNI_INCLUDE_DIRS
  ${CMAKE_SOURCE_DIR}/third_party/jni/include
)
target_include_directories(roblox_legacy_jnivm PUBLIC
  ${ROBLOX_JNI_INCLUDE_DIRS}
)

add_library(roblox_runtime STATIC
  src/runtime/atomic_json_file.cc
  src/runtime/command_line.cc
  src/runtime/crash_report_policy.cc
  src/runtime/device_memory_profile.cc
  src/runtime/device_profile.cc
  src/runtime/environment.cc
  src/runtime/frame_rate_policy.cc
  src/runtime/game_mode.cc
  src/runtime/game_session_coordinator.cc
  src/runtime/graphics_launch_policy.cc
  src/runtime/http_client_policy.cc
  src/runtime/owned_pthread.cc
  src/runtime/performance_policy.cc
  src/runtime/platform_cache_migration.cc
  src/runtime/roblox_desktop_app_policy.cc
  src/runtime/roblox_app_lifecycle.cc
  src/runtime/roblox_capability_resolver.cc
  src/runtime/roblox_experience_launch_bridge.cc
  src/runtime/roblox_launch_uri.cc
  src/runtime/roblox_presence_resolver.cc
  src/runtime/roblox_game_session_native_adapter.cc
  src/runtime/roblox_permissions_bridge.cc
  src/runtime/roblox_message_bus_request_bridge.cc
  src/runtime/roblox_call_protocol_bridge.cc
  src/runtime/process_launch_policy.cc
  src/runtime/process_diagnostics.cc
  src/runtime/runtime_config.cc
  src/runtime/runtime_paths.cc
  src/runtime/session_log.cc
  src/runtime/texture_memory_policy.cc
)
add_library(Roblox::Runtime ALIAS roblox_runtime)
target_include_directories(roblox_runtime PUBLIC include)
target_include_directories(roblox_runtime PUBLIC
  ${ROBLOX_JNI_INCLUDE_DIRS}
)
set_property(SOURCE src/runtime/session_log.cc APPEND PROPERTY
  COMPILE_DEFINITIONS
  "ROBLOX_PROJECT_VERSION=\"${PROJECT_VERSION}\""
  "ROBLOX_BUILD_GIT_COMMIT=\"${ROBLOX_BUILD_GIT_COMMIT}\""
  "ROBLOX_BUILD_GIT_DIRTY=\"${ROBLOX_BUILD_GIT_DIRTY}\""
  "ROBLOX_BUILD_TYPE=\"$<CONFIG>\""
  "ROBLOX_BUILD_COMPILER_ID=\"${CMAKE_CXX_COMPILER_ID}\""
  "ROBLOX_BUILD_COMPILER_VERSION=\"${CMAKE_CXX_COMPILER_VERSION}\""
  "ROBLOX_BUILD_TARGET_SYSTEM=\"${CMAKE_SYSTEM_NAME}\""
  "ROBLOX_BUILD_TARGET_PROCESSOR=\"${CMAKE_SYSTEM_PROCESSOR}\""
)
roblox_apply_compile_options(roblox_runtime)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
  # GCC 16 reports a false array-bounds warning in nlohmann::json::dump().
  set_property(SOURCE src/runtime/roblox_launch_uri.cc APPEND PROPERTY
    COMPILE_OPTIONS -Wno-array-bounds
  )
endif()
target_link_libraries(roblox_runtime PUBLIC Threads::Threads)
target_link_libraries(roblox_runtime PRIVATE
  OpenSSL::Crypto
  CURL::libcurl
  nlohmann_json::nlohmann_json
  ${CMAKE_DL_LIBS}
)

add_library(roblox_services STATIC
  src/services/http_client.cc
  src/services/auth_service.cc
  src/services/client_settings_service.cc
)
add_library(Roblox::Services ALIAS roblox_services)
target_include_directories(roblox_services PUBLIC include)
target_link_libraries(roblox_services PUBLIC
  roblox_runtime
  CURL::libcurl
  nlohmann_json::nlohmann_json
)
roblox_apply_compile_options(roblox_services)

add_library(roblox_auth_runtime_composition STATIC
  src/runtime/auth_runtime_composition.cc
)
add_library(Roblox::AuthRuntimeComposition ALIAS
  roblox_auth_runtime_composition)
target_include_directories(roblox_auth_runtime_composition PUBLIC include)
target_link_libraries(roblox_auth_runtime_composition PUBLIC
  roblox_legacy_jnivm
  roblox_runtime
  roblox_services
)
roblox_apply_compile_options(roblox_auth_runtime_composition)

add_library(roblox_linker STATIC
  src/linker/linker.cc
)
target_include_directories(roblox_linker PUBLIC include)
target_link_libraries(roblox_linker PUBLIC
  dl
  linker
  roblox_compat
  roblox_bionic_stdio_runtime
  roblox_bionic_abi_exports
)
roblox_apply_compile_options(roblox_linker)
target_compile_definitions(roblox_linker PUBLIC ROBLOX_USE_BIONIC_LINKER=1)
# The Bionic linker expects IPLT boundaries that host executables do not have.
target_link_options(roblox_linker INTERFACE
  -Wl,--defsym=__rela_iplt_start=0
  -Wl,--defsym=__rela_iplt_end=0
)

add_library(libc_shim STATIC
  src/libc_shim/libc_shim.cc
)
target_include_directories(libc_shim PUBLIC include)
roblox_apply_compile_options(libc_shim)
target_link_libraries(roblox_linker PUBLIC libc_shim)

add_subdirectory(stubs)
foreach(stub_target IN ITEMS
    stub_sys_m stub_sys_z stub_sys_dl stub_sys_c
    stub_android stub_log stub_mediandk stub_openmax
    stub_egl stub_glesv2 stub_vulkan)
  roblox_apply_compile_options(${stub_target})
endforeach()

add_library(roblox_window STATIC
  src/window/roblox_fullscreen_menu_request_gate.cc
  src/window/video_driver_policy.cc
  src/window/window.cc
  src/window/window_creation_policy.cc
  src/window/window_fullscreen_request_gate.cc
  src/window/window_game_surface_bridge.cc
  src/window/window_pointer_capture_owner.cc
  src/window/window_state_store.cc
  src/window/window_surface_lifecycle.cc
  src/window/window_text_input_owner.cc
)
target_include_directories(roblox_window PUBLIC src)
roblox_apply_compile_options(roblox_window)
target_link_libraries(roblox_window PUBLIC
  Roblox::PlatformSdl
  Roblox::SdlVulkanWsi
  SDL3::SDL3
  ${CMAKE_DL_LIBS}
  PRIVATE Roblox::Runtime Roblox::GlesTextOverlay
)

include(${CMAKE_SOURCE_DIR}/cmake/RobloxInput.cmake)

add_library(roblox_experience_runtime STATIC
  src/runtime/jnivm_platform_web_callbacks.cc
  src/runtime/roblox_experience_composition.cc
  src/runtime/roblox_fresh_game_launch_controller.cc
  src/runtime/roblox_platform_protocol_symbols.cc
)
add_library(Roblox::ExperienceRuntime ALIAS roblox_experience_runtime)
target_include_directories(roblox_experience_runtime PUBLIC include)
target_include_directories(roblox_experience_runtime PRIVATE src)
target_link_libraries(roblox_experience_runtime PUBLIC
  Roblox::Runtime
  PkgConfig::UTF8PROC
  PRIVATE
    Roblox::AuthRuntimeComposition
    Roblox::LegacyJni
    roblox_linker
    roblox_window
)
roblox_apply_compile_options(roblox_experience_runtime)

# Compatibility exports still used by the old startup path.
add_library(roblox_bionic_abi_exports STATIC
  src/compat/bionic_abi_exports.cc
  src/compat/http_client_spin_guard.cc
)
target_include_directories(roblox_bionic_abi_exports
  PUBLIC include
  PRIVATE src
)
roblox_apply_compile_options(roblox_bionic_abi_exports)
# These exported Bionic FORTIFY symbols intentionally overlap glibc's
# __*_chk entry points.  When the bridge is visible to whole-program LTO,
# GCC can fold a fortified host call back into its own compatibility wrapper
# and turn the resulting tail recursion into an infinite loop.  Keep this
# translation unit opaque to interprocedural optimization and make its calls
# to the host C library explicit external calls.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set_property(SOURCE src/compat/bionic_abi_exports.cc APPEND PROPERTY
    COMPILE_OPTIONS
      -fno-lto
      -fno-builtin
  )
endif()
target_link_libraries(roblox_bionic_abi_exports PRIVATE
  roblox_bionic_stdio_runtime
  roblox_window
  Threads::Threads
)

include(CheckCXXSourceCompiles)
include(CheckIncludeFileCXX)
include(CMakePushCheckState)

check_include_file_cxx(execinfo.h ROBLOX_HAVE_EXECINFO_H)
if(NOT ROBLOX_HAVE_EXECINFO_H)
  message(FATAL_ERROR
    "execinfo.h is required for crash diagnostics; on musl install "
    "libexecinfo-devel"
  )
endif()

set(ROBLOX_EXECINFO_LINK_LIBRARY "")
set(_roblox_execinfo_link_probe [=[
  #include <execinfo.h>
  int main() {
    void* frames[1] = {};
    const int count = backtrace(frames, 1);
    return backtrace_symbols(frames, count) == nullptr;
  }
]=])
check_cxx_source_compiles(
  "${_roblox_execinfo_link_probe}"
  ROBLOX_EXECINFO_IN_HOST_LIBC
)
if(NOT ROBLOX_EXECINFO_IN_HOST_LIBC)
  find_library(ROBLOX_EXECINFO_LIBRARY NAMES execinfo REQUIRED)
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_LIBRARIES "${ROBLOX_EXECINFO_LIBRARY}")
  check_cxx_source_compiles(
    "${_roblox_execinfo_link_probe}"
    ROBLOX_EXECINFO_IN_EXTERNAL_LIBRARY
  )
  cmake_pop_check_state()
  if(NOT ROBLOX_EXECINFO_IN_EXTERNAL_LIBRARY)
    message(FATAL_ERROR
      "execinfo.h is available, but backtrace and backtrace_symbols cannot "
      "be linked"
    )
  endif()
  set(ROBLOX_EXECINFO_LINK_LIBRARY "${ROBLOX_EXECINFO_LIBRARY}")
endif()
unset(_roblox_execinfo_link_probe)

add_library(roblox_legacy_runtime STATIC
  src/legacy/legacy_runtime.cc
  src/legacy/legacy_env.cc
  src/legacy/legacy_jni_builders.cc
  src/legacy/legacy_native_settings.cc
  src/legacy/legacy_engine_symbols.cc
  src/runtime/roblox_text_input_jni_bridge.cc
)
target_include_directories(roblox_legacy_runtime PRIVATE include src)
roblox_apply_compile_options(roblox_legacy_runtime)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # Diagnostics from signal handlers cannot handle a failed write.
  set_property(SOURCE src/legacy/legacy_runtime.cc APPEND PROPERTY
    COMPILE_OPTIONS -Wno-unused-result
  )
endif()
target_link_libraries(roblox_legacy_runtime PRIVATE
  roblox_legacy_jnivm
  roblox_compat
  roblox_runtime
  roblox_input_runtime
  roblox_experience_runtime
  roblox_services
  roblox_linker
  roblox_bionic_abi_exports
  libc_shim
  roblox_window
  roblox_graphics_foundation
  ${ROBLOX_EXECINFO_LINK_LIBRARY}
)
target_compile_definitions(roblox_legacy_runtime PRIVATE
  "ROBLOX_UNUSED_LEGACY_DEFINE=1"
)

add_executable(roblox src/main.cc)
target_include_directories(roblox PRIVATE include src)
roblox_apply_compile_options(roblox)
target_link_libraries(roblox PRIVATE
  roblox_legacy_runtime
  roblox_auth_runtime_composition
  roblox_fmod_jni_audio_bridge
  roblox_window
  libc_shim
)

# Libraries loaded with dlopen() need the stub symbols.
target_link_options(roblox PRIVATE
  -Wl,--export-dynamic
)

# Native FreeBSD helper for Linuxulator's unsupported DF socket option.
# The helper is x86-64 assembly; other host architectures have nothing to
# assemble.
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  set(ROBLOX_BUILD_FREEBSD_SOCKET_HELPER OFF)
endif()
# The helper only runs under FreeBSD Linuxulator; building it elsewhere just
# demands ld.lld for an artifact that is never executed.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  set(ROBLOX_BUILD_FREEBSD_SOCKET_HELPER OFF)
endif()
option(ROBLOX_BUILD_FREEBSD_SOCKET_HELPER
  "Build the native FreeBSD Linuxulator socket helper" ON)
if(ROBLOX_BUILD_FREEBSD_SOCKET_HELPER)
find_program(ROBLOX_LLD_EXECUTABLE NAMES ld.lld REQUIRED)
find_program(ROBLOX_ELFEDIT_EXECUTABLE NAMES elfedit REQUIRED)
get_filename_component(ROBLOX_LLD_DIRECTORY
  "${ROBLOX_LLD_EXECUTABLE}" DIRECTORY)
set(ROBLOX_FREEBSD_SOCKET_HELPER
  "${CMAKE_BINARY_DIR}/roblox_freebsd_socket_helper")
add_custom_command(
  OUTPUT "${ROBLOX_FREEBSD_SOCKET_HELPER}"
  COMMAND "${CMAKE_CXX_COMPILER}"
    -x assembler-with-cpp
    -nostdlib
    -static
    -no-pie
    "-B${ROBLOX_LLD_DIRECTORY}/"
    -fuse-ld=lld
    -Wl,-e,_start,--build-id=none,-z,noexecstack
    -o "${ROBLOX_FREEBSD_SOCKET_HELPER}"
    "${CMAKE_SOURCE_DIR}/src/compat/freebsd_socket_helper.S"
  COMMAND "${ROBLOX_ELFEDIT_EXECUTABLE}"
    --output-osabi FreeBSD "${ROBLOX_FREEBSD_SOCKET_HELPER}"
  DEPENDS "${CMAKE_SOURCE_DIR}/src/compat/freebsd_socket_helper.S"
  COMMENT "Building native FreeBSD Linuxulator socket helper"
  VERBATIM
)
add_custom_target(roblox_freebsd_socket_helper_build ALL
  DEPENDS "${ROBLOX_FREEBSD_SOCKET_HELPER}"
)
add_dependencies(roblox roblox_freebsd_socket_helper_build)
endif()

set_target_properties(roblox PROPERTIES
  BUILD_RPATH   "\$ORIGIN"
  INSTALL_RPATH "\$ORIGIN;\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/roblox"
)

set(ROBLOX_INSTALL_RUNTIME_DIR "${CMAKE_INSTALL_LIBDIR}/roblox")
set(ROBLOX_INSTALL_DATA_DIR "${CMAKE_INSTALL_DATADIR}/roblox")

install(TARGETS roblox
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(TARGETS
    roblox
    roblox_audio_sdl
    roblox_opensles
    stub_sys_m
    stub_sys_z
    stub_sys_dl
    stub_sys_c
    stub_android
    stub_log
    stub_mediandk
    stub_openmax
    stub_egl
    stub_glesv2
    stub_vulkan
  RUNTIME DESTINATION "${ROBLOX_INSTALL_RUNTIME_DIR}"
  LIBRARY DESTINATION "${ROBLOX_INSTALL_RUNTIME_DIR}"
)
if(ROBLOX_BUILD_FREEBSD_SOCKET_HELPER)
install(PROGRAMS "${ROBLOX_FREEBSD_SOCKET_HELPER}"
  DESTINATION "${ROBLOX_INSTALL_RUNTIME_DIR}"
)
endif()

add_dependencies(roblox all_stubs)

