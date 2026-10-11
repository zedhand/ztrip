# Stages the Unreal Engine plugin into <build>/unreal/Ztrip: the plugin
# sources in hosts/unreal/Ztrip plus the ztrip core and its third-party code
# (pffft, nlohmann/json), which UnrealBuildTool compiles with the engine's
# own toolchain. Copy the staged folder into a project's Plugins/ folder.
#
#   cmake --build --preset release --target ztrip_unreal_plugin
#
# The core's live inputs (audio capture, MIDI, OSC, DMX) stay out: the Unreal
# player has no live input yet, and they would bring miniaudio and RtMidi.
get_target_property(_ztrip_core_sources ztrip_core SOURCES)
set(ZTRIP_UE_CORE_SOURCES ${_ztrip_core_sources})
list(REMOVE_ITEM ZTRIP_UE_CORE_SOURCES io/audio_capture.cpp io/midi.cpp io/network.cpp)

# pffft's external symbols get a ztrip_ prefix so a monolithic game that
# links another copy of pffft doesn't clash with this one.
set(ZTRIP_UE_PFFFT_SYMBOLS
  pffft_aligned_free pffft_aligned_malloc pffft_cplx_finalize pffft_cplx_preprocess pffft_destroy_setup
  pffft_is_power_of_two pffft_is_valid_size pffft_min_fft_size pffft_nearest_transform_size pffft_new_setup
  pffft_next_power_of_two pffft_simd_arch pffft_simd_size pffft_transform pffft_transform_internal
  pffft_transform_ordered pffft_zconvolve_accumulate pffft_zconvolve_no_accu pffft_zreorder
  pffftd_aligned_free pffftd_aligned_malloc pffftd_is_power_of_two pffftd_next_power_of_two
  validate_pffft_simd validate_pffft_simd_ex)

set(ZTRIP_UE_STAGE_DIR "${CMAKE_BINARY_DIR}/unreal")
configure_file(stage.cmake.in "${ZTRIP_UE_STAGE_DIR}/stage.cmake" @ONLY)
add_custom_target(ztrip_unreal_plugin
  COMMAND "${CMAKE_COMMAND}" -P "${ZTRIP_UE_STAGE_DIR}/stage.cmake"
  COMMENT "Staging the Unreal plugin in ${ZTRIP_UE_STAGE_DIR}/Ztrip"
  VERBATIM)
