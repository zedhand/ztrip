cmake_minimum_required(VERSION 3.24)

project(ztrip
  VERSION 0.1.0
  DESCRIPTION "Music-reactive animation engine"
  LANGUAGES C CXX)

# Pre-release label appended to the version for --version, package names and
# release tags, e.g. "beta.1" gives 0.1.0-beta.1 and tag v0.1.0-beta.1. Leave
# it empty for a final release. See docs/RELEASING.md.
set(ZTRIP_VERSION_PRERELEASE "beta.1")
if(ZTRIP_VERSION_PRERELEASE)
  set(ZTRIP_VERSION_FULL "${PROJECT_VERSION}-${ZTRIP_VERSION_PRERELEASE}")
else()
  set(ZTRIP_VERSION_FULL "${PROJECT_VERSION}")
endif()
# Read by the release workflow to check the tag against the version.
file(WRITE "${CMAKE_BINARY_DIR}/ztrip-version.txt" "${ZTRIP_VERSION_FULL}\n")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ZTRIP_BUILD_TOOLS "Build command-line tools" ON)
option(ZTRIP_BUILD_TESTS "Build unit tests" ON)
option(ZTRIP_BUILD_GODOT "Build the Godot 4 GDExtension host (fetches godot-cpp)" OFF)

if(ZTRIP_BUILD_GODOT)
  # The extension is a shared library that links ztrip_core statically, so
  # everything must be position independent and, on MSVC, use the static
  # runtime that godot-cpp links by default.
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "MSVC runtime library" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(ZtripWarnings)
include(ZtripDependencies)

add_subdirectory(core)

if(ZTRIP_BUILD_TOOLS)
  add_subdirectory(tools/analyze-cli)
  add_subdirectory(tools/live-cli)
endif()

if(ZTRIP_BUILD_GODOT)
  add_subdirectory(hosts/godot)
endif()

# The Unreal plugin is built by the engine; this only stages its sources.
add_subdirectory(hosts/unreal)

include(ZtripInstall)
include(ZtripPackaging)

if(ZTRIP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
