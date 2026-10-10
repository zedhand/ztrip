# Architecture

`mre` (working name, "music-reactive engine") animates scene properties from music. All logic lives in one C++20 core library with no engine headers. The Godot 4 app and the Unreal Engine 5.6+ plugin are thin hosts that supply UI, a timeline, a property binder and rendering. A `.mgraph.json` saved in one host must load and behave identically in the other.

## Data flow

```text
Inputs -> Input adapters -> Analysis -> Musical time -> Motion Graph -> Host binder -> Scene targets
```

- **Inputs:** audio file, microphone, system audio, MIDI, OSC, DMX, gamepad, keyboard/mouse, mocap, keyframe curves, Note Sequences.
- **Input adapters:** every input implements `ISignalSource` (enumerate channels, open, poll/push, close) and runs on its own I/O thread.
- **Analysis:** STFT, bands, envelopes, onsets, beat tracking, stems.
- **Musical time:** tempo map and Note Sequences; authored timing is stored in beats.
- **Motion Graph:** typed dataflow graph evaluated once per tick in topological order.
- **Host binder:** resolves property paths to typed setters in the host engine.

## Repo layout

```text
core/            C++20 library, no engine headers
  include/mre/   public headers
  src/           shared implementation
  analysis/      STFT, bands, onsets, beat tracking, stems (onnx)
  graph/         node registry, compiler, evaluator, function nodes
  time/          tempo map, note sequences
  io/            ISignalSource adapters: audio, MIDI, OSC, DMX, gamepad
  serialize/     JSON schemas + migrations, sigcache
hosts/           (later phases)
  unreal/        Runtime + Editor modules, Sequencer track, binder
  godot/         GDExtension + editor plugin, timeline dock, binder
tools/
  analyze-cli/   offline analysis + plot dump for tuning
tests/           unit, determinism and analysis accuracy tests (Catch2)
docs/            ARCHITECTURE.md, DECISIONS.md, node reference
```

Core builds with CMake as the static library `mre_core` (alias `mre::core`). UE links it as a ThirdParty static library through `Build.cs`; Godot links it through godot-cpp.

## Building

Requires CMake 3.24+, Ninja and a C++20 compiler (GCC 13+, Clang 16+ or MSVC 2022).

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Options: `MRE_BUILD_TOOLS` and `MRE_BUILD_TESTS` (both `ON` by default). Catch2 is fetched at configure time unless an installed Catch2 3.x is found.

## Threading

- **Audio thread:** capture and live analysis per block, writing to lock-free single-producer/single-consumer rings.
- **I/O threads:** MIDI, OSC and DMX receive; every event is timestamped on the audio clock.
- **Evaluation:** host tick on the game/editor thread; an optional worker thread with double-buffered outputs for large graphs. Property writes always happen on the game thread.
- **Background jobs:** offline analysis, stem separation and baking, with progress reporting and cancel.

## Signal model

Every input becomes a typed signal. Every graph evaluation is a function of (time, signals, node state).

| Type | Shape | Examples |
| --- | --- | --- |
| `Continuous` | float per tick, nominally 0-1 | band energy, MIDI CC, fader, envelope |
| `Trigger` | timestamped events with velocity 0-1 | onset, beat, MIDI note-on, DMX threshold cross |
| `Gate` | on/off spans | held MIDI note, Note Sequence note |
| `Clock` | musical position (bar, beat, tick) + phase | tempo map |
| `Vector` / `Color` | 2-4 floats | mocap joint, XY pad, RGB from DMX |

## Evaluation modes

- **Offline (files):** analysis writes signal buffers at 120 Hz plus event lists. Stateful smoothing is baked into the buffers, so `eval(t)` never depends on prior frames. Stateful graph nodes downstream checkpoint their state every 1 s and replay from the nearest checkpoint on seek.
- **Live (mic, system audio, MIDI, OSC, DMX):** causal, frame by frame, same node code with a `live` flag. No lookahead.

**Determinism:** seeking to any time `t` must produce the same outputs (within 1e-5) as playing from 0 to `t`. A dedicated test suite enforces this.

## Persisted formats

All JSON formats are versioned and carry migrations.

| File | Contents |
| --- | --- |
| `project.json` | tempo map, audio sources and their analysis settings, stem files, graph and sequence references |
| `*.mgraph.json` | nodes (`id`, `type`, `params`, `pos`), edges (`from.node.port` -> `to.node.port`), exposed macro controls, layer settings |
| `*.nseq.json` | lanes, notes (`start_beat`, `length_beats`, `velocity`, `pitch`, `envelope_ref`), envelopes |
| `*.sigcache` | binary offline buffers, keyed by audio file hash + analysis settings hash; regenerated on change |
| `bindings.json` | target property paths per host, e.g. `/Game/Lights/Key.Intensity` (UE), `Lights/Key:light_energy` (Godot) |
