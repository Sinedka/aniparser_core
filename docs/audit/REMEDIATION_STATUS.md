# Исправления по результатам аудита

Документ относится к последующим исправлениям рабочего дерева. Исходный
FINDINGS.md сохранён как исторический отчёт; его строки и hashes не являются
указателями на новую реализацию. Существующие пользовательские изменения
сохранены. Снимок перед исправлениями находится под ignored out/repair-baseline.

## Что изменилось

| ID | Реализация | Проверка / предел вывода |
| --- | --- | --- |
| AUD-001 | `Result<bool>` для мутаций; кандидат записывается до публикации памяти/revisions | отказ каталога и rename; чтение памяти, restart, реальный IPC |
| AUD-002 | callbacks вне persistence/state locks; owning notifications FIFO | повторная мутация из callback и sequence |
| AUD-003 | независимая отмена I/O, deadline записи; Windows overlapped I/O | Unix slow-reader + SIGTERM; Windows runtime не проверен |
| AUD-004 | listener не изменяется/не закрывается до join accept thread; poll wakeup | static review и Unix stop; TSan runtime недоступен |
| AUD-005 | общий компилируемый dispatcher вместо разных helper names | Linux компилирует общий код; native Windows build остаётся обязательным |
| AUD-006 | reaping завершённых соединений, cap 128, обработка thread creation failure | 300 reconnects и последующий рабочий запрос |
| AUD-007 | sentinel/нечисловые значения исключены из partial merge | missing/null/вложенные числовые поля |
| AUD-008 | общие поля full-card обновляются Search/Feed без удаления details/videos | новая Search-карточка и повторный snapshot |
| AUD-009 | Core всегда загружает Anime с videos; прямой API false сохранён | backend fixture проверяет переданный флаг |
| AUD-010 | initial callbacks изолированы, completions упорядочены | бросающие Anime/Search/Feed initial callbacks |
| AUD-011 | terminal Search/Feed errors; metadata error snapshots без retry loop | Core, все шесть коллекций, Search/Feed через настоящий IPC |
| AUD-012 | общий type/range validator до get/cast; API numeric bounds | float, negative, uint64 overflow, huge float, неверный тип |
| AUD-013 | удаляется весь UTF-8 code point | ASCII/кириллица/emoji/empty; настоящий UI ввод |
| AUD-014 | null представляет loading, не передаётся object accessors | cold Anime с последующим full update в IPC и PTY |
| AUD-015 | проверка границ; owning navigation frames; восстановление модели и меню | поиск из вложенного меню, child viewing order, resize |
| AUD-016 | Notcurses context под unique_ptr/custom deleter | нормальный UI exit; scope cleanup при исключениях по коду |
| AUD-017 | request deadline, EOF, cancellation, ограниченные buffers, UI polling | silent/fragmented peer, events-before-reply, cancel, EOF, PTY |
| AUD-018 | Result загрузки, защита от unsupported/corrupt/unreadable файла | v1–4, v5, truncated JSON, неверные записи, сохранность оригинала |
| AUD-019 | exclusive temporary file, fsync+rename+directory fsync; Windows flush/replace | обычная запись/readback; power-loss/fault-injection не выполнялись |
| AUD-020 | 10 000 новых progress ID, явное removeAnimeSaveData, лимиты строк | cap, update at cap, delete→insert, restart; без автоматической эвикции |
| AUD-021 | общий protocol.hpp и request_validation.hpp | одинаковая бизнес-маршрутизация в обоих transport-ах |
| AUD-022 | назначение legacy test.cpp явно документировано; реальные checks запускаются отдельно | legacy не удалён и не включён в build |
| AUD-023 | status последней попытки и `data.attempts` AniLibria | source review; live provider и mock HTTP fallback не исполнялись |
| AUD-024 | submit немедленно отклоняет полную очередь | детерминированный nested-submit test |
| AUD-025 | full update заменяет модель; пустые players действительно удаляются | PTY full update→empty и безопасный Enter |

