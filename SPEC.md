# POE2FlaskHelper — спецификация v1.2

Дата: 2026-11-07; правки v1.1 датированы 2026-11-08 (этап 5). Статус: приложение
реализовано и работает, v1.1 отражает реализованное. Дополнение v1.2 (hot-reload
`config.json` + `reload.wav`) датировано 2026-11-08 и отражает реализованное в выпуске
1.2.0. Правки v1.1: §2.3 (права запуска в README), §3 (дерево
как реализовано, ассеты финальны), §3.1 («Тестируемость»), §4.5 (звук при
startup-enable), §9 (критерий 12). Дополнено 2026-11-08 (minors группа E2): §4.2
(амеднт про стартовый файловый I/O, BR-7), §7 (debug-запись часового сбоя видна только
при level=debug, BR-3). Дополнено 2026-11-08 (live-тесты, группа L1): §4.7 и §7 —
press-строка и единственная held-строка окна кулдауна пишутся на info, §9 критерий 8;
§2.3 и §3 — `README.txt` перенесён в `res/`.
Дополнено 2026-11-08 (версионирование): §2.2 (версионные гейты, README из .in),
§2.4 (новая — формат номера, правила, version.sh и гейт), §3 (дерево), §9 (критерий 14).
Дополнено 2026-11-08 (hot-reload `config.json`, план [`plan-hot-reload.md`](plan-hot-reload.md),
выпуск 1.2.0): §5 (переписано «читается один раз на старте» — 5 с цикл, штамп
`LAST_WRITE`+size, живые параметры, битый JSON в runtime без MessageBox, amend правила
старта), §4.2 (поправка про 5 с I/O на главном потоке, прецедент BR-7), §4.5
(`reload.wav`, звук только на успешный parse+apply и не на старте), §7 (reload-строки
info/warn/error), §8 (битый `config.json` в runtime), §9 (критерий 15), §3 (новые
файлы и длительности ассетов 0.300 / 0.633 / 0.387 с), §2 (ресурсы — третий WAV), §2.3
(README: раздел о правке `config.json` на лету, EN+RU, и описание в Files —
«проверяется каждые 5 секунд»). Дополнено 2026-11-08 (minors закрытия, выпуски 1.2.1/1.2.2):
§3 дерево — `src/win_text.h` (единый exe-слойный to_wide, m10).

## 1. Назначение и границы

Трей-приложение для Windows x64: читает HP/Mana/ES/Ward/Rage из памяти
`PathOfExile.exe` и при падении процента ниже порога отправляет игре клавишу зелья —
так, как будто игрок нажал её сам.

В объёме приложения:

- иконка в трее, автозапуск в свёрнутом виде (без окон);
- один глобальный хоткей — включение/выключение модуля сканирования;
- звуковое подтверждение включения и выключения (WAV из ресурсов);
- чтение Vital'ов по полному раскладу `offsets.json`;
- активация зелий с порогом и кулдауном на каждый Vital;
- JSON-конфиг и лог рядом с исполняемым файлом;
- сборка `build.sh` из cygwin и portable zip.

Вне объёма приложения: любые окна (включая placeholder), редактор конфига, оверлей,
профили, автозапуск с Windows, поиск смещений внутри C++ (остаётся Python-инструментом).
Будущий редактор конфига описан в [`backlog.md`](backlog.md).

## 2. Стек и сборка

| Пункт | Решение |
|---|---|
| Язык | C++20, Win32 API, UNICODE, только стандартная библиотека и system DLL |
| Архитектура | x64, подсистема WINDOWS |
| CRT | динамическая (`/MD`, `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL`) |
| Компилятор | MSVC 14.44 из `C:\Program Files (x86)\Microsoft Visual Studio\2022\BT2022` |
| Сборка | CMake 3.31 + Ninja из состава BT2022 |
| Флаги | `/W4 /utf-8 /permissive- /EHsc /GR-`; Release: `/O2 /Gy` |
| Линковка | `/OPT:REF /OPT:ICF /INCREMENTAL:NO /DEBUG:NONE`, без PDB |
| Библиотеки | линкуются по target: `user32 kernel32 shell32 psapi gdi32 winmm`; shipped exe импортирует `USER32 KERNEL32 SHELL32 WINMM` (psapi/gdi32 линкуются по target, их импорты вырезает `/OPT:REF` — см. примечание ниже) |
| Ресурсы | `app.rc`: иконка, VERSIONINFO, манифест (per-monitor DPI v2), `enable.wav`, `disable.wav`, `reload.wav` (три WAVE-листья ids 102/103/104, именованный тип `WAVE` + язык 0x409 — путь поиска `play_wav`, §4.5; проверка дерева ресурсов — `research/pe_res_check.py`) |

`advapi32` не используется: реестр, Event Log, права доступа и криптография не нужны.
Изменено 2026-11-08 (minors группа E1, S5-3): список §2 — это линкуемые по target библиотеки
(CMakeLists ограничивает линк-линию этим набором, S0-3). Штатный shipped exe импортирует
`USER32`, `KERNEL32`, `SHELL32`, `WINMM` и CRT-DLL: `psapi`/`gdi32` ни один целевой файл не
вызывает, и `/OPT:REF` вырезает их неиспользуемые импорты — список импортов бинарника
проверяет `research/pe_res_check.py` (он же ловит и дефолтные CMake-библиотеки).

### 2.1 VC++ v14 x64 runtime

При `/MD` реализация CRT и стандартной библиотеки живёт не в exe, а в
`VCRUNTIME140.dll`, `VCRUNTIME140_1.dll` и `MSVCP140.dll`, которых нет в Windows.
Их ставит Visual C++ v14 x64 Redistributable. В zip его не кладём, но в `README.txt`
указываем как требование. Статическая линковка (`/MT`) не используется: спека
фиксирует динамическую.

### 2.2 `build.sh`

Единственная точка входа, нативный bash под cygwin:

1. Определяет каталог скрипта и переходит в него; пути для Windows-инструментов
   конвертирует через `cygpath -w`.
2. Находит `vcvars64.bat`: сначала через `vswhere.exe`, затем по стандартному пути
   BT2022. Генерирует временный `.cmd` и выполняет его через `cmd.exe //c`.
3. В том же cmd: добавляет VS-cmake и ninja в `PATH`, выполняет
   `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` и
   `cmake --build build`.
4. Guard python (S0-1, перенесён выше 2026-11-08 — версии-гейты тоже на python): нет
   python в PATH → FATAL до упаковки.
5. Версионные гейты (SPEC §2.4, добавлено 2026-11-08), перед packaging:
   - `build/version.txt` равен контракту `project(... VERSION x.y.z ...)` из
     `CMakeLists.txt`;
   - `./version.sh check` — согласованность «код ↔ версия» по закоммиченной истории
     (красых → packaging не происходит);
   - stdout `POE2FlaskHelper.exe --version` = ровно `POE2FlaskHelper <version>`
     (пустой stdout — предупреждение: `print_version` печатает только при успешном
     `AttachConsole`, §2.3);
   - `python research/pe_res_check.py "$EXE" "$VERSION"` — VERSIONINFO и манифест
     носят ту же версию.
6. Strip: `strip --strip-all` по exe, затем проверка целостности PE и запуск
   `POE2FlaskHelper.exe --version` (код возврата 0). Если strip недоступен или
   проверка не прошла — предупреждение и сборка без strip.
