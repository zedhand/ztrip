# Decisions

Choices made where the spec left room, newest last.

1. **Test framework: Catch2 v3**, fetched with CMake `FetchContent` (or an installed Catch2 3.x if found). Simpler single-dependency setup than GoogleTest.
2. **Minimum CMake 3.24**, for `FetchContent` `FIND_PACKAGE_ARGS` and presets v6.
3. **Core target layout:** one static library `ztrip_core` with public headers under `core/include/ztrip/`. Subsystem folders (`analysis/`, `graph/`, ...) add sources to that one target as they land rather than becoming separate libraries.
4. **CI:** GitHub Actions builds and tests on Ubuntu 24.04 (GCC) and Windows 2022 (MSVC), matching the v1 platforms.
5. **License:** private for now; no license file is added.
6. **Name: `ztrip`**, matching the repository, replacing the spec's working name `mre` in namespaces, targets, CMake options and header paths.
7. **WAV only for file decode in Phase 1**, with an in-house parser (`ztrip::io::read_wav`). It has no dependencies and covers the reference tracks; libsndfile or miniaudio comes in with live capture (Phase 5) and wider formats.
8. **pffft from its maintained fork** (marton78/pffft, tag v1.1.0), compiling only `pffft.c` and `pffft_common.c` into a private static library.
9. **Onset flux is SuperFlux-style** (previous frame max-filtered over +-1 bin) and averaged per bin. Plain per-bin flux let bass-note leakage split kicks into early/late pairs in the Sub band at FFT 2048; the max filter raised Sub-band kick precision on reference track 1 from 0.56 to 0.85 at the same recall.
10. **Beat tracking uses equal-weight band flux**, not full-spectrum flux, and a tempo prior of 120 BPM with a 0.5 octave spread. Full-spectrum flux locked onto off-beat open hats on track 1; a 1 octave prior picked the half-tempo reading (65 BPM) for track 3. Users can move the prior with `--bpm-prior`.
11. **Reference audio stays out of the repo.** Accuracy tests read it from `ZTRIP_REFERENCE_TRACKS` and skip when it is unset, since tracks 02 and 03 are CC BY and would need attribution wherever the repo is distributed. Unit tests use deterministic synthetic drums instead.
12. **Downbeats are a heuristic** (best of `beats_per_bar` phases, fixed for the whole track). Meter changes and pickup bars are not handled; the Phase 4 tempo map with manual nudge covers those.
13. **Tempo scoring counts the subdivision, and beat tracking skips the Sub band.** Each tempo scores its autocorrelation plus the autocorrelation at twice its rate (the eighth-note pulse). Without that, plain autocorrelation read track 02 at 118 BPM, a 4:3 relation to the 89 BPM that librosa found and that holds up when clapping along. Sub (20-60 Hz) is one or two bins at FFT 2048 and sees kicks late, which made the beat phase wander on track 02. With both changes, tracks 01-03 read 128, 88.5 and 130 BPM. Track 01's beats and downbeats still match the truth exactly, and on tracks 02 and 03 the beats land on onsets about as strong as librosa's beats do.
