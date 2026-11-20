# Подключение приложения к Aniparser Core

Этот файл можно целиком передать нейросети вместе с проектом клиента.
Он описывает подключение стороннего приложения к локальному Aniparser Core
через IPC версии 3.

## Задание для реализации

Подключи приложение к отдельному процессу `aniparser_server` через локальный
IPC. У всех клиентов одного профиля общий сервер и один endpoint. Каждый
клиент открывает собственное соединение и закрывает его при выходе.
Используй язык, структуру и интерфейс существующего приложения. Добавь
настраиваемый endpoint и один IPC-клиент с постоянным соединением, чтением
событий в фоне и маршрутизацией по `id`. UI вызывает этот клиент асинхронно.
Подключи нужным экранам поиск, ленту, карточку, видео, коллекции и прогресс
по контракту ниже. Все данные получай через IPC. Сервер самостоятельно
обращается к провайдерам и хранит пользовательское состояние.

Если приложение работает в обычной браузерной вкладке, реализуй подключение
в его локальном backend или desktop-оболочке. Браузер обращается к этому слою
через интерфейс приложения. JSON Lines из этого документа передаётся внутри
локального сокета или именованного канала.

## Сборка и запуск сервера

Если исполняемый файл уже есть, переходите к запуску. Для сборки из корня
репозитория нужны CMake 3.25+, компилятор с C++23, CPR и nlohmann-json с
CMake-конфигурациями. На Linux конфигурация также требует pkg-config и
notcurses-core, даже при сборке только серверной цели. На Linux можно открыть
окружение проекта командой `nix develop`.

```sh
cmake -S . -B out/Debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF
cmake --build out/Debug --target aniparser_server -j2
out/Debug/src/server/aniparser_server /tmp/aniparser.sock
```

На Windows в PowerShell с доступными CMake и зависимостями:

```powershell
cmake -S . -B out/Debug -DBUILD_TESTING=OFF
cmake --build out/Debug --config Debug --target aniparser_server
& .\out\Debug\src\server\Debug\aniparser_server.exe '\\.\pipe\aniparser'
```

Путь в примере Windows соответствует многоконфигурационному генератору.
С одноконфигурационным генератором ищите `aniparser_server.exe` непосредственно
в `out/Debug/src/server/`.

Endpoint можно передать первым аргументом. При запуске без аргументов
используются адреса по умолчанию из следующего раздела. Готовность проверяйте подключением и успешным `hello` с ограниченным
временем ожидания. Проверку версии проводите на каждом новом соединении.
Для тестирования записей установите переменную `ANIPARSER_DATA_DIR`
**у процесса сервера** на отдельный каталог, например `$PWD/out/client-test-data`
в Linux. Для поиска, ленты, обновления карточек и получения видео серверу
нужна сеть.

## Один сервер для нескольких клиентов

Запускайте один экземпляр `aniparser_server` на пользовательский профиль.
Укажите один и тот же endpoint во всех клиентах. Сервер обслуживает их
одновременно, а история, избранное, статусы и прогресс общие. Подписки и
request ID относятся к отдельным соединениям. Запуск и остановка сервера
принадлежат менеджеру процессов или пользователю. При закрытии приложения
закройте его IPC-соединение, чтобы остальные приложения продолжили работу.

У сервера один каталог данных на профиль. Разные endpoint сами по себе
не изолируют хранилище. Для отдельного тестового экземпляра задайте и другой
endpoint, и другой `ANIPARSER_DATA_DIR`.

### Постоянный запуск на Linux с systemd

Пользовательская служба запускается при входе в сеанс и перезапускает сервер
после сбоя. Сокет лежит в каталоге текущего пользователя и доступен клиентам
этого пользователя. Остановите ранее запущенный вручную экземпляр перед
переходом на службу, чтобы два процесса не работали с одними сохранениями.

Из корня репозитория после сборки:

