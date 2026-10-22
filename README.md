# Aniparser Core

Сервер на C++23 получает данные аниме с `api.yani.tv`, держит последние
ответы в памяти процесса и отдаёт их клиентам через IPC. Linux TUI — отдельный
клиент этого IPC. Чтобы подключить собственное приложение, достаточно читать
и писать строки JSON через Unix-сокет; связывать клиент с библиотеками C++ или
устройством внутреннего кэша не нужно.

## Быстрый запуск

Зависимости Linux сборки описаны в `flake.nix`. В Linux с Nix:

```sh
nix develop
cmake -S . -B out/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build out/Debug -j2
```

Запустите сервер и оставьте его работать:

```sh
out/Debug/src/server/aniparser_server /tmp/aniparser.sock
```

На Linux сервер слушает `/tmp/aniparser.sock` по умолчанию. Можно передать
другой путь **первым** аргументом. Сервер принимает только один позиционный
аргумент endpoint: команда `aniparser_server aniparser /tmp/x.sock` выберет
`aniparser`; второй аргумент не используется.
На Linux socket создаётся с правами `0600`, поэтому клиент обычно нужно
запускать от того же Unix-пользователя, что и сервер.

Во втором терминале запустите TUI:

```sh
out/Debug/tui/aniparser_tui /tmp/aniparser.sock
```

Если путь не передан, TUI тоже использует `/tmp/aniparser.sock`. Это Linux-
клиент; для копирования ссылок из меню потоков нужны `wl-copy`, `xclip` или
`xsel`. Клавиши: `/` — поиск, стрелки и Enter — переход по меню, `n` — следующая
страница поиска, Esc — назад, `q` — выход.

## Подключение своего клиента

### Как устроен транспорт

- Linux: Unix domain stream socket, по умолчанию `/tmp/aniparser.sock`.
- Windows: именованный канал `\\.\pipe\aniparser` по умолчанию.
- Android: endpoint обязателен и передаётся первым аргументом сервера.
- Сообщения — JSON Lines: один JSON-объект на строку, завершённую `\n`.
- Каждому запросу задайте `id`. Ответ на запрос и его будущие события
  используют тот же `id`.
- Поддерживайте соединение открытым, если хотите получать события обновления.
  Запросы и ответы могут приходить частями TCP/pipe-потока: клиент должен
  накапливать байты до `\n` и разбирать каждую полную строку отдельно.
- Размер одной входящей серверу строки ограничен 1 MiB.

На Windows сервер создаёт named pipe с именем `\\.\pipe\aniparser`.
Windows-клиент открывает его как duplex byte stream через `CreateFile`, затем
использует тот же JSON Lines формат. POSIX-пример ниже намеренно использует
`AF_UNIX` и работает только на Linux.

Сначала отправьте проверку протокола:

```json
{"id":1,"method":"hello"}
```

Ответ версии 2:

```json
{"id":1,"ok":true,"protocol":2,"transport":"json-lines"}
```

Если `ok` равен `false` или версия протокола неизвестна, клиенту следует
показать ошибку и не отправлять остальные методы.

### Методы

Каждый объект запроса содержит `id` и `method`.

| Метод | Дополнительные поля | Что приходит в ответе |
| --- | --- | --- |
| `hello` | — | `protocol`, `transport` |
| `getFeed` | — | `feed`, `refreshing` |
| `getSearch` | `params` — объект фильтров | `results`, `offset`, `limit`, `refreshing` |
| `getAnime` | `anime_id` — положительное целое | `anime`, `refreshing`, `loaded` |
| `getVideoOptions` | `iframe` — ссылка Kodik | `qualities`: список `{quality,url}` |
| `getVideo` | `iframe`, необязательно `quality` | `url` |

Пример поиска:

```json
{"id":2,"method":"getSearch","params":{"q":"магическая битва","offset":0,"limit":10}}
```

Из `params` можно передавать фильтры из `yani::SearchInput`; полный перечень
есть в [описании API](docs/API.md). Для пагинации задайте `offset` и `limit`.
Сервер вернёт переданные значения (если они не заданы, `offset` будет `0`, а
`limit` — `10`).

Пример получения аниме и разбора видеопотоков:

```json
{"id":3,"method":"getAnime","anime_id":771}
{"id":4,"method":"getVideoOptions","iframe":"//kodikplayer.com/season/1/example/720p"}
```

В `anime` поле `players` имеет вид:

```json
[
  {
    "name":"Kodik",
    "dubbers":[
      {
        "name":"Озвучка",
        "episodes":[
          {"video_id":12,"iframe_url":"//kodikplayer.com/...","number":"1","date":0,"index":0,"skips":{"opening":{"time":"","length":""},"ending":{"time":"","length":""}}}
        ]
      }
    ]
  }
]
```

