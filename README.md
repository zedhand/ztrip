# ztrip

Music-reactive animation engine (`ztrip`): an engine-agnostic C++20 core that turns audio and live inputs into animation, with Godot 4 and Unreal Engine 5.6+ hosts planned.

Status: Phase 1 (offline audio analysis) and Phase 2 (Motion Graph core: compiler, evaluator, 15 nodes, `.mgraph.json`) are in. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/NODES.md](docs/NODES.md). Beta archives for Windows and Linux are described in [docs/RELEASING.md](docs/RELEASING.md).

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

## Run a graph (C++)

```cpp
auto doc = ztrip::serialize::load_mgraph("examples/kick_to_light.mgraph.json");
auto compiled = ztrip::graph::compile(doc);          // diagnostics in compiled.diagnostics
ztrip::graph::Evaluator ev(std::move(compiled.graph));
ev.bind_source("main", &analysis);                   // an analysis::AnalysisResult
ev.evaluate(t);                                      // any time, any order: seeking is deterministic
for (const auto& o : ev.outputs()) set_property(o.target, o.value);
```
