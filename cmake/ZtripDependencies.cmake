# Third-party dependencies for ztrip_core, fetched at configure time.
include(FetchContent)

# pffft: SIMD FFT used by the STFT. Only the float FFT sources are built;
# SOURCE_SUBDIR points at a folder without a CMakeLists.txt so pffft's own
# build (tests, benchmarks, extra DSP libs) is skipped.
FetchContent_Declare(pffft
  GIT_REPOSITORY https://github.com/marton78/pffft.git
  GIT_TAG        v1.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  ztrip-no-cmake
)
FetchContent_MakeAvailable(pffft)

add_library(ztrip_pffft STATIC
  ${pffft_SOURCE_DIR}/src/pffft.c
  ${pffft_SOURCE_DIR}/src/pffft_common.c
)
target_include_directories(ztrip_pffft
  PUBLIC  $<BUILD_INTERFACE:${pffft_SOURCE_DIR}/include/pffft>
  PRIVATE ${pffft_SOURCE_DIR}/src
)
target_compile_definitions(ztrip_pffft PUBLIC PFFFT_STATIC_DEFINE PRIVATE _USE_MATH_DEFINES)
set_target_properties(ztrip_pffft PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME pffft)

# nlohmann/json: .mgraph.json (and later project/sequence) load and save.
# Header-only and used privately by ztrip_core; the release tarball skips the
# repository's large test data.
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  FIND_PACKAGE_ARGS 3.11
)
FetchContent_MakeAvailable(nlohmann_json)

# miniaudio: live capture from microphones and loopback sources (Phase 5).
# Only its device I/O is compiled; backends (WASAPI, PulseAudio, ALSA, ...)
# are loaded at run time, so building needs no audio headers or libraries.
FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG        0.11.22
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  ztrip-no-cmake
)
FetchContent_MakeAvailable(miniaudio)

add_library(ztrip_miniaudio STATIC ${miniaudio_SOURCE_DIR}/miniaudio.c)
target_include_directories(ztrip_miniaudio PUBLIC $<BUILD_INTERFACE:${miniaudio_SOURCE_DIR}>)
target_compile_definitions(ztrip_miniaudio PUBLIC
  MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENGINE)
find_package(Threads REQUIRED)
target_link_libraries(ztrip_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
  target_link_libraries(ztrip_miniaudio PUBLIC m)
endif()
set_target_properties(ztrip_miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME miniaudio)

# RtMidi: MIDI input (Phase 5), with ALSA on Linux and Windows MM on Windows.
# Without the ALSA headers (libasound2-dev) it builds with no MIDI backend,
# and MIDI input reports that no ports exist.
FetchContent_Declare(rtmidi
  GIT_REPOSITORY https://github.com/thestk/rtmidi.git
  GIT_TAG        6.0.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  ztrip-no-cmake
)
FetchContent_MakeAvailable(rtmidi)

add_library(ztrip_rtmidi STATIC ${rtmidi_SOURCE_DIR}/RtMidi.cpp)
target_include_directories(ztrip_rtmidi PUBLIC $<BUILD_INTERFACE:${rtmidi_SOURCE_DIR}>)
if(WIN32)
  target_compile_definitions(ztrip_rtmidi PRIVATE __WINDOWS_MM__)
  target_link_libraries(ztrip_rtmidi PUBLIC winmm)
else()
  find_package(ALSA)
  if(ALSA_FOUND)
    target_compile_definitions(ztrip_rtmidi PRIVATE __LINUX_ALSA__)
    target_link_libraries(ztrip_rtmidi PUBLIC ALSA::ALSA Threads::Threads)
  else()
    message(WARNING "ALSA headers not found (install libasound2-dev): building without MIDI input")
    target_compile_definitions(ztrip_rtmidi PRIVATE __RTMIDI_DUMMY__)
  endif()
endif()
set_target_properties(ztrip_rtmidi PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME rtmidi)
