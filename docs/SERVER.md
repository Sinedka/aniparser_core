# Server и IPC

## Server и протокол IPC

### Фасад

`Server` не хранит данные отдельно: это адаптер типов. Он вызывает Core, превращает `Anime*` в `Anime::self`, `SharedSearch*` в корневой Search handle и `SharedFeed*` в Feed handle. Методы `deleteAnime`, `deleteSearch`, `deleteFeed` делегируют release соответствующего владения Cache/Core.

### Транспорт и конкурентность

На POSIX `IpcServer` использует поток accept и отдельный worker-поток для клиента. Сообщения читаются из byte stream до `\n`, допускается несколько команд в одном чтении. Буфер ещё не обработанного ввода ограничен 1 MiB; если `pending` превысил лимит, соединение получает ошибку и закрывается. Ответы сериализуются как JSON + newline, запись в соединение защищена mutex. При запуске удаляется только устаревший socket-файл: если путь не является socket или к нему можно подключиться, запуск отказывается его заменять. Socket создаётся с правами `0600`. `stop()` прекращает accept, прерывает клиентские сокеты и дожидается потоков.

В Windows используется Named Pipe byte stream с отклонением удалённых подключений, отдельным потоком для подключённых клиентов и теми же newline JSON-сообщениями. Реализация Windows дублирует формат команд/ответов, но использует системные операции `CreateNamedPipeW`, `ConnectNamedPipe`, `ReadFile`/`WriteFile`.

Все запросы — JSON object с `id` и `method`. Ответ успеха имеет `{"id": ..., "ok": true}`; ошибка — `ok:false` и `error`. Значения `id` повторяются в поздних event-сообщениях.

| Метод | Вход | Результат |
| --- | --- | --- |
| `hello` | Только `id`, `method` | `protocol:1`, `layout:8`, имена и snapshot числа сегментов всех heaps; у файловых провайдеров также `shared_memory_directory`. |
| `getAnime` | `anime_id` | Немедленный Anime handle, `loaded`, `refreshing`; позже `anime.updated` с тем же id запроса, Anime handle и `ok`. |
| `getSearch` | `params` — поля `SearchInput` | Одна страница, handle корня цепочки, `offset`, `limit:10`; stale обновление дополнительно приходит как `search.updated`. |
| `getFeed` | Необязательный `watches` | Feed handle; stale обновление — `feed.updated`. |
| `deleteAnime` | Anime handle | `deleted` — удалось ли снять Anime reference. |
| `deleteSearch` | Search handle страницы/корня | `deleted` — удалось ли уничтожить цепочку Search. |
| `deleteFeed` | Feed handle | `deleted` — удалось ли освободить Feed и его Anime-ссылки. |

Handle кодируется как `{"segment_id":N,"offset":N}` или `null`. `hello.heaps.*.segments` — snapshot, а не обещание, что дополнительных сегментов не появится. Клиенту нужно отображать новые сегменты, если полученный handle указывает на неизвестный ID. `layout` обязателен для проверки совместимости памяти; после изменения ABI shared structs версию необходимо поднять и согласовать с клиентами. Названия методов и поля параметров являются частью протокола.

### Выбор платформы и запуск сервера

`src/server/main.cpp` принимает аргументы `[cache-name] [endpoint] [data-directory]`. Cache по умолчанию — `aniparser`. Endpoint — `/tmp/aniparser.sock` на обычных POSIX системах, `\\.\pipe\aniparser` на Windows; Android требует endpoint и каталог от вызывающего приложения. Windows по умолчанию использует каталог под LocalAppData. Desktop standalone target `aniparser_server` не собирается для Android/iOS. На Android standalone main выбирает File provider и требует каталог аргументом; для iOS сервис должен встраиваться в приложение и настраивать File provider/socket с путями контейнера/App Group самостоятельно.

Сервер сначала инициализирует Core/Cache, потом начинает слушать endpoint. Ошибка инициализации кеша или bind/listen завершает старт с ненулевым кодом. SIGINT/SIGTERM останавливают IPC и затем Core. Shared-memory backing storage не удаляется при штатном `Cache::close()`.

Подробные краткие примеры протокола также есть в `src/server/IPC.md`.

## Примеры протокола со стороны клиента

Команды передаются отдельными JSON-строками с завершающим newline:

```json
{"id":1,"method":"hello"}
{"id":2,"method":"getSearch","params":{"q":"наруто","offset":0}}
{"id":3,"method":"getAnime","anime_id":771}
{"id":4,"method":"getFeed","watches":[]}
{"id":5,"method":"deleteAnime","handle":{"segment_id":1,"offset":128}}
```

Числа handle в примере условные; нужно использовать handle из ответа сервера. Обычный ответ имеет `id` и `ok`. Событие тоже включает `id`, а также `event`, например `anime.updated`. Запрос `getSearch` через IPC выдаёт только одну страницу, поскольку server callback возвращает `false`; следующий запрос клиент формирует сам, увеличивая `offset` на 10.

Рекомендуемая последовательность клиента: проверить `ok`, `protocol` и `layout` из `hello`; открыть сообщённые heap names и сегменты; прочитать JSON line; сопоставить reply/event с запросом по id; отображать дополнительные сегменты по мере появления новых segment IDs; разрешать nested handles через правильный heap; освободить handle после завершения использования. `segments` в hello — только моментальный snapshot.

Клиент не может копировать shared Anime как обычный сериализуемый объект с адресами. Для строк требуется разрешить `SharedString` через VariableHeap; для вектора — получить его буфер, а затем разрешить вложенные handles нужного типа. Интерпретация структуры должна совпадать с ABI заголовков именно этой версии раскладки.
