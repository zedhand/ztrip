# ztrip_core: engine-agnostic C++20 library. No engine headers allowed here.
add_library(ztrip_core STATIC
  src/version.cpp
  io/wav.cpp
  analysis/stft.cpp
  analysis/bands.cpp
  analysis/envelope.cpp
  analysis/onsets.cpp
  analysis/kick.cpp
  analysis/beats.cpp
  analysis/offline.cpp
  analysis/live.cpp
  io/audio_capture.cpp
  serialize/sigcache.cpp
  serialize/mgraph.cpp
  graph/node.cpp
  graph/registry.cpp
  graph/compiler.cpp
  graph/evaluator.cpp
  graph/nodes.cpp
  time/tempo_map.cpp
  time/note_sequence.cpp
  time/autodraft.cpp
  serialize/nseq.cpp
  serialize/tempo.cpp
)
add_library(ztrip::core ALIAS ztrip_core)
set_target_properties(ztrip_core PROPERTIES EXPORT_NAME core)

target_include_directories(ztrip_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(ztrip_core PUBLIC cxx_std_20)
target_compile_definitions(ztrip_core PRIVATE ZTRIP_VERSION_STRING="${ZTRIP_VERSION_FULL}")
# nlohmann_json is header-only and private, so installed consumers never need it.
target_link_libraries(ztrip_core PRIVATE ztrip_pffft ztrip_miniaudio $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>)
ztrip_set_warnings(ztrip_core)
