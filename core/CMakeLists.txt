# ztrip_core: engine-agnostic C++20 library. No engine headers allowed here.
add_library(ztrip_core STATIC
  src/version.cpp
)
add_library(ztrip::core ALIAS ztrip_core)

target_include_directories(ztrip_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(ztrip_core PUBLIC cxx_std_20)
target_compile_definitions(ztrip_core PRIVATE ZTRIP_VERSION_STRING="${PROJECT_VERSION}")
ztrip_set_warnings(ztrip_core)
