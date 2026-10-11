# Builds the staged Unreal plugin's engine-free files the way
# UnrealBuildTool compiles the ZtripRuntime module, without the engine:
# every .c and .cpp under the module folder except the module's own Unreal
# sources, one file at a time (no unity files), no RTTI, exceptions on, with
# the module's include folders and defines, symbols hidden, and (with GCC or
# Clang, as Unreal on Linux) warnings as errors. They go into a shared
# library that must link with no missing symbols, and a small program runs a
# song through it. Run after staging:
#
#   cmake --build --preset release --target ztrip_unreal_plugin
#   cmake -S hosts/unreal/check -B build/unreal-check -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DZTRIP_UNREAL_PLUGIN=build/release/unreal/Ztrip
#   cmake --build build/unreal-check
#   build/unreal-check/ztrip_unreal_check hosts/godot/project/demo/stage.mgraph.json
cmake_minimum_required(VERSION 3.24)
project(ztrip_unreal_check LANGUAGES C CXX)

set(ZTRIP_UNREAL_PLUGIN "" CACHE PATH "The staged plugin folder, <build>/unreal/Ztrip")
get_filename_component(plugin "${ZTRIP_UNREAL_PLUGIN}" ABSOLUTE)
if(NOT EXISTS "${plugin}/Ztrip.uplugin")
  message(FATAL_ERROR "ZTRIP_UNREAL_PLUGIN (${plugin}) is not a staged plugin: build the ztrip_unreal_plugin target first")
endif()
include("${plugin}/../ztrip-runtime-module.cmake")
set(module "${plugin}/Source/ZtripRuntime")

file(GLOB_RECURSE sources "${module}/*.c" "${module}/*.cpp")
# The module's own sources include engine headers; everything else in the
# module folder must build without them.
list(FILTER sources EXCLUDE REGEX "/Source/ZtripRuntime/Private/[^/]+\\.cpp$")
list(LENGTH sources count)
message(STATUS "Building ${count} engine-free files of the staged ZtripRuntime module")

add_library(ztrip_ue_runtime SHARED ${sources} entry.cpp)
set(includes "${module}/Public")
foreach(dir IN LISTS ZTRIP_UE_INCLUDES)
  list(APPEND includes "${module}/${dir}")
endforeach()
target_include_directories(ztrip_ue_runtime PRIVATE ${includes})
target_compile_definitions(ztrip_ue_runtime PRIVATE ${ZTRIP_UE_DEFINES})
set_target_properties(ztrip_ue_runtime PROPERTIES
  CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
  C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
find_package(Threads REQUIRED)
target_link_libraries(ztrip_ue_runtime PRIVATE Threads::Threads)
if(MSVC)
  target_compile_options(ztrip_ue_runtime PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/GR- /EHsc> /W3 /utf-8)
else()
  target_compile_options(ztrip_ue_runtime PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fexceptions -Wdelete-non-virtual-dtor>
    -Wall -Werror -Wno-unknown-pragmas)
  target_link_options(ztrip_ue_runtime PRIVATE -Wl,--no-undefined)
endif()

add_executable(ztrip_unreal_check main.cpp)
target_link_libraries(ztrip_unreal_check PRIVATE ztrip_ue_runtime)
set_target_properties(ztrip_unreal_check PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
