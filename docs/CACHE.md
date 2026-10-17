# Cache и разделяемая память

## Cache и разделяемая память

### Состав heaps

`Cache` содержит статические экземпляры четырёх heap-ов. К имени кеша добавляется суффикс раскладки `.v8` и имя heap-а: `.anime`, `.variable`, `.search`, `.feed`. Например, `aniparser.v8.search`. В `hello` IPC эти имена сообщаются клиенту.

| Heap | Реализация и содержимое |
| --- | --- |
| `AnimeHeap` | Фиксированные блоки по `sizeof(Anime)`. Хранит базовые поля, дескрипторы строк/векторов, `usage_count`, `last_used_ns` и собственный handle. |
| `VariableHeap` | Блоки произвольного размера. Хранит байты строк и буферы `SharedVector<T>`. |
| `SearchHeap` | Фиксированные записи `SharedSearch`; страница содержит максимум 10 Anime handles и ссылку на следующую страницу. |
| `FeedHeap` | Фиксированные записи `SharedFeed`; сами списки, строки коллекций размещаются в `VariableHeap`, элементы являются handles аниме. |

`Anime` и записи feed/search являются trivially copyable/destructible. В них нельзя хранить `std::string`, `std::vector`, локальные указатели или другой ресурс с деструктором: такие значения не переживают remap в другом процессе. `SharedString` содержит `{segment_id, offset, size, capacity}`; `view()` разрешает handle и возвращает `string_view` в текущем процессе. `set()` переиспользует имеющийся блок, если хватает capacity, иначе выделяет новый, копирует данные и освобождает прежний. `append()` работает аналогично для добавления в конец. Пустая строка может сохранять уже выделенный блок до `clear()`.

`SharedVector<T>` по тому же принципу содержит handle буфера и размер/capacity. Тип `T` обязан быть тривиально копируемым и разрушаемым. `reserve()` увеличивает capacity геометрически (обычно x2), выделяет новый блок, копирует элементы и удаляет старый блок; `push_back()` сначала копирует входной элемент, поэтому безопасен при reallocation, если аргумент ссылался на элемент того же вектора. `clear()` оставляет память, `release()` освобождает блок. Нельзя вызывать `release()` для вложенных типов без предварительного освобождения их собственных SharedString/векторов: для Anime это централизовано в `Anime::release_shared_storage()`.

### Сегменты и аллокатор

Общий интерфейс `IHeap` определяет `create/open/close`, `allocate/deallocate`, `resolve`, `allocation_size` и число сегментов. `HeapManager` привязывает логическое имя heap-а к провайдеру, создаёт сегмент №1 или при открытии последовательно подхватывает уже существующие сегменты. Перед поиском места/разрешением более высокого ID вызывается `refresh`/открытие пропущенных сегментов, благодаря чему процесс может увидеть расширение, созданное другим процессом.

`SegmentAllocator.hpp` задаёт общий формат сегмента: заголовок с magic/version/размером и атомарными блокировками, далее последовательность заголовков блоков и payload. Все payload выровнены по 8 байт. Аллокация проходит блоки линейно, находит первый свободный подходящего размера, при необходимости делит блок. Освобождение помечает блок свободным и объединяет соседние свободные блоки. Сегмент имеет общий spin lock для метаданных; отдельный registry lock координирует критические операции с записями и их ссылками между процессами. Статистика суммирует размер сегментов, занятый/свободный payload и число блоков.

`AnimeHeap` и `SearchHeap` принимают только блоки фиксированного размера; `VariableHeap` — произвольный ненулевой размер. При нехватке места каждый heap просматривает существующие сегменты, пытается добавить новый и повторяет поиск. Начальный размер следующего сегмента удваивается; для запроса крупнее текущего выбирается как минимум размер запроса с учётом служебного overhead. При ошибке provider/инициализации возвращается пустой handle. Старые сегменты автоматически не удаляются.

### Провайдеры платформ

`IShmSegmentProvider` абстрагирует создание/открытие сегмента, освобождение локального отображения и удаление backing storage.

