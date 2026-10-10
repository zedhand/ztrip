# Decisions

Choices made where the spec left room, newest last.

1. **Test framework: Catch2 v3**, fetched with CMake `FetchContent` (or an installed Catch2 3.x if found). Simpler single-dependency setup than GoogleTest.
2. **Minimum CMake 3.24**, for `FetchContent` `FIND_PACKAGE_ARGS` and presets v6.
3. **Core target layout:** one static library `mre_core` with public headers under `core/include/mre/`. Subsystem folders (`analysis/`, `graph/`, ...) add sources to that one target as they land rather than becoming separate libraries.
4. **CI:** GitHub Actions builds and tests on Ubuntu 24.04 (GCC) and Windows 2022 (MSVC), matching the v1 platforms.
5. **License:** private for now; no license file is added.
