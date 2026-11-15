# IPC protocol, version 3

Эта страница — краткая спецификация для автора клиента. Полный пример запуска
и Python-клиент находятся в корневом [README](../../README.md#подключение-своего-клиента).

## Транспорт и framing

- Linux: Unix domain stream socket; default `/tmp/aniparser.sock`.
- Windows: named pipe; default `\\.\pipe\aniparser`.
- Каждое сообщение — UTF-8 JSON object и завершающий байт LF (`\n`).
- Stream не сохраняет границы `send`/`recv`. Клиент собирает буфер и разбирает
  его только по LF; один `recv` может содержать часть строки или несколько строк.
- Максимальный размер одной команды сервера — 1 MiB.
- Первым запросом следует отправить `hello`. Протокол сейчас версии `3`.
- В версии 3 запросы, которые возвращают snapshot и события обновления,
  называются `subscribeAnime`, `subscribeSearch`, `subscribeFeed`,
  `subscribeHistory`, `subscribeFavorites` и `subscribePlanned`/
  `subscribeWatched`/`subscribeCompleted`/`subscribeDropped`. Старые потоковые
  имена `get...` больше не поддерживаются.
- Клиент создаёт `id` и использует его для сопоставления ответов. Рекомендуется
  уникальное целое число на одном соединении.

Windows-клиент открывает pipe как byte stream с duplex-доступом (`CreateFile`)
и применяет тот же newline framing. На Unix клиент подключается к пути через
`AF_UNIX`, `SOCK_STREAM`.

Пример handshake:

```json
{"id":1,"method":"hello"}
{"id":1,"ok":true,"protocol":3,"transport":"json-lines"}
```

Успешный ответ содержит `ok:true`; ошибка — `ok:false` и человекочитаемое
`error`. Ошибки разбора также по возможности содержат request `id`.

## Запросы

```json
{"id":2,"method":"subscribeFeed"}
{"id":3,"method":"subscribeSearch","params":{"q":"магическая битва","offset":0,"limit":10}}
{"id":4,"method":"subscribeAnime","anime_id":771}
{"id":5,"method":"getVideoOptions","iframe":"//kodikplayer.com/season/1/example/720p"}
{"id":6,"method":"getVideo","iframe":"//kodikplayer.com/season/1/example/720p","quality":"720"}
{"id":7,"method":"subscribeHistory","offset":0,"limit":20}
{"id":8,"method":"addHistory","anime_id":771}
{"id":9,"method":"removeHistory","anime_id":771}
{"id":10,"method":"updateAnimeSaveData","anime_id":771,"player":"Kodik","dubber":"Озвучка","episode":"1","time":312}
{"id":11,"method":"getAnimeSaveData","anime_id":771}
{"id":12,"method":"unsubscribeHistory"}
{"id":13,"method":"unsubscribeSearch","target_id":3}
{"id":14,"method":"subscribeFavorites","offset":0,"limit":20}
{"id":15,"method":"addFavorite","anime_id":771}
{"id":16,"method":"removeFavorite","anime_id":771}
{"id":17,"method":"unsubscribeFavorites"}
{"id":18,"method":"subscribePlanned","offset":0,"limit":20}
{"id":19,"method":"addPlanned","anime_id":771}
{"id":20,"method":"removePlanned","anime_id":771}
{"id":21,"method":"subscribeWatched","offset":0,"limit":20}
{"id":22,"method":"subscribeCompleted","offset":0,"limit":20}
{"id":23,"method":"subscribeDropped","offset":0,"limit":20}
{"id":24,"method":"isFavorite","anime_id":771}
{"id":25,"method":"getStatus","anime_id":771}
{"id":26,"method":"getAnilibriaVideos","anime_id":771}
```

### Ответы

| Метод | Поля успешного ответа |
| --- | --- |
| `hello` | `protocol:3`, `transport:"json-lines"` |
| `subscribeFeed` | `feed`, `refreshing`; затем `feed.updated` |
| `subscribeSearch` | `results`, `offset`, `limit`, `refreshing`; затем `search.updated` |
| `subscribeAnime` | `anime`, `refreshing`, `loaded`; затем `anime.updated` |
| `unsubscribeAnime`, `unsubscribeSearch`, `unsubscribeFeed` | отменяет ожидание обновления, указанное через `target_id` |
| `getVideoOptions` | `qualities` — массив `{quality,url}` |
| `getVideo` | `url` |
| `subscribeHistory` | `items`, `offset`, `limit`, `revision`, `sequence`, `refreshing`; затем `history.updated` с тем же `id` |
| `unsubscribeHistory` | отмена подписок истории на этом соединении |
| `addHistory` | добавленный `anime_id` и `revision` |
| `removeHistory` | `anime_id`, `removed` и `revision` |
| `subscribeFavorites` | страница `items`, `revision`, `sequence`, `refreshing`; затем `favorites.updated` |
| `unsubscribeFavorites` | отмена подписки избранного на этом соединении |
| `addFavorite` | добавленный `anime_id` и `revision` |
| `removeFavorite` | `anime_id`, `removed` и `revision` |
| `subscribePlanned`, `subscribeWatched`, `subscribeCompleted`, `subscribeDropped` | страница `items`, `revision`, `sequence`, `refreshing`; затем соответствующее `.updated` событие |
| `unsubscribePlanned`, `unsubscribeWatched`, `unsubscribeCompleted`, `unsubscribeDropped` | отмена подписки статуса на этом соединении |
| `addPlanned`, `addWatched`, `addCompleted`, `addDropped` | добавленный `anime_id` и revision |
| `removePlanned`, `removeWatched`, `removeCompleted`, `removeDropped` | `anime_id`, `removed` и revision |
| `isFavorite` | `anime_id`; ответ содержит boolean `value` |
| `getStatus` | `anime_id`; ответ содержит integer `status` |
| `getAnilibriaVideos` | `anime_id`; массив `episodes` с доступными `qualities` |
| `getAnimeSaveData` | `save_data` (объект или `null`) |
| `updateAnimeSaveData` | сохранённый `save_data` и `revision` |

Все ответы включают исходный `id` и `ok`. При пустом Anime-кэше `anime:null`,
`loaded:false`; пустой Search использует `results:[]`. Feed имеет форму
`FeedResponse` со значениями по умолчанию.

## Snapshot и события

`subscribeAnime`, `subscribeSearch`, `subscribeFeed` немедленно отдают имеющийся snapshot. Если
запись отсутствует или устарела, в фоне идёт API refresh. При успешном
обновлении сервер отправляет вторую JSON-строку с тем же `id`:

```json
{"id":4,"ok":true,"anime":null,"refreshing":true,"loaded":false}
{"id":4,"event":"anime.updated","ok":true,"anime_id":771,"anime":{...}}
```

Аналогично: `feed.updated` содержит `feed`; `search.updated` содержит `results`,
`offset` и `limit`. Событие может прийти до того, как клиент обработает уже
прочитанный первоначальный ответ. Маршрутизируйте сообщения по ID.

Anime ошибка обновления тоже приходит как `anime.updated`, с `ok:false`,
`anime:null`, `error` и `status`. Ошибка Feed/Search также приходит событием
`feed.updated`/`search.updated` с `ok:false`, `error`, `status`,
`refreshing:false`; ожидание этого запроса завершается. При `refreshing:false` нового события
для этого запроса не ожидается.

Если начальный ответ сообщает `refreshing:true`, клиент может отменить ожидание
через `unsubscribeAnime`, `unsubscribeSearch` или `unsubscribeFeed`. В поле
`target_id` передаётся `id` исходной subscribe-команды. Ответ отмены имеет свой
`id`. Отмена прекращает доставку результата этому IPC-клиенту, но не прерывает
общий API-запрос, который может быть нужен другим клиентам. При
`refreshing:false` ожидание завершается сразу; закрытие соединения также
прекращает доставку.

История хранит до 1000 последних ID аниме. `subscribeHistory` принимает
необязательные `offset` и `limit` (по умолчанию 0 и 20; `limit` ограничен
диапазоном 1–50). Клиент получает только запрошенную страницу. Каждая запись
`items` имеет форму `{anime_id, anime, save_data}`: карточка `anime` может быть
`null`, а состояние просмотра в `save_data` независимо и тоже может быть `null`.
Для карточек текущей страницы сервер использует данные из памяти; если карточка
отсутствует или старше пяти минут, он запрашивает её через API Search по ID.
Так за один раз обновляются только ID видимой страницы, а не вся история.
Недавно проверенный, но не найденный API ID также кэшируется на пять минут.

`addHistory` добавляет ID в начало списка; если он уже был, ID перемещается в
начало. `removeHistory` удаляет ID. Сохранение состояния просмотра делается
отдельными `updateAnimeSaveData` и `getAnimeSaveData`; поля `player`, `dubber`,
`episode` и `time` не дублируются в записи списка истории. Подписчики получают
новую полную страницу после изменения, а не локальную дельту:

```json
{"id":7,"ok":true,"event":"history.updated","revision":1,"sequence":1,"offset":0,"limit":20,"refreshing":false,"reason":"history","items":[{"anime_id":771,"anime":null,"save_data":null}]}
```

При дозагрузке карточек приходит ещё один snapshot страницы с причиной
`metadata`. `sequence` увеличивается для каждого события конкретной подписки;
используйте его для отбрасывания повторных или пришедших не по порядку событий.
`revision` меняется при добавлении/удалении ID и при обновлении save data.
Карточки могут обновиться без изменения `revision`, поэтому для событий нужен
именно `sequence`.

Подписка сохраняется до `unsubscribeHistory` или закрытия соединения. Новый
`subscribeHistory` на том же соединении заменяет старую подписку. Изменения
рассылаются только подписчикам, чьи страницы затронуты; при перестановке ID
обновляются страницы, на которых изменился состав.

`removeHistory` сообщает `removed:false`, если ID отсутствовал. Сохранённые
данные просмотра можно читать независимо от нахождения ID в списке истории:

```json
{"id":11,"ok":true,"save_data":{"anime_id":771,"player":"Kodik","dubber":"Озвучка","episode":"1","time":312}}
```

Избранное — отдельная упорядоченная коллекция до 1000 ID. Повторный
`addFavorite` перемещает ID в начало. `subscribeFavorites` работает так же,
как `subscribeHistory`: возвращает одну страницу сразу и затем присылает
`favorites.updated` при изменении страницы или загрузке карточек. Удаление
отсутствующего ID возвращает `removed:false`. История и избранное сохраняются
вместе, но имеют независимые revision и подписки.

Состояния аниме `planned`, `watched`, `completed` и `dropped` — четыре
независимо читаемые постраничные коллекции с тем же интерфейсом подписки.
Добавление через любой `add...` автоматически удаляет этот ID из остальных
трёх состояний и рассылает обновления каждой затронутой коллекции. Удаление
через `remove...` меняет только указанное состояние. Все четыре списка
сохраняются в `history.json` и ограничены 1000 ID каждый.
`getStatus` возвращает `0` для аниме без статуса, `1` для planned, `2` для
watched, `3` для completed и `5` для dropped. Код `4` не используется.
`isFavourite`, `isfavorite` и `isfavourite` принимаются как синонимы
`isFavorite`; `getstatus` принимается как синоним `getStatus`.

```json
{"id":24,"ok":true,"anime_id":771,"value":true}
{"id":25,"ok":true,"anime_id":771,"status":2}
{"id":26,"ok":true,"event":"watched.updated","revision":4,"sequence":2,"offset":0,"limit":20,"refreshing":false,"reason":"state","items":[]}
```

Кэш действует только в одном процессе сервера и имеет TTL пять минут. Частичные
данные из Feed/Search могут появиться в `anime` раньше полной карточки. Данные
клиента — его собственные JSON-копии; освобождать что-либо на сервере не надо.

## Anime и video JSON

В API-модели аниме хранится как плоский `videos`; IPC передаёт сгруппированные
`players → dubbers → episodes`. Пример:

```json
{"players":[{"name":"Kodik","dubbers":[{"name":"Озвучка","episodes":[{"video_id":1,"iframe_url":"//kodikplayer.com/...","number":"1","date":0,"index":0,"skips":{"opening":{"time":"","length":""},"ending":{"time":"","length":""}}}]}]}]}
```

В группы входят только iframe плееров, которые поддержаны backend extractor.
Сейчас поддержаны Kodik и Aksor. Episode URL — это iframe, а не прямая ссылка видео. Запрос
`getVideoOptions` превращает iframe в доступные прямые потоки; разрешения —
строки, например `"720"`. Видеопарсер вызывается синхронно в обработчике
клиента; ответ может задержаться на время HTTP-запроса к соответствующему
плееру.

`getAnilibriaVideos` берёт alias из Anime-метаданных yani.tv и получает релиз
AniLibria по alias. Ответ содержит `episodes` по возрастанию `ordinal`; у каждой
серии есть массив `qualities` с доступными `{quality,url}` (`fhd`, `hd`, `sd`).
Отсутствующие ссылки пропускаются. Ошибка возвращается как `ok:false`, `error`
и HTTP `status` (ноль для сетевой ошибки). Запрос синхронный и может выполнить
запрос к yani.tv, затем к основному и резервному хостам AniLibria. Этот источник
не добавляется в `anime.players`: TUI показывает AniLibria отдельно, если alias
присутствует в `anime.remote_ids`. Например:

```json
{"id":26,"ok":true,"episodes":[{"ordinal":1,"name":"Эпизод 1","qualities":[{"quality":"fhd","url":"https://...m3u8"},{"quality":"hd","url":"https://...m3u8"}]}]}
```

## Соединение и завершение

Держите соединение открытым, пока ожидаете обновления. При разрыве соединения
сервер больше не может отправить клиенту callback, но обновление кэша может
завершиться. Клиент может подключиться заново и запросить snapshot повторно.
Периодических heartbeat/ping, кроме `hello`, протокол не задаёт.

Сервер пишет команды и IPC-ошибки в терминал. Уровень логирования и формат
терминального вывода не являются частью wire protocol.

## Уточнения надёжности без изменения версии 3

- `id` обязателен: неотрицательное целое int64 или строка до 128 байт.
  ID активных запросов следует делать уникальными в пределах соединения.
  Повторный активный stream с той же парой method/id отклоняется;
  максимум 256 ожидающих Anime/Search/Feed streams на соединение.
- ID аниме — строго положительное целое int64; дробные значения, переполнения,
  отрицательные offset/time и limit < 1 отклоняются до мутации. Положительный
  limit коллекции по-прежнему ограничивается 50. Search limit — 1–50;
  фильтры массивов — до 1000 элементов, q/sort — до 4096 байт.
- Ошибка Search/Feed завершает ожидание `*.updated` с `ok:false`, `error`,
  `status`, `refreshing:false`. Для ошибок metadata snapshot содержит
  `reason:"error"`, `ok:false`, `refreshing:false` и прежнюю страницу.
- `removeAnimeSaveData` принимает `anime_id`, возвращает `removed` и удаляет
  только прогресс, без изменения членства в истории/избранном/статусах.
- Прогресс: максимум 10 000 новых ID. При заполнении новые ID отклоняются;
  обновление/удаление существующих разрешено. Данные при загрузке не отсекаются.
  Player/dubber — до 4096 байт, episode — до 256, time — целое >= 0.
- При ошибке сохранения возвращается `ok:false`, а не подтверждение прогресса.
  Ошибки до замены файла не меняют память и revisions. Если файл заменён, но
  fsync каталога не удался, `data.committed:true` сообщает, что состояние уже
  изменилось, а долговременная сохранность не подтверждена.
- Повреждённое/неподдерживаемое хранилище блокирует запись до восстановления
  файла и перезапуска. Оригинал не перезаписывается пустыми списками.
- Сервер ограничивает активные соединения 128 и прерывает зависшую отправку
  примерно через пять секунд. Завершённые соединения/threads освобождаются во
  время работы. Активный внешний HTTP может задержать shutdown до своего timeout.

Это additive error/validation уточнения: формат успешных ответов и handshake
остаются совместимыми с корректными клиентами версии 3. C++ mutation API теперь
возвращает `Result<bool>`; это изменение C++ интерфейса, не JSON wire format.