```sh
install -D -m 0755 out/Debug/src/server/aniparser_server   "$HOME/.local/bin/aniparser_server"
mkdir -p "$HOME/.config/systemd/user"
cat > "$HOME/.config/systemd/user/aniparser.service" <<'UNIT'
[Unit]
Description=Aniparser Core IPC server

[Service]
Type=exec
RuntimeDirectory=aniparser
RuntimeDirectoryMode=0700
ExecStart=%h/.local/bin/aniparser_server %t/aniparser/ipc.sock
Restart=on-failure
RestartSec=2s

[Install]
WantedBy=default.target
UNIT
systemctl --user daemon-reload
systemctl --user enable --now aniparser.service
```

Во всех Linux-клиентах задайте endpoint
`$XDG_RUNTIME_DIR/aniparser/ipc.sock`, например `/run/user/1000/aniparser/ipc.sock`
для UID 1000. TUI с этим адресом запускается так:

```sh
out/Debug/tui/aniparser_tui "$XDG_RUNTIME_DIR/aniparser/ipc.sock"
```

`systemctl --user status aniparser.service` показывает состояние процесса,
`journalctl --user -u aniparser.service -e` — журнал. Для обновления бинарника
остановите службу, повторите команду `install` и снова запустите службу.
Если прежний запуск использовал специальный `ANIPARSER_DATA_DIR`, задайте
тот же каталог через `Environment="ANIPARSER_DATA_DIR=/абсолютный/путь"`
в секции `[Service]`, чтобы служба открыла существующие сохранения.

### Ручной запуск и Windows

Для разработки достаточно один раз запустить сервер командой из предыдущего
раздела и открыть все клиенты с указанным там endpoint. На Windows для
автозапуска создайте одну задачу в Планировщике заданий, запускающую
`aniparser_server.exe` при входе под нужным пользователем. В параметрах
задачи выберите правило «Не запускать новый экземпляр». Клиенты подключаются
к одному именованному каналу. Само имя канала допускает несколько экземпляров
на стороне Windows, поэтому единственный процесс обеспечивает правило запуска.

Клиент, запущенный одновременно с сервером, повторяет попытку подключения
с небольшой задержкой в пределах тайм-аута запуска. Если за это время
`hello` не удался, покажите адрес и ошибку подключения. После перезапуска
сервера откройте новое соединение, проверьте версию и восстановите подписки
активных экранов.

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

На Windows откройте существующий канал через `CreateFileW` с
`GENERIC_READ | GENERIC_WRITE` и `OPEN_EXISTING`; используйте байтовый режим.
Если все экземпляры канала заняты (`ERROR_PIPE_BUSY`), дождитесь свободного
экземпляра через `WaitNamedPipeW` с тайм-аутом и повторите открытие. Читайте
через `ReadFile`, пишите через `WriteFile` до отправки всей строки. На Unix
подключайтесь к пути через `AF_UNIX`, `SOCK_STREAM`. Оба транспорта используют
одинаковые JSON-сообщения.

