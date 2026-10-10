cmake_minimum_required(VERSION 3.24)

project(ztrip
  VERSION 0.1.0
  DESCRIPTION "Music-reactive animation engine"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ZTRIP_BUILD_TOOLS "Build command-line tools" ON)
option(ZTRIP_BUILD_TESTS "Build unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ZtripWarnings)

add_subdirectory(core)

if(ZTRIP_BUILD_TOOLS)
  add_subdirectory(tools/analyze-cli)
endif()

if(ZTRIP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
