# Repository Guidelines

## Project Structure & Module Organization

This is a C++23 project. `src/api/` implements HTTP access and JSON models; `src/core/` coordinates process-local caching and asynchronous work; `src/server/` provides JSON IPC; `src/common/` contains shared types; and `src/app/` has a manual integration executable. The Linux-only `tui/` is a separate IPC client, not a backend module. Russian documentation is in `docs/`. Build output belongs in `out/`.

## Build, Test, and Development Commands

Use the Nix shell for declared dependencies, then configure and build:

```sh
nix develop
cmake -S . -B out/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build out/Debug -j2
```

Linux builds `aniparser_tui`; desktop builds also include `my_app` and `aniparser_server`. There is no configured unit-test framework or CTest suite. `my_app` calls the external API; it is not an offline test. Check module `CMakeLists.txt` when adding sources or targets.

## Coding Style & Naming Conventions

Follow `.clang-format`: two-space indentation, no tabs, and an 80-column limit. Use PascalCase for types and the naming style already used by the owning module for functions and variables. Keep public headers with their module and register new sources or headers in that module’s CMake target. Format changed C++ files with `clang-format -i path/to/file.cpp path/to/file.hpp`.

## IPC Changes

IPC transmits complete JSON values. Include a request ID in every response and asynchronous update, validate the method payload, and keep storage details inside `Core`. Update both platform transports and the protocol documentation together. Keep `tui/` coupled to the IPC protocol, not backend implementation.

## Testing and Pull Requests

For code changes, build the affected target with `cmake --build out/Debug -j2`; add tests only when introducing or extending configured test infrastructure. The Git history has short informal subjects and no strict convention; prefer an imperative subject naming the changed area. Pull requests should explain behavior and motivation, list checks performed, link issues when relevant, and include screenshots for visible TUI changes.

## Configuration Notes

Keep generated files and build artifacts under ignored `out/`. Dependencies are declared in `flake.nix`. Do not commit local build output or generated editor indexes.
