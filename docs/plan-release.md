# План доработок (релиз 1.3.0)

План от 2026-11-08. Порядок работ: сканер → ядро → app-слой → docs → сборка/релиз.
Опора: [`SPEC.md`](SPEC.md), [`plan.md`](plan.md), [`plan-hot-reload.md`](plan-hot-reload.md).

Статус (2026-11-09): **выполнено, архив** — п. 1–8 реализованы, релиз 1.3.0 (коммит
`11b97e2`).

## 1. Версия poe2 в offsets.json (сканер)

- `scanner/poe_scan.py`: helper `game_version(exe_path, pid)` — полный путь процесса из
  `find_pid`/`image_path` (`QueryFullProcessImageNameW`). PathOfExile.exe БЕЗ ресурса
  VERSIONINFO (проверено живым прогоном), основной источник — `logs/LatestClient.txt`:
  строка запуска `User agent: PoE poe2_production/tags/0.5.5f Windows x64` (первые
  200 строк, при pid обязательна `[INFO Client <pid>]`); VERSIONINFO — fallback для
  exe с ресурсом. `game_version_short`: "0.5.5f" -> "0.5.5f", "1.4.0.51299" -> "1.4.0".
  Печатать версию в не-quiet выводе скана.
- `scanner/find_offsets.py --save`: первой строкой JSON ключ `"GameVersion": "0.5.5f"`,
  затем смещения; selftest-файл без GameVersion (нет игры — нет версии).
- `scanner/poe_scan.py --offs-file`: пропускать не-числовые значения (строковый
  GameVersion) — правка цикла str()→int(...,16).
- Корневой `offsets.json`: `"GameVersion": "0.5.5f"` (rev34, проверен живым прогоном
  2026-11-07 на патче 0.5.5f).
- App: `offsets.cpp` игнорирует неизвестные ключи — тест `offsets_tests.cpp`
  на GameVersion-ключ.
- Проверка «версия вычитывается корректно»: живой прогон с запущенной игрой
  (`find_offsets.py --hp ... --save offsets.json`), офлайн — на логах последней
  сессии и exe с VERSIONINFO.

## 2. Неизвестная опция = невалидный конфиг (выход)

- `src/config.{h,cpp}`: `check_unknown` собирает список неизвестных ключей;
  `ConfigResult.invalid` + сообщение со именем ключа. Некорректные значения —
  как сейчас (warn + default).
- `main.cpp`: при невалидном конфиге на старте — error в лог
  (`config.json is invalid (unknown key "...")`), MessageBox, выход с кодом 2
  (как при отсутствии offsets.json), flush.
- Runtime (5 с цикл, `config_reload` → `ReloadOutcome.invalid`): error
  `config.json is invalid (unknown key "..."), current config kept`, конфиг
  не применяется, звука нет, stamp двигается (та же версия логируется один раз).
- Тесты: `config_tests.cpp` (unknown на всех уровнях: root/hotkeys/scanner/sound/
  vitals/имя Vital'а/logging), `reload_tests.cpp` (runtime keep-current).
  SPEC §5/§8, README'и.

## 3. «<=» в лог-строках

- `press_line()`/`held_line()` (`src/scanner_session.cpp`) → `% <=`
  (факт: округлённый percent может быть равен порогу); комментарий `vitals.h`.
- Тесты `vital_session_tests.cpp`, `reload_tests.cpp`; SPEC §7; README.txt.in EN+RU.

## 4. Info-лог первого получения vital'ов

- `ScannerSession`: флаг `first_values_logged_`; первый успешный `tick()` после
  attach пишет `values_line()` уровнем Info. Сброс: `detach_info()` (закрытие/
  перезапуск игры, disable сканера, chain break) + вход в `Phase::InMenu`.
- Тесты `vital_session_tests.cpp`; SPEC §7; README'и (Files).

## 5. config.json: генерация на первом запуске, не в архиве

- `build.sh`: убрать `cp config.json` из staging и состава zip-gate; состав
  архива: exe + offsets.json + README.txt + README.ru.txt; smoke-прогон без
  config.json подтверждает генерацию on-first-run.
- Корневой `config.json` → `res/config.example.json` (эталон дефолтов).
  SPEC §2.2/§3/§9, README'и.

## 6. README'и (EN + RU)

- Корень: `README.md` → английский (с учётом всех доработок), новый `readme.ru.md`.
- `res/README.txt.in` → `README.txt.in` (EN) + `README.ru.txt.in` (RU); два
  `configure_file` в `CMakeLists.txt`; build.sh кладёт оба.
- `scanner/README.md` → английский, новый `scanner/README.ru.md`.

## 7. Удаление research/code

- Удалить `research/code/`, `offsets_table.py`, `extract_code.py`, `rev32_rev33.diff`.
- Правки ссылок: корневые README'и, `poe_scan.py` (комментарий «из code/revNNN»),
  `../research/REPORT.md` (стр. 7–11, 320, 461–462). `pe_res_check.py`,
  `check_symbols.py`, `verify_signature.py`, `life_context.py`,
  `find_component_names.py` остаются.

## 8. Upload релизов на GitHub

- Новый `release.sh`: upload по умолчанию; отключение опцией `--no-upload`
  (для build.sh — `--no-upload` пробрасывается).
- gh: PATH, fallback `"C:/Program Files/GitHub CLI/gh"` (проверен: 2.103.3,
  аккаунт quangorn).
- Тег: `v<version>-poe2-<gamever>`; title и body — на английском.
  Источник body: англоязычная заметка релиза, `CHANGELOG.en.md` (новый файл;
  запись добавляет version.sh вторым описанием, русская запись CHANGELOG.md
  остаётся). Title: `POE2FlaskHelper <version> (POE2 <gamever>)`. Для
  offsets-only релиза (версия exe не менялась, менялась poe2) — body
  «Updated memory offsets for POE2 <gamever>, program code unchanged».
- `build.sh --no-upload` → create zip + release без upload; дефолт — upload.
- Повторный upload существующего тега: сообщение и non-zero выход.
- SPEC §2.2, README'и.

## Завершение

`version.sh major` (1.2.3 → 1.3.0, новые фичи), CHANGELOG, полный `./build.sh`
(все гейты), коммит. Сценарий «новый патч игры» без смены версии exe:
find_offsets.py → offsets.json в репозиторий → `./build.sh` (version.sh check
green при неизменном коде) → архив с новой poe2 в имени.
