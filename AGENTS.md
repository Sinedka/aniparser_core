# Development instructions

## Scope and layout

- Keep changes limited to the task. Do not perform unrelated refactoring,
  renaming, or repository-wide formatting. Preserve existing user changes.
- `src/api/`: HTTP clients, API models, JSON conversion, video extractors.
- `src/core/`: asynchronous requests, refresh policy, subscriptions, collections.
  `src/core/cache/`: shared AnimeCache, ID collections, SaveManager.
- `src/server/`: Core facade and IPC serialization; `ipc/` implements Unix
  sockets and Windows named pipes.
- `src/app/`: manual API executable and offline checks.
- `tui/`: standalone Linux IPC client using Notcurses; never include backend
  headers, link backend libraries, or read server storage here.
- Use `DOCS.md` to find Russian documentation. Read `src/server/IPC.md` before
  protocol changes and `docs/CORE.md` / `docs/CACHE.md` before cache changes.

## Build and verification

CMake requires version 3.25+ and C++23. Required libraries are CPR and
nlohmann-json; Linux TUI also requires pkg-config and notcurses-core. `flake.nix`
declares the Linux development environment.

Run from the repository root:

```sh
nix develop
cmake -S . -B out/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build out/Debug -j2
out/Debug/src/app/my_app --self-test
git diff --check
```

- For a focused build, use
  `cmake --build out/Debug --target aniparser_server -j2`; other targets include
  `api`, `aniparser_cache`, `core`, `server`, `my_app`, and `aniparser_tui`.
- `my_app` is enabled on Unix except Android/iOS; `aniparser_server` is enabled
  except Android/iOS; `aniparser_tui` is Linux-only.
- No CTest or unit-test framework is configured. `--self-test` checks URL
  recognition and Anime IPC serialization without network access.
- `out/Debug/src/app/my_app --test-get-anime` calls the live API for ID 771.
  Normal `my_app [id]` execution also uses the network. Report network checks
  separately from offline checks; a successful build does not verify providers.
- Build affected targets after code changes. For visible TUI changes, manually
  check the affected screen, keys, scrolling, and resize behavior with a server;
  include screenshots when submitting a PR.
- Report commands run, results, and platform/manual checks not performed.
  Keep build output and generated artifacts under ignored `out/`.

## Architecture and compatibility

- Keep HTTP/provider logic in API, cache/refresh/persistence policy in Core,
  and request validation and wire handling in Server/IPC.
- Pass owning values across module/process boundaries. IPC sends complete JSON
  values, never server addresses or cache handles.
- Search pages store ordered IDs; AnimeCache owns shared card data. Preserve
  refresh coalescing and the distinction between partial cards and full details.
  Partial merge ignores null, empty values, and numeric zero; inspect
  `anime_cache.cpp` before changing defaults or merge behavior.
- Keep Core state under its mutex. Invoke callbacks and submit potentially
  blocking TaskPool work outside that mutex. Preserve worker shutdown/join before
  Core state destruction and connection-safe callback lifetimes.
- Keep collection saves serialized through `collection_save_mutex_` and
  SaveManager. Preserve atomic file replacement and reading history formats
  1–4; the current writer uses version 4. For manual persistence checks, set
  `ANIPARSER_DATA_DIR` to a disposable directory under `out/`.
- Planned, watched, completed, and dropped are mutually exclusive; history and
  favorites remain independent. Preserve documented status numbers, collection
  limits, and paginated snapshot `revision` / `sequence` semantics.
- IPC uses JSON Lines, protocol version 3. Echo request IDs in responses and
  updates; validate payloads and retain `ok:false` / `error` responses.
  Preserve subscription cancellation and disconnect cleanup.
- For protocol changes, update handlers in both `ipc/unix.cpp` and
  `ipc/windows.cpp`, `src/server/IPC.md`, `docs/SERVER.md`, and affected clients.
  For incompatible changes, update both `hello` versions and the TUI version
  check together. Linux builds do not compile the Windows transport.
- `Server::animeToJson` converts API `videos` to `players → dubbers → episodes`.
  Preserve this separate wire representation when changing API models.
