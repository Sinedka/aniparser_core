# Маппинг API в shared cache

## Подробный маппинг моделей в shared cache

API-модель богаче, чем shared-memory представление. В shared structs сохраняются поля, которые текущий Core умеет предоставить клиентам напрямую; остальные поля остаются только в локальной API-модели и после преобразования отбрасываются. Наличие поля в `models.def` не означает, что оно доступно через `Anime` или `SharedFeed`.

| Источник | Поля shared-записи | Что не переносится / особенность |
| --- | --- | --- |
| `AnimeResult` → `Anime` | ID, год, просмотры, сезон, эпизоды, счётчики комментариев/отзывов/постов, top; статус, URL, постер, рейтинг, заголовок, тип, описание, min_age, remote IDs, original; other_titles, creators, studios, videos, genres, viewing_order, translates, blocked_in, random_screenshots. | Переносятся данные, присутствующие в ответе конкретного endpoint; `need_videos` влияет на API-ответ, но не на формат shared записи. |
| `SearchResult` → частичный `Anime` | ID, сезон, год, views, top, статус, URL, description, min_age, poster, rating, remote IDs, title, type. | Нет видео, авторов, жанров, полной viewing order и прочих подробных полей. Новая запись заполняется один раз; существующая запись partial-ответом не перезаписывается. |
| `FeedAnimeItem` → частичный `Anime` | ID, year/views/season, средний рейтинг и counters, top, min_age, remote IDs, статус, URL, poster, title/type/description, blocked_in. | Не переносятся feed-поля `user` (личный рейтинг/список); feed-only remote ID поля сохраняются лишь если destination Anime их имеет. Новая запись заполняется один раз. |
| `FeedScheduleItem` → частичный `Anime` | ID, aired/count/next_date, title, description, anime_url и poster. | Нет рейтингов, статуса и прочих полей подробной модели. Заполнение выполняется только при создании слота. |
| `ViewingOrder` → отдельный `Anime` | ID, year/date, status, title, type, URL, description, poster. | Не копирует полное аниме, не имеет обратной связи и сейчас не объединяется автоматически с уже существующим слотом того же ID. |
| `FeedResponse` → `SharedFeed` | top_carousel season/year; списки `new`, `announcements`, `recommends`, carousel items, `schedule`; коллекции с id/title/description/animes. | `new_videos`, `last_watches`, `posts`, `blogger` и metadata коллекций (owner/likes/language/poster previews) в SharedFeed не представлены и IPC-клиенту через него недоступны. |

Преобразование реализовано явными helper-функциями в начале `src/core/core.cpp`: `store_status`, `store_poster`, `store_rating`, `store_type`, `store_min_age`, `store_remote_ids`, `store_creator`, `store_studio`, `store_video`, `store_genre`, `store_translate`, `store_viewing_order`, `store_screenshot`, `store_anime`, `store_search_result`, `store_feed_item`, `store_feed_schedule_item`. Это не универсальная сериализация. При добавлении API-поля нужно решить, требуется ли оно в shared layout, добавить соответствующее поле и не забыть освободить его переменную память в `Anime::release_shared_storage()`.

## ABI Cache: структура за структурой

Типы ниже объявлены в `src/cache/`. Они являются форматом данных между процессами, а не обычными domain objects. Дескрипторы и handles содержат offset в heap; адреса в них не записываются.

### Адресация и отображённые сегменты (`types.hpp`)

| Тип | Поля | Смысл |
| --- | --- | --- |
| `shm::HeapHandle` | `uint32_t segment_id`, `uint64_t offset` | ID heap-сегмента и смещение payload. ID 0 — invalid; bool conversion проверяет только segment ID. |
| `shm::HeapStatistics` | `segment_bytes`, `allocated_bytes`, `free_bytes`, `allocation_count` | Суммарная статистика heap-а. Байты allocated/free считаются по payload и не включают заголовки блоков/сегмента. |
| `shm::HeapSegment` | `id`, `size`, `base` | Локальная запись отображения. `base` — адрес текущего процесса; этот тип нельзя хранить как shared reference или передавать клиенту. |

### `Anime` и вложенные записи (`Anime.hpp`)

`Anime` — фиксированный объект AnimeHeap. Вложенные типы ниже — не отдельные allocations сами по себе; vector-элементы этих типов размещаются VariableHeap, а их `SharedString` поля указывают на дополнительные блоки.

