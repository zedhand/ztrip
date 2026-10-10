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
hosts/
  godot/         GDExtension (src/), addon + demo Godot project (project/)
  unreal/        (later) Runtime + Editor modules, Sequencer track, binder
tools/
  analyze-cli/   offline analysis + plot dump for tuning
examples/        sample .mgraph.json graphs
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

Options: `ZTRIP_BUILD_TOOLS` and `ZTRIP_BUILD_TESTS` (both `ON` by default), and `ZTRIP_BUILD_GODOT` (`OFF`; the `godot` preset turns it on) for the Godot host. Catch2 is fetched at configure time unless an installed Catch2 3.x is found; pffft (v1.1.0, the FFT) is always fetched and only its float FFT sources are compiled; nlohmann/json (3.11.3) is fetched unless an installed 3.11+ is found.

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
6. **Tempo and beats:** onset strength is the sum of the default bands' normalised flux from Bass upwards (equal weight, so hats don't drown out kick and snare). Each candidate tempo scores the autocorrelation at its period plus at half its period (the subdivision), weighted by a log-normal prior (120 BPM, 0.5 octave). The kick and snare hits (steps 7 and 8) then check that estimate: its related tempos (x1/2, x2/3, x3/2, x2) are tracked too, and one replaces it when its beats fit the drum hits clearly better (`drum_fit`: share of beats on a hit and share of hit velocity on a beat). Beats come from Ellis-style dynamic programming at the chosen tempo over the onset strength plus a quarter-weight curve of the drum hits, which keeps four-on-the-floor tracks from settling on off-beat hats and bass. Downbeats from the beat phase with the most low-band onsets and harmonic change and the fewest backbeat (1.5-6 kHz) onsets. Events `beat` and `downbeat`.
7. **Kicks:** a dedicated time-domain detector, separate from the band onsets. The mono signal goes through a 150 Hz 4th-order Butterworth low-pass; its RMS amplitude over a centred 10 ms window is taken every 2.5 ms. A kick is a rise in that amplitude over 20 ms of at least 0.4 times the loudest low-band amplitude within 1.5 s, peaking within 5 dB of the track's loud low end (99th percentile); of hits within 60 ms of each other the larger rise wins. Each is then timed at the steepest 5 ms rise in dB within 20 ms before to 10 ms after. Velocity is the peak amplitude relative to the loud low end. Events `kick`; the CLI's `--kick-jump` and `--kick-gate` move the 0.4 and 5 dB.
8. **Snares:** the same jump measure on 1-4 kHz (at least 0.3), with a 4-10 kHz jump of at least 0.2 at the same moment, peaks within 4 dB (1-4 kHz) and 6 dB (4-10 kHz, 150-350 Hz body) of each band's loud end, and 1-4 kHz still within 15 dB of its peak 40-60 ms later (the snare wires ring; a kick's beater click does not). Events `snare` (claps count).
9. **Extras:** `rms` (dB, normalised) and `centroid` (log-frequency 20 Hz-20 kHz mapped to 0-1).

Offline buffers are pure functions of the file and settings, so `AnalysisResult::sample(signal, t)` is a lookup and results are bit-for-bit repeatable.

### `.sigcache` format

Little-endian binary: magic `ZTRSIGC\0`, format version, 64-bit FNV-1a hashes of the decoded audio and of every analysis setting (plus a pipeline version), then signal rate, duration, BPM, beats per bar, downbeat phase, the named signal buffers (f32), the named event lists (f64 time, f32 velocity) and the beat and downbeat times. A cache is reused only when both hashes match; malformed or stale files are ignored and regenerated. See `core/include/ztrip/serialize/sigcache.hpp`.

## Motion Graph core (Phase 2)

`ztrip::graph` turns a `.mgraph.json` document into per-tick values for host properties.

