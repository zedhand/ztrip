# Contributing to ztrip

Thanks for helping. ztrip is in beta, so bug reports from real songs are the
most useful thing you can send.

## Reporting bugs

Open a [GitHub issue](https://github.com/zedhand/ztrip/issues) with:

- your OS, and Godot version if it is about the addon;
- the ztrip version or commit;
- what you did, what you expected and what happened;
- for analysis problems, the song's genre and tempo, and the `analyze-cli`
  output or plot. Please don't attach copyrighted songs; a short clip you made
  yourself, or a link to a freely licensed track, is fine.

Security problems go through [SECURITY.md](SECURITY.md) instead.

## Building and testing

See [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md#build-from-source). In short:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Accuracy tests against the reference tracks run only when
`ZTRIP_REFERENCE_TRACKS` points at a folder holding them.

## Pull requests

- Keep each PR to one change, with tests for new behavior.
- Match the existing style: C++20, the warning flags in `cmake/ZtripWarnings.cmake`
  must stay clean.
- When you make a choice the docs leave open, add a numbered entry to
  [docs/DECISIONS.md](docs/DECISIONS.md) saying what and why.
- Never commit audio, video or build output; `.gitignore` covers the usual
  cases. Test audio is synthesized in `tests/synth.hpp`.
- New third-party dependencies need a license compatible with MIT and an entry
  in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

By contributing you agree that your work is released under the
[MIT license](LICENSE), and to follow the [Code of Conduct](CODE_OF_CONDUCT.md).
