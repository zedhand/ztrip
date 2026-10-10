# ztrip beta

Thanks for trying the ztrip beta. This archive holds:

- `bin/analyze-cli` (`analyze-cli.exe` on Windows): analyses a WAV file into
  band levels, envelopes, onsets and a beat grid. Run `analyze-cli --help`.
- `bin/live-cli`: listens to a microphone or what the computer plays and
  prints live band levels and onsets. `live-cli --list` lists inputs.
- `include/` and `lib/`: the `ztrip_core` C++20 static library. From CMake,
  point `CMAKE_PREFIX_PATH` at this folder, then
  `find_package(ztrip CONFIG REQUIRED)` and link `ztrip::core`.
- `examples/`: Motion Graph files (`.mgraph.json`).
- `docs/`: architecture, node reference and design decisions.
- `licenses/`: licenses of third-party code compiled into ztrip.
- `godot/`: the Godot 4.5+ demo project with the ztrip addon already built
  into `addons/ztrip/bin/`.
  In Godot's Project Manager choose **Import** and open `godot/project.godot`,
  then follow "Try the demo" in `godot/README.md` (it is written for the
  source tree, where this folder is `hosts/godot/project/`).

Quick start:

```sh
bin/analyze-cli --version
bin/analyze-cli song.wav -o song-analysis
```

Beta builds are tested on Windows 11 (x64) and Linux (x86_64, glibc 2.35 or
newer, for example Ubuntu 22.04). The Windows library is built with MSVC and
the dynamic runtime (`/MD`); the Linux library with GCC and libstdc++. Both
are Release builds, so link them from a Release (or RelWithDebInfo) build.
