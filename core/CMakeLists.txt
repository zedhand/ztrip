# ztrip_core: engine-agnostic C++20 library. No engine headers allowed here.
add_library(ztrip_core STATIC
  src/version.cpp
  io/wav.cpp
  analysis/stft.cpp
  analysis/bands.cpp
  analysis/envelope.cpp
  analysis/onsets.cpp
  analysis/beats.cpp
  analysis/offline.cpp
  serialize/sigcache.cpp
)
add_library(ztrip::core ALIAS ztrip_core)

target_include_directories(ztrip_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(ztrip_core PUBLIC cxx_std_20)
target_compile_definitions(ztrip_core PRIVATE ZTRIP_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(ztrip_core PRIVATE ztrip_pffft)
ztrip_set_warnings(ztrip_core)