7. `ctest` (красых останавливает сборку до упаковки, критерий §9-12).
8. Упаковка: `dist/POE2FlaskHelper-<version>-poe2-<gamever>-win64/` = `POE2FlaskHelper.exe`,
   `offsets.json`, `README.txt`, `README.ru.txt`; zip-архив
   `dist/POE2FlaskHelper-<version>-poe2-<gamever>-win64.zip`. Только zip, 7z не используется.
   Изменено 2026-11-08 (plan-release.md п. 5): `config.json` в архив НЕ входит — состав
   exe + `offsets.json` + `README.txt` + `README.ru.txt`; файл создаётся приложением при
   первом запуске (§4.1), распаковка в существующую папку не трогает конфиг пользователя.
   `<gamever>` — версия патча игры из строкового ключа `GameVersion` файла `offsets.json`
   (пишет `find_offsets.py --save`, §6); при отсутствии ключа — имя без poe2-части и
   предупреждение. Новый патч игры с новыми смещениями даёт новый архив без смены версии
   exe (§2.4).
   Изменено 2026-11-08 (minors группа E1, R5-5): перед созданием zip упакованный exe
   запускается из временной копии staged-каталога и закрывается documented-хуком
   `FindWindowW` + `PostMessageW(WM_CLOSE)` (src/tray.h) — полный запуск и чистый выход
   с кодом 0, критерий §9-2; красых случаев zip не создаётся. Копия smoke-прогона
   содержит `offsets.json` без `config.json` (изменено 2026-11-08, plan-release.md п. 5):
   прогон подтверждает генерацию `config.json` на первом запуске проверкой появления файла
   и байтовой сверкой с эталоном `res/config.example.json` (§3).
   Изменено 2026-11-08 (версионирование): `README.txt` в staged-каталоге —
   сгенерированный `build/README.txt` (§2.3), а не статический файл из `res/`.

9. Релиз на GitHub (`release.sh`, добавлено 2026-11-08, plan-release.md п. 8): после
   packaging публикация zip как GitHub release — тег `v<version>-poe2-<gamever>`, title
   и body на английском; загрузка по умолчанию, отключение `--no-upload` (`build.sh
   --no-upload` пробрасывает). Источник body — запись `CHANGELOG.en.md` текущей версии
   (§2.4); для offsets-only релиза (та же версия exe, новый патч игры) — «Updated memory
   offsets for POE2 <gamever>, program code unchanged». Повторный upload существующего
   тега — сообщение и non-zero выход. `gh` берётся из PATH, fallback
   `C:\Program Files\GitHub CLI\gh.exe` (проверен 2026-11-08: 2.103.3). Красых release
   = сборка падает после создания zip.

Версия берётся из `project(... VERSION ...)` и попадает в VERSIONINFO, манифест
(`assemblyIdentity`, генерируется из `res/app.manifest.in`), генерируемый
`README.txt` (§2.3) и имя архива (через сгенерированные `build/version.txt` и
`build/app.manifest`). Правило подъёма номера — §2.4.

### 2.3 `README.txt` в архиве

Изменено 2026-11-08 (live-тесты, группа L1): источник файла — `res/README.txt`,
`build.sh` копирует его в корень архива; содержимое и состав архива не меняются.
Лог: press-строка и единственная held-строка окна кулдауна описаны как info (§4.7, §7),
добавлено замечание про debug-шум наведения на иконку (`tray callback event 512`).
Изменено 2026-11-08 (версионирование): источник — `res/README.txt.in` c
`@PROJECT_VERSION@`, `configure_file` создаёт `build/README.txt` (NEWLINE_STYLE CRLF),
`build.sh` копирует в корень архива сгенерированный файл. Версия в пользовательском
документе не может разойтись с `project(... VERSION)` (мера против хардкода `1.0.0`);
текст и состав архива не меняются. Проверено: побайтовый diff `.in` c подстановкой
версии и HEAD-версии файла совпадает, генерированный README в zip — тот же текст
(152 CRLF-строки), §9-11.
Изменено 2026-11-08 (hot-reload, план plan-hot-reload.md): в `res/README.txt.in` добавлен
раздел «Editing config.json at runtime» / «Правка config.json на лету» (English + Russian),
описание `config.json` в секции Files меняется с «читается один раз на старте» на 5 с
проверку (механика — SPEC §5). Механизм генерации (`configure_file`, `@PROJECT_VERSION@`)
и состав архива не меняются; сгенерированный README — 205 CRLF-строк.

Дополнено 2026-11-08 (plan-release.md п. 6): README разделён на два файла:
`res/README.txt.in` — English, `res/README.ru.txt.in` — Russian; два `configure_file`
в CMakeLists генерируют `build/README.txt` и `build/README.ru.txt`, в корень архива
копируются оба (состав §2.2). Содержание описывает генерацию `config.json` на первом
запуске (архив его не содержит), неизвестный ключ = невалидный конфиг, press-строку
`% <= 50%` и первую info-строку значений (§4.7, §7). Корневой `README.md` — английский,
`README.ru.md` — русский; `scanner/README.md` — английский, `scanner/README.ru.md` —
русский.

Содержание каждого языка: запуск, статус
в трее, где лежат `config.json`, `offsets.json` и лог, что делать при
`Offsets mismatch`, требование VC++ v14 x64 Redistributable. Изменено 2026-11-08:
добавлено требование прав запуска — если игра запущена от администратора, хелпер
запускается с теми же правами (риск §5.1 плана, UIPI).

### 2.4 Версионирование (добавлено 2026-11-08)

Формат — `1.major.minor`: ведущая цифра — серия (всегда `1`), `major` — фича,
`minor` — фикс. Правила:

- **major** при добавлении новых пользовательских фич (`1.1.0 -> 1.2.0`, последняя
  цифра сбрасывается);
- **minor** при фиксах багов и небольших изменениях (`1.1.0 -> 1.1.1`);
- **серия** (`1.x.y -> 2.0.0`) — только существенная переработка архитектуры;
- **версия не поднимается, если код проекта не менялся** (exe актуален). Код проекта =
  `src/` и `res/` ассеты/rc/manifest, то есть всё, что линкуется в exe. Не поднимают
  версию: `tests/`, документация (`SPEC.md`, `plan.md`, `backlog.md`, `README.md`,
  `CHANGELOG.md`, `res/README.txt.in`), `research/`, `scanner/`, `build.sh`,
  `version.sh`, `config.json`, `offsets.json`, `res/gen_assets.py`. Дополнено 2026-11-08
  (plan-release.md п. 5–8): в список не поднимающих версию добавлены `release.sh`,
  `res/README.ru.txt.in` (русский документ архива) и `res/config.example.json` (эталон
  дефолтов, данные, не ассет exe). Новый патч игры (новые смещения, новый `GameVersion`
  при неизменном коде) — новый релизный архив с новой poe2 в имени и новый тег, версия exe
  не меняется; гейт `check` зелёный (код не менялся), body релиза — offsets-фраза (§2.2 п. 9).

Единственный источник номера — `project(POE2FlaskHelper VERSION x.y.z LANGUAGES CXX RC)`
в `CMakeLists.txt` (§2.2); его форма — контракт, который правит только `version.sh`.
История версий — `CHANGELOG.md` (записи newest-first: версия, дата, класс, что вошло).
Изменено 2026-11-08 (plan-release.md п. 8): англоязычные заметки — `CHANGELOG.en.md`,
запись добавляется обязательным вторым описанием `--en "English note"` и служит body
GitHub-релиза (`release.sh`, §2.2 п. 9). Git-тегов нет; якорь «последний подъём»
вычисляется по истории `CMakeLists.txt`.