| Метод `IShmSegmentProvider` | Контракт |
| --- | --- |
| `create_segment(name, id, size, out)` | Создать новый segment и вернуть локальное описание mapping в `out`; существующий сегмент обычно является ошибкой. |
| `open_segment(name, id, out)` | Подключить существующий segment; `false` используется HeapManager как признак, что следующего последовательного segment нет. |
| `close_segment(segment)` | Освободить ресурсы локального mapping; базовый интерфейс по умолчанию ничего не делает. |
| `remove_segment(name, id)` | Явно удалить backing storage; по умолчанию не поддерживается и возвращает false. Это не то же самое, что close. |

`IHeap` задаёт общий минимум операций для typed heaps: `create/open/close`, `allocate(size)`, `deallocate(handle)`, `resolve(handle)`, `allocation_size(handle)`, `segment_count()` и `valid()`. `allocate`/`resolve` не передают ownership самоописательным объектом: caller должен хранить handle и вызывать deallocate только для выделенного им ресурса. `AnimeHeap` дополнительно экспортирует статистику, перечень allocated handles и registry guard. `VariableHeap` добавляет статистику. `SearchHeap` имеет typed методы для Search pages и графа, `FeedHeap` typed операции Feed/collection.

#### HeapManager по операциям

| Метод | Работа |
| --- | --- |
| `create(name, initial_size, provider)` | Закрывает прежнее состояние менеджера, создаёт segment 1, запоминает provider и вычисляет следующий размер как удвоенный initial size. Сам allocator header инициализирует конкретный typed heap. |
| `open(name, provider)` | Последовательно открывает segment ID от 1 до первого отсутствующего. Первый отсутствующий segment завершает scan; валидность содержимого проверяет typed heap. |
| `refresh()` | Пытается подключить все вновь созданные последовательные segments. |
| `add_segment(minimum_size)` | Под registry процесса создаёт следующий ID размером max(next-size, minimum), добавляет mapping и удваивает размер следующего. |
| `get_segment(id)` | Ищет локальную запись; если ID ещё не отображён, пытается последовательно открыть сегменты до него. Возвращаемая ссылка указывает на локальный deque entry. |
| `resolve(handle)` | Получает segment по ID, проверяет `offset < size`, возвращает `base + offset`. Allocation validity дополнительно проверяет typed heap/allocator. |
| `statistics()` / `allocated_handles()` | Проходит доступные сегменты и собирает статистику либо handles allocated payload. Оба метода используют allocator segment lock при обходе каждого сегмента. |
| `lock_registry()` | Берёт spin lock из заголовка сегмента 1; guard освобождает его в деструкторе или раньше через `unlock()`. |

`HeapManager::open()` ожидает последовательные ID сегментов: если сегмент, например, №2 отсутствует, но №3 существует, автоматический поиск №3 после дырки не делается. Поэтому provider должен сохранять непрерывную нумерацию.

### Публичные операции Cache и typed heaps

