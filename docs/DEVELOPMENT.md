# Разработка

Документ описывает, как собрать проект, где менять код и как проверить, что
изменение не сломало соседний слой.

## Требования и сборка

Проект использует C++23 и CMake 3.25+. Зависимости объявлены в `flake.nix`.
Обычный Linux workflow:

```sh
nix develop
cmake -S . -B out/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build out/Debug -j2
```

Собирать отдельную цель быстрее:

```sh
cmake --build out/Debug --target api -j2
cmake --build out/Debug --target aniparser_server -j2
cmake --build out/Debug --target aniparser_tui -j2
cmake --build out/Debug --target my_app -j2
```

`aniparser_tui` добавляется только на Linux. `aniparser_server` и `my_app`
собираются на desktop-платформах согласно условиям CMake.

## Исполняемые программы

```sh
# Запросить Anime 771 (или указанный ID) и напечатать полную API-модель.
out/Debug/src/app/my_app 771

# Автономные проверки Kodik URL и Anime-to-players сериализации.
out/Debug/tests/video_serialization_checks

# Запустить сервер (endpoint — первый аргумент).
out/Debug/src/server/aniparser_server /tmp/aniparser.sock

# Подключить TUI ко второму терминалу.
out/Debug/tui/aniparser_tui /tmp/aniparser.sock
```

`video_serialization_checks` работает автономно. `my_app [id]` обращается
к внешнему API. Проверяйте доступность DNS, API и CDN отдельно от сборки.
C++-проверки используют GoogleTest. Их автоматически регистрирует CTest.

## Где менять код

| Изменение | Начните здесь | После изменения проверьте |
| --- | --- | --- |
| Новое API поле/модель | `src/api/models.def`, `src/api/json.hpp` | API справочник, `my_app`, IPC потребители при необходимости |
| Новый API endpoint | `src/api/api.hpp`, `src/api/api.cpp` или отдельный provider source | `ApiError`, caller, protocol/client docs и сборка |
| Кэш/TTL/coalescing | `src/core/core.hpp`, `src/core/cache/`, `src/core/{anime_service,search_feed}.cpp` | callbacks, mutex, API failure, CORE/CACHE docs |
| Новый IPC method/поле | `src/server/server.*`, `src/server/ipc/protocol.hpp`, `src/server/ipc/request_validation.hpp` | protocol docs, оба transport-а, клиенты |
| Новый player extractor | `video_extractor.hpp`, отдельный extractor, `video.cpp` registry | spoofed host cases, API и TUI video flow |
| Экран/клавиши TUI | `tui/tui_app.cpp` | IPC response/event, resize/scroll, `tui/README.md` |
| Clipboard | `tui/clipboard.*` | Wayland/X11 наличие утилиты и ошибка запуска |

## Порядок добавления IPC-функции

1. Добавьте операцию на уровне API/Core/Server согласно ответственности.
2. Добавьте общий обработчик и проверку полей в `ipc/protocol.hpp` и
   `ipc/request_validation.hpp`. Проверьте оба транспорта.
3. Определите точный response shape, ID и поведение при API ошибке.
4. Обновите `INTEGRATION.md` и описание реализации в `docs/SERVER.md`.
5. Обновите TUI или другой клиент.
6. Соберите затронутые цели. Используйте `video_serialization_checks` для автономной
   логики; сетевые проверки запускайте отдельно.

Unix и Windows transports должны сохранять один wire format. TUI не должен
включать `core.hpp`, держать ссылки на Core или пытаться читать состояние
серверного кэша напрямую.

## Стиль и отладка

- Формат: `.clang-format`, два пробела, без табов, около 80 колонок.
- После изменения форматируйте C++: `clang-format -i path/to/file.cpp`.
- Build tree и генерируемые файлы держите в `out/`.
- Проверка diff: `git diff --check`.
- IPC сервер печатает команды и IPC errors в терминал.
- API ошибки Core пишутся в stderr. Anime error сопровождается `curl` для
  воспроизведения API-запроса.
- TUI search/anime диагностические логи находятся в `/tmp`; точные имена файлов
  смотрите по вызовам `log_line` в `tui/tui_app.cpp`.

## Известные особенности текущей реализации

- Кэш Anime/Search/Feed не персистентный и сбрасывается при перезапуске;
  история, избранное, четыре статуса и позиция просмотра сохраняются через
  `SaveManager` в `history.json` (формат версии 4).
- TTL — пять минут; API и Kodik соединения могут завершиться сетевой ошибкой.
- Search/Feed refresh failure логируется и завершает ожидание событиями
  `search.updated`/`feed.updated` с `ok:false`, `error`, `status`.
- Anime IPC преобразует `videos` в `players`; внешняя API модель при этом не
  меняется.
- Kodik и Aksor зарегистрированы в video extractor registry. AniLibria вызывается
  отдельно через `getAnilibriaVideos(anime_id)`: метод читает alias из Anime-данных
  и возвращает эпизоды с доступными HLS-ссылками. TUI показывает AniLibria как
  дополнительный источник рядом с iframe-плеерами.
- Wire protocol имеет версию 3. Если существующие сообщения станут
  несовместимы для клиента, измените `protocol` в общем обработчике
  `hello`, проверку версии у клиентов и документацию.

## Автономные тесты

Для сборки тестов нужен GoogleTest (пакет `gtest` в `nix develop`).
После обычной сборки из корня репозитория:

```sh
ctest --test-dir out/Debug --output-on-failure
ctest --test-dir out/Debug -L offline --output-on-failure
```

Метка `offline` включает Core, валидацию IPC и сериализацию видео.
Метка `ipc` включает проверки сервера и TUI-клиента на Linux с локальными
Unix-сокетами. Среди них доставка событий двум клиентам, отключение одного
клиента и чтение сохранённого избранного после перезапуска сервера.
CTest запускает каждый сценарий GoogleTest отдельно. Для сборки без тестовых
целей передайте `-DBUILD_TESTING=OFF` при конфигурации CMake.

## Дополнительные интеграционные сценарии

```sh
python3 tests/ipc_regression.py
```

`ipc_regression.py` запускает fixture server и обычный сервер с отдельными
данными. Ему нужны локальные Unix-сокеты. Скрипт также запускает
`tui_client_checks`, если цель собрана. Артефакты хранятся в `out/`.
Для ручного UI без внешнего API: `aniparser_checks --fixture-server ENDPOINT`.
Этот режим тестового executable не является production provider.

Полный TUI smoke: `python3 tests/tui_smoke.py` (настоящий Notcurses на PTY,
mock IPC, cold success/error, UTF-8, навигация и resize). Для instrumented
сборки используйте `--build out/Sanitize --sanitizers`; signal stack settings
и пределы анализа описаны в docs/audit/REMEDIATION_STATUS.md.
