# ztrip for Godot 4

A GDExtension addon that plays a song through a Motion Graph and drives scene
properties from it, in the editor and in the running game. `project/` is a
Godot project holding the addon (`project/addons/ztrip/`) and a demo stage.

Requires Godot 4.5 or newer (tested on 4.5.1 and 4.7.2), Windows or Linux,
x86_64.

## Build

From the repository root:

```sh
cmake --preset godot
cmake --build --preset godot
```

The first configure fetches godot-cpp and the first build takes several
minutes (it compiles Godot's C++ bindings). The library lands in
`hosts/godot/project/addons/ztrip/bin/`. On Windows, run these in the
*x64 Native Tools Command Prompt for VS 2022*, as for the rest of ztrip.

CI also uploads a ready-built copy of `project/` for each platform as the
`ztrip-godot-Linux` and `ztrip-godot-Windows` artifacts of every run.

## Try the demo

1. Open `hosts/godot/project` in Godot (Project Manager > Import).
2. Put a song in `project/music/` as a WAV (16-bit or 24-bit PCM; see
   [Known gaps](#known-gaps) for the WAV formats Godot accepts) and drag it
   onto the `Music` node's **Stream** in `demo/stage.tscn`.
3. Select the `ZtripPlayer` node. Its **Status** in the inspector says when
   the song is analysed (a second or two; it's cached afterwards).
4. Drag **Time** to scrub through the song, or tick **Playing** to play it,
   all without pressing Play. Press F5 to run the stage with its HUD.

In the stage, the red light flashes on kick drums, the blue spotlight on
snares, the white light on hi-hats, the orb glows with the bass and pulses on
every beat, the camera pushes in on every bar and sways over eight beats, and
the ambient light follows the overall loudness. The kick, snare and hat
lights play the **Kick**, **Snare** and **Hats** lanes of the song's timeline,
which the player drafts from the analysis. The **Motion Graph** panel at the
bottom of the editor shows the graph, with live values on every output, and
the **Timeline** panel shows the tempo map and lanes for editing.

## Use it in your own project

Copy `project/addons/ztrip/` (with its `bin/` folder built) into your
project, enable *ztrip* under Project Settings > Plugins, then:

1. Add an `AudioStreamPlayer` with your song.
2. Add a `ZtripPlayer` node. Set **Audio Player** to the stream player and
   **Graph File** to a `.mgraph.json` (start from `demo/stage.mgraph.json` or
   one of `examples/` at the repo root).
3. Edit the graph's `PropertyOut` targets to point at your nodes (below).

### ZtripPlayer

| Property | What it does |
| --- | --- |
| `audio_player` | An AudioStreamPlayer (2D/3D work too). Its stream's WAV is analysed, and its playback position is the song time. |
| `audio_file` | A WAV to analyse instead of the stream's file. |
| `graph_file` | The `.mgraph.json` to run. Saving it (from the panel or any editor) reloads it within half a second, keeping node state and macro values. |
| `target_root` | Where target paths start. Default `..`, the player's parent. |
| `tempo_file`, `sequence_file` | The song's `.tempo.json` and `.nseq.json`. Loaded when they exist; otherwise the timeline is drafted from the analysis (and drafted again when the song changes). The Timeline panel's **Save** fills them in. |
| `active` | Off puts every driven property back to the value it had before. |
| `playing`, `time` | Play/stop, and the song position in seconds (drag it to scrub). |
| `autoplay` | Play from the start when the game runs. |
| `live_input` | Drive the graph from live audio instead of the song (see below). |
| `live_device`, `live_loopback`, `live_gain_db` | Which input: empty for the default microphone, or a device name (or part of one); `live_loopback` records what the computer plays instead. |
| `live_simulate_file` | A WAV played into the live input in real time, looping, to try a live setup without a microphone. |
| `macros/*` | One slider per macro the graph exposes. Saved with the scene. |
| `status` | What was loaded, how many targets are bound, and anything wrong. |

Methods: `play(from)`, `stop()`, `seek(t)`, `reload()`, `get_output(target)`,
`get_outputs()`, `get_port_value(node, port)` (any port, for HUDs and
previews), `set_macro(name, value)`, `get_analysis()`, `get_timeline()`,
`draft_timeline(grid)`, `save_timeline()`. Signals: `analysis_loaded`,
`graph_loaded`, `timeline_loaded`, `finished`.

`get_timeline()` is a `ZtripTimeline`: the tempo map (`seconds_at(beat)`,
`beat_at(t)`, `position_at(beat)`, `get_segments()`, `set_nudge(s)`,
`scale_tempo(f)`) and lanes of notes (`get_notes(lane)`, `set_notes(lane,
notes)`, `add_note(...)`), all timing in beats. Every edit emits `changed`
and the player rebinds the graph at once, so scripts can generate or edit
notes live.

`ZtripAnalysis` analyses a WAV on its own (`load_file`, `get_bpm`,
`get_beats`, `get_event_times("onset.Sub")`, `sample("env.Bass", t)`), and
`ZtripGraphTools` lists node types and checks graphs.

### Live input

Turn on `live_input` and press `playing` (or use `autoplay`): the graph's
`main` slot then reads live audio, analysed as it arrives, with the same
signal names as a song (`band.*`, `env.*`, `rms`, `centroid`, `onset.*`), so
the same graph works on both. The microphone is opened only while playing.
`ZtripPlayer.get_live_devices()` lists inputs as `{name, default, loopback}`
dictionaries. Loopback records an output with WASAPI on Windows and uses the
output's "Monitor of" source on Linux (PulseAudio or PipeWire).

Live analysis adds about 12-15 ms before an onset is known, on top of the
sound card's own buffer. Live input has no beat grid or kick detector yet:
use `AudioOnset` on `Sub` with `min_velocity` around 0.3 for kicks, and
`BeatClock` runs at its `bpm` param. `NoteLane` stays silent, since the
timeline belongs to the song.

### Target paths

A `PropertyOut` target is `<node path>:<property path>`, relative to
`target_root`. The property path is anything `Object.set_indexed` accepts:

| Target | Drives |
| --- | --- |
| `Lights/Key:light_energy` | a light's energy |
| `Lights/Key:light_color:h` | one component of a color (`r`, `g`, `b`, `a`, `h`, `s`, `v`) |
| `Orb:material_override:shader_parameter/glow` | a ShaderMaterial uniform |
| `Orb:surface_material_override/0:emission_energy_multiplier` | a StandardMaterial3D setting |
| `Camera:fov`, `Camera:position:x` | camera settings and transforms (one component) |
| `Environment:environment:glow_intensity` | WorldEnvironment settings |
| `@globals:pulse` | a global shader parameter |

Float, int and bool properties work (ints round, bools are on at 0.5 and
above). The value a property had before the graph first wrote to it is
kept: turning `active` off, removing the player, reloading a graph that no
longer drives it, or saving the scene puts it back, so graph-driven values
never end up saved in your `.tscn`. Targets that don't resolve are listed in
`status` and as node warnings.

## Motion Graph panel

The bottom panel edits `.mgraph.json` files with Godot's GraphEdit:
**Add Node** (or right-click the canvas) by category, drag between ports to
wire (an input takes one wire; port colours are Continuous blue, Trigger
orange, Clock purple), Delete to remove, and edit params on the nodes. The
side list holds macros; **+ Macro** promotes any number param. Errors and
warnings from the compiler show at the top right. Selecting a `ZtripPlayer`
opens its graph, and while that player runs, each output shows its live
value. Files are saved in the core's canonical format, so they diff cleanly.

## Timeline panel

The second bottom panel edits the selected player's timeline. Time runs left
to right in seconds, so the song's loudness stays put while the bar grid and
notes move with the tempo map. Rows: bar numbers (click to seek), tempo (BPM
line and meter), loudness, then the lanes, with every detected hit drawn
faintly behind the notes. **Nudge** and **Tempo** align the grid by ear;
clicking adds a note, dragging moves or resizes the selection, right-click
deletes; arrows, Up/Down (velocity), Q (quantize), Delete, Ctrl+Z/Ctrl+Shift+Z
and Space work when the timeline has focus. The full key list is at the top
of `addons/ztrip/editor/timeline_panel.gd`. **Save** writes the two files
next to the song unless the player already names them.

## Render a video

`render_video.sh` renders the demo stage reacting to any WAV into an MP4
with sound, using Godot's Movie Maker mode (Linux, needs ffmpeg; uses
`xvfb-run` when there is no display):

```sh
GODOT=/path/to/godot hosts/godot/render_video.sh song.wav out.mp4 30 20 "optional caption"
```

The arguments after the output are the start time and length in seconds, a
caption, and `1` to show the Timeline panel along the bottom.

## Tests

`project/tests/test_host.gd` synthesises a 120 BPM kick track and checks the
extension end to end: analysis and its cache, a light flashing on kicks and
decaying between them, a shader uniform, the camera, seek-equals-play through
the binder, macros, hot reload, write-back, the graph panel's load, edit
and save, and the timeline: the drafted Kick lane within 20 ms of the true
kicks, NoteLane playback, edits in the Timeline panel through synthetic mouse
and key events (add, drag, arrows, delete, undo/redo, nudge), and saving and
reloading the `.tempo.json` and `.nseq.json`. CI runs it on Linux and Windows in Godot 4.5.1 and 4.7.2:

```sh
godot --headless --path hosts/godot/project --import
godot --headless --path hosts/godot/project -s res://tests/test_host.gd
```

`tools/build_stage.gd` regenerates `demo/stage.tscn` from code (it
overwrites edits made in the editor).

## Known gaps

- WAV only, and Godot itself only plays plain PCM WAVs: files saved as
  `WAVE_FORMAT_EXTENSIBLE` (common for 24-bit and float) import with an
  error. With the plugin enabled, the editor rewrites such a file's header
  as plain PCM or float in place (the samples are untouched) and reimports
  it, so this shows as one error and a "ztrip: rewrote the header" line.
- One song (or one live input) per player, bound to the graph's `main` source slot.
- Targets take one number each; colours and vectors are driven per component.
- Graph layers and blend modes, undo/redo in the Motion Graph panel and
  sparkline previews come in later phases.
- The Timeline panel can't draw tempo changes or meter changes yet (edit the
  `.tempo.json`), shows one waveform (the mix, no stems), and has no piano-roll
  pitch view. Its undo is its own (Ctrl+Z in the panel), separate from the
  editor's.
- Only a `template_debug` build of the library is made, and release exports
  load it too.
- Quitting while a song plays (for example with `--quit-after`) prints
  "ObjectDB instances leaked at exit" and "1 resources still in use at
  exit" for the song. That comes from Godot itself (a plain project with an
  AudioStreamPlayer does the same in 4.5.1 and 4.7.2) and is harmless.
- Exported games analyse the song at start-up, so the original WAV must be in
  the export: add `*.wav` to the export preset's *Filters to export
  non-resource files*. The analysis cache lives in `user://ztrip_cache/`.