| Операция | Результат/ownership |
| --- | --- |
| `Cache::open(name, provider)` | Открывает все четыре heap-а с именами `{name}.v8.{anime,variable,search,feed}`. При сбое закрывает ранее открытые heap-ы и возвращает false. |
| `Cache::create(name, anime_size, variable_size, provider)` | Создаёт все четыре heap-а. Anime/Variable получают заданные размеры первых сегментов; Search/Feed используют anime size. Инициализирует allocator headers. При промежуточном сбое закрывает mapping-и и пытается убрать созданные segment 1. |
| `Cache::close()` | Закрывает mappings в порядке Feed, Search, Variable, Anime; не удаляет persistent segments и не разрушает все allocations. |
| `allocate_anime()` | Выделяет ровно `sizeof(Anime)`, вызывает `construct_at`, ставит self/initial refcount/time и возвращает owning handle. |
| `resolve_anime(handle)` | Проверяет allocated block и eviction sentinel, touch-ит last-used, возвращает локальный pointer или null. Самостоятельно reference count не увеличивает. Для долгого доступа нужен уже retained handle. |
| `find_or_create_anime(id, created*)` | При положительном ID делает линейный поиск; найденному Anime добавляет reference. Иначе выделяет новую запись и ставит created=true. |
| `retain_anime(handle)` / `release_anime(handle)` | Атомарно меняют usage count под Anime registry lock. Нельзя retain-ить claimed/destroying slot или release-ить нулевой счётчик. |
| `discard_anime(handle)` | Удаляет только временную новую запись с ожидаемым ровно одним reference; сначала атомарно claim-ит слот, затем уничтожает shared payload. Используется для rollback при ошибке заполнения. |
| `deallocate_anime(handle)` | Физически удаляет только idle slot с refcount 0. Для обычного владения используйте release, не эту операцию. |
| `evict_unused_before(cutoff, max_count)` | Ищет oldest eligible records до cutoff и удаляет максимум max_count. `evict_oldest_unused` использует практически неограниченный cutoff. |
| `allocate_search()` / `deallocate_search(handle)` | Выделяет Search page; deallocate для handle любой страницы определяет root и уничтожает всю цепочку. |
| `allocate_feed()` / `deallocate_feed(handle)` | Создаёт Feed record; deallocate рекурсивно снимает refs на Anime и освобождает strings/vectors коллекций. |

`anime_heap()`, `variable_heap()`, `search_heap()`, `feed_heap()` возвращают глобальные mutable heap objects. Они предназначены для внутренних слоёв Core и клиента-прототипа; напрямую обращаться к ним из нескольких независимых Core instances в одном процессе небезопасно.

| Typed heap | Дополнительные операции |
| --- | --- |
| `AnimeHeap` | Хранит только блоки заданного `anime_size`; принимает allocate только при равенстве переданного размера. Предоставляет `statistics`, `allocated_handles`, `lock_registry`. |
| `VariableHeap` | `allocate(size)` отклоняет нулевой размер, позволяет блок любого размера, `allocation_size` возвращает фактический выровненный payload size. |
| `SearchHeap` | `allocate_search`, `resolve_search` (проверяет root not claimed и touch-ит root/page), `append_anime`, `set_continuation`, `evict_unused_before`. |
| `FeedHeap` | `allocate/resolve/deallocate`, `append_anime` (сначала retain), `append_collection` (создаёт строки, затем добавляет коллекцию в shared vector). `storage()` даёт доступ к underlying fixed-size AnimeHeap wrapper для statistics/segment count. |

- `PosixShmSegmentProvider` использует `shm_open`, `ftruncate`, `mmap(MAP_SHARED)` и `shm_unlink`; применяется для POSIX-сборок Linux/macOS.
- `FileShmSegmentProvider` создаёт файлы в заданном каталоге и отображает их через `mmap(MAP_SHARED)`. Имя файла образуется из FNV-подобного 64-bit hash имени heap-а и ID сегмента. Один и тот же каталог должен быть доступен всем взаимодействующим процессам. Этот провайдер включён CMake для Android/iOS.
- `WindowsShmSegmentProvider` создаёт backing-файлы и отображения через `CreateFileW`, `CreateFileMappingW`, `MapViewOfFile`. Каталог по умолчанию у standalone сервера берётся из `%LOCALAPPDATA%\Aniparser\cache`.

Провайдеры кешируют отображения внутри процесса и считают локальные ссылки на mapping; `close_segment()` снимает mapping/дескрипторы при последнем локальном использовании. Удаление сегмента — отдельная операция и не вызывается при обычном закрытии кеша. Следовательно, данные backing storage сохраняются между перезапусками, если их явно не удалить. Важно: desktop `main.cpp` выбирает Windows, Android или Posix provider; iOS не имеет standalone server target. Встраивающий iOS-код должен создать `FileShmSegmentProvider` самостоятельно и передать общий App Group каталог, когда доступ нужен нескольким процессам приложения.

