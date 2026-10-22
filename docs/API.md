# API-клиент

`src/api/` отвечает только за внешний HTTP API и работу с видеоплеерами. Он не
знает про IPC-сокет, UI или кэш Core.

## Файлы и публичные функции

| Файл | Назначение |
| --- | --- |
| `api.hpp` / `api.cpp` | Запросы к `https://api.yani.tv/`, таймауты, разбор ответа и `ApiError`. |
| `models.def` | Единственный список полей API-моделей. |
| `models.hpp` | Создаёт C++-структуры из `models.def` макросами. |
| `json.hpp` | Создаёт `from_json`/`to_json` из того же списка полей. |
| `video_extractor.hpp` | Общий интерфейс извлекателя ссылок и тип `VideoQuality`. |
| `video.cpp` | Реестр video extractors и функции `getVideoOptions`, `getVideo`. |
| `kodik.hpp` / `kodik.cpp` | Проверка Kodik iframe, запрос player API и декодирование ссылок. |

Основные функции пространства имён `yani`:

```cpp
yani::Result<yani::AnimeResult> yani::getAnime("771", true);
yani::Result<std::vector<yani::SearchResult>> yani::searchTitles(input);
yani::Result<yani::FeedResponse> yani::getFeed();
yani::Result<std::vector<yani::VideoQuality>> yani::getVideoOptions(iframe);
yani::Result<std::string> yani::getVideo(iframe, "720");
```

`Result<T>` — это `std::expected<T, ApiError>`. При успехе читайте `*result`;
при ошибке — `result.error().status`, `.message` и `.data`. Нулевой статус
обычно означает сетевую ошибку, timeout или ошибку разбора до получения HTTP
статуса. Для сетевой ошибки `.data` пустой.

Есть и Async-варианты (`getAsync`, `getAnimeAsync`, `searchTitlesAsync`,
`getFeedAsync`), возвращающие `std::future<Result<T>>`. Core использует
синхронные функции в собственном worker pool — не создавайте второй пул без
понимания того, где будет ожидаться future.

## HTTP-поведение

- Базовый URL: `https://api.yani.tv/`.
- Язык запроса задаётся заголовком `Lang: ru`.
- Таймаут установления соединения: 15 секунд; полный timeout: 30 секунд.
- У Kodik iframe/player/API запросов собственные параметры в `kodik.cpp`:
  connect timeout 5 секунд и полный timeout 30 секунд.
- Anime: `GET /anime/{id-or-alias}?need_videos=true|false`.
- Search: `GET /anime` с фильтрами в query string.
- Feed: `GET /feed`.
- HTTP-клиент сначала проверяет сетевую ошибку CPR, затем парсит JSON и
  требует HTTP 2xx. Ответ API обычно содержит нужную модель в `response`.

Списки фильтров преобразуются в строку через запятую. Bool передаётся как
`true`/`false`; числа форматируются в строку. Не заданные (`std::nullopt`)
фильтры не попадают в URL. Клиент не проверяет допустимость значений фильтра
на стороне сайта.

### Фильтры поиска

Поля `yani::SearchInput` находятся в `src/common/types.hpp`:

| Поля | Тип | Пример смысла |
| --- | --- | --- |
| `q` | `string` | Текст названия. |
| `ids`, `studio_ids`, `director_ids`, `worldart_ids`, `mal_ids`, `kp_ids`, `shikimori_ids` | `vector<int64_t>` | ID аниме или связанных сущностей. |
| `min_age` | `vector<int64_t>` | Возрастные категории. |
| `min_rating_counters`, `max_rating_counters` | `int64_t` | Нижняя/верхняя граница количества голосов. |
| `season`, `status`, `types`, `genres`, `exclude_genres` | `vector<string>` | Категории и жанровые фильтры. |
| `min_rating`, `max_rating` | `double` | Диапазон оценки. |
| `ep_from`, `ep_to`, `to_year` | `int64_t` | Ограничения по эпизодам и году. |
| `sort`, `sort_forward` | `string`, `bool` | Сортировка. |
| `offset`, `limit` | `int64_t` | Пагинация. |

Core использует все поля при построении ключа Search-кэша. TUI отправляет
`offset` и `limit: 10`, но это правило TUI, а не HTTP-клиента.

## Обработка API-моделей

Структуры объявлены в `models.def`, каждая строка описывает тип, имя C++-поля
и, если нужно, внешний ключ JSON. Например, поле `class_` связано с ключом
`class`, а `new_` — с ключом `new`. Макросы в `models.hpp` и `json.hpp`
используют одну схему, чтобы объявления и сериализация не разъезжались.

`yani_get<T>` применяет значение по умолчанию для отсутствующего/null поля.
Число может прийти как JSON-число или как полностью разбираемая числовая
строка. Строковые поля принимают строку и число. Вектор строк принимает массив
строк/чисел. Остальные типы передаются конвертеру nlohmann-json. Ошибка
конвертации дополняется именем API-поля.

Некоторые sentinel-defaults исторически необычны: отсутствующий `int64_t` —
`INT64_MIN`, `double` — `-1`, `bool` — `true`. Не трактуйте это как обычные
пользовательские значения; если меняете правила, обновите `json.hpp` и
`API_MODELS.md`.

Полный перечень полей находится в [справочнике моделей](API_MODELS.md).

## Видео и добавление нового плеера

Публичный интерфейс extractor — `VideoExtractor`:

1. `supports(iframe)` должен безопасно и точно распознавать домен плеера.
2. `extract(iframe)` возвращает `Result<vector<VideoQuality>>`.
3. Каждый `VideoQuality` содержит строковое разрешение (`"720"`) и прямой URL
   потока.
4. Добавьте реализацию и зарегистрируйте её в `src/api/video.cpp`.
5. Добавьте тестовые случаи для разрешённых и похожих/поддельных доменов.

Сейчас в реестре только Kodik. Извлекатель загружает iframe, находит параметры
player API, получает JS с динамическим API-путём, вызывает API с Origin/Referer,
декодирует URL и сортирует качества по убыванию. IPC и TUI используют
`getVideoOptions`; Kodik-парсинг не происходит при простом `getAnime` API
запросе.

Внешний API требует сеть. Для ручной проверки:

```sh
out/Debug/src/app/my_app --test-get-anime
```

Если запрос не проходит, сравните лог с эквивалентным `curl`, проверьте DNS,
доступ к API и timeout. Ошибка `Connection timed out` до HTTP-ответа не говорит,
что JSON-модель повреждена.
