# Bundled third-party dependencies. Each lives in third_party/ as a git
# submodule and, when the checkout is present, it wins over the system
# package so the build is hermetic. Otherwise fall back to the system
# package so older checkouts keep configuring. Run
# `git submodule update --init --recursive` (scripts/build.sh does this)
# after cloning.
include_guard(GLOBAL)

if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/Vulkan-Headers/CMakeLists.txt")
  add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/Vulkan-Headers"
                   "${CMAKE_BINARY_DIR}/third_party/Vulkan-Headers"
                   EXCLUDE_FROM_ALL)
  message(STATUS "Mocktail: using bundled Vulkan-Headers")
else()
  find_package(VulkanHeaders REQUIRED)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/CMakeLists.txt")
  set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
  set(JSON_Install OFF CACHE BOOL "" FORCE)
  add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/nlohmann_json"
                   "${CMAKE_BINARY_DIR}/third_party/nlohmann_json"
                   EXCLUDE_FROM_ALL)
  message(STATUS "Mocktail: using bundled nlohmann_json")
else()
  find_package(nlohmann_json CONFIG REQUIRED)
endif()

find_package(PkgConfig REQUIRED)
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/libyaml/CMakeLists.txt")
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/libyaml"
                   "${CMAKE_BINARY_DIR}/third_party/libyaml"
                   EXCLUDE_FROM_ALL)
  set_target_properties(yaml PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(NOT TARGET PkgConfig::LIBYAML)
    add_library(PkgConfig::LIBYAML ALIAS yaml)
  endif()
  message(STATUS "Mocktail: using bundled libyaml")
else()
  pkg_check_modules(LIBYAML REQUIRED IMPORTED_TARGET yaml-0.1)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/utf8proc/CMakeLists.txt")
  set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/utf8proc"
                   "${CMAKE_BINARY_DIR}/third_party/utf8proc"
                   EXCLUDE_FROM_ALL)
  set_target_properties(utf8proc PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(NOT TARGET PkgConfig::UTF8PROC)
    add_library(PkgConfig::UTF8PROC ALIAS utf8proc)
  endif()
  message(STATUS "Mocktail: using bundled utf8proc")
else()
  pkg_check_modules(UTF8PROC REQUIRED IMPORTED_TARGET libutf8proc)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/SDL_ttf/CMakeLists.txt")
  set(SDLTTF_VENDORED OFF CACHE BOOL "" FORCE)
  set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
  set(SDLTTF_WERROR OFF CACHE BOOL "" FORCE)
  add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/SDL_ttf"
                   "${CMAKE_BINARY_DIR}/third_party/SDL_ttf"
                   EXCLUDE_FROM_ALL)
  message(STATUS "Mocktail: using bundled SDL_ttf")
else()
  find_package(SDL3_ttf REQUIRED CONFIG)
endif()
