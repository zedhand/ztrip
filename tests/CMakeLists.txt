include(FetchContent)

FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.7.1
  GIT_SHALLOW    TRUE
  FIND_PACKAGE_ARGS 3
)
FetchContent_MakeAvailable(Catch2)

add_executable(ztrip_tests
  test_smoke.cpp
  test_wav.cpp
  test_stft.cpp
  test_envelope.cpp
  test_onsets.cpp
  test_kick.cpp
  test_beats.cpp
  test_pipeline.cpp
  test_reference_tracks.cpp
  test_graph_compile.cpp
  test_graph_nodes.cpp
  test_graph_determinism.cpp
  test_graph_reference.cpp
  test_mgraph.cpp
  test_tempo_map.cpp
  test_note_sequence.cpp
  test_autodraft.cpp
  test_nseq.cpp
  test_graph_timeline.cpp
)
target_link_libraries(ztrip_tests PRIVATE ztrip::core Catch2::Catch2WithMain)
target_compile_definitions(ztrip_tests PRIVATE ZTRIP_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
ztrip_set_warnings(ztrip_tests)

include(Catch)
catch_discover_tests(ztrip_tests)

if(TARGET ztrip_analyze)
  # Song names are often outside the Windows code page. The file doesn't
  # exist, so this checks that the name reaches the error message intact.
  add_test(NAME "analyze-cli keeps non-ASCII file names intact"
           COMMAND ztrip_analyze "Пёс_テスト_Motörhead.wav")
  set_tests_properties("analyze-cli keeps non-ASCII file names intact" PROPERTIES
    PASS_REGULAR_EXPRESSION "cannot open Пёс_テスト_Motörhead\\.wav")
endif()
