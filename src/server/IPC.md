# IPC protocol, version 2

Эта страница — краткая спецификация для автора клиента. Полный пример запуска
и Python-клиент находятся в корневом [README](../../README.md#подключение-своего-клиента).

## Транспорт и framing

- Linux: Unix domain stream socket; default `/tmp/aniparser.sock`.
- Windows: named pipe; default `\\.\pipe\aniparser`.
- Каждое сообщение — UTF-8 JSON object и завершающий байт LF (`\n`).
- Stream не сохраняет границы `send`/`recv`. Клиент собирает буфер и разбирает
  его только по LF; один `recv` может содержать часть строки или несколько строк.
- Максимальный размер одной команды сервера — 1 MiB.
- Первым запросом следует отправить `hello`. Протокол сейчас версии `2`.
- Клиент создаёт `id` и использует его для сопоставления ответов. Рекомендуется
  уникальное целое число на одном соединении.

Windows-клиент открывает pipe как byte stream с duplex-доступом (`CreateFile`)
и применяет тот же newline framing. На Unix клиент подключается к пути через
`AF_UNIX`, `SOCK_STREAM`.

Пример handshake:

```json
{"id":1,"method":"hello"}
{"id":1,"ok":true,"protocol":2,"transport":"json-lines"}
```

Успешный ответ содержит `ok:true`; ошибка — `ok:false` и человекочитаемое
`error`. Ошибки разбора также по возможности содержат request `id`.

## Запросы

```json
{"id":2,"method":"getFeed"}
{"id":3,"method":"getSearch","params":{"q":"магическая битва","offset":0,"limit":10}}
{"id":4,"method":"getAnime","anime_id":771}
{"id":5,"method":"getVideoOptions","iframe":"//kodikplayer.com/season/1/example/720p"}
{"id":6,"method":"getVideo","iframe":"//kodikplayer.com/season/1/example/720p","quality":"720"}
```

### Ответы

| Метод | Поля успешного ответа |
| --- | --- |
| `hello` | `protocol:2`, `transport:"json-lines"` |
| `getFeed` | `feed`, `refreshing` |
| `getSearch` | `results`, `offset`, `limit`, `refreshing` |
| `getAnime` | `anime`, `refreshing`, `loaded` |
| `getVideoOptions` | `qualities` — массив `{quality,url}` |
| `getVideo` | `url` |

Все ответы включают исходный `id` и `ok`. При пустом Anime-кэше `anime:null`,
`loaded:false`; пустой Search использует `results:[]`. Feed имеет форму
`FeedResponse` со значениями по умолчанию.

## Snapshot и события

`getAnime`, `getSearch`, `getFeed` немедленно отдают имеющийся snapshot. Если
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
`anime:null`, `error` и `status`. Ошибка Feed/Search пока не вызывает событие
клиенту; сервер пишет её в свой stderr. При `refreshing:false` нового события
для этого запроса не ожидается.

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
Сейчас это Kodik. Episode URL — это iframe, а не прямая ссылка видео. Запрос
`getVideoOptions` превращает iframe в доступные прямые потоки; разрешения —
строки, например `"720"`. Видеопарсер вызывается синхронно в обработчике
клиента; ответ может задержаться на время HTTP-запросов к Kodik.

## Соединение и завершение

Держите соединение открытым, пока ожидаете обновления. При разрыве соединения
сервер больше не может отправить клиенту callback, но обновление кэша может
завершиться. Клиент может подключиться заново и запросить snapshot повторно.
Периодических heartbeat/ping, кроме `hello`, протокол не задаёт.

Сервер пишет команды и IPC-ошибки в терминал. Уровень логирования и формат
терминального вывода не являются частью wire protocol.
