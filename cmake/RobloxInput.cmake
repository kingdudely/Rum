# Copyright 2026 Roblox Project Authors
# Licensed under the Apache License, Version 2.0.

include_guard(GLOBAL)

find_package(PkgConfig REQUIRED)
if(NOT TARGET PkgConfig::UTF8PROC)
  pkg_check_modules(UTF8PROC REQUIRED IMPORTED_TARGET libutf8proc)
endif()
if(NOT TARGET SDL3_ttf::SDL3_ttf)
  find_package(SDL3_ttf REQUIRED CONFIG)
endif()
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)

get_filename_component(ROBLOX_INPUT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.."
  ABSOLUTE
)

add_library(roblox_input_runtime STATIC
  ${ROBLOX_INPUT_ROOT}/src/runtime/roblox_gamepad_input.cc
  ${ROBLOX_INPUT_ROOT}/src/runtime/roblox_input_native_adapter.cc
  ${ROBLOX_INPUT_ROOT}/src/runtime/roblox_input_router.cc
  ${ROBLOX_INPUT_ROOT}/src/runtime/roblox_native_text_box_info_reader.cc
  ${ROBLOX_INPUT_ROOT}/src/runtime/roblox_text_display_state.cc
  ${ROBLOX_INPUT_ROOT}/src/runtime/roblox_text_editor.cc
  ${ROBLOX_INPUT_ROOT}/src/runtime/roblox_text_font_resolver.cc
  ${ROBLOX_INPUT_ROOT}/src/runtime/roblox_text_surface_overlay.cc
  ${ROBLOX_INPUT_ROOT}/src/runtime/roblox_window_input_runtime.cc
)
add_library(Roblox::InputRuntime ALIAS roblox_input_runtime)
target_include_directories(roblox_input_runtime PUBLIC
  ${ROBLOX_INPUT_ROOT}/include
)
target_include_directories(roblox_input_runtime PRIVATE
  ${ROBLOX_INPUT_ROOT}/src
)
target_link_libraries(roblox_input_runtime PUBLIC
  Roblox::PlatformSdl
  Roblox::Runtime
  roblox_window
  PkgConfig::UTF8PROC
  PRIVATE
    PkgConfig::FONTCONFIG
    SDL3_ttf::SDL3_ttf
    nlohmann_json::nlohmann_json
)
target_compile_features(roblox_input_runtime PUBLIC cxx_std_17)
roblox_apply_compile_options(roblox_input_runtime)

