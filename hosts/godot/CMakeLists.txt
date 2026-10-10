# ztrip Godot 4 host: a GDExtension built against godot-cpp, written into the
# addon folder of the bundled Godot project so the editor picks it up.
include(FetchContent)

# ztrip_core reports load errors with exceptions, so keep them on.
set(GODOTCPP_DISABLE_EXCEPTIONS OFF CACHE BOOL "" FORCE)
set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "" FORCE)

# godot-cpp 4.5 bindings: the extension loads in Godot 4.5 and newer.
FetchContent_Declare(godot-cpp
  GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
  GIT_TAG        godot-4.5-stable
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(godot-cpp)

add_library(ztrip_godot SHARED
  src/register_types.cpp
  src/convert.cpp
  src/ztrip_analysis.cpp
  src/ztrip_graph_tools.cpp
  src/property_binder.cpp
  src/ztrip_player.cpp
  src/ztrip_timeline.cpp
)
target_link_libraries(ztrip_godot PRIVATE ztrip::core godot::cpp)
target_compile_features(ztrip_godot PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(ztrip_godot PRIVATE /W4 /permissive- /utf-8 /EHsc)
else()
  target_compile_options(ztrip_godot PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()

get_target_property(ZTRIP_GODOT_SUFFIX godot-cpp GODOTCPP_SUFFIX)
set(ZTRIP_GODOT_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/project/addons/ztrip/bin")

set_target_properties(ztrip_godot PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  BUILD_RPATH_USE_ORIGIN ON
  PREFIX "lib"
  OUTPUT_NAME "ztrip${ZTRIP_GODOT_SUFFIX}"
  LIBRARY_OUTPUT_DIRECTORY "$<1:${ZTRIP_GODOT_BIN_DIR}>"
  RUNTIME_OUTPUT_DIRECTORY "$<1:${ZTRIP_GODOT_BIN_DIR}>"
  PDB_OUTPUT_DIRECTORY     "$<1:${ZTRIP_GODOT_BIN_DIR}>"
)
