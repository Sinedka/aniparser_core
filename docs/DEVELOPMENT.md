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

# Сетевой ручной тест, проверяет getAnime(771, true).
out/Debug/src/app/my_app --test-get-anime

# Автономные проверки Kodik URL и Anime-to-players сериализации.
out/Debug/src/app/my_app --self-test

# Запустить сервер (endpoint — первый аргумент).
out/Debug/src/server/aniparser_server /tmp/aniparser.sock

# Подключить TUI ко второму терминалу.
out/Debug/tui/aniparser_tui /tmp/aniparser.sock
```

`my_app --self-test` не требует сети. `--test-get-anime` и обычный запуск
обращаются к живому внешнему API; это integration check, а не hermetic unit
test. В проекте нет настроенного CTest/GoogleTest suite. Успешный build не
проверяет доступность DNS/API или Kodik CDN.

## Где менять код

| Изменение | Начните здесь | После изменения проверьте |
| --- | --- | --- |
| Новое API поле/модель | `src/api/models.def`, `src/api/json.hpp` | API справочник, `my_app`, IPC потребители при необходимости |
| Новый API endpoint | `src/api/api.hpp`, `src/api/api.cpp` или отдельный provider source | `ApiError`, caller, protocol/client docs и сборка |
| Кэш/TTL/coalescing | `src/core/core.hpp`, `src/core/cache/`, `src/core/{anime_service,search_feed}.cpp` | callbacks, mutex, API failure, CORE/CACHE docs |
| Новый IPC method/поле | `src/server/server.*`, `src/server/ipc/unix.cpp`, `src/server/ipc/windows.cpp` | protocol docs, оба transport-а, клиенты |
| Новый player extractor | `video_extractor.hpp`, отдельный extractor, `video.cpp` registry | spoofed host cases, API и TUI video flow |
| Экран/клавиши TUI | `tui/tui_app.cpp` | IPC response/event, resize/scroll, `tui/README.md` |
| Clipboard | `tui/clipboard.*` | Wayland/X11 наличие утилиты и ошибка запуска |

## Порядок добавления IPC-функции

1. Добавьте операцию на уровне API/Core/Server согласно ответственности.
2. Обработайте метод и проверьте обязательные JSON поля в
   `ipc/unix.cpp` **и** `ipc/windows.cpp`.
3. Определите точный response shape, ID и поведение при API ошибке.
4. Обновите `src/server/IPC.md`, `docs/SERVER.md` и README, если это пример,
   который должен знать каждый клиент.
5. Обновите TUI или другой клиент.
6. Соберите затронутые цели. Используйте `my_app --self-test` для автономной
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
  несовместимы для клиента, синхронно измените `protocol` в `hello` на обоих
  transport-ах и обновите клиентскую проверку версии и документацию.

## Регрессионные проверки после аудита

```sh
out/Debug/src/app/aniparser_checks
python3 tests/ipc_regression.py
```

Первый executable проверяет кэш, транзакции, версии сохранений, лимит прогресса,
callbacks, очередь и UTF-8 без сети. Второй тест запускает fixture server и
обычный сервер с disposable data paths; ему нужны локальные Unix sockets.
На Linux дополнительно запускается `tui_client_checks` (deadline, cancellation,
framing, EOF), без Notcurses/backend linkage. Все artifacts сохраняются в out/.
Для ручного UI без внешнего API: `aniparser_checks --fixture-server ENDPOINT`.
Этот режим тестового executable не является production provider.

Полный TUI smoke: `python3 tests/tui_smoke.py` (настоящий Notcurses на PTY,
mock IPC, cold success/error, UTF-8, навигация и resize). Для instrumented
сборки используйте `--build out/Sanitize --sanitizers`; signal stack settings
и пределы анализа описаны в docs/audit/REMEDIATION_STATUS.md.
