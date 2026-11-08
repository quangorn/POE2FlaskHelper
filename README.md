# POE2FlaskHelper

Трей-приложение для Windows x64: читает HP/Mana/Energy Shield/Ward/Rage из памяти
`PathOfExile.exe` (Path of Exile 2) и при падении значения ниже порога отправляет игре
клавишу зелья. Запись в память игры отсутствует.

## Структура

- [`SPEC.md`](SPEC.md) — спецификация приложения: стек, сборка, конфиг, поведение
  трея и сканера, критерии приёмки.
- [`plan.md`](plan.md) — план этапов реализации.
- [`backlog.md`](backlog.md) — что отложено (окно настройки конфига, громкость WAV).
- [`res/README.txt`](res/README.txt) — пользовательский двуязычный документ (English +
  Russian); `build.sh` копирует его в корень архива.
- [`src/`](src) — код: чистое ядро `poe2_core` (json, config, offsets, log, state, keys,
  signature, chain, vital_math, vitals, scanner_session, clock) и exe-слой со всем Win32
  (main, tray, hotkeys, sound, scanner, game_process, win_keys, mem/, system_clock,
  res_ids).
- [`tests/`](tests) — мини-фреймворк `test.h` + `*_tests.cpp` + фейки
  (`FakeMemory`, `FakeClock`, `FakeProcess`, `RecordingKeySender`) и синтетический образ
  процесса (`synthetic_layout`, `session_fixture`); ядро тестируется без игры, без
  Win32-вызовов и без sleep.
- [`res/`](res) — ассеты: `app.ico`, `enable.wav`, `disable.wav` (финальные, PCM16),
  `app.rc`, `app.manifest.in`, `version.h.in`, `res_paths.rc.in`, `gen_assets.py`,
  `README.txt`.
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

`./build.sh` (cygwin bash): vcvars64 → VS cmake/ninja → Release → strip + целостность →
`ctest` (красные тесты останавливают сборку до упаковки) → dist zip
`dist/POE2FlaskHelper-<version>-win64.zip` (exe + `config.json` + `offsets.json` +
`README.txt`) и smoke-запуск staged-копии.

Сборка требует Visual Studio 2022 с компонентом «CMake tools for Windows» (cmake + ninja
из установки VS) и python в PATH (gate состава zip). Сборку не гоняй при запущенном
`POE2FlaskHelper`: smoke-шаг находит трей-окно по классу и шлёт ему `WM_CLOSE`.

## Требования

Для приложения: Windows x64 и Visual C++ v14 x64 Redistributable
(https://aka.ms/vs/17/release/vc_redist.x64.exe). Для инструментов `scanner/`:
Windows-нативный Python 3.x 64-bit, `pymem` не требуется. Подробности запуска и опции —
в [`scanner/README.md`](scanner/README.md).
