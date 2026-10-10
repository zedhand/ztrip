# Node reference

The Phase 2 catalog (the 15 essential nodes) plus Phase 4's `NoteLane`. Each Continuous input also has a
number param of the same name, used when the input is unconnected (and
promotable to a macro). Defaults are in brackets.

Port types: **C** Continuous (float per tick), **T** Trigger (fires on the
tick an event lands in, value = velocity), **K** Clock (beat position; beat 0
is the first downbeat). T and K outputs may feed C inputs (as velocity and
beat position); C may feed K; T inputs only take T.

A tick `n` is at `t = n / 120` s and reports events in `((n-1)/120, n/120]`.

## Sources

| Node | Inputs | Outputs | Params |
| --- | --- | --- | --- |
| `AudioBand` | | `out` C | `source` ["main"], `band` ["Bass"], `smoothed` [true] reads `env.<band>`, false reads `band.<band>`; `signal` [""] overrides with any signal name (e.g. `rms`, `centroid`) |
| `AudioOnset` | | `trigger` T | `source` ["main"], `band` ["full"] reads `onset.<band>`, `min_velocity` [0] |
| `BeatClock` | | `clock` K, `beat` T, `bar` T, `beat_phase` C, `bar_phase` C | `source` ["main"]; with no beat grid bound: `bpm` [120], `beats_per_bar` [4] |
| `NoteLane` | | `gate` C, `trigger` T, `envelope` C, `velocity` C | `source` ["main"] tempo slot, `sequence` ["main"] Note Sequence slot, `lane` ["Kick"] |
| `Constant` | | `out` C | `value` [0] |

`source` is a slot name. `Evaluator::bind_source("main", &analysis)` points
every node reading that slot at an analysis result, so swapping a song (or a
stem) only rebinds the slot. Unbound sources output 0.

A bound tempo map (`Evaluator::bind_tempo("main", &tempo)`) takes priority
over the analysed beat grid for `BeatClock` and `NoteLane`: beats and bars then
follow the map, including its offset, nudge and meter changes.

`NoteLane` reads one lane of the Note Sequence bound with
`Evaluator::bind_sequence`. `gate` is 1 while any note is held, `trigger` fires
on note starts with the note velocity, `envelope` is the notes' envelope
(ADSR or drawn curve, release tails included; velocity while held when a note
has none) and `velocity` is the held note's velocity. Notes are resolved to
seconds when bound, so rebind after editing the sequence or tempo map. With
no tempo map bound it follows the analysed beats of its `source` slot, else a
steady 120 BPM; with no sequence or lane it outputs 0. Pair `trigger` and
`gate` with `ADSR` for a held-note envelope in seconds.

`BeatClock` interpolates between detected beats and extrapolates outside them
at the first and last interval; `beat` and `bar` fire only on detected beats
and downbeats.

## Shaping

| Node | Inputs | Outputs | Params |
| --- | --- | --- | --- |
| `Smooth` | `in` C | `out` C | `attack_ms` [10], `release_ms` [200]; the spec's one-pole follower |
| `Threshold` | `in` C | `gate` C (0/1), `rise` T, `fall` T | `threshold` [0.5], `hysteresis` [0.05]; opens at `threshold + h/2`, closes below `threshold - h/2`; `rise` velocity is the input |
| `RemapRange` | `in` C | `out` C | `in_min` [0], `in_max` [1], `out_min` [0], `out_max` [1], `clamp` [true] |
| `ResponseCurve` | `in` C | `out` C | `curve` ["linear"]: linear, ease_in, ease_out, ease_in_out, smoothstep, exponential, logarithmic, custom; `amount` [2] (exponent or steepness); `points` [0,0, 1,1] flat x,y pairs for custom, x increasing. Input clamped to 0-1 |
| `Clamp` | `in` C | `out` C | `min` [0], `max` [1] |

## Triggers

| Node | Inputs | Outputs | Params |
| --- | --- | --- | --- |
| `ADSR` | `trigger` T, `gate` C | `out` C | `attack_ms` [5], `decay_ms` [150], `sustain` [0.6] (fraction of peak), `release_ms` [300], `hold_ms` [0] sustain time when `gate` is unconnected, `velocity` [true] scales the peak. With `gate` connected, sustains while gate >= 0.5 and releases when it drops. Retriggers attack from the current level |
| `ImpulseDecay` | `trigger` T | `out` C | `decay_ms` [250] time constant, `velocity` [true]; jumps to max(current, velocity) |

## Time

| Node | Inputs | Outputs | Params |
| --- | --- | --- | --- |
| `BeatLFO` | `clock` K | `out` C, 0-1 | `shape` ["sine"]: sine (peaks on the beat), triangle (peaks on the beat), saw (falls from 1), ramp (rises from 0), square, random (one value per cycle); `beats` [1] per cycle, `offset` [0] in cycles, `width` [0.5] square duty, `seed` [0 = derived from the node id] |

## Math

| Node | Inputs | Outputs | Params |
| --- | --- | --- | --- |
| `Add` | `a` C [0], `b` C [0] | `out` C | |
| `Multiply` | `a` C [1], `b` C [1] | `out` C | |

## Targets

| Node | Inputs | Outputs | Params |
| --- | --- | --- | --- |
| `PropertyOut` | `in` C | | `target` [""], a host property path such as `Lights/Key:light_energy`. Read with `Evaluator::outputs()` |

## Example

[`examples/kick_to_light.mgraph.json`](../examples/kick_to_light.mgraph.json):
Sub-band onsets retrigger an `ImpulseDecay`, scaled by an `Intensity` macro,
into a light's energy. [`examples/kitchen_sink.mgraph.json`](../examples/kitchen_sink.mgraph.json)
uses every node and backs the determinism tests.
