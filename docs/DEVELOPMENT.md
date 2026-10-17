# Сборка, ограничения и сопровождение

## Сборка и исполняемые цели

В Linux `flake.nix` объявляет dev shell с CMake, Ninja, GCC, Boost, nlohmann-json, CPR, curl, ncurses/notcurses, OpenSSL и pkg-config. Базовые команды:

```sh
nix develop
cmake -S . -B out/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build out/Debug -j2
```

Требуется CMake 3.25+ и C++23. Основные библиотеки: `common` (заголовочные типы), `api`, `cache`, `core`, `server`. На UNIX desktop добавляется `my_app`, кроме Android/iOS; `aniparser_server` добавляется, если не Android/iOS. Сборка cache на Linux проверяет, доступен ли `shm_open` из libc, и только при необходимости линкует `rt`. Форматирование C++ задаётся `.clang-format`: два пробела, ширина 80 колонок. Универсального тестового target и `ctest` набора нет; `my_app` запускает сценарий проверки API/cache, а `src/api/test.cpp` отдельно не включён как тестовая цель.

Пример запуска backend:

```sh
out/Debug/src/server/aniparser_server aniparser /tmp/aniparser.sock
```

`src/app/main.cpp` демонстрирует прямой Core API: тестирует полную загрузку ID 771 и 4958, печатает имя/статистику heap, асинхронность callback, ищет «наруто», получает первые три Anime и сценарий feed. Это ручная интеграционная программа и требует доступа к API.

### Файлы ключевых реализаций

| Файл | Что реализует |
| --- | --- |
| `src/api/api.hpp`, `api.cpp` | Публичные HTTP-вызовы, параметры CPR, синхронные и асинхронные методы, разбор транспортного/HTTP ответа. |
| `src/api/models.def`, `models.hpp`, `json.hpp` | Описание моделей, макросное создание структур и преобразователей JSON. |
| `src/common/types.hpp` | Общие `SearchInput` поля и типы параметров поиска. |
| `src/cache/Anime.hpp` | Фиксированная shared Anime запись и рекурсивное освобождение её динамических полей. |
| `src/cache/SharedString.hpp`, `SharedVector.hpp` | Дескрипторы строк и векторов, чьи данные размещаются в VariableHeap. |
| `src/cache/SegmentAllocator.hpp` | Формат сегмента/блока, проверки, first-fit allocation, split/coalesce и статистика. |
| `src/cache/HeapManager.hpp/.cpp` | Создание, открытие, refresh и отображение нумерованных сегментов heap-а. |
| `src/cache/AnimeHeap.cpp`, `VariableHeap.cpp`, `SearchHeap.cpp`, `FeedHeap.cpp` | Типизированные heap wrappers и операции над Anime/Search/Feed. |
| `src/cache/cache.hpp/.cpp` | Глобальная композиция heap-ов, Anime refcount, ID lookup и LRU-подобное удаление по времени использования. |
| `src/core/core.hpp/.cpp` | Модельные конвертеры, Core API, асинхронные задачи, process caches и maintenance. |
| `src/server/server.hpp/.cpp` | Тонкая адаптация указателей Core и shared handles к серверным callback контрактам. |
| `src/server/ipc_server.cpp` | POSIX socket transport, framing, JSON routing, client threads и cleanup socket endpoint. |
| `src/server/ipc_server_windows.cpp` | Windows Named Pipe transport с теми же методами протокола. |
| `src/server/main.cpp` | Standalone bootstrap: выбор provider, Cache/Core, IPC, остановка процесса. |
| `src/app/main.cpp` | Ручные интеграционные сценарии Core и диагностика heap usage. |

В сборке `cache` провайдер выбирается CMake-условиями: Windows → `WindowsShmSegmentProvider`, Android/iOS → `FileShmSegmentProvider`, UNIX → `PosixShmSegmentProvider`. Для IPC выбирается Windows реализация только на WIN32, иначе POSIX socket реализация. `src/server/main.cpp` собирается только если не Android/iOS. При встраивании в приложение нужно самому создать provider с подходящей директорией и запустить `Core`, `Server`, `IpcServer` в подходящем lifecycle приложения.

## Ограничения текущей реализации

- Поиск Anime по ID — линейный проход всех занятых блоков AnimeHeap; отдельного persistent ID-индекса нет.
- Search/Feed API-модель кешируется в RAM и не переживает перезапуск Core. Shared heaps не содержат полный независимый source-of-truth для этих ответов.
- Async обновление Anime меняет содержимое записи на месте. Пока запись обновляется, строки/векторы могут перейти на новые блоки; в Anime нет snapshot/version guard для читателей. Клиенту следует синхронизировать чтение с callback/event обновления.
- У handles нет ACL и учёта владельца по клиенту. POSIX socket имеет режим 0600; Windows named pipe блокирует удалённых клиентов, а файловые права определяются каталогом.
- Shared spin lock не восстанавливается автоматически, если процесс аварийно завершился, удерживая его.
- Shared structs используют нативный layout C++/атомики. Формат рассчитан на совместимые процессы одной машины, а не на разные архитектуры, endianness или произвольные версии бинарников.
- Обычное закрытие mappings не удаляет POSIX shared-memory objects и не удаляет backing files. Сброс кеша требует отдельной административной очистки при остановленных клиентах.

## Практические правила изменения кода

1. Новые `.cpp` включайте в `CMakeLists.txt` соответствующего модуля, public headers добавляйте в FILE_SET.
2. Общие API-модели описывайте в `models.def`; search-параметры поддерживайте согласованными в JSON IPC-парсерах POSIX и Windows.
   После изменения `models.def` обновите [каталог API-моделей](API_MODELS.md), включая новые поля и JSON aliases.
3. При изменении Anime/SharedSearch/SharedFeed или allocator header учитывайте межпроцессную ABI-совместимость, layout version `v8`, ответ `hello` и реализацию чтения клиента.
4. Для нового shared-memory поля выбирайте trivially-copyable дескриптор/handle; строки и динамические массивы размещайте через VariableHeap.
5. Каждому `retain` должен соответствовать ровно один `release`. На ошибочных путях освобождайте уже выделенные вложенные строки/векторы и references.
6. Не держите registry lock во время callback или сетевого запроса. Callbacks могут выполнять пользовательский код и обращаться к другим heaps.
7. Следуйте `.clang-format`; форматируйте только изменённые C++-файлы. Build output и локальные generated файлы держите в `out/`.
