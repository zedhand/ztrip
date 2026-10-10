# Privacy

ztrip does not collect, send or store any personal data.

- **No telemetry, analytics, crash reporting or update checks.** The core
  library, `analyze-cli` and the Godot addon make no network requests.
- **No accounts and no servers.** There is nothing to sign in to.
- **Your files stay on your machine.** ztrip reads the songs and graph files you
  give it and writes its results next to them or where you point it: analysis
  output from `analyze-cli`, `.mgraph.json`, `.tempo.json` and `.nseq.json`
  files you save from the Godot panels, and an analysis cache in Godot's
  `user://ztrip_cache/` folder. Delete that folder to clear the cache.
- **One in-place edit in the Godot editor:** if a WAV in your project has a
  header Godot can't import (WAVE_FORMAT_EXTENSIBLE, common for 24-bit and
  float files), the addon rewrites that file's header, not its audio, so Godot
  can load it.

Once live input ships, ztrip will also open the microphone or audio device,
MIDI ports, and UDP ports for OSC, Art-Net and sACN that you choose. That data
is processed in memory to drive your graph and is not recorded or sent
anywhere.

Building from source downloads the dependencies listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) from GitHub; that is the
build tool talking to GitHub, not ztrip.

Questions: open an issue on [GitHub](https://github.com/zedhand/ztrip/issues).
