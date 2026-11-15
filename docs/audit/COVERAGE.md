# Покрытие, доказательства и ограничения аудита

> Исторический отчёт до исправлений: строки, hashes и результаты ниже относятся
> к исходному снимку аудита. Последующие изменения и актуальные проверки описаны
> в [REMEDIATION_STATUS.md](REMEDIATION_STATUS.md).


Дата: 13 ноября 2026. HEAD: `03a1e85efdd6eac2725f13f0dd0a80467f39da0d`.
Проверялось текущее рабочее дерево с пользовательскими изменениями, включая
три untracked provider-файла. Существующие изменения не откатывались.

## Что означает покрытие

Учтены все 70 файлов исходного проекта вне build/generated/VCS-каталогов.
Активные C++ исходники и заголовки рассмотрены по реализации и взаимодействиям;
CMake — по графу целей и платформенным условиям. Это не заявление, что проверены
все возможные входы, interleavings или все ОС. Legacy `src/api/test.cpp`
проверен структурно, не построчно как действующий модуль. Документация API models
сверена автоматически по всем 56 именам моделей и именам полей, но не с живой
схемой внешнего API. Все 56 разделов и поля присутствуют.

Обозначения: **S** — статический разбор активного кода; **C** — конфигурация;
**D** — документация/контракты; **L** — legacy, структурная проверка.
SHA-256 prefix фиксирует проверенную версию файла (для AGENTS — итоговую,
после добавления правил). Это позволяет заметить, что файл изменился после аудита.
Статус S не означает, что каждый метод исполнялся в тесте.

## Реестр файлов

