# POE2FlaskHelper

Трей-приложение для Windows x64: читает HP/Mana/Energy Shield/Ward/Rage из памяти
`PathOfExile.exe` (Path of Exile 2) и при падении значения ниже порога отправляет игре
клавишу зелья. Запись в память игры отсутствует.

## Структура

- [`SPEC.md`](SPEC.md) — спецификация приложения: стек, сборка, конфиг, поведение
  трея и сканера, критерии приёмки.
- [`plan.md`](plan.md) — план этапов реализации.
- [`backlog.md`](backlog.md) — что отложено (окно настройки конфига). Громкость WAV
  реализована 2026-11-08 (`sound.volume`).
- [`CHANGELOG.md`](CHANGELOG.md) — история версий и правила подъёма (`1.major.minor`).
- [`version.sh`](version.sh) — подъём версии (`major`/`minor`/`series`) и гейт
  согласованности «код ↔ версия» для `build.sh` (SPEC §2.4).
- [`res/README.txt`](res/README.txt.in) — пользовательский двуязычный документ (English +
  Russian); `configure_file` подставляет в него версию проекта, `build.sh` копирует
  сгенерированный файл в корень архива.
- [`src/`](src) — код: чистое ядро `poe2_core` (json, config, offsets, log, state, keys,
  signature, chain, vital_math, vitals, scanner_session, wav, clock) и exe-слой со всем
  Win32 (main, tray, hotkeys, sound, scanner, game_process, win_keys, mem/, system_clock,
  res_ids).
- [`tests/`](tests) — мини-фреймворк `test.h` + `*_tests.cpp` + фейки
  (`FakeMemory`, `FakeClock`, `FakeProcess`, `RecordingKeySender`) и синтетический образ
  процесса (`synthetic_layout`, `session_fixture`); ядро тестируется без игры, без
  Win32-вызовов и без sleep.
- [`res/`](res) — ассеты: `app.ico`, `enable.wav`, `disable.wav` (финальные, PCM16),
  `app.rc`, `app.manifest.in`, `version.h.in`, `res_paths.rc.in`, `gen_assets.py`,
  `README.txt.in`.
- [`scanner/`](scanner/README.md) — работа с живым процессом: чтение значений по
  цепочке смещений (`poe_scan.py`), поиск смещений HP/Mana/ES (`find_offsets.py`) и
  новых Vital'ов Ward/Rage/… (`find_vital.py`). Только чтение.
- [`research/REPORT.md`](research/REPORT.md) — разбор цепочки адресации, кода ауры
  POE2-Mem (Cheat Engine), сравнение ревизий.
  - `code/` — извлечённый C#-код ауры по ревизиям
  - `extract_code.py`, `offsets_table.py`, `verify_signature.py`, `check_symbols.py`,
    `find_component_names.py`, `life_context.py`, `pe_res_check.py` — инструменты анализа
  - `revisions/` — распакованный сторед конфига пака (в репозиторий не входит, см. `.gitignore`)

## Сборка и тесты

`./build.sh` (cygwin bash): vcvars64 → VS cmake/ninja → Release → версионные гейты
(SPEC §2.4: `./version.sh check`, stdout `--version`, PE-ресурсы) → strip + целостность →
`ctest` (красные тесты останавливают сборку до упаковки) → dist zip
`dist/POE2FlaskHelper-<version>-win64.zip` (exe + `config.json` + `offsets.json` +
`README.txt`) и smoke-запуск staged-копии.

Сборка требует Visual Studio 2022 с компонентом «CMake tools for Windows» (cmake + ninja
из установки VS) и python в PATH (gate состава zip). Сборку не гоняй при запущенном
`POE2FlaskHelper`: smoke-шаг находит трей-окно по классу и шлёт ему `WM_CLOSE`.

## Версионирование

Формат — `1.major.minor` (ведущая цифра — серия): **major** при новых фичах
(`1.1.0 → 1.2.0`), **minor** при фиксах и мелких изменениях (`1.1.0 → 1.1.1`), смена
серии (`2.0.0`) — только при существенной переработке архитектуры. Если код проекта
(`src/` и `res/` ассеты/rc/manifest) не менялся, exe актуален и версия не поднимается.
История — [`CHANGELOG.md`](CHANGELOG.md).

Номер живёт в одном месте — `project(POE2FlaskHelper VERSION ...)` в `CMakeLists.txt`;
он попадает в `--version`, VERSIONINFO, манифест, генерируемый `README.txt` и имя zip.
Подъём делается только через `./version.sh`:

```
./version.sh major "описание фичи"     # 1.1.0 -> 1.2.0
./version.sh minor "описание фикса"    # 1.1.0 -> 1.1.1
./version.sh series "описание"         # смена серии
./version.sh check                     # гейт согласованности код <-> версия (build.sh)
```

`version.sh` правит `CMakeLists.txt` и `CHANGELOG.md` и никогда не коммитит; после
подъёма гоняй `./build.sh` и коммити `CMakeLists.txt` + `CHANGELOG.md`. Правило 3
проверяется при подъёме (отказ, если код не менялся; исключение — `--force`) и в сборке
(красых = zip не создаётся). Подробности — SPEC §2.4.

## Требования

Для приложения: Windows x64 и Visual C++ v14 x64 Redistributable
(https://aka.ms/vs/17/release/vc_redist.x64.exe). Для инструментов `scanner/`:
Windows-нативный Python 3.x 64-bit, `pymem` не требуется. Подробности запуска и опции —
в [`scanner/README.md`](scanner/README.md).
