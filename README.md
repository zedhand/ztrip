# ztrip

Music-reactive animation engine (working name `mre`): an engine-agnostic C++20 core that turns audio and live inputs into animation, with Godot 4 and Unreal Engine 5.6+ hosts planned.

Status: Phase 1 skeleton. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Build

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```
