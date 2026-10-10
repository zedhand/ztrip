# Releasing

## Version

The version lives in the top-level `CMakeLists.txt`: `project(ztrip VERSION x.y.z)`
plus `ZTRIP_VERSION_PRERELEASE` (for example `beta.1`, empty for a final
release). Together they give the full version, such as `0.1.0-beta.1`, which
`analyze-cli --version`, `ztrip::version()` and the archive names report.
`find_package(ztrip x.y)` matches on `x.y.z` only, and accepts any patch of the
same minor version.

## Builds

`.github/workflows/release.yml` builds, tests and packages:

| Archive | Built on |
| --- | --- |
| `ztrip-<version>-linux-x86_64.tar.gz` | Ubuntu 22.04, GCC 12, libstdc++ linked statically into `analyze-cli` |
| `ztrip-<version>-windows-x64.zip` | Windows Server 2022, MSVC, runtime DLLs bundled next to `analyze-cli.exe` |

Each archive is extracted and checked before upload: `analyze-cli --version`
must print the version, and a small CMake project
(`tests/package-consumer/`) must build and run against the installed library.

The workflow runs on:

- a pushed tag `v*`: builds, then creates a **draft** GitHub release with both
  archives and `SHA256SUMS.txt`. Nothing is public until the draft is published
  by hand. Tags with a `-` (like `v0.1.0-beta.1`) are marked pre-release.
- manual dispatch (Actions > Release > Run workflow): builds the archives as
  workflow artifacts only.
- pull requests that touch the build or packaging files: same as dispatch.

To build an archive locally:

```sh
cmake --preset release
cmake --build --preset release
cpack --config build/release/CPackConfig.cmake -B build/release/packages
```

The `godot` preset builds the same archive plus a `godot/` folder holding the
demo project with the built addon (the `godot` component). The release
workflow still uses the `release` preset: with the Godot host on, MSVC builds
use the static runtime that godot-cpp needs, and the SDK library would then
no longer link into ordinary `/MD` consumer projects. Until the addon gets its
own build in the workflow, CI uploads it as the `ztrip-godot-*` artifacts.

## Cutting a release

1. Set the version in `CMakeLists.txt` and merge it to `main`.
2. Tag the merge commit with `v` plus the full version and push the tag:
   `git tag v0.1.0-beta.1 && git push origin v0.1.0-beta.1`.
   The workflow fails if the tag and the CMake version disagree.
3. When the workflow finishes, review the draft under Releases, edit the notes
   and publish it.
4. Bump the version on `main` (for example to `beta.2`) for the next round.
