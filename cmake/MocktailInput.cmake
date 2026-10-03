# Copyright 2026 Mocktail Project Authors
# Licensed under the Apache License, Version 2.0.

include_guard(GLOBAL)

find_package(PkgConfig REQUIRED)
pkg_check_modules(UTF8PROC REQUIRED IMPORTED_TARGET libutf8proc)
find_package(SDL3_ttf REQUIRED CONFIG)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)

get_filename_component(MOCKTAIL_INPUT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.."
  ABSOLUTE
)

add_library(mocktail_input_runtime STATIC
  ${MOCKTAIL_INPUT_ROOT}/src/runtime/roblox_gamepad_input.cc
  ${MOCKTAIL_INPUT_ROOT}/src/runtime/roblox_input_native_adapter.cc
  ${MOCKTAIL_INPUT_ROOT}/src/runtime/roblox_input_router.cc
  ${MOCKTAIL_INPUT_ROOT}/src/runtime/roblox_native_text_box_info_reader.cc
  ${MOCKTAIL_INPUT_ROOT}/src/runtime/roblox_text_display_state.cc
  ${MOCKTAIL_INPUT_ROOT}/src/runtime/roblox_text_editor.cc
  ${MOCKTAIL_INPUT_ROOT}/src/runtime/roblox_text_font_resolver.cc
  ${MOCKTAIL_INPUT_ROOT}/src/runtime/roblox_text_surface_overlay.cc
  ${MOCKTAIL_INPUT_ROOT}/src/runtime/roblox_window_input_runtime.cc
)
add_library(Mocktail::InputRuntime ALIAS mocktail_input_runtime)
target_include_directories(mocktail_input_runtime PUBLIC
  ${MOCKTAIL_INPUT_ROOT}/include
)
target_include_directories(mocktail_input_runtime PRIVATE
  ${MOCKTAIL_INPUT_ROOT}/src
)
target_link_libraries(mocktail_input_runtime PUBLIC
  Mocktail::PlatformSdl
  Mocktail::Runtime
  mocktail_window
  PkgConfig::UTF8PROC
  PRIVATE
    PkgConfig::FONTCONFIG
    SDL3_ttf::SDL3_ttf
    nlohmann_json::nlohmann_json
)
target_compile_features(mocktail_input_runtime PUBLIC cxx_std_17)
mocktail_apply_compile_options(mocktail_input_runtime)

