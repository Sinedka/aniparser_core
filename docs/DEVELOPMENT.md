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
| Новый API endpoint | `src/api/api.hpp`, `src/api/api.cpp` | `ApiError`, Core caller и integration check |
| Кэш/TTL/coalescing | `src/core/core.hpp`, `src/core/core.cpp` | callbacks, mutex, API failure, CORE/CACHE docs |
| Новый IPC method/поле | оба `ipc_server*.cpp`, `server.*` | protocol docs, Unix и Windows transport, клиенты |
| Новый player extractor | `video_extractor.hpp`, отдельный extractor, `video.cpp` registry | spoofed host cases, API и TUI video flow |
| Экран/клавиши TUI | `tui/tui_app.cpp` | IPC response/event, resize/scroll, `tui/README.md` |
| Clipboard | `tui/clipboard.*` | Wayland/X11 наличие утилиты и ошибка запуска |

## Порядок добавления IPC-функции

1. Добавьте операцию на уровне API/Core/Server согласно ответственности.
2. Обработайте метод и проверьте обязательные JSON поля в
   `ipc_server.cpp` **и** `ipc_server_windows.cpp`.
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

- Кэш не персистентный и сбрасывается при перезапуске.
- TTL — пять минут; API и Kodik соединения могут завершиться сетевой ошибкой.
- Search/Feed refresh failure сейчас логируется сервером, но клиент не получает
  отдельного error event.
- Anime IPC преобразует `videos` в `players`; внешняя API модель при этом не
  меняется.
- Только Kodik зарегистрирован в video extractor registry.
- Wire protocol имеет версию 2. Если существующие сообщения станут
  несовместимы для клиента, синхронно измените `protocol` в `hello` на обоих
  transport-ах и обновите клиентскую проверку версии и документацию.