Процедура (`version.sh`, cygwin bash):

```
./version.sh major "описание фичи" --en "English note"      # 1.1.0 -> 1.2.0
./version.sh minor "описание фикса" --en "English note"     # 1.1.0 -> 1.1.1
./version.sh series "описание" --en "English note"          # смена серии; правило 3 действует и здесь
./version.sh check                      # гейт для build.sh
```

`version.sh` правит `CMakeLists.txt`, `CHANGELOG.md` и `CHANGELOG.en.md`, никогда не
коммитит; описание из второго аргумента идёт в русскую запись CHANGELOG, `--en` — в
английскую. Проверки: git-репозиторий и HEAD, контрактная форма строки версии, чистые
`CMakeLists.txt`/`CHANGELOG.md`/`CHANGELOG.en.md`, правило 3 (код менялся от последнего
подъёма до HEAD; исключение — documented-`--force`).

Гейт `check` перед packaging (красых = zip не создаётся):

1. код менялся после последнего подъёма версии, а версия не поднята → FATAL;
2. версия поднята без изменений кода между подъёмами (`src/` + `res/`) → FATAL;
3. незакоммиченные правки кода → предупреждение (гейт смотрит закоммиченное);
4. первый релиз / история без подъёмов → предупреждение, гейт пропускается: полная
   строгость включается со первого подъёма после `1.0.0`.

Историческая запись: весь MVP (этапы 0–5 плана + live-тесты L1/M1) выпущен как `1.0.0`
(CHANGELOG). Строгость гейта включается со следующего подъёма.

## 3. Структура проекта

Изменено 2026-11-08: дерево приведено к реализованному (план §6).
Изменено 2026-11-08 (minors группа E1, S5-2): дерево дополнено реальными файлами
`src/scanner_ctl.h`, `res/version.h.in`, `res/res_paths.rc.in`, `res/gen_assets.py`
(build-time helper); stage-0 заглушка `src/core_placeholder.{h,cpp}` удалена (S5-1).
Изменено 2026-11-08 (live-тесты, группа L1): `README.txt` перенесён в `res/` (файл
ассетов, `build.sh` копирует его в корень zip); `MINOR_ISSUES.md` удалён — список
закрыт, оставшиеся записи живут как комментарии кода/тестов с историческим ID и датой
(`review S… , 2026-11-08`), сам файл доступен в истории git.
Изменено 2026-11-08 (версионирование, §2.4): в дерево добавлены `version.sh` и `CHANGELOG.md`;
пользовательский документ — `res/README.txt.in` (версия подставляется configure_file).
Изменено 2026-11-08 (hot-reload, план plan-hot-reload.md §3): в ядро добавлен
`src/config_snapshot.{h,cpp}` (живой снимок конфига), в exe-слой — `src/file_stamp.{h,cpp}`
(штамп `LAST_WRITE`+size), в тесты — `tests/reload_tests.cpp` (ветки 5 с цикла без Win32
и файлов), в ассеты — `res/reload.wav`; длительности ассетов: 0.300 / 0.633 / 0.387 с.
Изменено 2026-11-08 (minors закрытия, m10): в exe-слой добавлен `src/win_text.h` —
единый header-only inline `poe2::to_wide` для `tray.cpp` и `file_stamp.cpp` (третья
per-TU копия убрана; reverse-копии `main.cpp`/`game_process.cpp` вне scope m10).

```
build.sh  version.sh  release.sh  CHANGELOG.md  CHANGELOG.en.md  CMakeLists.txt
README.md  README.ru.md  SPEC.md  plan.md  plan-release.md  backlog.md
статическая библиотека poe2_core (общая для exe и тестов; чистая логика, интерфейсы §3.1):
src/json.{h,cpp}       разбор/сериализация
src/config.{h,cpp}     схема, валидация, значения по умолчанию
src/offsets.{h,cpp}    обязательный набор ключей, Off − TotalOff
src/log.{h,cpp}        async-лог: очередь + поток записи, ротация
src/app.{h,cpp}        пути от каталога exe, helpers текстовых файлов
src/args.{h,cpp}       разбор командной строки (--version/--batch)
src/state.{h,cpp}      состояние трея, текст/галочка меню, статус→WAV
src/keys.{h,cpp}       разбор "Ctrl+9", клавиша Vital'а, интерфейс IKeySender
src/signature.{h,cpp}  скан сигнатуры по IMemoryReader, чанки 1 МБ
src/chain.{h,cpp}      цепочка A–J, кэши, tick() на IMemoryReader + Layout
src/vital_math.{h,cpp} ceil-формула резервов зелий
src/vitals.{h,cpp}     evaluate(): порог, кулдаун, foreground
src/scanner_session.{h,cpp} state machine сканера, step()
src/scanner_ctl.h      ScannerCtl: атомиксы enable/stop + снимок phase/pid (UI ↔ поток)
                       + ConfigSnapshot live-канал конфига (§5)
src/config_snapshot.{h,cpp} ConfigSnapshot: mutex + shared_ptr<const Config>; set()
        ставит снимок из UI-потока, get() отдаёт целостный снимок потоку сканера (§5)
src/wav.{h,cpp}       PCM16-масштабирование WAV-байт (sound.volume, §4.5)
src/clock.h            интерфейс IClock
exe (граница ОС — весь Win32 здесь):
src/main.cpp           WinMain, single instance, message loop, --version
src/tray.{h,cpp}       иконка, контекстное меню из двух пунктов, скрытое окно
src/hotkeys.{h,cpp}    RegisterHotKey
src/file_stamp.{h,cpp} штамп config.json: CreateFileW(share READ|WRITE) +
                       GetFileTime(LAST_WRITE) + GetFileSizeEx (5 с цикл §5)
src/win_text.h         единый poe2::to_wide (UTF-8→wide), header-only inline
                       (tray.cpp + file_stamp.cpp, m10)
src/sound.{h,cpp}      PlaySound из ресурсов + PCM16-громкость (SND_MEMORY, §4.5)
src/scanner.{h,cpp}    рабочий поток-обёртка над ScannerSession (atomic + событие)
src/game_process.{h,cpp} поиск PID/foreground/liveness; заменяет плановый
                       process.{h,cpp}: имя collide'ит с UCRT <process.h>
                       (<thread> включает его), src/ лежит на include-пути
src/win_keys.cpp       реализация IKeySender на SendInput
src/mem/             reader.h (IMemoryReader, IMemoryReaderFactory) + win_reader.{h,cpp}
src/system_clock.h   реализация IClock
src/res_ids.h        общие id ресурсов (app.rc, C++, тесты)
res/app.ico res/app.rc res/app.manifest.in res/enable.wav res/disable.wav res/reload.wav
res/README.txt.in  пользовательский документ архива (English, §2.3): лежит в res/, `configure_file` создаёт из него
         `build/README.txt` с версией проекта (§2.2), в корень zip копируется
         сгенерированный файл (build.sh);
res/README.ru.txt.in  то же для русской версии архивного документа (`build/README.ru.txt`), добавлено 2026-11-08 (plan-release.md п. 6);
res/config.example.json  эталон дефолтов конфига: байтово дословный вывод `config_serialize(config_defaults())`
         (проверен smoke- прогоном build.sh, §2.2); сам `config.json` в репозитории и архиве
         отсутствует — создаётся приложением при первом запуске (SPEC §4.1, plan-release.md п. 5);
res/version.h.in res/res_paths.rc.in  входы configure_file: build/version.h,
        build/res_paths.rc, build/app.manifest (SPEC §2.2 — версия в VERSIONINFO,
        манифест и имя архива);
res/gen_assets.py  build-time helper (не линкуется, не в zip): генератор stub-ассетов
        этапа 0 (app.ico/enable.wav/disable.wav); ассеты финальны (коммиты 7b286a3,
        6e3596f), скрипт оставлен как история воспроизводимости заглушек;
tests/ мини-фреймворк test.h + test_main.cpp + *_tests.cpp (в том числе
       reload_tests.cpp — ветки 5 с цикла: stamp_changed / config_diff / path_is_live /
       config_reload, §5) + test_tmp.{h,cpp} (временные
       каталоги) + session_fixture.h (общий фиксированный синтетический образ для
       session/vital-тестов) + fakes/ (FakeMemory,
       FakeClock, FakeProcess, RecordingKeySender) + synthetic_layout.{h,cpp};
       консольный exe poe2_tests + ctest (unit). Единственный exe-слойный файл в
       тестовой сборке: tests/scanner_thread_tests.cpp (GA-1) гоняет src/scanner.cpp —
       старт→стоп→старт на реальном event-объекте, всё остальное — фейки;
offsets.json offsets_selftest.json (selftest-данные scanner/find_offsets.py)
```