| Вложенный тип | Поля | Роль |
| --- | --- | --- |
| `Anime::AnimeStatus` | `title`, `class_`, `alias`: `SharedString`; `value`: `int64_t` | Статус API; JSON `class` хранится как C++ `class_`. |
| `Anime::Poster` | `big`, `fullsize`, `huge`, `medium`, `small`: `SharedString` | URL вариантов постера. |
| `Anime::Rating` | `average`, `kp_rating`, `anidub_rating`, `myanimelist_rating`, `shikimori_rating`, `worldart_rating`: `double`; `counters`: `int64_t` | Оценки и число оценок. |
| `Anime::AnimeType` | `name`, `shortname`: `SharedString`; `value`: `int64_t` | Тип аниме. |
| `Anime::MinAge` | `value`: `int64_t`; `title`, `titleLong`: `SharedString` | Возрастная категория; `titleLong` заполняется из API `titleLong` или feed `title_long`. |
| `Anime::RemoteIds` | `worldart_id`, `kp_id`, `anidb_id`, `anidub_id`, `sr_id`, `shikimori_id`, `myanimelist_id`: `int64_t`; `worldart_type`, `anilibria_alias`: `SharedString` | Внешние каталоги и псевдонимы. Обычный API `RemoteIds` не имеет anidub_id; он есть в feed model и хранится в destination. |
| `Anime::Creator`, `Anime::Studio` | `title`, `url`: `SharedString`; `id`: `int64_t` | Участник/студия; `release(heap)` освобождает две строки. |
| `Anime::SkipData` | `time`, `length`: `SharedString` | Границы opening/ending skip-маркера. |
| `Anime::Skips` | `opening`, `ending`: `SkipData` | Skip-информация для видео. |
| `Anime::VideoInfo` | `video_id`, `date`, `index`: `int64_t`; `iframe_url`, `number`, `data.dubbing`, `data.player`: `SharedString`; `skips`: `Skips` | Данные видео и вложенные строки плеера/озвучки/таймингов. `data` — анонимная nested struct из двух SharedString. В `release()` проходят все эти строки. |
| `Anime::Genre` | `title`, `alias`, `url`: `SharedString`; `id`: `int64_t` | Жанр и его идентификаторы. |
| `Anime::Translates` | `title`, `href`, `value`: `SharedString` | Элемент перевода/локализации. |
| `Anime::Episodes` | `aired`, `count`, `next_date`: `int64_t` | Текущие/ожидаемые эпизоды. |
| `Anime::RandomScreenshot` | `sizes.small`, `sizes.full`: `SharedString`; `id`, `time`, `episode`: `int64_t` | `sizes` — анонимная nested struct. Скриншот и его варианты. |
| `Anime::Top` | `category`, `global`: `int64_t` | Позиции в категориях и глобальном рейтинге. |

Поля верхнего уровня `Anime`:

| Поля | Тип/назначение |
| --- | --- |
| `anime_id`, `year`, `date`, `views`, `season`: `int64_t` | Идентичность и базовые характеристики. `date` особенно используется для viewing-order записи. |
| `anime_status`: `AnimeStatus`; `poster`: `Poster`; `rating`: `Rating`; `type`: `AnimeType`; `min_age`: `MinAge`; `remote_ids`: `RemoteIds`; `episodes`: `Episodes`; `top`: `Top` | Вложенные scalar/shared-string группы, описанные выше. |
| `anime_url`, `title`, `description`, `original`: `SharedString` | Строковые поля верхнего уровня. |
| `other_titles`, `blocked_in`: `SharedVector<SharedString>` | Каждый элемент хранит собственный блок текста. |
| `creators`, `studios`, `videos`, `genres`, `translates`, `random_screenshots`: `SharedVector<T>` | Буферы соответствующих trivially-copyable элементов; строки каждого элемента освобождаются до освобождения буфера. |
| `viewing_order`: `SharedVector<HeapHandle>` | Handles child Anime записей; каждая связь удерживает refcount. |
| `comments_count`, `reviews_count`, `posts_count`: `int64_t` | Счётчики API. |
| `usage_count`: выровненный `uint64_t` | Ownership counter. `UINT64_MAX` — eviction claim sentinel; поле server-managed. |
| `last_used_ns`: `uint64_t` | Монотонная метка времени последнего использования для LRU-подобной очистки. |
| `self`: `shm::HeapHandle` | Handle данной записи, сохраняемый при обновлении payload. |