### Ссылки, время и очистка Anime

Каждый `HeapHandle` — смещение внутри конкретного сегмента, а не стабильный C++-адрес. `Cache::allocate_anime()` выделяет `Anime`, начинает его lifetime через `construct_at`, записывает `self`, устанавливает начальный usage count в 1 и трогает метку времени. `resolve_anime()` проверяет запись и обновляет last-used. `retain_anime()` атомарно увеличивает счётчик под registry lock; `release_anime()` уменьшает, не позволяя уйти ниже нуля. Sentinel `UINT64_MAX` означает, что запись уже захвачена на удаление и новые ссылки запрещены.

`find_or_create_anime(id)` под registry lock сканирует занятые блоки AnimeHeap, ищет такой `anime_id`, который не удаляется, и увеличивает его ссылку. Если не найден — выделяет и инициализирует запись. Это линейный поиск, без отдельного persistent ID-индекса. Создаваемые из API search/feed частичные аниме наполняются соответствующим подмножеством полей. Подробный ответ преобразуется функциями `store_anime`, `store_search_result` или `store_feed_item`; строки и вектора распределяются в VariableHeap. `viewing_order` на данный момент создаёт отдельную Anime-запись для каждого элемента (не ищет дубликат через `find_or_create_anime`) и сохраняет её handle в родительском векторе.

Ссылки на аниме удерживаются вызывающим клиентом, Search page, Feed и Anime `viewing_order`. Для освобождения payload вызывается `release_shared_storage`: освобождаются строки, вложенные векторы и child-ссылки. Счётчик аниме не равен числу всех внешних процессов напрямую — это суммарное число выданных владений/ребер, поэтому все владельцы обязаны парно вызвать release. Метка времени берётся из `steady_clock` в наносекундах; она подходит для сравнения процессов на той же машине при текущем запуске ОС, но не является календарным timestamp для передачи между перезагрузками.

## Реализация Cache-структур

### Anime

Полная схема записи описана в `src/cache/Anime.hpp`. Числовые поля хранятся непосредственно, строковые поля представлены `SharedString`, массивы — `SharedVector`. `viewing_order` — именно вектор handles на Anime-записи, а не копия структур; каждое ребро удерживает child Anime. Связи могут повторно использовать уже созданные по ID записи. `release_shared_storage()` проходит все вложенные дескрипторы и child handles, после чего их блоки VariableHeap и ссылки становятся освобождаемыми.

### Search

`SharedSearch` содержит `self`, общий `root`, `std::array<HeapHandle, 10> anime`, `anime_count`, `continuation` и `last_used_ns`. При добавлении результата SearchHeap сначала сохраняет владение ссылкой на Anime. `set_continuation()` проверяет ссылки и проходит будущую цепочку, запрещая циклы, затем назначает child общий root. `resolve_search()` обновляет время страницы и корня; обращение к любой странице поддерживает свежесть всей цепочки.

Удаление любого page handle находит root, атомарно помечает его удаляемым и освобождает всю цепочку: у каждой страницы снимаются ссылки на аниме, затем освобождаются записи страниц. Очистка выбирает только корневые страницы, проверяет всю цепочку и её самое новое время использования; удаляет цепочку целиком только если все страницы старше cutoff. При конкуренции с retain/resolve registry lock и sentinel не позволяют очистке уничтожить используемый объект.

### Feed

`SharedFeed` содержит значения сезона/года карусели и векторы handles: `new_anime`, `announcements`, `recommendations`, `carousel_items`, `schedule_items`; также вектор `SharedFeedCollection`. Коллекция хранит id, SharedString заголовка/описания и SharedVector Anime handles. Расписание также представлено ссылками на Anime-записи, частично заполненные полями schedule. При `deallocate(feed)` все векторы ссылок освобождаются с уменьшением Anime reference count; затем освобождаются строки коллекций и блоки векторов. FeedHeap сам по себе не реализует refcount или TTL для feed handle — освобождение выполняет получивший его клиент.