Изменено 2026-11-08 (plan-release.md п. 5–7): корневой `config.json` перенесён в
`res/config.example.json` (архив и приложение генерируют `config.json` при первом
запуске); README'и разделены на English/Russian: корень (`README.md` + `README.ru.md`),
архив (`res/README.txt.in` + `res/README.ru.txt.in`), scanner (`scanner/README.md` +
`scanner/README.ru.md`); `research/code/`, `research/offsets_table.py`,
`research/extract_code.py`, `research/rev32_rev33.diff` удалены (артефакты в истории
git); в дерево добавлены `release.sh` и `CHANGELOG.en.md`.

Ассеты финальны и предоставлены пользователем 2026-11-07: `res/app.ico`
(записи 16–256 px, прозрачный фон), `res/enable.wav` и `res/disable.wav`
(0.82 с / 0.689 с, короткие, < 1 с). Заглушки больше не нужны.
Изменено 2026-11-08 (hot-reload, план plan-hot-reload.md §0): `res/reload.wav` — новый
ассет (26540 B, PCM16 mono 44100 Hz, измерено 0.300 с), `res/enable.wav` и
`res/disable.wav` подрезаны пользователем до 0.633 с / 0.387 с. Все три < 1 с;
`reload.wav` играется только на релоад конфига (§4.5).

### 3.1 Тестируемость (добавлено 2026-11-08)

Правило: **ядро не знает про Win32-процессы и реальное время** — `poe2_core`
содержит только чистые функции/классы, всё взаимодействие с ОС и временем идёт через
внедряемые интерфейсы. В exe подставляются реальные реализации, в тестах — фейки;
сканер тестируется без POE, без Win32 и без sleep (plan §1).
Изменено 2026-11-08 (minors группа E1, GA-1): поверх этого правила тестовый exe гоняет и
поток-обёртку `src/scanner.cpp` (tests/scanner_thread_tests.cpp): реальный event-объект и
поток, фейки для процесса/памяти/часов/клавиш, цикл старт→стоп→старт; без POE, без sleep
(спин на атомиксах с фиксированным лимитом).

| Интерфейс | Файл | Реализация в exe | Фейк в тестах |
|---|---|---|---|
| `IMemoryReader`, `IMemoryReaderFactory` | `src/mem/reader.h` | `src/mem/win_reader.{h,cpp}` (`ReadProcessMemory`) | `FakeMemory` |
| `IProcessProvider` | `src/game_process.h` | `src/game_process.cpp` (`EnumProcesses`, foreground) | `FakeProcess` |
| `IClock` | `src/clock.h` | `src/system_clock.h` | `FakeClock` |
| `IKeySender` | `src/keys.h` | `src/win_keys.cpp` (`SendInput`) | `RecordingKeySender` |

## 4. Поведение

### 4.1 Запуск и завершение

- Named mutex `Local\POE2FlaskHelper.SingleInstance`: второй экземпляр показывает
  сообщение и выходит.
- Все пути строятся от каталога exe (`GetModuleFileNameW`); текущая рабочая
  директория игнорируется — важно при запуске из cygwin.
- Нет `config.json` — создаётся файл со значениями по умолчанию.
- Неизвестный ключ в `config.json` на старте — изменено 2026-11-08 (plan-release.md п. 2):
  error + MessageBox + выход с кодом 2 (как отсутствующий `offsets.json`, §5).
- Нет, битый или неполный `offsets.json` — запись в лог, MessageBox, выход с кодом 2.
  Встроенных смещений в exe нет.
- Сразу после старта приложение живёт в трее, окон не создаёт.
- `--version` печатает версию (если есть консоль) и выходит с кодом 0, минуя
  single-instance и загрузку конфигов.

### 4.2 Потоки

- **Главный поток (UI/GDI)**: message loop, скрытое top-level окно для `WM_HOTKEY`
  и колбэков трея, `Shell_NotifyIcon`, `PlaySound`, весь GDI. Никаких
  `ReadProcessMemory` и файлового I/O.
- **Поток сканера**: единственный владелец хэндла процесса, всех чтений памяти,
  таймеров кулдауна и вызовов `SendInput`. Управление — `std::atomic` (флаг
  включения, запрос остановки) и событие; join при выходе.
- Логгер общий для обоих потоков, защищён mutex.
- Изменено 2026-11-08 (minors группа E2, BR-7; прецедент — «Изменено»-примечания §4.3):
  формулировка «никакого файлового I/O» на главном потоке распространяется на время
  message loop — это дух плана (§4.2 фиксирует, что UI-поток не блокируется долгими
  операциями). Файловый I/O главного потока разрешён только на старте, до входа в
  message loop: чтение `config.json`, создание `config.json` по умолчанию, чтение
  `offsets.json` (main.cpp:111-115 — комментарий остаётся, SPEC-фраза не переписывается).
  К этому моменту нет ни окна, ни UI, который можно заблокировать, и сканерный поток ещё
  не запущен. Все строки лога пишет поток-записщик (async), главный поток лог-файл не
  трогает.
- Изменено 2026-11-08 (hot-reload, план plan-hot-reload.md §3; прецедент — тот же BR-7
  пункт выше): формулировка «никакого файлового I/O» на главном потоке фиксирует дух
  плана — UI-поток не блокируется долгими операциями во время message loop. 5 с проверка
  `config.json` живёт на главном потоке (`WM_TIMER(kConfigTimerId)`, handler в main.cpp)
  и читает ~1 КБ: `GetFileTime` + `GetFileSizeEx` (штамп) и чтение текста файла; UI не
  блокируется, масштаб — тот же, что стартовый I/O предыдущего пункта. Файл лога главный
  поток по-прежнему не трогает (строки пишет async-записщик). На главном потоке остаются
  только Win32-вызовы ветки применения: `RegisterHotKey`/`UnregisterHotKey` и `PlaySound`;
  всё решение (штамп, диф, живой/после-рестарта сплит, ветки битого JSON) — чистое ядро
  (§5). Сканерный поток файлов не читает. Открытое popup-меню (`TrackPopupMenu` modal
  loop) может задержать проверку таймера — известный пункт чеклиста плана §6 п.9.

### 4.3 Трей

- Иконка и тултип: `Scanner off` / `Waiting for game` / `In menu / loading` /
  `Attached (PID n)`.
