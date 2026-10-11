# ztrip for Unreal Engine

A plugin for Unreal Engine 5.6 or newer that plays a song through a Motion
Graph and drives level properties from it: lights flash on the kick,
materials pulse with the bass, the camera pushes in on every bar. It runs in
the editor as well as in game, follows Sequencer, and bakes the motion into a
Level Sequence as keyframes that play without the plugin.

The same `.mgraph.json` runs here and in the Godot addon, through the same
core: song analysis, graph evaluation, tempo map and Note Sequence are shared
code, and the core's tests check that the Unreal player's evaluation path gives
values identical to the Godot player's for the same song and graph.

> **Beta, not yet built against the engine.** The plugin's engine-free part
> (the ztrip core it compiles in) is built and tested on every change, the
> way UnrealBuildTool will compile it. The Unreal-facing code (the player
> actor, property binder and Sequencer tools) has not yet been compiled
> against a real Unreal Engine install, since none is available to the
> project's CI. Expect the first build to need small fixes; please report
> them.

Windows (Visual Studio 2022) and Linux, x86_64. The plugin is C++ source, so
the project must be a C++ project (add any C++ class once to convert a
Blueprint-only project).

## Install

1. Get the staged plugin folder `Ztrip/`: download the `ztrip-unreal-plugin`
   artifact of a [CI run](https://github.com/zedhand/ztrip/actions), or build
   it from the repository:

   ```sh
   cmake --preset release
   cmake --build --preset release --target ztrip_unreal_plugin
   ```

   It lands in `build/release/unreal/Ztrip`. (The `hosts/unreal/Ztrip` folder
   in the repository is not usable on its own: staging adds the ztrip core
   and fills in its build settings.)
2. Copy `Ztrip/` into your project's `Plugins/` folder.
3. Right-click the `.uproject` > *Generate Visual Studio project files*, then
   build the project's *Editor* target, or open the project and accept the
   prompt to build the missing modules.

## Use

1. Put the song in your project as a WAV, for example
   `Content/Music/song.wav`, and import it too if you want to hear it.
2. *Place Actors* > search **Ztrip Player** and drop one in the level.
3. In its details, set:
   - **Audio File**: the WAV (a path relative to the project folder). If you
     leave it empty, the player analyses the file the **Sound** was imported
     from.
   - **Sound**: the imported song, to hear it when the player plays on its own.
   - **Graph File**: a `.mgraph.json`, for example one of `examples/` in the
     repository.
4. **Status** says when the song is analysed (a few seconds; it is cached in
   `Saved/Ztrip/Cache` afterwards) and which targets are bound.
5. Drag **Song Time** to scrub, or press **Play**. The level reacts in the
   editor, without Play In Editor.

Lights must be *Movable* (Unreal ignores intensity changes on static
lights), and so must components the graph moves.

### Targets

A graph's `PropertyOut` nodes name their targets. In Unreal a target is
`<actor>[/<component>]:<property>`:

| Target | Drives |
| --- | --- |
| `KickLight:Intensity` | a light's intensity (found on the actor's light component) |
| `KickLight/LightComponent0:LightColor.R` | one field of a struct property, on a named component |
| `PostProcess:Settings.BloomIntensity` | a Post Process Volume setting (tick its override checkbox once) |
| `CineCamera:CurrentFocalLength`, `Camera:FieldOfView` | camera settings |
| `Orb:location.z`, `Orb:rotation.yaw`, `Orb:scale.x` | the transform, relative to the parent; rotation also takes `roll`, `pitch`, `x`, `y`, `z` |
| `Orb:material.Glow`, `Orb:material[1].Glow` | a scalar parameter on a dynamic instance of every material slot, or of one |
| `@mpc:Pulse`, `@mpc/MPC_Stage:Pulse` | a scalar parameter of the player's Material Parameter Collection (add it to **Parameter Collections**) |
| `Sparks:niagara.Rate` | a Niagara user parameter (`User.Rate`) |
| `Sparks:@burst` | restarts a Niagara or Cascade system each time the value rises through 0.5 |
| `Stage:@call.OnKick` | calls a function or custom event: with one number parameter, whenever the value changes; with none, each time it rises through 0.5 |

- `<actor>` is the actor's label as shown in the Outliner, its object name,
  or one of its tags. Labels only exist in the editor, so the player keeps
  the actors it found by label (**Actor Cache**) and packaged games use that.
- Without `/<component>`, a property is looked up on the actor, then its root
  component, then its other components. `<component>` is a component's name
  or its class (`PointLightComponent`).
- Property names are as in C++ (`Intensity`, `LightColor`), case-insensitive.
  Numbers and bools work; a struct (a color, a vector) needs a field. When
  the class has a `Set<Property>` function taking the value (like
  `SetIntensity`), it is called so the engine sees the change; otherwise the
  value is written and the component's render state refreshed.
- Targets that don't resolve are listed in **Status** and the Output Log.

