# Getting started (beta)

This guide is for beta testers. It walks you through building ztrip, running the analyser on a song of your own, and reading what it found. The timeline is still being built; its section below is a placeholder and will be filled in when it lands.

If something here does not match what you see, that is worth reporting too (see [Reporting problems](#reporting-problems)).

## What you can try today

| Piece | State | How to try it |
| --- | --- | --- |
| Audio analysis (`analyze-cli`) | Ready for testing | [Analyse a song](#analyse-a-song) |
| Motion Graph core | Works from C++ only | [Motion Graphs](#motion-graphs) |
| Godot addon | Ready for testing | [Godot addon](#godot-addon) |
| Timeline, tempo map and note lanes | In progress | [Timeline](#timeline-coming-soon) |

Supported platforms for the beta are Windows 11 and Linux. macOS is not supported yet.

## Build from source

Prebuilt downloads are planned for the beta. Until they exist, build from source; it takes a few minutes and needs no manual dependency installs beyond the compiler and build tools (the rest is fetched by CMake on first configure, so the first build needs internet access).

### Linux

You need CMake 3.24+, Ninja, Git and GCC 13+ or Clang 16+. On Ubuntu 24.04 or Debian 13:

```sh
sudo apt install build-essential cmake ninja-build git
```

Then:

```sh
git clone https://github.com/zedhand/ztrip.git
cd ztrip
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The last command runs the test suite; every test should pass (a few reference-track tests report as skipped, which is expected).

### Windows 11

1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/vs/) (Community is fine) with the **Desktop development with C++** workload. It includes CMake and Ninja.
2. Install [Git for Windows](https://git-scm.com/download/win).
3. Open **x64 Native Tools Command Prompt for VS 2022** from the Start menu. The commands below must run in that prompt, not a plain PowerShell or cmd window.

```bat
git clone https://github.com/zedhand/ztrip.git
cd ztrip
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The analyser ends up at `build\release\tools\analyze-cli\analyze-cli.exe`.

## Analyse a song

`analyze-cli` reads a WAV file and works out how loud each frequency band is over time, where the hits (onsets) are, the tempo, and where each beat and bar starts. Everything ztrip animates later is driven by these signals, so this is the part to test first.

### 1. Get a WAV file

Only WAV is supported for now (8/16/24/32-bit PCM or float, any sample rate, any number of channels). If you pass an MP3, FLAC or OGG file, `analyze-cli` stops and says so. To convert an MP3, FLAC or OGG, use [ffmpeg](https://ffmpeg.org/) or Audacity (File > Export > Export as WAV):

```sh
ffmpeg -i song.mp3 song.wav
```

### 2. Run the analyser

Linux:

```sh
build/release/tools/analyze-cli/analyze-cli song.wav -o song-analysis
```

Windows:

```bat
build\release\tools\analyze-cli\analyze-cli.exe song.wav -o song-analysis
```

It prints a summary like this (from a 128 BPM test track):

```text
input     song.wav
audio     62.00 s, 48000 Hz, 2 ch
analysis  song-analysis/song.sigcache (computed)
tempo     128.00 BPM, 128 beats, 32 downbeats
onset.Sub        131
onset.Bass       195
...
kick             112
tempo map song-analysis/song.tempo.json (1 segments)
lane      Kick: 112 notes -> song-analysis/song.nseq.json
plots     python3 song-analysis/plot.py
```

The first thing to check is the **tempo** line. If you know the song's BPM, compare. Getting half or double the real tempo (64 or 256 instead of 128) is a known failure mode worth reporting. The `kick` line counts the kick drum hits it found.

### 3. Look at the result

The output folder holds:

| File | What it is |
| --- | --- |
| `<name>.sigcache` | The analysis in ztrip's binary format. Re-running with the same song and settings reuses it instead of recomputing. |
| `signals.csv` | 120 values per second for each band level (`band.Sub` ... `band.Air`), its smoothed envelope (`env.*`), overall loudness (`rms`) and brightness (`centroid`). All are 0 to 1. |
| `events.csv` | Every detected onset per band, plus each beat and downbeat, with a time in seconds and a strength from 0 to 1. |
| `beats.csv` | The beat grid: time, bar number, beat number in the bar, and whether it is a downbeat. |
| `<name>.tempo.json` | The tempo map: the song's tempo, bar by bar, fitted to the detected beats. |
| `<name>.nseq.json` | A Note Sequence with a Kick lane: one note per detected kick, snapped to a 1/16 grid. |
| `kick_clicks.wav` | Only with `--clicks`: the song with a click on every note of the Kick lane. |
| `plot.py` | A script that draws all of the above as a picture. |

To see it as a picture, install Python 3 with numpy and matplotlib once. On Ubuntu or Debian use `sudo apt install python3-numpy python3-matplotlib` (plain `pip install` is blocked there). On Windows, install Python from [python.org](https://www.python.org/downloads/) and run `py -m pip install numpy matplotlib`. Then (on Windows, `py` instead of `python3`):

```sh
python3 song-analysis/plot.py --start 30 --end 45
```

That writes `song-analysis/analysis.png` for 30 s to 45 s of the song. There is one row per band, from Sub (deep bass, 20-60 Hz) up to Air (cymbal shimmer, 6-20 kHz), and a bottom row with loudness and the beat grid. In each band row, the light fill is the level, the dark line is the envelope, and red ticks are detected hits. Orange triangles along the top of the Sub row are the kicks. Dashed grey lines mark the start of each bar.

Quick checks that need no musical training:

- **Kicks:** in a song with a steady kick drum, the Sub row should show an orange triangle on every kick, evenly spaced.
- **Kicks by ear:** run again with `--clicks` and play `kick_clicks.wav`. Each click should land on a kick drum hit. Clicks with no kick under them, or kicks with no click, are worth reporting.
- **Hi-hats and cymbals:** these show up in the HighMid and Air rows as dense, regular ticks.
- **Beats:** the green ticks in the bottom row should be evenly spaced and, if you play the song and tap along, line up with your taps. Orange ticks (downbeats) should land on the "1" of each bar.
- **Quiet parts:** in a breakdown, the levels should drop and the ticks should thin out.

### 4. Tuning options

Run `analyze-cli --help` for the full list. The ones most worth trying:

| Option | Default | When to change it |
| --- | --- | --- |
| `--bpm-prior BPM` | 120 | The tempo comes out at half or double the real one. Set this near the real BPM. |
| `--sensitivity X` | 1.5 | Too many red ticks: raise it. Hits are missed: lower it. |
| `--min-gap MS` | 50 | Fast rolls are merged into one hit: lower it. |
| `--kick-rise DB` | 12 | Clicks with no kick under them: raise it. Kicks are missed: lower it. |
| `--nudge MS` | 0 | Every click is early or late by the same amount: shift them (positive is later). |
| `--release MS` | 150 | Envelopes fall too slowly (lights would linger): lower it. |
| `--beats-per-bar N` | 4 | Songs in 3/4 or 6/8. |
| `--fft N` | 2048 | Larger sizes separate low frequencies better but blur timing. |

Changing any option recomputes the analysis automatically.

## Motion Graphs

A Motion Graph is a small `.mgraph.json` file that turns signals into property values, for example "on each kick, flash the key light and let it fade over 150 ms". [`examples/kick_to_light.mgraph.json`](../examples/kick_to_light.mgraph.json) is exactly that, and [`examples/kitchen_sink.mgraph.json`](../examples/kitchen_sink.mgraph.json) uses every node type. [NODES.md](NODES.md) lists the nodes and their settings.

Graphs run in the Godot addon (below), which also has a panel for editing them, or from C++ (see the README).

## Godot addon

You need [Godot 4.5 or newer](https://godotengine.org/download) (the standard build, not .NET) and the build tools from [Build from source](#build-from-source).

1. Build the addon from the ztrip folder (the first build takes several minutes):

   ```sh
   cmake --preset godot
   cmake --build --preset godot
   ```

2. In Godot's Project Manager, choose **Import** and open `hosts/godot/project/project.godot`.
3. Copy a WAV into the project's `music/` folder (it shows up in the FileSystem dock). If Godot reports "Format not supported for WAVE file", re-save it as 16-bit PCM: `ffmpeg -i song.wav -c:a pcm_s16le song16.wav`.
4. Open `demo/stage.tscn`, select the `Music` node and drag your WAV onto its **Stream** in the inspector.
5. Select the `ZtripPlayer` node. After a second or two its **Status** shows the song's length and tempo.
6. Drag **Time** to scrub, or tick **Playing** to hear the song while the scene reacts, without pressing Play. Press F5 to run the stage full screen with its beat counter.

What to look for: the red light flashes with the kick drum, the blue spotlight with the snare, the white light with hi-hats, the orb glows with the bass and flashes on every beat, and the camera pushes in at the start of each bar. If the song has a part without drums, those lights go dark there.

The **Motion Graph** panel at the bottom of the editor shows the graph that does this, with each output's live value. Change a number (for example `kick_flash`'s `decay_ms`) and **Save**: the scene picks it up straight away. The sliders under **Macros** in the `ZtripPlayer` inspector are quick controls for the same graph. [hosts/godot/README.md](../hosts/godot/README.md) explains how to point a graph at your own lights, materials and cameras.

## Timeline (coming soon)

> **Placeholder.** Musical time (tempo map, note lanes and the timeline dock) is being built now and this section will be written when it lands.

What it will cover:

- The bars-and-beats timeline, and setting or nudging the tempo and first downbeat.
- Note lanes: drawing notes, and auto-drafting a kick lane from the song.
- Driving a graph from a note lane instead of raw audio.

## Known limitations

- Input must be WAV; convert other formats first. In Godot, WAVs must be plain PCM (see [Godot addon](#godot-addon)).
- Songs played live (not to a click) can drift in tempo. The beat grid is a single fixed tempo for now, so it can slide onto the off-beat in places. The tempo map in the timeline will fix this.
- The Sub band also picks up bass notes, so it can tick on a bassline as well as the kick.
- No live input yet (microphone, MIDI, OSC, DMX). That comes after the beta.

## Reporting problems

Open an issue at <https://github.com/zedhand/ztrip/issues> with:

- Your OS and what you ran (copy the command).
- The full console output.
- For analysis problems: the song's name and, if you know it, its real BPM, plus the `analysis.png` for the part that looks wrong (`--start`/`--end` to zoom in). Please don't attach copyrighted audio.