- ПКМ — ровно два пункта: один переключатель сканера и `Exit`. Текст переключателя
  отражает состояние: `Enabled` при включённом сканере, `Disabled` при выключенном;
  галочка стоит при включённом. Клик по переключателю меняет состояние — то же самое,
  что хоткей: переключение и соответствующий WAV (SPEC §4.5).
- Двойной клик по иконке — без действия.
- Изменено 2026-11-07: один пункт-переключатель с текстом `Enabled`/`Disabled` по
  состоянию (галочка = включён) вместо двух (`Enable scanner` / `Disable scanner`).
- Изменено 2026-11-08 (этап 5): к статусам §4.3 добавлен `Admin rights required` —
  тултип при `OpenProcess` → `ERROR_ACCESS_DENIED` (§8). Статус не залипает: следующий
  успешный опрос процесса возвращает обычные статусы.
- Изменено 2026-11-08 (live-тесты, группа M1): добавлен статус `In menu / loading` —
  процесс игры найден и открыт, но игры в зоне нет (§4.6 п.5). PID в тултипе не
  показывается (PID публикуется только в `Attached`), строка вместе с суффиксом
  занятого хоткея влезает в `szTip` (64 wchar).
- Фикс 2026-11-08: при `NOTIFYICON_VERSION_4` стандартный тултип подавлен по
  умолчанию (NOTIFYICONDATA szTip Remarks), наведение на иконку ничего не показывало.
  `NIF_SHOWTIP` добавлен в `uFlags` при `NIM_ADD` и `NIM_MODIFY` — тултип появляется
  при наведении.
- Изменено 2026-11-08: первой строкой тултипа — имя приложения и версия проекта
  (`POE2FlaskHelper <version>`, из `project VERSION` через build/version.h), статус
  второй строкой. Разделитель `\n` работает в v4-раскладке (szTip 128 wchar, кламп
  127); при классическом пути (v4 не установлен, warn-путь `tray.cpp`) показывается
  одна линия, обрезанная Explorer'ом на 63 символа — деградация fallback-а, не
  переполнение буфера.

### 4.4 Хоткей

- Один глобальный: `hotkeys.toggleScanner`, по умолчанию `Ctrl+9`.
- `RegisterHotKey`; работает и когда фокус у игры.
- Валидация при загрузке: обязателен модификатор или F-клавиша.
- Хоткей занят — warning в лог и статус в тултипе, приложение продолжает работать.

### 4.5 Звук

- Включение сканера — `enable.wav`, выключение — `disable.wav`; срабатывает и на
  хоткей, и на пункт меню.
- Изменено 2026-11-08: включение при старте (`scanner.enabledOnStart: true`) — это
  тот же путь переключения, поэтому играет `enable.wav` (решает расхождение S2-5).
- Изменено 2026-11-08 (hot-reload, план plan-hot-reload.md §0/§3): третий WAV —
  `reload.wav` (id 104, PCM16 mono 44100 Hz, 0.300 с; `enable.wav`/`disable.wav`
  подрезаны до 0.633 с / 0.387 с). Играется только на успешный разбор + применение
  валидного `config.json` в 5 с цикле (§5) — пустой diff тоже успех; на старте звук
  не играется, битый JSON и заблокированный файл = тишина. Громкость берётся из живого
  снимка конфига (`sound.volume`), тем же путём PCM16-масштабирования ниже: `play_wav`
  пересчитывает байты на каждое проигрывание, поэтому правка `sound.volume` в том же
  цикле делает собственный звук релоада тише. `reload.wav` вне маппинга
  enable/disable (§4.3), id `kReloadWavResId`. Ошибка ресурса на reload-пути —
  warn «reload applied without sound», релоад применяется без звука (§7).
- `PlaySound(MAKEINTRESOURCE(...), hInst, SND_RESOURCE | SND_ASYNC | SND_NODEFAULT)`;
  UI не блокируется.
- Ошибка ресурса — warning, переключение происходит без звука.
- Добавлено 2026-11-08 (backlog «Громкость WAV»): `sound.volume` (0–100 %, по
  умолчанию 100, §5). WAV-ресурс читается в память (`FindResource`/`LoadResource`),
  int16-семплы PCM16 масштабируются коэффициентом `volume/100` чистой функцией ядра
  `apply_pcm_gain` (`src/wav.cpp`, тестируется без Win32), проигрывание через
  `PlaySoundW(ptr, hInst, SND_MEMORY | SND_ASYNC | SND_NODEFAULT)`. Масштабирование
  выполняется на каждое проигрывание — значение берётся из живого конфига, это шов
  для будущего reload конфига на лету. Non-PCM16/битый WAV — масштабирование
  пропускается, играет оригинал через `SND_RESOURCE` (warn-путь §8). `volume: 0` —
  тишина. Системная громкость не трогается.
- Измерено 2026-11-08 (причина первого builds, игравшего 100 %): c `SND_MEMORY | SND_ASYNC`
  PlaySound НЕ копирует буфер — «the memory buffer that contains the sound must remain
  valid until the sound has completed playing» (документация PlaySound). Масштабированные
  байты держатся в модульном буфере `sound.cpp` (main thread, §4.2), заменяются следующим
  проигрыванием, `stop_sound` чистит после purge-вызова. Стартовый кэш gain не делается
  (per-play пересчёт — шов reload).