| Файл | Покрытие | Проверенный аспект | SHA-256 prefix |
| --- | --- | --- | --- |
| `.clang-format` | C | активные значения, отличие комментариев от настроек | `24894b782952` |
| `.envrc` | C | use flake | `1d26c74fd25a` |
| `.gitignore` | C | out/generated/cache исключения | `c350a7e352cf` |
| `AGENTS.md` | D | существующие инструкции сохранены и дополнены правилами надёжности | `fa79fe93d14f` |
| `CMakeLists.txt` | C | source registration, link dependencies, platform conditions | `035c802bd554` |
| `DOCS.md` | D | назначение, команды и соответствие контрактам исходников | `d4d52c86d04f` |
| `README.md` | D | назначение, команды и соответствие контрактам исходников | `366dc5c677a2` |
| `cmake/CompilerOptions.cmake` | C | пустой файл; проверки/flags не включены | `e3b0c44298fc` |
| `cmake/Sanitizers.cmake` | C | пустой файл; проверки/flags не включены | `e3b0c44298fc` |
| `cmake/Warnings.cmake` | C | пустой файл; проверки/flags не включены | `e3b0c44298fc` |
| `docs/API.md` | D | назначение, команды и соответствие контрактам исходников | `f15ec1f6cf23` |
| `docs/API_MODELS.md` | D | 56 моделей/имена полей сверены со схемой; не с внешним API | `c294fea77ed0` |
| `docs/ARCHITECTURE.md` | D | назначение, команды и соответствие контрактам исходников | `dcf71daffa3d` |
| `docs/CACHE.md` | D | TTL/full/partial/limits, state persistence | `f05e9b1a3c7c` |
| `docs/CORE.md` | D | locks, snapshots, failures, pool, persistence contracts | `1ce79fef0b8c` |
| `docs/DEVELOPMENT.md` | D | назначение, команды и соответствие контрактам исходников | `bfdcd56b4d97` |
| `docs/SERVER.md` | D | назначение, команды и соответствие контрактам исходников | `758eebee7847` |
| `docs/STRUCTURES.md` | D | назначение, команды и соответствие контрактам исходников | `1b04f7b15642` |
| `flake.lock` | C | dependency pinning, без обновления или vulnerability scan | `c2b6e06a956d` |
| `flake.nix` | C | Linux devShell inputs и platform scope | `446626cef979` |
| `src/CMakeLists.txt` | C | source registration, link dependencies, platform conditions | `4c31e9b78ad9` |
| `src/api/CMakeLists.txt` | C | source registration, link dependencies, platform conditions | `94dde2f807b2` |
| `src/api/aksor.cpp` | S | authority/path validation, API JSON, quality fields | `07ec96d0262b` |
| `src/api/aksor.hpp` | S | объявления, ownership, соответствие реализации | `33401250b5ec` |
| `src/api/anilibria.cpp` | S | alias encoding, fallback, normalization, error status | `d4103a928724` |
| `src/api/api.cpp` | S | HTTP errors, sync/future, query encoding, timeouts | `0a9034c60a83` |
| `src/api/api.hpp` | S | объявления, ownership, соответствие реализации | `4c229c6f9e2d` |
| `src/api/json.hpp` | S | missing/null, numeric strings, sentinel defaults, exceptions | `56ee99f12ab8` |
| `src/api/kodik.cpp` | S | domain recognition, HTML/JS regex, decode, quality sort | `bed929add284` |
| `src/api/kodik.hpp` | S | объявления, ownership, соответствие реализации | `91b44657d772` |
| `src/api/models.def` | S | все 56 структур, wire names, вложенные типы | `dcbfd2e9c3ae` |
| `src/api/models.hpp` | S | macro expansion и owning fields | `5828b541597f` |
| `src/api/test.cpp` | L | не включён в CMake; дубликат моделей/JSON, не runner | `dcadecd0f59c` |
| `src/api/types.hpp` | S | объявления, ownership, соответствие реализации | `11f045d80905` |
| `src/api/video.cpp` | S | extractor registry, quality fallback, supports contract | `134c3c0f19ea` |
| `src/api/video_extractor.hpp` | S | объявления, ownership, соответствие реализации | `fbe5fb054762` |
| `src/app/CMakeLists.txt` | C | source registration, link dependencies, platform conditions | `125282297b44` |
| `src/app/main.cpp` | S | 10 offline checks, live ID 771, CLI errors | `b47f3754b6cf` |
| `src/core/CMakeLists.txt` | C | source registration, link dependencies, platform conditions | `b011fa7a73e4` |
| `src/core/anime_service.cpp` | S | coalescing, first callback, full/video completeness | `76615f85c65c` |
| `src/core/cache/CMakeLists.txt` | C | source registration, link dependencies, platform conditions | `561ec6e06396` |
| `src/core/cache/anime_cache.cpp` | S | Entry lifetime, cleanup, timestamps, sentinels, projection | `53cd8c04da0f` |
| `src/core/cache/anime_cache.hpp` | S | объявления, ownership, соответствие реализации | `e0e007dda85d` |
| `src/core/cache/anime_collection.cpp` | S | list/index инварианты, unique IDs, limits, load | `b480f4fd6cc3` |
| `src/core/cache/anime_collection.hpp` | S | объявления, ownership, соответствие реализации | `de16e17935ba` |
| `src/core/cache/history.hpp` | S | объявления, ownership, соответствие реализации | `a68a2bfb2070` |
| `src/core/cache/save_manager.cpp` | S | v1–4, invalid state, temporary file/replace, platform paths | `17d08d4928ce` |
| `src/core/cache/save_manager.hpp` | S | объявления, ownership, соответствие реализации | `d9dc36826ee3` |
| `src/core/core.cpp` | S | load/normalization, destruction, shutdown order | `2ab8b48f6850` |
| `src/core/core.hpp` | S | публичные контракты, поля состояния, locks, ownership | `6b08b6bd42c2` |
| `src/core/history.cpp` | S | все 6 коллекций, mutations, persistence locks, pagination, notifications | `4462e8dfcbb6` |
| `src/core/search_feed.cpp` | S | все search-key поля, snapshots, cache, callback failures | `4dd736637efc` |
| `src/core/task_pool.cpp` | S | bounded queue, blocking submit, worker errors/shutdown | `6fded1a62f29` |
| `src/server/CMakeLists.txt` | C | source registration, link dependencies, platform conditions | `5e4aac023bc1` |
| `src/server/IPC.md` | D | version 3, response/event/error, subscriptions, status numbers | `40aaf58923a8` |
| `src/server/ipc/CMakeLists.txt` | C | source registration, link dependencies, platform conditions | `f72211f6a392` |
| `src/server/ipc/unix.cpp` | S | все handlers, streams, cancellation, framing, writes, accept/stop | `ca67a3a1210d` |
| `src/server/ipc/windows.cpp` | S | все handlers статически, parity, helpers, pipe lifetime; без Windows build | `e6ca562af0ec` |
| `src/server/ipc_server.hpp` | S | объявления, ownership, соответствие реализации | `e4becd126f97` |
| `src/server/main.cpp` | S | lifetime order, endpoint, signal handler, stop | `0807b9d92028` |
| `src/server/server.cpp` | S | facade, player grouping, stable episode sorting, video paths | `1f15e27d1498` |
| `src/server/server.hpp` | S | объявления, ownership, соответствие реализации | `5331fdd326b2` |
| `tui/CMakeLists.txt` | C | source registration, link dependencies, platform conditions | `8e3211269a49` |
| `tui/README.md` | D | назначение, команды и соответствие контрактам исходников | `f200589ab389` |
| `tui/clipboard.cpp` | S | spawn argv/stdin, pipe/SIGPIPE handling, child reaping | `4ce3ca9e68fb` |
| `tui/clipboard.hpp` | S | объявления, ownership, соответствие реализации | `7993f796664e` |
| `tui/ipc_client.hpp` | S | request IDs, framing, read/write, EOF/deadline, resource ownership | `344b1ad3ba10` |
| `tui/main.cpp` | S | entrypoint и передача endpoint | `2eb2b2bc8e0e` |
| `tui/tui_app.cpp` | S | все экраны, navigation, snapshot/events, UTF-8, selection, cleanup | `c643cbf9067d` |
| `tui/tui_app.hpp` | S | объявления, ownership, соответствие реализации | `4971d0be413c` |