- Edit model fields in `src/api/models.def`; `models.hpp` and `json.hpp` expand
  that schema. Preserve JSON names via `YANI_FIELD_JSON`; check missing/null and
  numeric-string conversions. Update `docs/API_MODELS.md` when fields change.
- Add iframe providers through `VideoExtractor` and the `video.cpp` registry.
  AniLibria uses the separate `getAnilibriaVideos` path.

## Style, errors, and change checks

- Follow `.clang-format` settings: LLVM base, two spaces, no tabs, 80 columns.
  Run `clang-format -i` on changed C++ files only. Match the owning module's
  naming; public camelCase methods and private snake_case helpers coexist.
- Register new compiled sources and public headers in the owning CMake target.
- Follow API `yani::Result<T>` (`std::expected<T, ApiError>`) error handling;
  retain HTTP status, message, and response data where available. Keep failures
  from escaping worker/callback boundaries and terminating later requests.
- Backend diagnostics use tagged stderr output. Anime/Search/Feed refresh
  failures reach terminal IPC events. Collection metadata errors carry
  reason="error" and refreshing=false. Preserve the last successful cache value.
- Treat `.clang-format` values as authoritative over its explanatory comments.
  Its `Standard: c++20` is a formatter setting; CMake selects C++23 for builds.

## Reliability rules derived from the audit

- Audit map, evidence, coverage, and the staged repair plan are in
  `docs/audit/PROJECT_OVERVIEW.md`, `FINDINGS.md`, `COVERAGE.md`, and
  `REMEDIATION_PLAN.md`. Audit findings describe current limitations; they do
  not authorize unrelated fixes or a broad redesign.
- Invoke user callbacks outside all state and persistence locks, not merely
  outside `Core::mutex_`. Preserve notification ordering when releasing locks.
  Check reentrant and throwing initial callbacks; every updating flag must
  have a completion or rollback path.
- TaskPool submission rejects saturated/stopping queues without blocking.
  Handle rejected work explicitly, reset refresh flags, and deliver terminal
  errors. Keep I/O cancellation independent of a client's reads.
- Keep socket/pipe descriptor ownership explicit. Coordinate interruption,
  writes, close, and worker joins; an atomic stop flag does not protect other
  shared variables. Reap completed connection workers during normal operation.
  Do not copy owning descriptor wrappers or containers with iterator indexes
  without defining correct ownership/copy semantics.
- Distinguish missing storage from corrupt, unsupported, or unreadable state.
  Never silently replace an unreadable original with an empty state. Report
  persistence failures through the operation result; specify whether memory
  changed. Atomic replacement and power-loss durability are separate guarantees.
- Validate JSON numeric types and ranges before conversions. Missing/null
  model sentinels are not known partial values. Preserve full details while
  updating shared card fields, and account for `need_videos` completeness.
- TUI must handle null/loading/error/disconnected snapshots. Full snapshots
  replace prior state; revalidate the entire player/dubber/episode selection
  after updates. Use RAII for Notcurses and subprocess/descriptor resources.
- For concurrency, persistence, model conversion, or IPC fixes, add a focused
  offline regression scenario including failure paths. Run sanitizers on
  affected code when available; report instrumentation and platform limits.
  Windows transport changes require a Windows compile check: Linux omits it.
  `src/api/test.cpp` is legacy duplicated model code, not a runnable test suite;
  empty `cmake/*` helper files do not enable sanitizers or warnings.

- Run `out/Debug/src/app/aniparser_checks` and, on Linux with local sockets,
  `python3 tests/ipc_regression.py` for changes covered by the audit fixes.
  Use `Core::ApiBackend` fixtures for deterministic offline API failures.
  `CallbackQueue` must deliver the initial snapshot before queued completions;
  initial refreshing metadata is captured under the Core state lock.
- Collection mutations return `Result<bool>` and commit the durable candidate
  before publishing memory. Preserve error.data.committed for post-rename
  durability errors. Do not silently evict progress; new IDs are capped at
  10000, with explicit removeAnimeSaveData to free space.

- The text TUI uses notcurses_core_init and notcurses-core; do not reintroduce
  multimedia Notcurses/FFmpeg/SDL or CPR linkage without a feature requiring it.