- Измерено 2026-11-08 (поиск ресурса): app.rc компилирует `WAVE` как ИМЕНОВАННЫЙ тип
  "WAVE" (не ordinal RT_RIFF 11), ресурсы с языком 0x409 (`Translation`).
  `FindResourceW` раскрывается в `FindResourceExW` с языком системы по умолчанию и на
  не-английской системе возвращает NULL; поиск `FindResourceExW(hInst, L"WAVE",
  MAKEINTRESOURCEW(id), MAKELANGID(ENGLISH, US))` находится (измерено с gle=1813
  TYPE_NOT_FOUND и EnumResourceTypes: типы WAVE,#3,#14,#16,#24).

### 4.6 Сканер

Состояния:

1. `ожидание процесса` — `EnumProcesses` + `QueryFullProcessImageNameW`, опрос 1–2 с;
2. `подключение` — `OpenProcess(VM_READ | QUERY_INFORMATION)`, PEB через
   `NtQueryInformationProcess`, `ImageBase` из `PEB+0x10`, `SizeOfImage` из
   PE-заголовка; скан сигнатуры по образу модуля чанками по 1 МБ; полная цепочка A–J;
3. `чтение` — `tick()` каждые `pollIntervalMs`, около 10 чтений на тик;
4. разрыв цепочки или смерть процесса — назад в `ожидание процесса`; после рестарта
   игры сигнатура сканируется заново;
5. Изменено 2026-11-08 (live-тесты, группа M1): `игра не в зоне` — процесс найден и
   открыт, сигнатура решена, но цепочка упёрлась в нулевой указатель зоны:
   `GameStates`, `States[4]` (InGameState) или `AreaInstance` равны 0 (главное меню,
   экран выбора персонажа, загрузка зоны). По §8 это штатный статус, а не разрыв
   цепочки: хэндл `OpenProcess` НЕ закрывается, `global_addr` сохраняется (скан
   сигнатуры не повторяется), удаляются только позонные кэши `off_lp`/`life_index`.
   Опрос — 1 с (тот же темп, что п.1), на шаг приходится одно `EnumProcesses` и ~3
   чтения (`GameStates`, `States`, `AreaInstance`). Вход в зону первой успешной
   цепочкой переводит в `чтение` на том же хэндле. Смерть процесса или его рестарт
   (другой PID) во время меню — кэш сбрасывается и сигнатура сканируется заново;
   реальный разрыв цепочки (например `IsValid != 0xC`) по-прежнему идёт по п.4.

Пока модуль выключен, не выполняется ни одного `ReadProcessMemory`.

Цепочка и структуры — как в `scanner/poe_scan.py`:

- сигнатура `48 83 EC ?? 48 8B F1 33 ED 48 39 2D ^ ?? ?? ?? ??` (константа бинарника,
  не смещение); `global_addr = anchor + disp + 4`;
- `GameStates* → CurrentStatePtr → States[4] → AreaInstance → LocalPlayers → Entity →
  ComponentList → ComponentLookUp("Life") → Life component → Vital'ы`;
- кэшируются `global_addr`, фактическое `LocalPlayers` (окно ±0x40) и `life_index`;
- проверки валидности: `IsValid == 0xC`, `capacity <= 50`, границы векторов.

### 4.7 Активация зелий

Для каждого включённого Vital на каждом тике:

- `reserved = ceil(total * reservedPercent / 10000 + reservedFlat)`,
  `unreserved = total - reserved`; при `unreserved <= 0` — пропуск (формула
  `poe_scan.py`);
- сравнение без округления: `current * 100 < threshold * unreserved`;
- если ниже порога и с прошлой отправки прошло `cooldownMs` — отправка клавиши
  и info-запись `Health 23% <= 50%, key=1` (изменено 2026-11-08, live-тесты: press-строка
  пишется на info, чтобы быть видимой при уровне по умолчанию; изменено 2026-11-08,
  plan-release.md п. 3: знак `<` -> `<=` — сравнение строгое (`current*100 <
  threshold*unreserved`), но печатаемый процент округлённый half-even, и округлённое
  значение может равняться порогу легального нажатия — `<=` описывает факт);
- пока значение остаётся ниже порога, повтор каждые `cooldownMs`.

Изменено 2026-11-08 (live-тесты, группа L1). Ситуация «ниже порога, кулдаун ещё не
прошёл» ловится кодом и пишется на info ровно **один раз за окно кулдауна**:
`Health 26% <= 50%, key=1 held: cooldown left 950 ms` (знак `<=` — plan-release.md п. 3).
Окно открывается отправкой; флаг «строка уже потрачена» на Vital сбрасывается следующей
отправкой этого Vital'а
(то есть срабатыванием кулдауна), поэтому каждое новое окно даёт одну строку. Тик выше
порога флаг не трогает: значение, вернувшееся в уже отмеченное окно, строки не даёт.
detach (выключение сканера, разрыв цепочки) закрывает кулдаунные таймеры и флаги:
новое присоединение начинает чистым, и правило «без взведения» (§4.7) даёт отправку
на первом тике после присоединения. Отчёты о held `evaluate()` возвращает вызывающему
коду (чистое ядро не логирует); logирует сессия.

Признак «ниже порога» не требует предварительного «взведения»: если приложение
запущено при 30% HP, клавиша отправится на первом же тике.

### 4.8 Отправка клавиш

- `SendInput`, VK + scan code из `MapVirtualKey`, extended-флаги для стрелок и
  NumPad, keydown + keyup.
- Отправляет поток сканера.
- Только если foreground-окно принадлежит процессу игры
  (`GetForegroundWindow` + `GetWindowThreadProcessId`), при
  `scanner.requireGameForeground: true`.
- `PostMessage` не используется: игра читает клавиатуру через DirectInput.
- В память игры запись отсутствует.

## 5. `config.json`

Лежит рядом с exe. Изменено 2026-11-08 (hot-reload, план plan-hot-reload.md): файл
читается на старте и перечитывается каждые 5 с; изменения живых параметров применяются
без перезапуска. На старте приложение запоминает штамп файла — `LAST_WRITE` время
(100 ns тики) + размер. Каждые 5 с главный поток читает штамп: штамп не двигался — цикл
ничего не делает; двигался — текст читается, разбирается по правилам ниже и применяется.
Штамп двигается только на обработанную версию: одна битая версия логируется один раз,
заблокированный файл перечитывается следующим циклом.

Ветки 5 с цикла (plan-hot-reload.md §3 handler):

- штамп двигался, текст не читается (exclusive lock редактора, удалённый или
  неоткрываемый файл) — warn §7, штамп не двигается, следующая проверка повторит;
- битый JSON или корень не объект — error в лог, **MessageBox нет** (MessageBox
  §4.1/§8 остаётся стартовой веткой), приложение и сканер живут на текущем конфиге,
  5 с цикл сохраняется: файл можно починить, и правка применится;
- валидный JSON — порядок применения: живой снимок конфига (`ConfigSnapshot::set`) →
  опции лога (`Logger::set_options`, reload-строки пишутся уже при новом уровне) →
  parse-warning'и §5 → перерегистрация хоткея, если менялся `hotkeys.toggleScanner` →
  info-строка применённых путей → warn полей после рестарта → `reload.wav` (§4.5).
  Пустой diff (сохранение без правок) — успех: info `config.json reloaded (no changes)`
  и звук;
- применяются только живые параметры таблицы ниже, restart-only поля — после
  перезапуска; решение о ветках — чистое ядро (`config_reload`), тестируется без Win32.

Живые параметры (полный список, plan-hot-reload.md §2):

| Параметр | Механика применения |
|---|---|
| `vitals.*.enabled` / `key` / `threshold` / `cooldownMs` | следующий тик `evaluate()` (≤ `pollIntervalMs`) |
| `scanner.pollIntervalMs` | темп опроса потока сканера (живой снимок в `step_delay_ms()`) |
| `scanner.requireGameForeground` | следующий `tick_step()` foreground-gate |
| `scanner.processNames` | следующий опрос поиска процесса |
| `sound.volume` | PCM16-масштабирование на каждое проигрывание (§4.5) |
| `hotkeys.toggleScanner` | `UnregisterHotKey` + `RegisterHotKey` на скрытом окне, tooltip refresh; комбинация занята — warn и возврат старой (§7) |
| `logging.level` / `logging.maxSizeKb` / `logging.keepFiles` | `Logger::set_options()`: фильтр уровня, ротация считается с следующей записи |

После перезапуска (warn §7): `scanner.enabledOnStart` (старт-семантика) и
`logging.file` (перезапуск writer-потока = риск потерять reload-строки).
`configVersion` — метаданные, не применяется, warn не печатает.

Стартовые правила (аменд 2026-11-08: старт берёт тот же разбор, что и 5 с цикл;
неизвестный ключ — аменд plan-release.md п. 2 2026-11-08: error + MessageBox + выход с
кодом 2, не работа на дефолтах): файла нет — создаётся дефолтный; битый файл на старте
— error + MessageBox, работа на значениях по умолчанию (§4.1, §8).

```json
{
  "configVersion": 1,
  "hotkeys": { "toggleScanner": "Ctrl+9" },
  "scanner": {
    "enabledOnStart": false,
    "pollIntervalMs": 50,
    "requireGameForeground": true,
    "processNames": ["PathOfExile.exe", "PathOfExile_x64.exe", "PathOfExileSteam.exe", "PathOfExile_KG.exe"]
  },
  "sound": { "volume": 100 },
  "vitals": {
    "Health":       { "enabled": true,  "key": "1", "threshold": 50, "cooldownMs": 1000 },
    "Mana":         { "enabled": true,  "key": "2", "threshold": 40, "cooldownMs": 1000 },
    "EnergyShield": { "enabled": false, "key": "3", "threshold": 35, "cooldownMs": 1000 },
    "Ward":         { "enabled": false, "key": "4", "threshold": 30, "cooldownMs": 1000 },
    "Rage":         { "enabled": false, "key": "5", "threshold": 20, "cooldownMs": 2000 }
  },
  "logging": { "level": "info", "file": "logs/app.log", "maxSizeKb": 1024, "keepFiles": 3 }
}
```

Правила разбора (изменено 2026-11-08: действуют и на старте, и в 5 с цикле; warning'и
цикла пишутся уже при новом уровне логирования, §5 ветка «валидный JSON»):

- неизвестные ключи (любой уровень: root, `hotkeys`, `scanner`, `sound`, `vitals`, имя
  Vital'а, поля Vital'а, `logging`) — изменено 2026-11-08 (plan-release.md п. 2): конфиг
  целиком **невалиден**, не «warning + игнор». Старте: error в лог
  `config.json is invalid (unknown key "<путь>", …)` + MessageBox + выход с кодом 2
  (тот же контракт старта, что отсутствующий `offsets.json`, §4.1/§8). Runtime: error
  `config.json is invalid (unknown key "<путь>", …), current config kept`, приложение и
  сканер живут на текущем конфиге, MessageBox нет, 5 с цикл сохраняется, та же
  невалидная версия логируется один раз (штамп двигается на обработанную версию);
- отсутствующие — значения по умолчанию;
- невалидные (`threshold` вне 1–100, `cooldownMs < 0`, `pollIntervalMs` вне 10–1000,
  `sound.volume` вне 0–100, неизвестная клавиша, хоткей без модификатора) — warning
  и значение по умолчанию;
- битый JSON — на старте: error в лог, MessageBox, работа на значениях по умолчанию;
  в runtime: error в лог без MessageBox, текущий конфиг сохраняется (§8);
- ключи Vital'ов пишутся одиночными клавишами без модификаторов (буквы, цифры,
  F1–F24, NumPad).

