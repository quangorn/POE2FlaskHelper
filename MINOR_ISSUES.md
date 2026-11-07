# MINOR_ISSUES — накопленный список minor-проблем

Формат записи:

```
## Stage N — <краткая суть>
- файл: src/... (строка/функция)
- суть: что не так
- статус: open | fixed-removed | live
- источник: reviewer stage N / live-checklist stage N
```

Правила:
- Major-проблемы сюда НЕ пишутся — они чинятся сразу в цикле ревью.
- Minor, мешающий следующему этапу, или появившийся повторно — исправляется
  сразу и удаляется из этого файла.
- В конце проекта — совместный проход по оставшимся записям.

---

## Stage 0 — review (build.sh, CMake, main.cpp, res/, tests/)

### S0-1: build.sh:90 — упаковка через python без проверки наличия
- файл: build.sh:90
- суть: cygwin `python` не входит в тулчейн SPEC §2; если его нет — сборка падает в самом конце с «command not found».
- статус: open
- источник: reviewer stage 0

### S0-2: CMakeLists.txt:14 — CMAKE_RC_FLAGS без кавычек
- файл: CMakeLists.txt:14
- суть: `-I ${CMAKE_BINARY_DIR}` ломается при пробелах в пути проекта; строка избыточна — include poe2_core уже транслируется в app.rc.
- статус: open
- источник: reviewer stage 0

### S0-3: CMakeLists.txt:29-30 — дефолтные лины CMake в линк-лайне
- файл: CMakeLists.txt:29-30
- суть: advapi32/ole32/uuid и т.д. от дефолта CMAKE_LINK_LIBRARY_FLAG; в exe не попадают (dumpbin чист), SPEC §2 формально говорит «advapi32 не используется». Комментарий, чтобы никто не паниковал.
- статус: open
- источник: reviewer stage 0

### S0-4: CMakeLists.txt:18 — /MANIFEST:NO отсутствует в списке линк-флагов SPEC §2
- файл: CMakeLists.txt:18
- суть: флаг необходим (манифест внедряет app.rc, линковочный дублировал бы); добавить комментарий над флагом.
- статус: open
- источник: reviewer stage 0

### S0-5: res/app.manifest:3 — версия assemblyIdentity захардкожена
- файл: res/app.manifest:3
- суть: version="1.0.0.0" zestareет при bump VERSION; генерить из .in с @PROJECT_VERSION@.
- статус: open
- источник: reviewer stage 0

### S0-9: build.sh:12-13 — vswhere -latest может выбрать VS без CMake/Ninja
- файл: build.sh:12-13
- суть: тогда cmake «command not found» внутри cmd; проверить наличие cmake.exe и FATAL с внятным сообщением.
- статус: open
- источник: reviewer stage 0

### S0-10: README.txt:4,10-13 — внутреннее противоречие «незапускаемый каркас» vs «--version печатает»
- файл: README.txt
- суть: формулировка EN/RU про не запускаемый каркас contradicts строке про --version. Косметика, переформулировать при финальном README (этап 5).
- статус: open
- источник: reviewer stage 0

### S0-11: res/app.rc:1-2 — лишний #include <windows.h>
- файл: res/app.rc:1-2
- суть: VERSIONINFO его не требует; убрать.
- статус: open
- источник: reviewer stage 0

### S0-12: tests/test_main.cpp:42-44 — exit-path run_all() проверен только через probe-счётчики
- файл: tests/test_main.cpp:42-44
- суть: тест не может самозапустить run_all(); честная запись в комментарии есть. Действий нет.
- статус: open (no-action)
- источник: reviewer stage 0

### S0-13: res/gen_assets.py:29 — AND-маска all-zero
- файл: res/gen_assets.py:29
- суть: прозрачность только через alpha 32bpp; для современных оболочек корректно. Действий нет.
- статус: open (no-action)
- источник: reviewer stage 0

### S0-14: build.sh:4,63 — set -u отсутствует; rc strip игнорируется (за ним guard)
- файл: build.sh:4,63
- суть: намеренно; действий нет.
- статус: open (no-action)
- источник: reviewer stage 0

## Stage 1 — review (json, log, config, offsets, keys, app)

### S1-1: src/json.cpp:112-124 — одиночная низкая суррогатная пара \udc00 принимается
- файл: src/json.cpp:112-124
- суть: \udc00 принимается и эмитит CESU-8 байты ed b0 80; отклонять симметрично одиночной высокой суррогат.
- статус: open
- источник: reviewer stage 1

### S1-2: README.txt:12,23 — скрытый флаг --batch в дистовом README
- файл: README.txt:12,23
- суть: поведение флага SPEC-совместимо, но SPEC §2.3 не предусматривает его в README; убрать из дистового README на этапе 5.
- статус: open
- источник: reviewer stage 1

### S1-3: README.txt:1,15 — «Stage 0 stub / незапускаемый каркас» уже ложь
- файл: README.txt:1,15
- суть: exe этапа 1 создаёт конфиг, логи, выходит чисто; переформулировать на этапе 5 (усугубление S0-10).
- статус: open
- источник: reviewer stage 1

### S1-4: src/main.cpp:78-102 — конфиг/offsets I/O на главном потоке
- файл: src/main.cpp:78-102
- суть: SPEC §4.2 формально запрещает файловый I/O на главном потоке; по духу — речь о message loop, на старте других потоков нет. Добавить комментарий, что это startup-only, до message loop.
- статус: open
- источник: reviewer stage 1

### S1-5: tests/log_tests.cpp — нет теста откры失败的 лога и read-only exe dir
- файл: tests/log_tests.cpp; src/log.cpp:133-136; src/main.cpp:94-100
- суть: ветка healthy()==false (лог не открывается) и MessageBox-ветка read-only каталога exe не покрыты тестами. Тест: log path под regular file; read-only dir — через scratch.
- статус: open
- источник: reviewer stage 1