Это статус внесённой реализации и её проверки, а не заявление, что каждая
строка протестирована на всех ОС. В частности, Windows изменения ещё нельзя
считать платформенно подтверждёнными.

## Дополнительные исправления, выявленные при реализации

- Быстрый coalesced refresh мог обогнать initial callback. Повторное чтение
  freshness при сериализации также могло ошибочно закрыть stream. CallbackQueue
  ставит initial первым и исполняет callbacks без своего mutex; initial
  refreshing вычисляется под mutex Core. Есть детерминированный двухподписочный
  regression case, проверяющий и порядок, и один API-вызов.
- Возврат из viewing order ранее восстанавливал экран с моделью дочернего
  аниме. Navigation frames теперь восстанавливают родительскую модель. Глубина
  вложенных карточек ограничена 64 с явной ошибкой, без выхода за индексы.
- Очистка Unix endpoint проверяет сохранённый inode/type и не удаляет
  подменённый обычный файл; неудачный bind не удаляет путь другого listener.
- Framing проверяет лимит одной JSON-строки, а не суммарный pending buffer:
  валидная строка 1 MiB с началом следующей команды больше не отклоняется.
- Примитив/null вместо корня API-модели отклоняется; Anime/Search должны
  содержать положительный ID. Numeric conversions проверяются до cast.
- Async API callback охватывает исключения parse_response и conversion;
  conversion error сохраняет response data.
- Классы с iterator indexes запрещают копирование; list/index сохраняют
  согласованность при исключении добавления/загрузки.
- При разрушении Core ещё не начатые задачи отбрасываются, активные workers
  join-ятся до уничтожения состояния. Захваты отменённых задач разрушаются
  вне mutex очереди. Обычный TaskPool shutdown без discard сохраняет drain.
- Ожидающие Anime/Search/Feed streams ограничены 256 на соединение;
  повторная активная пара method/id отклоняется без замены старого ожидания.

## Важные контракты для пользователя и C++ caller

1. Failed save до rename не меняет память/revisions. Если rename уже выполнен,
   но directory fsync не подтвердился, возвращается ошибка с
   `data.committed:true`, а память соответствует файлу. Повторить чтение и
   не считать ошибку доказательством отсутствия изменения.
2. Повреждённый или неизвестный файл автоматически не «чинится» пустыми
   списками. Восстановить/исправить оригинал и перезапустить сервер.
3. При полном лимите прогресса новые ID отклоняются; старые не удаляются.
   Явное удаление не меняет историю/избранное/статусы.
4. C++ mutation API возвращает `Result<bool>`: наличие Result и boolean
   значение надо проверять отдельно. Wire protocol остаётся версии 3:
   успешные формы сохранены, ошибки/валидация уточнены, remove добавлен.
5. TUI request имеет deadline 100s, поддерживает q/resize во время ожидания.
   Esc отменяет request и закрывает IPC; поздний ответ не будет принят за новый.
   После disconnect требуется перезапуск клиента.
6. Активный внешний HTTP может задержать shutdown до своего timeout.
   Тест остановки за 3s относится к I/O slow-reader без активного HTTP.

## Проверка

```sh
cmake -S . -B out/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build out/Debug -j2
out/Debug/src/app/my_app --self-test
out/Debug/src/app/aniparser_checks
python3 tests/ipc_regression.py
python3 tests/tui_smoke.py
git diff --check
```

`aniparser_checks` использует подставленный Core::ApiBackend, не сеть.
`ipc_regression.py` запускает fixture и production серверы с disposable storage
под out/. На Linux он также запускает `tui_client_checks`, не связанный с
backend/Notcurses. PTY smoke использует отдельный mock IPC, без HTTP/clipboard
subprocess; terminal.log и requests.json сохраняются под out/tui-checks-*.
Terminal transcript — запись байтов сеанса, не screenshot терминального GUI.
После согласованного переключения notcurses-core default PTY run прошёл без
SDL workaround; последний Debug error-path run — out/tui-checks-_m_mn9ge.