## 6. `offsets.json`

Лежит рядом с `config.json`, обязателен. Содержит полный расклад, чтобы в exe не
было зашитых смещений. Имена и семантика совместимы с `poe_scan.py --offs-file`:

```json
{
  "GameVersion": "0.5.5f",
  "CurrentStatePtr": 16, "States": 80, "TOTAL": 13, "AreaInstanceData": 656,
  "LocalPlayers": 1472, "EntityId": 136, "EntityIsValid": 140,
  "EntityDetailsName": 8, "EntityDetailsLookup": 40, "CLU_Bucket": 40,
  "TotalOff": 52, "CurrentOff": 56, "ReservedOff": 28, "ReservedFlat": 24,
  "HealthOff": 476, "ManaOff": 564, "ESOff": 628, "WardOff": 788, "RageOff": 716,
  "BackPtrOff": 16, "Score": 9
}
```

- Изменено 2026-11-08 (plan-release.md п. 1): `GameVersion` — необязательная **строковая**
  мета-ключа версии патча игры (`find_offsets.py --save` пишет его первым; источник —
  `logs/LatestClient.txt`, см. scanner/README). Приложение игнорирует неизвестные ключи
  (`offsets_parse` читает только набор §6), строковое значение не является ошибкой;
  имя релизного архива строится по этой версии.

- `HealthOff/ManaOff/ESOff/WardOff/RageOff` — позиции Total внутри Life; основа
  Vital'а = `Off − TotalOff`.
- `ReservedOff` — ReservedPercent; `ReservedFlat` — ReservedFlat.
- Все ключи, используемые приложением, обязательны. Отсутствующий, нечисловой или
  невалидный ключ — error в лог, MessageBox, выход с кодом 2.
- `BackPtrOff` и `Score` приложением не используются, сохраняются ради совместимости.
- Обновление после патча игры: `find_offsets.py --save offsets.json` (и
  `find_vital.py` при новых Vital'ах), затем файл кладётся рядом с exe.

## 7. Логи

`<exe_dir>/logs/app.log`, UTF-8, формат:
`2026-11-07 12:34:56.789 [info] message`. Ротация по `maxSizeKb`, хранится
`keepFiles` файлов. Язык — только английский.

- **info**: старт/выход, версия, загрузка конфига и предупреждения, регистрация
  хоткея, scanner enabled/disabled, attach/detach (PID, ImageBase), потеря цепочки
  (не чаще раза в 5 с), fatal-ошибки. Изменено 2026-11-08 (live-тесты, группа L1):
  каждое нажатие (`Health 23% <= 50%, key=1`) и единственная за окно кулдауна строка
  «ниже порога, кулдаун не прошёл» (`Health 26% <= 50%, key=1 held: cooldown left 950 ms`,
  §4.7) пишутся на info. Строки различимы: у held-строки хвост ` held: `. Знак `<=` —
  изменено 2026-11-08 (plan-release.md п. 3, §4.7): печатаемый процент округлённый,
  легальному нажатию может соответствовать округлённый процент, равный порогу.
  Изменено 2026-11-08 (plan-release.md п. 4): **первая** успешная строка значений после
  присоединения/входа в зону (`tick: Health …`) пишется на info; все последние тики —
  debug-only. Право первой info-строки закрывается при detach (выключение сканера,
  разрыв цепочки, закрытие/рестарт игры) и при каждом входе в `InMenu`; следующий вход
  в зону даёт свою первую info-строку.
- Изменено 2026-11-08 (live-тесты, группа M1): `attach` и `detach` пишутся один раз на
  процесс, а не на каждый опрос: `attached PID …` — при первом `OpenProcess`,
  `detached from PID …` — при реальном закрытии хэндла (выключение сканера, смерть
  процесса, настоящий разрыв цепочки). Смена состояния `игра не в зоне` (§4.6 п.5)
  пишется одной info-строкой на вход — `in menu / loading (PID n) - not in a zone`,
  возвращение в зону — одной строкой `in zone (PID n)`; опросы меню лога не дают
  (`in menu / loading` за 12 шагов ровно одна строка). `chain lost` остаётся только за
  настоящий разрыв цепочки (§4.6 п.4), не чаще раза в 5 с; меню и загрузка зоны
  разрывом не считаются. Строка `waiting for game (no configured process found)`
  пишется только когда процесс действительно не найден.
- **debug**: значения Vital'ов на каждом тике (`tick: Health …`), тайминги тика,
  шаги цепочки; единственная стартовая запись о сбое поиска часовой зоны (штампы тогда
  в UTC). Press- и held-строки из debug-списка перенесены на info (2026-11-08, группа L1);
  на debug они тоже пишутся (фильтр пропускает строки своего и высшего уровня).
- Изменено 2026-11-08 (minors группа E2, BR-3): debug-запись о часовом сбое (S1-12)
  видна в логе только при `logging.level: "debug"`; при `info` она не появляется —
  фильтр лога отбрасывает строки ниже уровня.
- **warn/error**: конфиг, ресурсы, `OpenProcess`, несовпадение смещений.
- Изменено 2026-11-08 (hot-reload, план plan-hot-reload.md §5): строки 5 с цикла
  `config.json`, формы дословно. **info**: `config.json reloaded (<live_changed
  canonical>)` — применённые пути в canonical-порядке таблицы ядра (порядок закрепляет
  `config_diff`: `sound.volume` идёт до `vitals.*`, тесты
  `config_diff_canonical_order_full`/`config_diff_partial_paths`; plan §2 задаёт
  набор путей), пример
  `config.json reloaded (sound.volume, vitals.Health.threshold)`; и
  `config.json reloaded (no changes)` — пустой diff (сохранение без правок) и случай,
  когда изменились только restart-only поля (в цикле ничего не применено);
  `hotkey <x> re-registered` после успешной перерегистрации (`hotkey Ctrl+9
  re-registered`). **warn**: parse-warning'и §5 (`unknown key "<ключ>" ignored`,
  `invalid "<путь>", default used`, `invalid "<путь>" (<детали>), default "<дефолт>"
  used`) — пишутся сразу после смены уровня, уже при новом; `config.json is locked by
  another process, retrying next cycle` — файл заблокирован редактором exclusive-lock,
  **тот же warn при удалённом/неоткрываемом файле** (штамп не читается; закрытый
  minor m11 ревью стадии 4 — один warn за цикл, штамп не двигается);
  `logging.file changed - applies after restart`, `scanner.enabledOnStart changed -
  applies after restart` (ровно эти две restart-only строки; `configVersion` —
  метаданные, warn не печатает, §5); `hotkey <новая> is taken by another process, kept
  <старая>` — новая комбинация занята, слот возвращается старой (`hotkey Ctrl+8 is
  taken by another process, kept Ctrl+9`); `sound resource failed (PlaySound), reload
  applied without sound` — WAVE-ресурс reload.wav недоступен (закрытый minor m12),
  релоад применён без звука. **error**: `config.json is broken (line N, column M: msg),
  current config kept` — битый JSON в runtime, MessageBox нет (§8). Штамп двигается
  только на обработанную версию: та же битая версия логируется один раз.

