# Архитектура проекта

Эта страница описывает границы модулей и последовательность выполнения, не включая Linux-клиент `tui/`. Подробные схемы типов вынесены в [каталог API-моделей](API_MODELS.md) и [каталог shared-структур](STRUCTURES.md).

## Дерево проекта

| Путь | Содержимое и ответственность |
| --- | --- |
| `src/common/` | Типы, нужные нескольким библиотекам. Сейчас основной публичный тип — `yani::SearchInput`. |
| `src/api/` | CPR HTTP-клиент, модельные структуры, JSON-конвертеры; отдельный `test.cpp` не подключён как CMake test. |
| `src/cache/` | Хранилище shared memory, heap wrappers, структуры Anime/Search/Feed и платформенные segment providers. |
| `src/core/` | Преобразует API-модели в shared layout, организует callback/async работу и сроки кеширования. |
| `src/server/` | Адаптер Core и newline JSON IPC по Unix socket или Windows named pipe. |
| `src/app/` | `my_app`, ручной интеграционный сценарий для Core, кеша, search и feed. |
| `cmake/` | Повторно используемые фрагменты настроек compiler options, warnings и sanitizers. |
| `docs/` | Подробная документация по подсистемам и структурам. |
| `out/` | Обычное место для локальных CMake build trees; каталог игнорируется Git. |

Корневой `CMakeLists.txt` требует CMake 3.25, включает C++23, затем обходит `src/`. Linux также подключает `tui/`, который здесь не описан. Внутри `src/CMakeLists.txt` добавляются `common`, `api`, `cache`, `core`, `server`; `app` добавляется на UNIX кроме Android/iOS.

## Граф зависимостей

```text
                    +------> api -------> common
                    |
app ------> core ---+------> cache
              ^
              |
server -------+
```

`common` — header-only интерфейс. `api` зависит от common, nlohmann-json и CPR. `cache` зависит от потоков и платформенного provider. `core` зависит от API и cache. `server` зависит от core и выбирает один транспортный cpp. `my_app` использует core напрямую; серверная исполняемая цель `aniparser_server` связывает server и cache.

## Сквозной запрос Anime

### Прямой Core-вызов

`Core::getAnime(int64_t, callback)` возвращает указатель на shared Anime slot немедленно. Core резервирует/переиспользует запись, затем worker вызывает `yani::getAnimeAsync`, преобразует `AnimeResult` в поля Anime и вызывает callback. Callback выполняется вне `entries_mutex_`. Caller использует ту же запись по указателю/handle и затем release-ит свою ссылку. Эта перегрузка подходит приложениям, которые уже работают с C++ Core в том же процессе.

Другая перегрузка `Core::getAnime(string_view, need_videos)` синхронна и возвращает `expected<HeapHandle, ApiError>`. Её endpoint вызывается напрямую, она создаёт новую запись, не проходит через `anime_by_id_` refresh registry и не инициирует callback.

### Вызов через IPC

Путь сервера длиннее: `IpcServer` разбирает JSON, `Server::getAnime` вызывает Core, возвращает `anime->self` в немедленном reply. В Core callback Server преобразует `Anime*` в handle, а IpcServer пишет поздний `anime.updated`. Так IPC никогда не отправляет указатели. В POSIX реализации `Gate` не даёт callback событию обогнать initial reply: callback ждёт, пока сервер отправит reply, затем отправляет event.

### Search и Feed

Search/Feed имеют два уровня данных:

1. `yani::SearchResult` / `yani::FeedResponse` хранятся как обычные C++-объекты в локальных Core map-ах для 5-минутного API cache.
2. Для каждого обращения Core создаёт shared page/Feed snapshot в heaps. Эти записи содержат только fixed-layout значения, shared descriptors и handles.

Для stale cache Core выдаёт первый snapshot из старых моделей, затем обновляет API cache и передаёт второй snapshot. На IPC уровне первый callback становится обычным ответом, следующий — event. Клиент сам управляет временем жизни обоих handles.

## Разделение persistent и process-local данных

| Persistent в backing storage | Только в памяти процесса Core/Server |
| --- | --- |
| Heap segment headers/blocks. | `anime_by_id_`, его `loaded_full`, `updating`, callback queues, TTL timestamps. |
| Anime records, strings/vectors, Search pages и Feed records, пока они не deallocate-нуты. | Search cache `SearchInput page key -> vector<SearchResult>` и время API вызова. |
| Usage counts и last-use timestamps на shared Anime/Search записях. | Feed cache `watches key -> FeedResponse` и время API вызова. |
| Имена/ID сегментов, созданные provider-ом. | Server client sockets/pipe handles, worker threads и запросные correlation IDs. |

Поэтому после перезапуска ОС heap-файлы или POSIX shm могут остаться, но process-local API cache и состояние обновлений сбрасываются. Наличие Anime record в shared heap не сообщает новому Core, полная ли запись, когда она загружена или когда нужно её обновить; Core должен сверяться со своей локальной таблицей и при необходимости обращаться к API.

## Подсистемы подробнее

- Схемы API-типов и особенности JSON парсинга: [API](API.md), [API-модели](API_MODELS.md).
- Сегменты, аллокатор, providers и Cache ownership: [Cache](CACHE.md).
- Подробные callbacks, процессные кеши, TTL и cleanup: [Core](CORE.md).
- Команды, ответы и протокол клиента: [Server и IPC](SERVER.md), исходная шпаргалка [`src/server/IPC.md`](../src/server/IPC.md).
- Цели CMake, запуск и ограничения: [сборка и сопровождение](DEVELOPMENT.md).