`Anime::release_shared_storage()` не освобождает Anime slot сам: он очищает строки/векторы в VariableHeap, освобождает child Anime refs, затем вызывающая сторона уничтожает запись и deallocate-ит блок AnimeHeap. Временный `Anime replacement` при обновлении имеет те же правила освобождения.

### Search и Feed записи

| Тип | Поле | Назначение |
| --- | --- | --- |
| `SharedSearch` | `self`, `root`, `continuation`: `shm::SearchHandle` (`HeapHandle`) | Собственная идентичность, корень цепочки и следующая страница. |
|  | `anime`: `std::array<shm::HeapHandle, 10>` | До 10 Anime handles. Записи удерживаются SearchHeap. |
|  | `anime_count`: `uint64_t` | Число валидных элементов в массиве, не превышает 10. |
|  | `last_used_ns`: `uint64_t` | Время последнего обращения к этой странице; root timestamp обновляется совместно. |
| `SharedFeedCollection` | `id`: `int64_t`; `title`, `description`: `shm::SharedString`; `anime`: `shm::SharedVector<shm::HeapHandle>` | Коллекция внутри Feed и список её Anime. |
| `SharedFeed` | `self`: `shm::HeapHandle` | Handle feed-записи. |
|  | `carousel_season`, `carousel_year`: `int64_t` | Параметры верхней карусели. |
|  | `new_anime`, `announcements`, `recommendations`, `carousel_items`, `schedule_items`: `shm::SharedVector<shm::HeapHandle>` | Списки ссылок на Anime. Название списка задаёт семантику элемента. |
|  | `collections`: `shm::SharedVector<SharedFeedCollection>` | Backing block находится в VariableHeap; вложенные строки и списки требуют рекурсивного освобождения. |

`static_assert` в заголовках запрещают добавлять к Anime/Search/Feed owning C++ members. Размещение новых полей изменяет binary layout: поднимайте cache layout version и протокольную `layout` версию, иначе клиент может неверно интерпретировать offsets полей.

### Дескрипторы VariableHeap

| Структура | Поля | Инвариант |
| --- | --- | --- |
| `shm::SharedString` | `segment_id`, `offset`, `size`, `capacity` | `size <= capacity`; invalid segment означает отсутствие буфера. `view()` возвращает не null-terminated `string_view`, длина задаётся size. |
| `shm::SharedVector<T>` | `segment_id`, `offset`, `size`, `capacity` | Блок содержит `capacity` элементов T, валидный диапазон — `[0,size)`. `T` должен быть trivially copyable и trivially destructible. |

Обе структуры одинаково кодируют HeapHandle, но владеют разными типами payload. `clear()` у SharedString освобождает allocation и сбрасывает дескриптор; `SharedVector::clear()` только устанавливает `size=0`, а для физического освобождения нужно вызвать `release()`. API общего интерфейса вроде автоматически управляемого C++ lifetime здесь нет: вызывающий код явно передаёт правильный heap.

## Allocator и registry структуры

Внутренние детали определены в `SegmentAllocator.hpp` и `HeapManager.hpp`:

| Тип | Состав/поведение |
| --- | --- |
| `detail::SegmentHeader` | `magic: atomic<uint64_t>`, `version/reserved: uint32_t`, `segment_size: uint64_t`, `lock/registry_lock: atomic_flag`. Живёт в начале каждого сегмента. `magic` публикуется release-store после инициализации первого свободного блока. |
| `detail::BlockHeader` | `payload_size: uint64_t`, `flags/reserved: uint32_t`; расположен перед каждым payload. `flags == 1` — allocated, `flags == 0` — free. Заголовок выровнен на 8 байт. |
| `detail::SegmentLock` | Ссылка `SegmentHeader&`; RAII spin-lock только для allocator metadata одного сегмента. Копирование запрещено. |
| `shm::HeapRegistryGuard` | Поле `atomic_flag*`; RAII wrapper, поддерживает move и явный `unlock()`. Registry lock берётся из segment 1 и защищает кросс-сегментные операции владения/перечисления. |
| `HeapManager` | `name: string`, `initial_segment_size_/next_segment_size_: size_t`, `provider_: IShmSegmentProvider*`, mutable `mutex`, mutable `deque<HeapSegment>`. В shared memory сам менеджер не хранится. |