При уровне `info` одиночные изменения значений (строки `tick:`) не пишутся, кроме первой
строки значений после присоединения/входа в зону (изменено 2026-11-08, plan-release.md
п. 4, §7); нажатия клавиш и held-строки пишутся (изменено 2026-11-08, группа L1). Live-тесты ведутся при
`debug`: пачка `tray callback event 512` (`WM_MOUSEMOVE` от трея при наведении курсора
на иконку, до ~50 событий) — штатный debug-шум, не проблема.

## 8. Обработка ошибок

| Ситуация | Поведение |
|---|---|
| `OpenProcess` → `ERROR_ACCESS_DENIED` | error в лог, тултип `Admin rights required` |
| Игра не запущена | штатный статус `Waiting for game`, опрос процесса продолжается |
| Игра запущена, не в зоне (главное меню, выбор персонажа, загрузка зоны) — изменено 2026-11-08, группа M1 | штатный статус `In menu / loading` (§4.3), хэндл держится открытым, опрос 1 с (§4.6 п.5), лога на опрос нет (§7) |
| Смещения не подходят | статус `Offsets mismatch`, подсказка про `find_offsets.py` |
| Каталог exe только для чтения | сообщение и выход |
| Отсутствующий/битый WAV | warning, работа без звука |
| Нет/битый `offsets.json` | error + MessageBox + выход с кодом 2 |
| Битый `config.json` в runtime (5 с цикл, hot-reload — изменено 2026-11-08) | error в лог **без MessageBox** (`config.json is broken (line N, column M: msg), current config kept`), сканер и приложение живут на текущем конфиге, 5 с цикл сохраняется (починка применяется), та же битая версия логируется один раз (§5, §7) |
| `config.json` с неизвестным ключом (изменено 2026-11-08, plan-release.md п. 2) | старт: error + MessageBox + выход с кодом 2; runtime: error **без MessageBox** (`config.json is invalid (unknown key "<путь>", …), current config kept`), жизнь на текущем конфиге, 5 с цикл сохраняется, та же версия логируется один раз (§5, §7) |

## 9. Критерии приёмки

1. `./build.sh` из cygwin собирает x64 Release без предупреждений `/W4`, делает
   strip и создаёт `dist/POE2FlaskHelper-<version>-poe2-<gamever>-win64.zip` с exe,
   `offsets.json`, `README.txt`, `README.ru.txt` (`config.json` в архиве нет — он
   генерируется при первом запуске, §2.2, plan-release.md п. 5) и публикует релиз на
   GitHub (отключение `--no-upload`, §2.2 п. 9).
2. Запуск из распакованного архива; требуется только VC++ v14 x64 Redistributable.
3. Старт в трее без окон; второй экземпляр не стартует.
4. ПКМ по трею: ровно `Enable/Disable scanner` (с галочкой) и `Exit`.
5. `Ctrl+9` переключает сканер при фокусе на игре; включение и выключение
   сопровождаются соответствующим WAV.
6. Нет `offsets.json` → error в лог, MessageBox, выход с кодом 2; чтений памяти нет.
7. При включённом сканере — attach и значения; при выключенном — ни одного
   `ReadProcessMemory`.
8. Vital ниже порога → клавиша в игре не чаще кулдауна; при `info` нажатия (`key=`) и
   единственная held-строка окна кулдауна в логе есть, строк значений (`tick:`) нет
   кроме первой после присоединения/входа в зону (изменено 2026-11-08, plan-release.md
   п. 4, §7); при `debug` есть и то, и другое (изменено 2026-11-08, live-тесты, группа L1 —
   §4.7, §7).
9. Игра не в фокусе — клавиши не отправляются.
10. `config.json`, `offsets.json`, `logs/` — рядом с exe независимо от cwd.
11. Изменено 2026-11-08 (plan-release.md п. 6): архив содержит два пользовательских
    документа — `README.txt` (English) и `README.ru.txt` (Russian), каждый на своём
    языке, с версией проекта из `configure_file`.
12. Изменено 2026-11-08: `ctest` зелёный; сборка пакета при красных тестах
    невозможна — `build.sh` гоняет `ctest` после strip и останавливается до
    упаковки, тестовый exe в zip не попадает.
13. Изменено 2026-11-08 (live-тесты, группа M1): игра запущена и находится в главном
    меню / на экране выбора персонажа — тултип `In menu / loading`, за 12 шагов опроса
    ровно одна info-строка входа (`in menu / loading (PID n) - not in a zone`),
    `attached PID` и `detached from` — не больше одной строки на процесс, строк
    `chain lost` и `waiting for game` нет, `OpenProcess` ровно один, на шаг меню
    приходится ~3 чтения памяти (§4.6 п.5, §7).
14. Изменено 2026-11-08 (версионирование, §2.4): номер живёт в одном месте —
    `project(... VERSION)`, `POE2FlaskHelper.exe --version` печатает ровно его, VERSIONINFO,
    манифест и имя zip носят ту же версию (`research/pe_res_check.py` в build.sh); README
    в архиве генерируется с той же версией; `build.sh` не пакует при нарушении
    согласованности «код <-> версия» (правила и гейт — §2.4).
15. Изменено 2026-11-08 (hot-reload, план plan-hot-reload.md §6): правка живого параметра
    `config.json` применяется без перезапуска не более чем за один 5 с цикл + следующий
    тик сканера: правка `vitals.Health.threshold` даёт info-строку
    `config.json reloaded (vitals.Health.threshold)`, играет `reload.wav` и срабатывание
    по новому порогу; старт — звука нет; битый JSON в runtime — error без MessageBox,
    сканер живёт на текущем конфиге, починка применяется, повторной строки про ту же
    битую версию нет; restart-only поля (`logging.file`, `scanner.enabledOnStart`) дают
    warn «applies after restart»; `./build.sh` зелёный с 3 WAVE-листьями в дереве
    ресурсов (`pe_res_check`), `ctest` зелёный, smoke-выход rc 0.