ASan/UBSan build: `out/Sanitize`, все backend static libraries инструментированы,
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`.
Внутри песочницы LeakSanitizer несовместим с ptrace, поэтому там выключен.
Отдельный запуск того же instrumented executable вне песочницы, с LSan по
умолчанию, прошёл все 50 checks без сообщений об утечках. Это проверка
выполненных сценариев, не доказательство отсутствия всех возможных утечек.
ThreadSanitize target собран; первый runtime запуск завершился до тестов:
`FATAL: ThreadSanitizer: unexpected memory mapping`. Повторный запуск
актуальной сборки вне песочницы дал ту же ошибку, exit 66, до выполнения checks.
Это ограничение окружения,
не успешный TSan run и не обнаруженная им data race.

## Оставшиеся проверки

- Native Windows build/run: named pipe cancellation, shutdown, ACL и storage.
- Рабочий TSan runtime; расширение проверки утечек на другие сценарии.
- Реальные yani.tv/Kodik/Aksor/AniLibria; HTTP fault injection/fallback.
- Power-loss recovery и ошибки fsync/close после rename на целевых FS.
- Другие терминалы, Wayland/X11 clipboard и macOS/Android/iOS.
- Полный security/vulnerability scan сторонних зависимостей не выполнялся.

## Финальные результаты

- Debug: все цели собраны; my_app self-test — 10 PASS, aniparser_checks — 50 PASS.
- Unix IPC: 12 групп проверок, включая 300 reconnects, slow-reader shutdown,
  JSON Lines 1 MiB, terminal errors Search/Feed, progress deletion и storage errors.
- Linux TUI client: 5 проверок framing/deadline/cancellation/EOF, без backend.
- ASan/UBSan: 50 regression + 10 self-test PASS; тот же IPC suite с
  instrumented fixture/production servers также прошёл без диагностик.
- LeakSanitizer вне sandbox: 50 regression PASS, exit 0, без leak reports.
- PTY: cold loading/error, players/dubbers/episodes/links, UTF-8 Backspace,
  возврат из поиска/viewing order, authoritative empty update, resize и q.
- git diff --check: без ошибок; clang-format применён только к изменённым C++.

По согласованию с пользователем TUI переключён на уже установленный
notcurses-core и notcurses_core_init; неиспользуемая связь с CPR удалена.
Новые сторонние пакеты не добавлялись. Это устраняет прежнюю аварийную загрузку
SDL multimedia library до main. Backend CPR остаётся без изменений.

Основные логи: out/repair-checks.log, repair-asan-checks.log,
repair-lsan-checks.log, repair-debug-build.log, repair-sanitize-build.log.
Записи реального IPC и терминала находятся в out/ipc-checks-* и out/tui-checks-*.


### Instrumented TUI

`python3 tests/tui_smoke.py --build out/Sanitize --sanitizers` — PASS,
exit 0, out/tui-checks-z2ye4_fy. Проверены также cold Anime failure и завершение
loading state. Для совместимости с альтернативным signal stack Notcurses
тест задаёт `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:use_sigaltstack=0`
и `UBSAN_OPTIONS=halt_on_error=1`. Обработчики обычного приложения не менялись.
Первый запуск с default ASan stack settings завершился при остановке в
`AsanThread::Destroy/UnsetAlternateSignalStack` с `unable to unmmap`
(out/tui-checks-9khwqeg3), после выполнения UI-сценария. Это отдельно от
успешного LSan run Core/API regression executable; утечки multimedia/terminal
зависимостей этим TUI-прогоном не проверены.

### Воспроизводимая sanitizer-сборка

```sh
cmake -S . -B out/Sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS_DEBUG=-g1 \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build out/Sanitize -j2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  out/Sanitize/src/app/aniparser_checks
python3 tests/ipc_regression.py --build out/Sanitize --sanitizers
python3 tests/tui_smoke.py --build out/Sanitize --sanitizers
```

Все production changes прошли сборку. Проверка реальных provider endpoints,
Windows и полноценный TSan анализ остаются ограничениями, перечисленными выше.