### S1-6: tests/test_tmp.cpp:8, tests/fakes/FakeClock.cpp:8 — anchor-функции не объявлены в заголовках
- файл: tests/test_tmp.cpp:8; tests/fakes/FakeClock.cpp:8
- суть: безвредно; объявить в headers.
- статус: open
- источник: reviewer stage 1

### S1-7: src/offsets.cpp:70 — field() отдаёт &o.Score как fallback для любого неизвестного имени
- файл: src/offsets.cpp:70
- суть: ветка Score должна быть явной, не fallback.
- статус: open
- источник: reviewer stage 1

### S1-8: src/config.cpp:207-208 — дублируется литерал массива имён Vital'ов
- файл: src/config.cpp:207-208
- суть: переиспользовать kVitalNames.
- статус: open
- источник: reviewer stage 1

### S1-9: numeric-поля не принимают float ({"threshold":50.0}) — нет теста
- файл: src/config.cpp
- суть: float в int-поле отвергается → warning+default; SPEC не запрещает; добавить тест, фиксирующий поведение.
- статус: open
- источник: reviewer stage 1

### S1-10: src/args.cpp:34 — упрощения vs CommandLineToArgvW не задокументированы
- файл: src/args.cpp:34, src/args.h
- суть: `"a""b"` → `ab` (референс `a"b`); ведущие пробелы: референс даёт пустой argv[0], split его дропает. На match_flag не влияет; добавить одну строку документации в args.h.
- статус: open
- источник: reviewer stage 1 (re-review)

### S1-11: tests/args_tests.cpp — два гэпа покрытия
- файл: tests/args_tests.cpp (command_line_split_quoting)
- суть: не покрыты незакрытая кавычка (`app.exe "unclosed --version`) и trailing 2n бэкслешей перед закрывающей кавычкой в конце строки (`app.exe C:\dir\\"`); обе ветки проверены ревьюером вручную как корректные.
- статус: open
- источник: reviewer stage 1 (re-review)

### S1-12: src/system_clock.h:27 — TIME_ZONE_ID_INVALID молча штампуется UTC
- файл: src/system_clock.h:27
- суть: приемлемо для MVP (задокументировано); добавить одноразовную debug-запись в лог при неудачном взгляде зоны.
- статус: open
- источник: reviewer stage 1 (re-review)

## Assets (задача, не проблема)
- Пользователь предоставил res/on.wav и res/off.wav; заменено в enable.wav/disable.wav (коммит `6e3596f`).
- Пользователь предоставил иконку: готовые записи 16–256 px с прозрачным фоном легли в app.ico (коммит `7b286a3`), заглушка заменена.
- Ассеты закрыты: финальных от пользователя больше не ждём.
- статус: fixed-removed
- источник: user 2026-11-07

## Stage 2 — review (tray, hotkeys, sound, state)

### S2-5: src/main.cpp:228-232 — startup-enable играет enable.wav
- файл: src/main.cpp:228-232
- суть: SPEC §4.5 перечисляет триггеры hotkey+menu, старта нет; defensable, расхождение. Уточнить в SPEC v1.1 на этапе 5.
- статус: open
- источник: reviewer stage 2

## Stage 3 — review (signature, chain, tick, scanner_session, win_reader)

### S3-2: src/scanner.cpp:15-18 — event handle `wake_` не закрывается
- файл: src/scanner.cpp:15-18
- суть: нет CloseHandle(wake_) после join; поглощается выходом процесса. Fix: CloseHandle в ~Scanner() после stop().
- статус: open
- источник: reviewer stage 3

### S3-3: src/scanner.h:51 — мёртвый член `joined_`
- файл: src/scanner.h:51
- суть: delete или использовать явной bookkeeping-отметкой join.
- статус: open
- источник: reviewer stage 3

### S3-4: src/chain.cpp:121-127 — find_life_index строже poe_scan.py на границе региона
- файл: src/chain.cpp:121-127
- суть: py read_cstring читает 16-байтовые чанки за коротким чтением и принимает "Life" на границе без явного NUL; C++ требует got≥5+NUL. Impact пренебрежим, строгость defensable; паритет — эмулировать read_cstring.
- статус: open
- источник: reviewer stage 3

### S3-5: src/scanner_session.cpp:34 — half-up округление debug-строки vs banker's в py
- файл: src/scanner_session.cpp:34
- суть: `(100*current + unres/2)/unres` half-up; py round() banker's; debug-дисплей на .5 печатает на 1 выше; решения не зависит. Косметика, выравнивание через nearbyint или оставить с заметкой.
- статус: open
- источник: reviewer stage 3

### S3-6: src/chain.cpp:160-182 — full_chain step C строже poe_scan.py
- файл: src/chain.cpp:160-182
- суть: проверяет `(cs_last − cs_first) % 16` и non-null current-state — py не валидирует; строгость безвредна; расширить комментарий о намеренной строгости.
- статус: open
- источник: reviewer stage 3

### S4-1: src/vital_math.cpp:11 — reserved_amount клампит отрицательный flat к 0 vs py truncation
- файл: src/vital_math.cpp:11
- суть: py `int(x + 0.999999)`truncate'т отрицательные к нулю, C клампит к 0; недостижимо (tick читает u32), «defensive»; добавить parity-заметку.
- статус: open
- источник: reviewer stage 4

### S4-2: src/vitals.h:36 — комментарий «extended = E0 prefix for NumPad», SPEC §4.8 включает стрелки
- файл: src/vitals.h:36
- суть: формулировка неполная (keys.h:53 корректна); код верён; косметика.
- статус: open
- источник: reviewer stage 4
