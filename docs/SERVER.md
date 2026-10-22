# Server и IPC

`src/server/` — мост между `Core` и внешним клиентом.

- `Server` (`server.hpp/.cpp`) — фасад над Core плюс video extractor calls.
- `IpcServer` — transport и парсер JSON Lines.
- `ipc_server.cpp` — Linux/Unix domain socket.
- `ipc_server_windows.cpp` — Windows named pipe.
- `main.cpp` — создаёт Core → Server → IpcServer и ждёт SIGINT/SIGTERM.

Клиент связывается с протоколом, а не с C++-типами: изменение JSON-формы
требует изменить `Server::animeToJson`/IPC и документацию, но не требует линковать
Core в клиент.

## Запуск сервера

```sh
out/Debug/src/server/aniparser_server /tmp/aniparser.sock
```

Первый аргумент — весь endpoint. На Linux endpoint по умолчанию
`/tmp/aniparser.sock`; Windows default — `\\.\pipe\aniparser`. Дополнительный
аргумент не является вторым путём или именем кэша. На Linux server удаляет
оставшийся socket-файл, только если проверка показывает, что socket устарел;
обычный файл с таким именем не удаляется. Доступ к созданному socket ограничен
правами `0600`.

При запуске сервер печатает endpoint, команды IPC (`[ipc command]`) и ошибки
(`[ipc error]`). API worker пишет API ошибки и команду curl для воспроизведения
Anime-запроса. Логи идут в stderr, кроме строки `IPC server listening ...`,
которая идёт в stdout.

## Методы

Все сообщения — JSON-объекты, по одному на строку. Запрос имеет `id` и `method`.
Один request id может иметь несколько ответных сообщений: первоначальный
snapshot и событие обновления.

| Метод | Поля | Синхронный ответ | Событие после обновления |
| --- | --- | --- | --- |
| `hello` | нет | `{ok:true, protocol:2, transport:"json-lines"}` | нет |
| `getAnime` | `anime_id` | `anime`, `refreshing`, `loaded` | `anime.updated` |
| `getSearch` | `params` | `results`, `offset`, `limit`, `refreshing` | `search.updated` |
| `getFeed` | нет | `feed`, `refreshing` | `feed.updated` |
| `getVideoOptions` | `iframe` | `qualities: [{quality,url}, ...]` | нет |
| `getVideo` | `iframe`, optional `quality` | `url` | нет |

Успешное начальное сообщение содержит `ok:true` и `id`. Если снимка ещё нет,
Anime отправляется как `null`, Search — с пустым `results`, Feed — с пустыми
массивами/объектами модели. Поле `refreshing` показывает, выполняется ли
обновление сейчас. Для Anime `loaded` говорит, есть ли вообще snapshot.

### Ошибки

Ошибка запроса имеет `ok:false`, `id` и `error`; для Anime API update — также
`event:"anime.updated"`, `anime_id`, `anime:null`, `status`.

IPC проверяет JSON, тип method и обязательные поля. Неподдерживаемый метод,
неверный JSON, отсутствующий `anime_id`/`iframe` или неверный тип поля дают
ошибочный ответ. Входная строка длиннее 1 MiB отклоняется и закрывает клиента.

Anime API ошибка доставляется callback-событием. При ошибке Search/Feed
немедленный ответ всё равно уже отправлен, но update-событие сейчас не
формируется; проверьте терминал сервера. Это известная особенность протокола,
которую нужно учесть, если будете добавлять единый канал ошибок.

## Anime JSON

API возвращает `AnimeResult.videos`, плоский список записей. Перед IPC
`Server::animeToJson` удаляет ключ `videos` и добавляет сгруппированный массив:

```json
{"players":[{"name":"Kodik","dubbers":[{"name":"Озвучка","episodes":[{"video_id":1,"iframe_url":"//kodikplayer.com/...","number":"1","date":0,"index":0,"skips":{"opening":{"time":"","length":""},"ending":{"time":"","length":""}}}]}]}]}
```

Записи без поддержанного плеера, имени плеера, имени озвучки или корректной
iframe-ссылки не попадут в группы. Эпизоды сортируются по номеру как числа; если
номер нельзя прочитать как число, используется лексикографический порядок.
Порядок плееров/озвучек соответствует первому появлению в API-массиве.

## Видео

`getVideoOptions` принимает iframe, проверяет extractor registry в
`src/api/video.cpp`, и для поддержанного плеера получает доступные URL.
Пока зарегистрирован только Kodik. Ошибка неподдержанного домена возвращается
как `ok:false`. `getVideo` выбирает указанное качество; если такого качества
нет, API helper возвращает первый доступный поток. `getVideoOptions` предпочтителен,
когда клиенту нужно показать варианты.

## Куда вносить изменения

- Новый IPC method: добавить validation/handler в оба transport-файла,
  обновить [src/server/IPC.md](../src/server/IPC.md), этот документ и клиент.
- Новое поле JSON Anime: изменить сериализацию, IPC-справочник и все клиенты.
- Новый video player: реализовать общий `VideoExtractor` и зарегистрировать его;
  серверная форма меню автоматически получает только поддержанные записи.
- Политика cache/refresh относится к Core, не переносите её в IPC.