Пример handshake

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
{"id":27,"method":"removeAnimeSaveData","anime_id":771}
```

### Параметры поиска

Все поля передаются внутри `params`. По умолчанию `offset=0`, `limit=10`.
Пропущенный ключ и явно переданное значение `null` означают отсутствие фильтра.

| Ключи | Тип и границы |
| --- | --- |
| `q`, `sort` | Строка до 4096 байт |
| `sort_forward` | Boolean |
| `offset`, `ep_from`, `ep_to`, `to_year`, `min_rating_counters`, `max_rating_counters` | Целое от 0 до максимума int64 |
| `limit` | Целое 1–50 |
| `min_rating`, `max_rating` | Число 0–10 |
| `ids`, `studio_ids`, `director_ids`, `worldart_ids`, `mal_ids`, `kp_ids`, `shikimori_ids` | Массив положительных int64 |
| `min_age` | Массив неотрицательных int64 |
| `season`, `status`, `types`, `genres`, `exclude_genres` | Массив строк до 256 байт каждая |

Максимум 1000 значений в каждом массиве. Нижняя граница диапазона не должна
превышать верхнюю. Для базового поиска достаточно `q`, `offset` и `limit`.
При смене запроса сбрасывайте `offset` до нуля. Следующая страница запрашивается
новым ID с `offset + limit`. Общее число результатов в ответе отсутствует.

### Ответы

Для команд с аниме используйте `anime_id` из карточки yani.tv. Для каждой
коллекции из таблицы используются четыре метода с одним суффиксом. Например,
для Watched это `subscribeWatched`, `unsubscribeWatched`, `addWatched` и
`removeWatched`. Команды добавления и удаления принимают `anime_id`;
отписка от коллекции принимает только `id` и `method`. Для прогресса
`updateAnimeSaveData` принимает все пять полей из примера. `time` хранит
позицию в секундах по соглашению клиента, а сервер сохраняет переданное целое.

| Метод | Поля успешного ответа |
| --- | --- |
| `hello` | `protocol:3`, `transport:"json-lines"` |
| `subscribeFeed` | `feed`, `refreshing`; затем `feed.updated` |
| `subscribeSearch` | `results`, `offset`, `limit`, `refreshing`; затем `search.updated` |
| `subscribeAnime` | `anime`, `refreshing`, `loaded`; затем `anime.updated` |
| `unsubscribeAnime`, `unsubscribeSearch`, `unsubscribeFeed` | `target_id`, `unsubscribed` |
| `getVideoOptions` | `qualities` — массив `{quality,url}` |
| `getVideo` | `url` |
| `subscribeHistory` | `items`, `offset`, `limit`, `revision`, `sequence`, `refreshing`; затем `history.updated` с тем же `id` |
| `unsubscribeHistory` | `revision` соответствующей коллекции |
| `addHistory` | добавленный `anime_id` и `revision` |
| `removeHistory` | `anime_id`, `removed` и `revision` |
| `subscribeFavorites` | страница `items`, `revision`, `sequence`, `refreshing`; затем `favorites.updated` |
| `unsubscribeFavorites` | `revision` соответствующей коллекции |
| `addFavorite` | добавленный `anime_id` и `revision` |
| `removeFavorite` | `anime_id`, `removed` и `revision` |
| `subscribePlanned`, `subscribeWatched`, `subscribeCompleted`, `subscribeDropped` | страница `items`, `revision`, `sequence`, `refreshing`; затем соответствующее `.updated` событие |
| `unsubscribePlanned`, `unsubscribeWatched`, `unsubscribeCompleted`, `unsubscribeDropped` | `revision` соответствующей коллекции |
| `addPlanned`, `addWatched`, `addCompleted`, `addDropped` | добавленный `anime_id` и revision |
| `removePlanned`, `removeWatched`, `removeCompleted`, `removeDropped` | `anime_id`, `removed` и revision |
| `isFavorite` | `anime_id`; ответ содержит boolean `value` |
| `getStatus` | `anime_id`; ответ содержит integer `status` |
| `getAnilibriaVideos` | `anime_id`; массив `episodes` с доступными `qualities` |
| `getAnimeSaveData` | `save_data` (объект или `null`) |
| `updateAnimeSaveData` | сохранённый `save_data` и `revision` |
| `removeAnimeSaveData` | `anime_id`, `removed` |

Все ответы включают исходный `id` и `ok`. При пустом Anime-кэше `anime:null`,
`loaded:false`; пустой Search использует `results:[]`. Feed имеет форму
`FeedResponse` со значениями по умолчанию.

## Snapshot и события

`subscribeAnime`, `subscribeSearch`, `subscribeFeed` немедленно отдают имеющийся snapshot. Если
запись отсутствует или устарела, в фоне идёт API refresh. При успешном
обновлении сервер отправляет вторую JSON-строку с тем же `id`:

```json
{"id":4,"ok":true,"anime":null,"refreshing":true,"loaded":false}
{"id":4,"event":"anime.updated","ok":true,"anime_id":771,"anime":{"anime_id":771,"title":"Название","players":[]}}
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
`revision` истории меняется при изменении её состава, порядка или прогресса
одного из входящих в неё ID. Карточки могут обновиться при прежнем
`revision`. Для событий используйте `sequence`.

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
отсутствующего ID возвращает `removed:false`. У истории и избранного независимые `revision` и подписки.
В страницах избранного и статусов `save_data` равно `null`; прогресс для
этих экранов можно получить через `getAnimeSaveData`.

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
Отсутствующие ссылки пропускаются. Ошибка возвращается как `ok:false`, `error`, HTTP `status` (ноль для
сетевой ошибки) и `data` с дополнительными сведениями. Запрос синхронный и может выполнить
запрос к yani.tv, затем к основному и резервному хостам AniLibria. Этот источник
не добавляется в `anime.players`: TUI показывает AniLibria отдельно, если alias
присутствует в `anime.remote_ids`. Например:

