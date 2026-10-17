# Repository Guidelines

## Project Structure & Module Organization

This repository is a C++ project organized by module under `src/`: `api/` contains HTTP/API and JSON models, `core/` contains application logic, `cache/` contains shared-memory and heap code, `common/` holds shared types, and `app/` contains the executable entry point. Each module has a `CMakeLists.txt`. Root build configuration and reusable CMake helpers live in `CMakeLists.txt` and `cmake/`. Build output belongs in the ignored `out/` directory. There is no separate assets directory or established test directory.

## Build, Test, and Development Commands

Use the Nix development shell to obtain the declared compiler and libraries:

```sh
nix develop
cmake -S . -B out/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build out/Debug
```

The first command enters the environment from `flake.nix`; CMake configures the project and the build command compiles its targets. The build currently requires CMake 3.25+, C++23, Boost, nlohmann-json, cpr, and OpenSSL. No test target or test runner is configured; `src/api/test.cpp` is not evidence of an integrated test suite. Check the CMake configuration before relying on a target name or adding test commands.

## Coding Style & Naming Conventions

Follow `.clang-format` for C++ formatting: two spaces, no tabs, and an 80-column limit. Keep headers and implementations with their owning module, and use descriptive names consistent with nearby code (types in PascalCase, functions and variables in the existing lower/camel-case style). Prefer updating the relevant module's CMake target when adding source or public headers. Use `clang-format -i path/to/file.cpp path/to/file.hpp` to format changed C++ files.

## Testing Guidelines

There is no configured framework, coverage requirement, or test naming convention yet. For changes, build the affected target with `cmake --build out/Debug`; add focused tests alongside a clearly configured test target when introducing test infrastructure. Do not treat standalone test-like files as automatically runnable.

## Commit & Pull Request Guidelines

The available Git history uses short, informal subjects (for example, `some changes`), so no strict commit convention is established. Prefer a concise imperative subject that names the area changed. Pull requests should explain behavior and motivation, list build or test checks performed, link related issues when applicable, and include screenshots only for user-visible changes.

## Configuration Notes

Keep generated files in `out/` and avoid committing local build artifacts or `compile_commands.json`; these are ignored. Dependencies and development tools are declared in `flake.nix`.
