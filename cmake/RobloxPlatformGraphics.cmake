# Copyright 2026 Roblox Project Authors
# Licensed under the Apache License, Version 2.0.

include_guard(GLOBAL)

get_filename_component(ROBLOX_PLATFORM_GRAPHICS_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE
)

find_package(SDL3 3.2 REQUIRED CONFIG)
find_path(ROBLOX_EGL_INCLUDE_DIR EGL/egl.h REQUIRED)
find_path(ROBLOX_GLES3_INCLUDE_DIR GLES3/gl3.h REQUIRED)

set(ROBLOX_ANGLE_HEADERS_INCLUDE_DIR
  "${ROBLOX_PLATFORM_GRAPHICS_ROOT}/third_party/angle_headers/include"
)
if(NOT EXISTS
    "${ROBLOX_ANGLE_HEADERS_INCLUDE_DIR}/EGL/eglext_angle.h")
  message(FATAL_ERROR "pinned ANGLE EGL extension header is unavailable")
endif()
add_library(roblox_angle_headers INTERFACE)
target_include_directories(roblox_angle_headers SYSTEM INTERFACE
  "${ROBLOX_ANGLE_HEADERS_INCLUDE_DIR}"
)
add_library(Roblox::AngleHeaders ALIAS roblox_angle_headers)

add_library(roblox_platform_sdl STATIC
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/src/platform/sdl_application_metadata.cc
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/src/platform/sdl_display_refresh_capabilities.cc
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/src/platform/sdl_event_converter.cc
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/src/platform/sdl_gamepad_manager.cc
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/src/platform/sdl_platform_runtime.cc
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/src/platform/sdl_text_clipboard.cc
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/src/platform/sdl_window_icon.cc
)
target_include_directories(roblox_platform_sdl PUBLIC
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/include
)
target_link_libraries(roblox_platform_sdl PUBLIC SDL3::SDL3)
target_compile_features(roblox_platform_sdl PUBLIC cxx_std_17)
target_compile_definitions(roblox_platform_sdl PRIVATE
  ROBLOX_PROJECT_VERSION="${PROJECT_VERSION}"
)
add_library(Roblox::PlatformSdl ALIAS roblox_platform_sdl)

add_library(roblox_graphics_foundation STATIC
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/src/graphics/graphics_backend.cc
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/src/graphics/angle_probe.cc
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/src/graphics/bionic_egl_bridge.cc
)
target_include_directories(roblox_graphics_foundation
  PUBLIC ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/include
  PRIVATE ${ROBLOX_EGL_INCLUDE_DIR}
)
target_link_libraries(roblox_graphics_foundation PRIVATE
  Roblox::AngleHeaders
  ${CMAKE_DL_LIBS}
)
target_compile_features(roblox_graphics_foundation PUBLIC cxx_std_17)
add_library(Roblox::GraphicsFoundation ALIAS roblox_graphics_foundation)

add_library(roblox_gles_text_overlay STATIC
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/src/graphics/gles_text_overlay_compositor.cc
)
target_include_directories(roblox_gles_text_overlay
  PUBLIC ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/include
  PRIVATE ${ROBLOX_GLES3_INCLUDE_DIR}
)
target_link_libraries(roblox_gles_text_overlay PUBLIC SDL3::SDL3)
target_compile_features(roblox_gles_text_overlay PUBLIC cxx_std_17)
roblox_apply_compile_options(roblox_gles_text_overlay)
add_library(Roblox::GlesTextOverlay ALIAS roblox_gles_text_overlay)

add_library(roblox_sdl_vulkan_wsi STATIC
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/src/graphics/sdl_vulkan_wsi.cc
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/src/graphics/android_vulkan_wsi_adapter.cc
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/src/graphics/present_mode_policy.cc
)
set_target_properties(roblox_sdl_vulkan_wsi PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
target_include_directories(roblox_sdl_vulkan_wsi PUBLIC
  ${ROBLOX_PLATFORM_GRAPHICS_ROOT}/include
)
target_link_libraries(roblox_sdl_vulkan_wsi PUBLIC SDL3::SDL3)
target_link_libraries(roblox_sdl_vulkan_wsi PUBLIC Vulkan::Headers)
target_compile_features(roblox_sdl_vulkan_wsi PUBLIC cxx_std_17)
add_library(Roblox::SdlVulkanWsi ALIAS roblox_sdl_vulkan_wsi)

