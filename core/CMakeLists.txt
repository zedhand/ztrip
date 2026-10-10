# mre_core: engine-agnostic C++20 library. No engine headers allowed here.
add_library(mre_core STATIC
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
add_library(mre::core ALIAS mre_core)

target_include_directories(mre_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(mre_core PUBLIC cxx_std_20)
target_compile_definitions(mre_core PRIVATE MRE_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(mre_core PRIVATE mre_pffft)
mre_set_warnings(mre_core)
