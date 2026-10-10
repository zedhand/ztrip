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
| `ztrip-<version>-linux-x86_64.tar.gz` | Ubuntu 22.04, GCC 12, libstdc++ linked statically into `analyze-cli` and the Godot addon |
| `ztrip-<version>-windows-x64.zip` | Windows Server 2022, MSVC, runtime DLLs bundled next to `analyze-cli.exe` |

Both hold the CLI, the SDK, the docs and the Godot demo project with the
built addon in `godot/`.

The addon has its own build tree (`build/godot-addon`, from the `godot`
preset with tools and tests off). With the Godot host on, MSVC uses the
static runtime that godot-cpp needs, and an SDK library built that way would
no longer link into ordinary `/MD` consumer projects. So the SDK and CLI come
from the `release` build, and `ZTRIP_PACKAGE_GODOT_BUILD_DIR` tells its CPack
config to take only the `godot` component from the addon's tree.

Each archive is extracted and checked before upload: `analyze-cli --version`
must print the version, a small CMake project (`tests/package-consumer/`)
must build and run against the installed library, and the archive's Godot
project must import and pass the host tests (`hosts/godot/project/tests/`,
copied in for the check, not shipped) in Godot 4.5.1, the oldest supported
version.

The workflow runs on:

- a pushed tag `v*`: builds, then creates a **draft** GitHub release with both
  archives and `SHA256SUMS.txt`. Nothing is public until the draft is published
  by hand. Tags with a `-` (like `v0.1.0-beta.1`) are marked pre-release.
- manual dispatch (Actions > Release > Run workflow): builds the archives as
  workflow artifacts only. Pull requests don't run it (to stay inside the free
  Actions quota), so dispatch it on a branch that changes the build or
  packaging before merging.

To build an archive locally the way the workflow does (on Linux, the
workflow also passes `-static-libstdc++ -static-libgcc` as linker flags; on
Windows, run this in the x64 Native Tools prompt):

```sh
cmake --preset godot -B build/godot-addon -DZTRIP_BUILD_TOOLS=OFF -DZTRIP_BUILD_TESTS=OFF
cmake --build build/godot-addon --target ztrip_godot
cmake --preset release -DZTRIP_PACKAGE_GODOT_BUILD_DIR=$PWD/build/godot-addon
cmake --build --preset release
cpack --config build/release/CPackConfig.cmake -B build/release/packages
```

Leave out the first two lines and the `-D` option for an archive without
`godot/`. Note the addon library is written into the source tree
(`hosts/godot/project/addons/ztrip/bin/`) and packaged from there.

## Cutting a release

1. Set the version in `CMakeLists.txt` and merge it to `main`.
2. Tag the merge commit with `v` plus the full version and push the tag:
   `git tag v0.1.0-beta.1 && git push origin v0.1.0-beta.1`.
   The workflow fails if the tag and the CMake version disagree.
3. When the workflow finishes, review the draft under Releases, edit the notes
   and publish it.
4. Bump the version on `main` (for example to `beta.2`) for the next round.
