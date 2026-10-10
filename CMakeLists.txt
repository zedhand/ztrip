cmake_minimum_required(VERSION 3.24)

project(mre
  VERSION 0.1.0
  DESCRIPTION "Music-reactive animation engine"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MRE_BUILD_TOOLS "Build command-line tools" ON)
option(MRE_BUILD_TESTS "Build unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MreWarnings)

add_subdirectory(core)

if(MRE_BUILD_TOOLS)
  add_subdirectory(tools/analyze-cli)
endif()

if(MRE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