Исключены: `.git/`, `out/`, `.cache/`, `.direnv/`, generated
`compile_commands.json`; сторонних vendored source directories в учтённом
дереве нет. Новые пять файлов `docs/audit/` — результат аудита, в исходные 70
не включены. Installed CPR заголовки читались только для проверки использования
`GetCallback`/`AsyncWrapper`; внутренний код всех сторонних библиотек не аудирован.

## Сценарии взаимодействия

| Сценарий | Проверка | Ограничение |
| --- | --- | --- |
| Запуск/остановка сервера | статически + локальный Unix launch/stop | saturation/stop race не воспроизводились |
| Anime cold/partial/full/coalescing | статически, AnimeCache probe | без live HTTP и нагрузочного interleaving |
| Search/Feed cache и errors | статически, cache projection probe | без mock/live HTTP полного Core |
| История/favorites/status/progress | весь history.cpp + IPC error probes | полная матрица мутаций/подписок не исполнялась |
| Pagination/revisions/sequence | статический разбор + invalid paging IPC | reorder concurrency не тестировалась |
| Reentrant callback коллекции | реальный Core, 2s timeout | не TSan |
| Ошибка сохранения/restart | реальный Unix IPC и отдельный data path | не ENOSPC/rename fault injection |
| Миграция v1–4 | читатель и normalizer статически | fixtures всех версий не запускались |
| Video providers | исходники, URL/serialization self-test | сеть не запускалась |
| TUI cold loading/navigation/refresh | статически + isolated helper probe | ручной UI/TTY не выполнен |
| Clipboard | статически, argv без shell | Wayland/X11/spawn failures не исполнялись |
| Windows transport | весь код и список методов сверены | нет компиляции/запуска Windows |