Поддержанные плееры, озвучки и эпизоды в ответе сгруппированы; эпизоды
отсортированы по номеру по возрастанию. `getVideoOptions` возвращает доступные
качества, например:

```json
{"id":4,"ok":true,"qualities":[{"quality":"720","url":"https://...m3u8"},{"quality":"480","url":"https://...m3u8"}]}
```

Сейчас URL iframe поддерживает только Kodik. Ссылка `url` — поток для
видеоплеера; сервер сам не запускает `mpv`. В отличие от кэшируемых Anime,
Feed и Search, разбор Kodik выполняется прямо при обработке video IPC-запроса;
клиент ждёт ответ, пока парсер загрузит страницу и получит список качеств.

### Кэш и два ответа на запрос

`getFeed`, `getSearch` и `getAnime` сначала возвращают то, что уже есть в
серверном кэше. Пустой кэш не блокирует клиента до ответа внешнего API: он
получит пустой снимок (для Anime — `null`) и `refreshing: true`. После
успешного запроса сервер отправит вторую строку-событие с тем же `id`:

```json
{"id":3,"ok":true,"anime":null,"refreshing":true,"loaded":false}
{"id":3,"event":"anime.updated","ok":true,"anime_id":771,"anime":{...}}
```

Для остальных методов события называются `feed.updated` и `search.updated`.
Событие может прийти сразу после первоначального ответа. Клиенту нужно
маршрутизировать сообщения по `id`, а не полагаться на время прихода.

Кэш существует только внутри процесса сервера, очищается при его завершении и
считается устаревшим через 5 минут. Успешные результаты поиска и feed также
заполняют краткие сведения об аниме, чтобы `getAnime` мог сразу вернуть
частичный объект. После API-обновления приходит полный объект. При ошибке
обновления сервер сохраняет последнюю успешную версию. Ошибка Anime приходит
событием `anime.updated` с `ok:false` и `error`; ошибки Feed/Search сейчас
только пишутся в лог сервера.

Клиент владеет полученными JSON-значениями: запросов `release`, `delete` или
подтверждения владения нет.

### Минимальный Python-клиент для Linux

Пример проверяет handshake и получает полную карточку Anime. Первое сообщение
содержит текущий snapshot; если он устарел или отсутствует, пример ожидает
update event не дольше 40 секунд. В полноценном приложении используйте event
loop и маршрутизируйте сообщения по `id`.

```python
import json
import socket

path = "/tmp/aniparser.sock"
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(path)
reader = sock.makefile("r", encoding="utf-8")

def send(request):
    sock.sendall((json.dumps(request, ensure_ascii=False) + "\n").encode())

def receive():
    line = reader.readline()
    if not line:
        raise ConnectionError("aniparser_server закрывает соединение")
    return json.loads(line)

send({"id": 1, "method": "hello"})
hello = receive()
if not hello.get("ok") or hello.get("protocol") != 2:
    raise RuntimeError(f"Несовместимый IPC: {hello}")

send({"id": 2, "method": "getAnime", "anime_id": 771})
snapshot = receive()
print("snapshot:", json.dumps(snapshot, ensure_ascii=False, indent=2))

if snapshot.get("refreshing"):
    sock.settimeout(40)
    try:
        update = receive()
        if update.get("id") == 2 and update.get("event") == "anime.updated":
            print("update:", json.dumps(update, ensure_ascii=False, indent=2))
    except socket.timeout:
        print("API update did not arrive before the client timeout")

reader.close()
sock.close()
```

Для полноценного клиента обработайте отключение и ошибки JSON, не смешивайте
ответы разных `id`, а `*.updated` применяйте как новый снимок. Образец клиента
на C++ находится в `tui/ipc_client.hpp`; навигация TUI — в `tui/tui_app.cpp`.

## Проект и документация

- `src/api/` — HTTP-клиент, модели ответов API и извлекатель Kodik.
- `src/core/` — процессный кэш и очередь фоновых API-задач.
- `src/server/` — фасад приложения и IPC-сервер.
- `src/app/` — ручная интеграционная программа и автономные проверки моделей.
- `tui/` — независимый Linux-клиент IPC на Notcurses.
- `docs/` — подробное описание модулей и правила разработки.

Начните с [карты документации](DOCS.md), [архитектуры](docs/ARCHITECTURE.md) и
[спецификации IPC](src/server/IPC.md). Для изменений моделей см.
[API-модели](docs/API_MODELS.md); для изменений клиента — [TUI](tui/README.md).
