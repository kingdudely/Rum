# Copyright 2026 Roblox Project Authors
# Licensed under the Apache License, Version 2.0.

include_guard(GLOBAL)

get_filename_component(ROBLOX_AUDIO_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE
)

# SDL 3.4 provides the callback needed for OpenSL buffer completion.
find_package(SDL3 3.2 REQUIRED CONFIG)
find_package(Threads REQUIRED)

add_library(roblox_audio_core STATIC
  ${ROBLOX_AUDIO_ROOT}/src/audio/audio_sink.cc
)
target_include_directories(roblox_audio_core PUBLIC
  ${ROBLOX_AUDIO_ROOT}/include
)
target_compile_features(roblox_audio_core PUBLIC cxx_std_17)
set_target_properties(roblox_audio_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
add_library(Roblox::AudioCore ALIAS roblox_audio_core)

add_library(roblox_audio_sdl SHARED
  ${ROBLOX_AUDIO_ROOT}/src/audio/sdl_audio_capture.cc
  ${ROBLOX_AUDIO_ROOT}/src/audio/sdl_audio_sink.cc
)
target_include_directories(roblox_audio_sdl PUBLIC
  ${ROBLOX_AUDIO_ROOT}/include
)
target_link_libraries(roblox_audio_sdl PUBLIC
  Roblox::AudioCore
  SDL3::SDL3
)
target_compile_features(roblox_audio_sdl PUBLIC cxx_std_17)
set_target_properties(roblox_audio_sdl PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  BUILD_RPATH_USE_ORIGIN TRUE
  BUILD_RPATH "\$ORIGIN"
  INSTALL_RPATH "\$ORIGIN"
)
add_library(Roblox::AudioSdl ALIAS roblox_audio_sdl)

add_library(roblox_audio_fmod_java_runtime STATIC
  ${ROBLOX_AUDIO_ROOT}/src/audio/fmod_java_audio_runtime.cc
)
target_include_directories(roblox_audio_fmod_java_runtime PUBLIC
  ${ROBLOX_AUDIO_ROOT}/include
)
target_link_libraries(roblox_audio_fmod_java_runtime PUBLIC
  Roblox::AudioSdl
  Threads::Threads
)
target_compile_features(roblox_audio_fmod_java_runtime PUBLIC cxx_std_17)
set_target_properties(roblox_audio_fmod_java_runtime PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
add_library(Roblox::FmodJavaAudioRuntime ALIAS
  roblox_audio_fmod_java_runtime
)

add_library(roblox_fmod_jni_audio_bridge STATIC
  ${ROBLOX_AUDIO_ROOT}/src/audio/fmod_jni_audio_bridge.cc
  ${ROBLOX_AUDIO_ROOT}/src/audio/webrtc_jni_audio_bridge.cc
)
target_include_directories(roblox_fmod_jni_audio_bridge PUBLIC
  ${ROBLOX_AUDIO_ROOT}/include
)
target_link_libraries(roblox_fmod_jni_audio_bridge PUBLIC
  Roblox::FmodJavaAudioRuntime
  Roblox::LegacyJni
)
target_compile_features(roblox_fmod_jni_audio_bridge PUBLIC cxx_std_17)
set_target_properties(roblox_fmod_jni_audio_bridge PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
add_library(Roblox::FmodJniAudioBridge ALIAS
  roblox_fmod_jni_audio_bridge
)

add_library(roblox_audio INTERFACE)
target_link_libraries(roblox_audio INTERFACE
  Roblox::AudioSdl
)
add_library(Roblox::Audio ALIAS roblox_audio)

add_library(roblox_opensles SHARED
  ${ROBLOX_AUDIO_ROOT}/src/audio/opensl_playback_runtime.cc
)
target_include_directories(roblox_opensles PUBLIC
  ${ROBLOX_AUDIO_ROOT}/include
)
target_link_libraries(roblox_opensles PRIVATE
  Roblox::Audio
)
target_compile_features(roblox_opensles PRIVATE cxx_std_17)
set_target_properties(roblox_opensles PROPERTIES
  OUTPUT_NAME OpenSLES
  PREFIX "lib"
  SUFFIX ".so"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
  BUILD_RPATH_USE_ORIGIN TRUE
  BUILD_RPATH "\$ORIGIN"
  INSTALL_RPATH "\$ORIGIN"
)
target_link_options(roblox_opensles PRIVATE
  "-Wl,-soname,libOpenSLES.so"
  "-Wl,--exclude-libs,ALL"
)

