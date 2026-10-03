# Copyright 2026 Mocktail Project Authors
# Licensed under the Apache License, Version 2.0.

include_guard(GLOBAL)

get_filename_component(MOCKTAIL_AUDIO_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE
)

# SDL 3.4 provides the callback needed for OpenSL buffer completion.
find_package(SDL3 3.2 REQUIRED CONFIG)
find_package(Threads REQUIRED)

add_library(mocktail_audio_core STATIC
  ${MOCKTAIL_AUDIO_ROOT}/src/audio/audio_sink.cc
)
target_include_directories(mocktail_audio_core PUBLIC
  ${MOCKTAIL_AUDIO_ROOT}/include
)
target_compile_features(mocktail_audio_core PUBLIC cxx_std_17)
set_target_properties(mocktail_audio_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
add_library(Mocktail::AudioCore ALIAS mocktail_audio_core)

add_library(mocktail_audio_sdl SHARED
  ${MOCKTAIL_AUDIO_ROOT}/src/audio/sdl_audio_capture.cc
  ${MOCKTAIL_AUDIO_ROOT}/src/audio/sdl_audio_sink.cc
)
target_include_directories(mocktail_audio_sdl PUBLIC
  ${MOCKTAIL_AUDIO_ROOT}/include
)
target_link_libraries(mocktail_audio_sdl PUBLIC
  Mocktail::AudioCore
  SDL3::SDL3
)
target_compile_features(mocktail_audio_sdl PUBLIC cxx_std_17)
set_target_properties(mocktail_audio_sdl PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  BUILD_RPATH_USE_ORIGIN TRUE
  BUILD_RPATH "\$ORIGIN"
  INSTALL_RPATH "\$ORIGIN"
)
add_library(Mocktail::AudioSdl ALIAS mocktail_audio_sdl)

add_library(mocktail_audio_fmod_java_runtime STATIC
  ${MOCKTAIL_AUDIO_ROOT}/src/audio/fmod_java_audio_runtime.cc
)
target_include_directories(mocktail_audio_fmod_java_runtime PUBLIC
  ${MOCKTAIL_AUDIO_ROOT}/include
)
target_link_libraries(mocktail_audio_fmod_java_runtime PUBLIC
  Mocktail::AudioSdl
  Threads::Threads
)
target_compile_features(mocktail_audio_fmod_java_runtime PUBLIC cxx_std_17)
set_target_properties(mocktail_audio_fmod_java_runtime PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
add_library(Mocktail::FmodJavaAudioRuntime ALIAS
  mocktail_audio_fmod_java_runtime
)

add_library(mocktail_fmod_jni_audio_bridge STATIC
  ${MOCKTAIL_AUDIO_ROOT}/src/audio/fmod_jni_audio_bridge.cc
  ${MOCKTAIL_AUDIO_ROOT}/src/audio/webrtc_jni_audio_bridge.cc
)
target_include_directories(mocktail_fmod_jni_audio_bridge PUBLIC
  ${MOCKTAIL_AUDIO_ROOT}/include
)
target_link_libraries(mocktail_fmod_jni_audio_bridge PUBLIC
  Mocktail::FmodJavaAudioRuntime
  Mocktail::LegacyJni
)
target_compile_features(mocktail_fmod_jni_audio_bridge PUBLIC cxx_std_17)
set_target_properties(mocktail_fmod_jni_audio_bridge PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
add_library(Mocktail::FmodJniAudioBridge ALIAS
  mocktail_fmod_jni_audio_bridge
)

add_library(mocktail_audio_opensl_adapter STATIC
  ${MOCKTAIL_AUDIO_ROOT}/src/audio/opensl_simple_buffer_queue.cc
)
target_include_directories(mocktail_audio_opensl_adapter PUBLIC
  ${MOCKTAIL_AUDIO_ROOT}/include
)
target_link_libraries(mocktail_audio_opensl_adapter PUBLIC
  Mocktail::AudioCore
  Threads::Threads
)
target_compile_features(mocktail_audio_opensl_adapter PUBLIC cxx_std_17)
set_target_properties(mocktail_audio_opensl_adapter PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
add_library(Mocktail::AudioOpenSlAdapter ALIAS
  mocktail_audio_opensl_adapter
)

add_library(mocktail_audio INTERFACE)
target_link_libraries(mocktail_audio INTERFACE
  Mocktail::AudioSdl
  Mocktail::AudioOpenSlAdapter
)
add_library(Mocktail::Audio ALIAS mocktail_audio)

# Desktop builds use opensl_abi.h when NDK headers are unavailable.
find_path(MOCKTAIL_OPENSL_CORE_INCLUDE_DIR SLES/OpenSLES.h)
find_path(MOCKTAIL_OPENSL_ANDROID_INCLUDE_DIR SLES/OpenSLES_Android.h)
if(MOCKTAIL_OPENSL_CORE_INCLUDE_DIR AND MOCKTAIL_OPENSL_ANDROID_INCLUDE_DIR)
  target_include_directories(mocktail_audio_opensl_adapter PUBLIC
    ${MOCKTAIL_OPENSL_CORE_INCLUDE_DIR}
    ${MOCKTAIL_OPENSL_ANDROID_INCLUDE_DIR}
  )
  target_compile_definitions(mocktail_audio_opensl_adapter PUBLIC
    MOCKTAIL_USE_SYSTEM_OPENSL_HEADERS=1
  )
  message(STATUS "Mocktail audio: using system Android OpenSL headers")
else()
  message(STATUS
    "Mocktail audio: Android OpenSL headers unavailable; using minimal queue ABI boundary"
  )
endif()

add_library(mocktail_opensles SHARED
  ${MOCKTAIL_AUDIO_ROOT}/src/audio/opensl_playback_runtime.cc
)
target_include_directories(mocktail_opensles PUBLIC
  ${MOCKTAIL_AUDIO_ROOT}/include
)
target_link_libraries(mocktail_opensles PRIVATE
  Mocktail::Audio
)
target_compile_features(mocktail_opensles PRIVATE cxx_std_17)
set_target_properties(mocktail_opensles PROPERTIES
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
target_link_options(mocktail_opensles PRIVATE
  "-Wl,-soname,libOpenSLES.so"
  "-Wl,--exclude-libs,ALL"
)