1. **Document:** `serialize::load_mgraph` / `parse_mgraph` read a `GraphDoc` (nodes with typed params, `node.port` edges, macros); `write_mgraph` writes it back with a stable key order. Versioned, with a migration table for future versions; newer files are rejected.
2. **Registry:** `NodeRegistry::builtin()` holds the 15 essential nodes (see [NODES.md](NODES.md)). A node type declares typed ports, params and how many doubles of state it keeps.
3. **Compile:** `graph::compile` validates ids, types, params, edges and port types, rejects cycles, topo-sorts (ties keep document order), lays out one value array for all outputs and one state array for all nodes, and applies macro defaults. All problems come back as diagnostics, not exceptions.
4. **Evaluate:** `graph::Evaluator` steps the compiled graph on a fixed 120 Hz tick grid, whatever the host frame rate: `evaluate(t)` runs ticks up to `floor(t * 120)`. Nodes are const and may carry information between ticks only in their slice of the state array. Source nodes read analysis through named slots (`bind_source`), so repointing a song or stem is a rebind.
5. **Seeking:** while playing, the state array is checkpointed every second (state before tick `k`). A seek restores the latest checkpoint at or before the target and replays, so seek and play run the same ticks and agree exactly; the tests require 1e-5. Changing a macro, a binding or the graph clears checkpoints: playing continues from the current state, while a seek, or re-evaluating the current time, replays with the new settings.
6. **Hot swap:** `swap_graph` keeps the state and port values of nodes whose id and type persist, and macro values whose name persists.
7. **Live mode:** the same nodes with `live` set, no checkpoints; a backwards jump resets state instead of replaying.

`Evaluator::outputs()` lists each `PropertyOut` target and value; `port_value(node, port)` reads any port for previews.

## Godot host (Phase 3)

`hosts/godot/` is a GDExtension built with godot-cpp (`godot-4.5-stable` bindings, so it loads in Godot 4.5 and newer). It links `ztrip_core` statically into one shared library, written to `hosts/godot/project/addons/ztrip/bin/`. See [hosts/godot/README.md](../hosts/godot/README.md) for usage.

- **`ZtripPlayer` (Node):** reads the WAV behind an AudioStreamPlayer (or a file), analyses it on a worker thread (cached as `user://ztrip_cache/<audio hash>.sigcache`), loads and compiles the `.mgraph.json`, and binds the analysis to the graph's `main` slot. Each frame (in the editor too, through internal processing) it takes the song time from the stream player's playback position (plus the time since the last mix, minus output latency; exact in Movie Maker mode), evaluates the graph there and hands `outputs()` to the binder. Without a playing stream, time advances by frame delta or follows `time`/`seek()`. Macros are dynamic `macros/<name>` properties. The graph file is polled for changes and hot-swapped with `Evaluator::swap_graph`, so state and macro values persist.
- **Property binder:** a target `<node path>:<property path>` resolves once to an object id and an indexed property path (`Object.set_indexed`, so `material_override:shader_parameter/glow` and `position:x` work), plus `@globals:<name>` for global shader parameters. Floats, ints and bools are written; the value before the first write is kept and restored when the player is turned off, leaves the tree, reloads its graph, or the editor saves the scene (`NOTIFICATION_EDITOR_PRE_SAVE`).
- **Editor:** an EditorPlugin (GDScript) adds a Motion Graph bottom panel: GraphEdit over the document, with the node catalog, port types and diagnostics from `ZtripGraphTools` and live port values read from the `ZtripPlayer` that plays the file. Saving goes through the core's `write_mgraph`.
- **Tests:** `project/tests/test_host.gd` runs headless in CI on Linux and Windows against Godot 4.5.1 and 4.7.2.

## Musical time (Phase 4, core)

`ztrip::time` holds authored timing in beats and converts to seconds only when evaluating.