Allocator segment version — `2`, cache heap name suffix — `v8`, IPC handshake layout — `8`: это три разных версии. Изменение внутреннего заголовка сегмента требует учитывать allocator `segment_version`; изменение структур клиентов — cache/IPC ABI; не следует автоматически считать числа взаимозаменяемыми.

## Объекты состояния процессов и provider

Эти структуры не являются публичной shared-memory ABI; они размещаются только в памяти backend процесса.

| Структура | Поля/роль |
| --- | --- |
| `yani::ApiError` | `status: int`, `message: std::string`, `data: nlohmann::json`; `Result<T>` — alias `std::expected<T, ApiError>`. |
| `Core::AnimeEntry` | `handle: HeapHandle`, `updating/loaded_full: bool`, `last_api_request: steady_clock::time_point`, `callbacks: vector<AnimeUpdateCallback>`; одна process-local запись на anime ID. |
| `Core::CachedSearchPage` | `last_requested_at: steady_clock::time_point`, `results: vector<SearchResult>` для точного page key. |
| `Core::CachedFeed` | `last_api_request: steady_clock::time_point`, `response: FeedResponse` для watches key. |
| `PosixShmSegmentProvider::Mapping` | `fd: int`, `base: void*`, `size/references: size_t`; повторное отображение одного shm name в процессе разделяет mapping и fd. |
| `FileShmSegmentProvider::Mapping` | `fd: int`, `base: void*`, `size/references: size_t`; backing объект — файл, отображённый `mmap`. |
| `WindowsShmSegmentProvider::Mapping` | `file/mapping/base: void*`, `size/references: size_t`; file handle, mapping handle и view освобождаются отдельно. |
| POSIX IPC `Client` | `write_mutex: mutex`, `fd: atomic<int>`; сериализует ответы от request и callback threads. `close()` меняет fd на -1, shutdown и close выполняются под mutex. |
| Windows IPC `Client` | `write_mutex: mutex`, `pipe: HANDLE`; JSON сообщения записываются циклом `WriteFile`, close отключает pipe и закрывает handle. |
| POSIX `IpcServer::Impl` | `server: Server&`, `socket_path/cache_name/shared_memory_directory: string`, `stopping: atomic<bool>`, `listener: int`, `accept_thread: thread`, `clients_mutex: mutex`, `clients: vector<shared_ptr<Client>>`, `client_threads: vector<thread>`. |
| Windows `IpcServer::Impl` | `server: Server&`, `pipe_name: wstring`, cache/shared directory: `string`, `stopping: atomic<bool>`, accept thread, clients mutex/vector и worker thread vector. |
| IPC request `Gate` | `mutex`, `condition_variable`, `sent: bool`; Anime handler заставляет callback дождаться отправки немедленного reply, чтобы event не пришёл раньше него. |

Дополнительные plain structs `Mapping`, `Client`, `Impl`, `Gate` объявлены в `.cpp` или приватных секциях заголовков; они не сериализуются и не должны включаться в layout клиента. Модельные структуры перечислены отдельно полностью в [API_MODELS.md](API_MODELS.md).

В `src/api/json.hpp` также объявлены `yani_null_value<T>` и специализации: общий шаблон возвращает `T{}`, `std::string` — пустую строку, `int64_t` — минимальное sentinel значение, `double` — `-1.0`, bool — `true`, vector — пустой вектор. Эти sentinel-значения являются текущей политикой null-конвертации, а не типобезопасным optional-представлением; проверяйте код модели перед трактовкой поля как реально присутствующего.

## Legacy и тестовые структуры

`src/cache/SharedHeap.hpp` — legacy single-segment `VariableHeap` с полями `memory_` и `size_`, константами magic/version/alignment и API `ptr/allocate/deallocate/getmemory`. Новый Cache использует `shm::VariableHeap` из `VariableHeap.hpp`; legacy класс не является тем же типом и не участвует в текущей композиции heaps. Не смешивайте их в документации API или коде нового функционала.

`src/api/test.cpp` содержит отдельные вручную объявленные копии API-моделей/парсера для проверки прежнего прототипа. Он не включён в `src/api/CMakeLists.txt` как executable или test target. Каноническая production-схема — `models.def`, каталог которой находится в [API_MODELS.md](API_MODELS.md).
