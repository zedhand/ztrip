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
  PUBLIC  ${pffft_SOURCE_DIR}/include/pffft
  PRIVATE ${pffft_SOURCE_DIR}/src
)
target_compile_definitions(ztrip_pffft PUBLIC PFFFT_STATIC_DEFINE PRIVATE _USE_MATH_DEFINES)
set_target_properties(ztrip_pffft PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