1. **Tempo map:** `TempoMap` has tempo segments (`start_beat`, `bpm_start`, `bpm_end`, curve `step` / `linear` / `exponential`), time-signature changes at bar boundaries, a song offset (time of beat 0, the first downbeat) and a separate nudge for alignment by ear, plus `scale_tempo` for a grid that runs slightly fast or slow. `seconds_at` and `beat_at` use each curve's closed-form integral and its inverse; `position_at` gives bar, beat and phases under the meter in force.
2. **Timeline sync:** `tempo_map_from_beats` turns the analysed beat grid into a tempo map that passes through the detected beats, with steady runs merged into single segments.
3. **Note Sequences:** a `NoteSequence` has named `Lane`s of `Note`s (`start_beat`, `length_beats`, `velocity`, optional `pitch`, `envelope_ref`) and `Envelope`s (ADSR in beats, or a curve drawn over the note). Each lane has swing (0 straight, 1/3 triplet) on a swing division. `parse_grid` / `snap` cover 1/1 to 1/64 grids and triplets.
4. **Lookup:** `LaneTimeline` resolves a lane through a tempo map (swing applied) into notes in seconds and answers gate, note starts in a tick and envelope level at any time by binary search, so evaluation stays a pure function of time.
5. **Auto-draft:** `auto_draft` maps event lists to lanes (default: `kick` events, else `onset.Sub`, to `Kick`), drops ghost hits, quantizes to the grid when the grid point is close and keeps the strongest hit per grid step.
6. **Graph:** the `NoteLane` node outputs a lane's gate, triggers, envelope and velocity. `Evaluator::bind_tempo` / `bind_sequence` bind tempo maps and sequences by slot, and `BeatClock` follows a bound tempo map.
7. **Files:** `.nseq.json` (`serialize::load_nseq` / `save_nseq`) and `.tempo.json` (`serialize::load_tempo_map` / `save_tempo_map`), versioned with migration tables like `.mgraph.json`.

`analyze-cli` writes `<name>.tempo.json` and `<name>.nseq.json` next to its CSVs; `--clicks` adds `kick_clicks.wav`, the track with a click on every drafted kick. On reference track 01 the drafted kick lane matches 99% of the true kicks within 20 ms (mean error under 1 ms), with 99% precision.

## Live analysis (Phase 5)

`ztrip::analysis::LiveAnalyzer` runs the analysis causally on audio pushed in blocks of any size, reusing the offline building blocks (`FrameTransform` for one STFT frame, `frame_energy_db`, `BandFlux`, `frame_centroid_hz`, `EnvelopeFollower`).

1. **Frames:** every hop of new samples (default FFT 1024, hop 256) transforms the last FFT-size samples. Nothing waits for future audio.
2. **Levels:** band and RMS energy in dB, normalised to 0-1 between a running floor and peak (each jumps to a new extreme and drifts back towards the current level with a 10 s time constant; the floor stays 12-60 dB under the peak, and the peak never drops below -60 dB, so silence reads 0). Interpolated onto the 120 Hz grid as `band.<Name>`, then the envelope follower gives `env.<Name>`; `rms` and `centroid` as offline.
3. **Onsets:** the same band flux. A frame is an onset one hop later, when the next frame shows it was a peak, if it clears delta + threshold times the median of the trailing 0.4 s (relative to a running peak of the flux) and is past the minimum gap. Events `onset.<Name>` and `onset.full`.
4. **Result:** an `AnalysisResult` with the offline names, growing as audio arrives, so a graph moves from a file to live input by binding the live result to the slot and setting `EvaluatorSettings::live`. Values and events are stamped on the stream's sample clock when they become known, and `time()` is the latest analysed frame, so a host evaluates at `time()` and never misses an event. `kick`, `snare`, `beat` and `downbeat` exist but are empty for now (BeatClock falls back to its `bpm` param).
5. **Threads:** the analyser is not thread safe. A capture thread hands samples over through `io::SpscRing` (lock free, drops and counts what doesn't fit), and the evaluating thread drains it into `push`, with `push_silence` for dropped samples so the clock stays on the device's.

On reference track 01, live `onset.Sub` (velocity 0.3 or more) finds 99% of the kicks with 100% precision, 12 ms late on average and 15 ms at worst; `onset.Air` finds every hat within 18 ms.

## Persisted formats

All JSON formats are versioned and carry migrations.

| File | Contents |
| --- | --- |
| `project.json` | tempo map, audio sources and their analysis settings, stem files, graph and sequence references |
| `*.mgraph.json` | nodes (`id`, `type`, `params`, `pos`), edges (`from.node.port` -> `to.node.port`), exposed macro controls, layer settings |
| `*.tempo.json` | tempo segments, time signatures, offset and nudge (to be embedded in `project.json`) |
| `*.nseq.json` | lanes, notes (`start_beat`, `length_beats`, `velocity`, `pitch`, `envelope_ref`), envelopes |
| `*.sigcache` | binary offline buffers, keyed by audio file hash + analysis settings hash; regenerated on change |
| `bindings.json` | target property paths per host, e.g. `/Game/Lights/Key.Intensity` (UE), `Lights/Key:light_energy` (Godot) |
