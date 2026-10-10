# ztrip

Music-reactive animation engine (`ztrip`): an engine-agnostic C++20 core that turns audio and live inputs into animation, with Godot 4 and Unreal Engine 5.6+ hosts planned.

Status: Phase 1 (offline audio analysis) in progress. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Build

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

## Analyse a track

```sh
build/dev/tools/analyze-cli/analyze-cli song.wav -o song-analysis
python3 song-analysis/plot.py --start 30 --end 45   # needs numpy + matplotlib
```

This writes a `.sigcache`, CSVs of the 120 Hz band levels, envelopes, onsets and beat grid, and a plot script. `analyze-cli --help` lists the tuning options (FFT size, window, attack/release, onset sensitivity, tempo prior).

Accuracy tests against the reference tracks run when `ZTRIP_REFERENCE_TRACKS` points at a folder holding them; otherwise they are skipped.
