# ztrip

ztrip animates things from music. Point it at a song and it works out the beat, the bars, and when the kick, snare, bass and cymbals hit. A small node graph then turns those signals into motion: lights that flash on the kick, a camera that pushes in on the bass, materials that pulse with the hi-hats. No per-beat keyframing, and scrubbing back and forth through the song always gives the same result.

Under the hood it is an engine-agnostic C++20 core with a Godot 4 addon and an Unreal Engine 5.6+ host planned.

**Beta testers:** start with [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md).

## Status

| Part | State |
| --- | --- |
| Audio analysis: bands, envelopes, onsets, tempo, beat grid, `analyze-cli` | Done |
| Motion Graph core: compiler, evaluator, 15 nodes, `.mgraph.json` | Done |
| Godot 4 addon: song playback, property binder, Motion Graph panel, demo stage | Done (beta) |
| Musical time core: tempo map, Note Sequences, auto-drafted kick lane, `NoteLane` node | Done |
| Musical time: Timeline panel in Godot (tempo map, note lanes, draw, nudge, save) | Done (beta) |
| Live audio input: microphone, loopback, `live-cli`, Godot `live_input` | Done (beta stretch) |
| Live control input: MIDI, OSC, DMX (Art-Net, sACN), learn, controller profiles | Done (beta stretch) |
| Unreal Engine plugin | After beta |

Platforms: Windows 11 and Linux.

## Build

Requires CMake 3.24+, Ninja and a C++20 compiler (GCC 13+, Clang 16+ or MSVC 2022). Dependencies are fetched at configure time. Step-by-step instructions for each platform are in [GETTING_STARTED.md](docs/GETTING_STARTED.md#build-from-source).

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Use the `dev` preset for a Debug build in `build/dev`.

## Try it in Godot

```sh
cmake --preset godot
cmake --build --preset godot
```

Then open `hosts/godot/project` in Godot 4.5 or newer, drop a WAV onto the demo stage's `Music` node and scrub the `ZtripPlayer`'s **Time**: lights, the orb's material and the camera follow the song in the editor. The **Timeline** panel at the bottom shows the drafted tempo map and Kick, Snare and Hats lanes that fire the lights; edit a note and the light follows. [hosts/godot/README.md](hosts/godot/README.md) covers the addon, target paths and the graph panel.

## Analyse a track

```sh
build/release/tools/analyze-cli/analyze-cli song.wav -o song-analysis
python3 song-analysis/plot.py --start 30 --end 45   # needs numpy + matplotlib
```

This writes a `.sigcache`, CSVs of the 120 Hz band levels, envelopes, onsets and beat grid, a plot script, a tempo map (`.tempo.json`) and a Note Sequence with an auto-drafted Kick lane (`.nseq.json`). Add `--clicks` to also get `kick_clicks.wav`, the track with a click on each drafted kick. `analyze-cli --help` lists the tuning options (FFT size, window, attack/release, onset sensitivity, tempo prior). [GETTING_STARTED.md](docs/GETTING_STARTED.md#analyse-a-song) explains how to read the output.

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

Note Sequences reach a graph through `NoteLane` nodes:

```cpp
auto tempo = ztrip::time::tempo_map_from_beats(analysis.beats);
tempo.nudge_by(0.010);                               // 10 ms later, by ear
auto seq = ztrip::time::auto_draft(analysis, tempo); // or serialize::load_nseq("song.nseq.json")
ev.bind_tempo("main", &tempo);
ev.bind_sequence("main", &seq);
```

## Documentation

- [GETTING_STARTED.md](docs/GETTING_STARTED.md): build, analyse a song, report problems
- [ARCHITECTURE.md](docs/ARCHITECTURE.md): data flow, analysis pipeline, graph evaluation, file formats
- [NODES.md](docs/NODES.md): Motion Graph node reference
- [hosts/godot/README.md](hosts/godot/README.md): the Godot addon
- [DECISIONS.md](docs/DECISIONS.md): design decisions and why
- [RELEASING.md](docs/RELEASING.md): versioning, beta downloads and cutting a release

## Feedback

Bugs and questions go to [GitHub issues](https://github.com/zedhand/ztrip/issues).
