# Third-party dependencies for mre_core, fetched at configure time.
include(FetchContent)

# pffft: SIMD FFT used by the STFT. Only the float FFT sources are built;
# SOURCE_SUBDIR points at a folder without a CMakeLists.txt so pffft's own
# build (tests, benchmarks, extra DSP libs) is skipped.
FetchContent_Declare(pffft
  GIT_REPOSITORY https://github.com/marton78/pffft.git
  GIT_TAG        v1.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  mre-no-cmake
)
FetchContent_MakeAvailable(pffft)

add_library(mre_pffft STATIC
  ${pffft_SOURCE_DIR}/src/pffft.c
  ${pffft_SOURCE_DIR}/src/pffft_common.c
)
target_include_directories(mre_pffft
  PUBLIC  ${pffft_SOURCE_DIR}/include/pffft
  PRIVATE ${pffft_SOURCE_DIR}/src
)
target_compile_definitions(mre_pffft PUBLIC PFFFT_STATIC_DEFINE PRIVATE _USE_MATH_DEFINES)
set_target_properties(mre_pffft PROPERTIES POSITION_INDEPENDENT_CODE ON)