## Выполненные команды и результаты

1. `git status --short`, `rg --files`, `wc -l`, `rg`, `sed`, `cat`, `nl`:
   инвентаризация и чтение исходников/документации. На старте уже изменены
   AGENTS, README, API/ARCHITECTURE/DEVELOPMENT/SERVER docs, provider/build/API
   headers, app, IPC/Server, TUI docs/source. Новые provider files уже существовали.
2. `cmake -S . -B out/Debug -DCMAKE_BUILD_TYPE=Debug` — exit 0.
3. `cmake --build out/Debug -j2` — exit 0, все цели, включая server и TUI.
   Использован существующий build tree; это incremental build, не clean rebuild.
   Среда уже содержала Nix зависимости, отдельный `nix develop` не запускался.
   CMake 3.31.6, доступный g++ 14.3.0; CPR 1.11.0, nlohmann-json 3.11.3.
4. `out/Debug/src/app/my_app --self-test` — exit 0, 10 PASS. Сеть не нужна.
5. Inline Python + `subprocess.Popen(aniparser_server)` + AF_UNIX request/restart:
   в sandbox создание listener не удалось; повтор с разрешённым локальным IPC
   завершился exit 0. Артефакты `out/audit-f2jw1dxq/`.
   `ANIPARSER_DATA_DIR` был обычным disposable файлом `not-a-directory`, а не
   настоящим каталогом пользователя. Вывод:

   ```text
   updateAnimeSaveData ID 42: ok=true, time=10
   getAnimeSaveData в том же процессе: объект time=10
   addPlanned anime_id=42.9: ok=true, anime_id=42
   subscribeHistory offset=-1, limit=-1:
     offset=18446744073709551615, limit=50
   getAnimeSaveData после restart: null
   ```

6. Inline C++ из stdin, linked с существующими core/cache/api libraries,
   `-std=c++23 -fsanitize=address,undefined` — compile exit 0. Артефакты
   `out/audit-cpp-1hyucuuf/core-probes`. Probe API:

   ```cpp
   AnimeCache cache;
   yani::SearchResult first;
   first.anime_id = 1;
   first.year = 2020;
   cache.storeSearchResults("a", {first});
   auto partial = nlohmann::json{{"anime_id", 1}, {"title", "partial"}}
                      .get<yani::SearchResult>();
   cache.storeSearchResults("b", {partial});
   // Получено cache.get(1)->year == INT64_MIN.

   cache.subscribe(2, {});
   yani::AnimeResult full;
   full.anime_id = 2;
   full.title = "old";
   full.year = 2020;
   cache.finishRefresh(2, full);
   yani::SearchResult fresh;
   fresh.anime_id = 2;
   fresh.title = "new";
   fresh.year = 2026;
   cache.storeSearchResults("c", {fresh});
   // searchResults({2}) всё ещё old/2020.
   ```

   Второй режим того же executable:

   ```cpp
   Core core;
   core.subscribeFavorites([&](auto, bool updated) {
     if (updated) core.removeFavorite(1);
   });
   core.addFavorite(1); // timeout 2s, воспроизведение AUD-002
   ```

   Сеть не использовалась: начальная favorites страница пуста, callback
   блокируется до scheduling metadata. Timeout завершил только тестовый child.
   Первоначальный sanitizer-run упёрся в запрет LeakSanitizer под ptrace;
   повтор `ASAN_OPTIONS=detect_leaks=0` завершился exit 0 с ожидаемыми
   логическими ошибками кэша. **Backend static libraries не пересобирались
   с instrumentation**: это ограниченная проверка harness, не полный ASan
   аудит backend и не доказательство отсутствия утечек.
