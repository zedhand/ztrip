# Architecture

`ztrip`, a music-reactive animation engine, animates scene properties from music. All logic lives in one C++20 core library with no engine headers. The Godot 4 app and the Unreal Engine 5.6+ plugin are thin hosts that supply UI, a timeline, a property binder and rendering. A `.mgraph.json` saved in one host must load and behave identically in the other.

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
  include/ztrip/   public headers
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

Core builds with CMake as the static library `ztrip_core` (alias `ztrip::core`). UE links it as a ThirdParty static library through `Build.cs`; Godot links it through godot-cpp.

## Building

Requires CMake 3.24+, Ninja and a C++20 compiler (GCC 13+, Clang 16+ or MSVC 2022).

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Options: `ZTRIP_BUILD_TOOLS` and `ZTRIP_BUILD_TESTS` (both `ON` by default). Catch2 is fetched at configure time unless an installed Catch2 3.x is found; pffft (v1.1.0, the FFT) is always fetched and only its float FFT sources are compiled.

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

## Offline analysis (Phase 1)

`ztrip::analysis::analyze()` turns mono (or one selected channel of) PCM into an `AnalysisResult`:

1. **Decode:** `ztrip::io::read_wav` (8/16/24/32-bit PCM, 32/64-bit float, `WAVE_FORMAT_EXTENSIBLE`).
2. **STFT:** pffft real FFT, size 512-8192 (default 2048), hop size/4, Hann/Hamming/Blackman-Harris. Frames are centred (frame `i` at `i * hop / sr`) and magnitudes are window-normalised.
3. **Bands:** default Sub, Bass, LowMid, Mid, HighMid, Air. Per-frame energy in dB, normalised to 0-1 between the 5th and 99th whole-file percentiles (with the low anchor no more than 60 dB below the high one), resampled to 120 Hz as `band.<Name>`.
4. **Envelopes:** the spec's attack/release follower (plus optional hold) over each 120 Hz band level, as `env.<Name>`.
5. **Onsets:** per-band SuperFlux-style spectral flux (log magnitude increase against the previous frame max-filtered over +-1 bin, averaged over the band), median-adaptive threshold, minimum gap, parabolic peak refinement. Events `onset.<Name>` and `onset.full`, velocity = normalised flux.
6. **Tempo and beats:** onset strength is the sum of the six default bands' normalised flux (equal weight, so hats don't drown out kick and snare). Tempo comes from its autocorrelation weighted by a log-normal prior (120 BPM, 0.5 octave); beats from Ellis-style dynamic programming at that tempo; downbeats from the beat phase with the most low-band onsets and harmonic change and the fewest backbeat (1.5-6 kHz) onsets. Events `beat` and `downbeat`.
7. **Extras:** `rms` (dB, normalised) and `centroid` (log-frequency 20 Hz-20 kHz mapped to 0-1).

Offline buffers are pure functions of the file and settings, so `AnalysisResult::sample(signal, t)` is a lookup and results are bit-for-bit repeatable.

### `.sigcache` format

Little-endian binary: magic `ZTRSIGC\0`, format version, 64-bit FNV-1a hashes of the decoded audio and of every analysis setting (plus a pipeline version), then signal rate, duration, BPM, beats per bar, downbeat phase, the named signal buffers (f32), the named event lists (f64 time, f32 velocity) and the beat and downbeat times. A cache is reused only when both hashes match; malformed or stale files are ignored and regenerated. See `core/include/ztrip/serialize/sigcache.hpp`.

## Persisted formats

All JSON formats are versioned and carry migrations.

| File | Contents |
| --- | --- |
| `project.json` | tempo map, audio sources and their analysis settings, stem files, graph and sequence references |
| `*.mgraph.json` | nodes (`id`, `type`, `params`, `pos`), edges (`from.node.port` -> `to.node.port`), exposed macro controls, layer settings |
| `*.nseq.json` | lanes, notes (`start_beat`, `length_beats`, `velocity`, `pitch`, `envelope_ref`), envelopes |
| `*.sigcache` | binary offline buffers, keyed by audio file hash + analysis settings hash; regenerated on change |
| `bindings.json` | target property paths per host, e.g. `/Game/Lights/Key.Intensity` (UE), `Lights/Key:light_energy` (Godot) |