Values the graph overrides are put back when **Active** is turned off, the
player is removed, play ends, the graph is reloaded, or the level is saved,
so graph-driven values never end up saved in the level. Niagara parameters
are the exception: they keep their last value.

### Running a graph made for another scene

**Target Map** sends a graph's targets elsewhere, with a scale and offset:
the value written is `value * Scale + Offset`. The Godot demo stage's graph
(`hosts/godot/project/demo/stage.mgraph.json`) runs unchanged with entries
like these (Godot uses metres, Unreal centimetres):

| From | To | Scale |
| --- | --- | --- |
| `Lights/Kick:light_energy` | `KickLight:Intensity` | 500 |
| `Orb:material_override:shader_parameter/glow` | `Orb:material.Glow` | 1 |
| `Camera:fov` | `Camera:FieldOfView` | 1 |
| `Camera:position:x` | `Camera:location.y` | 100 |

### Macros

**Macros** lists the graph's macro controls once it loads. Change a value
and the graph follows; values are saved with the level. Blueprint:
`Set Macro`, `Get Macro`.

### Timeline

The player drafts the song's tempo map and Kick, Snare and Hats note lanes
from the analysis, exactly as the Godot addon does; `NoteLane` and
`BeatClock` nodes read them. **Draft Timeline** drafts them again and **Save
Timeline** writes them to **Tempo File** and **Sequence File** (next to the
song when those are empty), where the Godot Timeline panel can edit them; the
player loads them when they exist.

## Sequencer

Set the player's **Level Sequence** (create one with *Cinematics > Add Level
Sequence*), then:

- **Set Up Level Sequence** adds the song as an audio track, a *Song Time*
  track running from 0 to the song's end, the playback range and a marker on
  every bar. Playing or scrubbing the sequence (or rendering it with Movie
  Render Queue) now drives the level. Song time 0 sits at the start of the
  playback range.
- **Bake To Level Sequence** evaluates the graph at the sequence's display
  rate over **Bake Start** to **Bake End** (0: the song's end) and keys every
  target a Level Sequence can animate on its own: float and double
  properties of actors and components, and transforms. **Bake Tolerance**
  drops keys while the curve stays within that much of every frame. The keys
  play without ztrip installed; turn the player's **Active** off to watch
  them on their own. Material, collection, Niagara, burst and call targets
  are not keyed yet (the notification lists what was skipped).

**Song Time** is an ordinary animatable property, so it can also be keyed by
hand: slow the song down, loop a section, or jump.

## Blueprint

`Play(From)`, `Stop`, `Set Song Time`, `Set Active`, `Reload`, `Set Macro`,
`Get Macro`, `Get Output(Target)`, `Get Port Value(Node, Port)` (any node's
output, for HUDs), `Is Ready`, `Get Duration`, `Get BPM`, `Wait Until
Loaded`, `Update Now`. Events: `On Analysis Loaded`, `On Graph Loaded`, `On
Finished`. **Autoplay** starts the song when the game starts, once it is
analysed.

## Packaged games

The player reads the WAV and the graph, tempo and sequence files at run time.
Keep them in one folder under `Content/` (for example `Content/Ztrip`) and
add that folder to *Project Settings > Packaging > Additional Non-Asset
Directories To Package*. Give the player paths relative to the project
folder (`Content/Ztrip/song.wav`), which is what the file pickers do.

## How it is built

`Source/ZtripRuntime` holds the player (`AZtripPlayer`), the property binder
(`FZtripBinder`) and, once staged, the ztrip core (`Public/ztrip`,
`Private/Core`) with pffft and nlohmann/json (`Private/ThirdParty`), compiled
from source with the engine's own toolchain. `Source/ZtripEditor` holds the
Sequencer tools. The engine-free part of each Unreal-specific feature lives
in the core and is unit-tested there:

- `ztrip/host/session.hpp`: song analysis and cache, graph hot reload, macros
  and the timeline, the same steps the Godot player takes.
- `ztrip/host/bake.hpp`: sampling a graph per frame and reducing keys.
- `ztrip/host/unreal_target.hpp`: the target syntax above.

`hosts/unreal/check` builds the staged core the way UnrealBuildTool does (no
unity files, no RTTI, warnings as errors with Clang, every file in the module
folder) into a shared library that must link with no missing symbols, and
runs a song through it. CI runs it on Linux and Windows.

## Known gaps

- Not yet compiled against Unreal Engine (see the note at the top).
- No Motion Graph editor inside Unreal yet: edit `.mgraph.json` files in the
  Godot addon's Motion Graph panel or a text editor; the player reloads them
  on save.
- No Timeline panel inside Unreal: the tempo map and notes are drafted
  automatically and can be edited in Godot.
- No live input (microphone, system audio, MIDI, OSC, DMX) in Unreal yet.
- Bake keys properties and transforms only.
- Played on its own (not under a Level Sequence), song time follows the
  game clock rather than the audio device, so very long songs can drift by a
  frame or two from what you hear.
- WAV only.