7. Попытка harness с включением полного `tui_app.cpp` — compile exit 0, runtime
   завершился с `Failed loading SDL3 library`, exit -6. Поэтому это не успешная
   проверка TUI. Затем из исходника без изменений извлечена только функция
   `erase_last_utf8`, скомпилирована из stdin с JSON-пробой и ASan/UBSan:
   `out/audit-cpp-1hyucuuf/utf8-probe`. С `detect_leaks=0` exit 0:

   ```text
   remaining=1 byte=209
   [json.exception.type_error.306] cannot use value() with null
   ```

   Это подтверждает обработку «я» и null JSON; не проверяет настоящий экран.
8. Скриптовая сверка всех имён `method ==` Unix/Windows: одинаковые 42 уникальных
   имени, но найдено расхождение helper names (AUD-005). Проверены имена всех
   56 моделей и их полей в API_MODELS; отсутствующих имён не найдено. Это не
   автоматическая проверка всех типов/defaults или внешних API.
9. `git diff --check` — exit 0 до и после подготовки документов. Финально
   проверены ссылки, число findings, реестр файлов и разрешённые пути изменений.

## Не выполнено и что проверять дальше

- Полная Windows-сборка, macOS, Android/iOS и platform-specific signals/ACL.
  Windows compile дефект найден статически; runtime cancellation требует
  самостоятельного исследования после восстановления сборки.
- Полностью инструментированный ASan/UBSan backend, TSan, clang-tidy/cppcheck
  полный проход. `clang-tidy` доступен, но не запускался. Не объявлять обзор
  исчерпывающим поиском UB/UAF/data races.
- Stress slow-reader, reconnect resource exhaustion, saturated queue с четырьмя
  workers, fault injection allocation/thread creation, shutdown interleavings.
- Все версии persistence на fixtures, ENOSPC, short write/close/rename failures,
  crash/power-loss recovery, несколько процессов и symlink adversarial tests.
- Реальные yani.tv/Kodik/Aksor/AniLibria запросы, доступность DNS/CDN,
  redirects/SSRF, лимиты HTTP body, текущая совместимость HTML/JS/schema.
- Настоящий TUI сеанс с сервером: клавиши, scroll, resize, nested navigation,
  clipboard Wayland/X11, screenshots. В аудите UI не менялся.
- Построчная проверка legacy test.cpp как самостоятельной библиотеки (он не
  участвует в сборке), глубинный аудит внешних CPR/curl/JSON/Notcurses,
  dependency vulnerability/license scan.

Инвентаризация завершена; статический обзор активных модулей выполнен.
Динамическая и платформенная проверка остаётся ограниченной указанными выше
сценариями. Следующий этап — регрессионные проверки из REMEDIATION_PLAN,
затем исправления только по отдельному поручению владельца проекта.


## Дополнение после исправлений

Исходные hashes выше сохранены для исторического снимка. Статический разбор и
regression coverage новых файлов описаны в REMEDIATION_STATUS.md:

| Новый файл | Проверка |
| --- | --- |
| src/core/collection_changes.cpp | транзакции, rollback до rename, FIFO и reentrant callbacks |
| src/core/callback_queue.hpp | initial-before-update и coalesced refresh |
| src/server/ipc/protocol.hpp | общий dispatcher, оба транспорта; настоящий Unix IPC |
| src/server/ipc/request_validation.hpp | типы, диапазоны, invalid requests и 1 MiB framing |
| tui/state_utils.hpp | UTF-8 и bounded menu selection |
| src/app/regression_tests.cpp | 50 offline checks и fixture backend |
| src/app/tui_client_checks.cpp | 5 networking checks без backend |
| tests/ipc_regression.py | 12 групп Unix integration checks, Debug и ASan/UBSan |
| tests/tui_smoke.py | настоящий TUI, mock IPC, PTY, Debug и ASan/UBSan |

TUI после согласованного переключения использует notcurses-core без CPR/SDL.
LSan вне sandbox прошёл 50 checks; TSan повторно не запустился и вне sandbox.
Это обновляет прежние ограничения аудита, но не отменяет непроверенные Windows,
живые провайдеры, power-loss и другие платформы.