```json
{"id":26,"ok":true,"episodes":[{"ordinal":1,"name":"Эпизод 1","qualities":[{"quality":"fhd","url":"https://...m3u8"},{"quality":"hd","url":"https://...m3u8"}]}]}
```

## Данные для интерфейса

- Поиск возвращает массив карточек в `results`. Для списка обычно нужны
  `anime_id`, `title`, `year`, `description`, `poster.medium` или
  `poster.big` и `rating.average`. Для открытия деталей передавайте
  `anime_id` в `subscribeAnime`.
- В деталях дополнительно доступны `original`, `other_titles`, `genres`,
  `studios`, `episodes`, `players` и другие поля. Верхнеуровневое
  `episodes` описывает метаданные выпуска. Эпизоды выбранной озвучки лежат
  внутри `players[].dubbers[].episodes`. Номер серии в этом массиве —
  строка. Прогресс сохраняйте по выбранному плееру, озвучке и номеру.
- В `feed` ключи `new`, `announcements`, `recommends` — массивы карточек.
  Также могут использоваться `new_videos`, `top_carousel`, `last_watches`,
  `schedule`, `posts`, `blogger` и `collections`. Для первого экрана
  достаточно трёх массивов карточек. Пустые массивы допустимы.
- Принимайте неполные карточки, пустые строки, пустые массивы и отсутствующие
  пользовательские значения. Считайте `loaded` признаком наличия снимка.
  Полнота карточки и наличие ссылок воспроизведения проверяются отдельно.
  Неизвестные ключи JSON пропускайте.

## Соединение и завершение

Держите соединение открытым, пока ожидаете обновления. При разрыве соединения
сервер больше не может отправить клиенту callback, но обновление кэша может
завершиться. Клиент может подключиться заново и запросить snapshot повторно.
Периодических heartbeat/ping, кроме `hello`, протокол не задаёт.

Сервер пишет команды и IPC-ошибки в терминал. Уровень логирования и формат
терминального вывода не являются частью wire protocol.

## Ограничения и ошибки хранения

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
  `player`, `dubber` и `episode` — непустые строки. Для первых двух максимум
  4096 байт, для `episode` — 256. `time` — целое >= 0.
- При ошибке сохранения возвращается `ok:false`, а не подтверждение прогресса.
  Ошибки до замены файла не меняют память и revisions. Если файл заменён, но
  fsync каталога не удался, `data.committed:true` сообщает, что состояние уже
  изменилось, а долговременная сохранность не подтверждена.
- Повреждённое/неподдерживаемое хранилище блокирует запись до восстановления
  файла и перезапуска. Оригинал не перезаписывается пустыми списками.
- Сервер ограничивает активные соединения 128 и прерывает зависшую отправку
  примерно через пять секунд. Завершённые соединения/threads освобождаются во
  время работы. Активный внешний HTTP может задержать shutdown до своего timeout.

## Как обрабатывать сообщения в приложении

1. Создайте один читатель и последовательную очередь записи на соединение.
   Перед записью зарегистрируйте обработчик request ID. Сначала разбирайте
   `id` и наличие `event`, затем проверяйте `ok` и нужные поля. Ответы и
   события разных запросов могут чередоваться.
2. Для Anime/Search/Feed покажите начальное значение. При `refreshing:true`
   ждите одно терминальное событие. Успешное событие может не содержать
   `refreshing`; `anime.updated` при ошибке тоже может обходиться без этого
   поля. После события завершите локальное ожидание. При ошибке оставьте
   прежние отображаемые данные, если они есть.
3. При смене поиска или уходе с экрана отмените активное ожидание через
   соответствующий `unsubscribe...` с `target_id`. Уже полученное старое
   сообщение отбрасывайте по ID. Для коллекции на том же соединении новая
   подписка того же вида заменяет предыдущую. Начинайте отслеживать
   `sequence` заново для нового ID. Ошибка metadata завершает загрузку
   карточек текущей страницы, сама подписка коллекции остаётся активной.
4. Заменяйте страницу коллекции целиком по `items`, в том числе если
   мутация изменила порядок. Данные `anime:null` можно показать с ID
   или заглушкой. Применяйте событие для действующей подписки, только если
   его `sequence` больше последнего принятого. `revision` относится к
   состоянию коллекции и не подходит для фильтрации событий метаданных.
5. Для запуска видео запросите `getVideoOptions` с `iframe_url` выбранной
   серии и передайте URL выбранного качества медиаплееру. `getVideo` с
   опущенным `quality` запрашивает `"720"`; при отсутствии этого качества
   сервер может выбрать первый доступный поток. Для AniLibria выберите
   серию по `ordinal` и качество прямо из ответа `getAnilibriaVideos`.
   Учитывайте пустой список вариантов. Видеокоманды могут задержать
   обработку следующих команд на том же соединении.
6. Прочитайте позицию через `getAnimeSaveData`. Восстанавливайте серию и
   позицию, если источник, озвучка и номер ещё совпадают с доступными.
   Сохраняйте прогресс отдельной командой. Историю, избранное и статус
   меняйте только через их собственные команды.
7. При EOF, ошибке соединения или тайм-ауте завершите локальное ожидание.
   После переподключения и `hello` заново подпишитесь на открытые экраны.
   После неоднозначного исхода записи перечитайте состояние перед повтором
   мутации. При `data.committed:true` изменение уже применено.

## Минимальная проверка подключения на Linux

При запущенном сервере выполните следующий скрипт стандартным Python 3.
По умолчанию он проверяет только локальный handshake. С аргументом
`--search` после endpoint он дополнительно отправляет поисковый запрос и дожидается результата
обновления, если оно запущено. Этот режим зависит от внешнего API.

```python
import json
import socket
import sys

endpoint = sys.argv[1] if len(sys.argv) > 1 else "/tmp/aniparser.sock"
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
    connection.settimeout(30)
    connection.connect(endpoint)
    with connection.makefile("rb") as reader:
        def send(request):
            connection.sendall(json.dumps(request).encode("utf-8") + b"\n")

        def receive(request_id, event=None):
            while line := reader.readline():
                message = json.loads(line)
                if message.get("id") == request_id and message.get("event") == event:
                    if message.get("ok") is not True:
                        raise RuntimeError(message.get("error", "IPC error"))
                    return message
            raise ConnectionError("IPC connection closed")

        send({"id": 1, "method": "hello"})
        hello = receive(1)
        if hello.get("protocol") != 3 or hello.get("transport") != "json-lines":
            raise RuntimeError(f"Unsupported IPC handshake: {hello}")
        print(hello)

        if "--search" in sys.argv[2:]:
            send({"id": 2, "method": "subscribeSearch",
                  "params": {"q": "магическая битва", "offset": 0, "limit": 10}})
            snapshot = receive(2)
            if snapshot["refreshing"]:
                snapshot = receive(2, "search.updated")
            print(json.dumps(snapshot["results"], ensure_ascii=False, indent=2))
```

Сохраните пример как `ipc_smoke.py` и запустите
`python3 ipc_smoke.py /tmp/aniparser.sock`. Для поиска добавьте `--search`.
В этом примере один запрос выполняется за раз. В приложении один читатель
должен маршрутизировать сообщения всех активных запросов и подписок.

## Что проверить после подключения

- Handshake с версией 3, неверный endpoint и восстановление после разрыва.
- Два клиента подключаются к одному серверу. Изменение коллекции в одном
  приходит событием в другой. После закрытия первого второй продолжает работу.
- Чтение JSON при разбиении строки между пакетами и нескольких строках
  в одном пакете. Для тестов можно использовать локальный mock сервера.
- Холодный и тёплый кэш, успешное обновление, терминальную ошибку и отмену.
- Смену запроса или страницы, игнорирование событий старой подписки и
  замену страницы при изменениях коллекции.
- Отдельную запись прогресса, смену взаимоисключающих статусов и выбор
  качества. Проверки записи проводите с тестовым каталогом данных.
- Работу с реальными провайдерами проверяйте отдельно от IPC и mock-сценариев.
