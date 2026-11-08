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

S0-1, S0-2, S0-3, S0-4, S0-5, S0-9, S0-11 исправлены и удалены в проходе по GROUP D (2026-11-08).

### S0-12: tests/test_main.cpp:42-44 — exit-path run_all() проверен только через probe-счётчики
- файл: tests/test_main.cpp:42-44
- суть: тест не может самозапустить run_all(); честная запись в комментарии есть. Действий нет.
- статус: leave (фреймворк не может сам себя запустить изнутри теста: exit-код `run_all()`
  проверяется снаружи ctest, probe-счётчики честны; комментарий в тесте это описывает)
- источник: reviewer stage 0

### S0-13: res/gen_assets.py:29 — AND-маска all-zero
- файл: res/gen_assets.py:29
- суть: прозрачность только через alpha 32bpp; для современных оболочек корректно. Действий нет.
- статус: leave (32bpp alpha — корректный путь прозрачности для современных оболочек,
  all-zero AND-маска = «полностью непрозрачно» по формату ICO; legacy-монохромный путь не нужен)
- источник: reviewer stage 0

### S0-14: build.sh:4,63 — set -u отсутствует; rc strip игнорируется (за ним guard)
- файл: build.sh:4,63
- суть: намеренно; действий нет.
- статус: leave (`set -u` опущен намеренно, rc strip игнорируется осознанно — за strip стоит
  guard целостности (plan §5 risk 2), поведение задокументировано в комментариях build.sh)
- источник: reviewer stage 0

## Stage 1 — review (json, log, config, offsets, args, system_clock)

Пусто: S1-1, S1-4..S1-12 исправлены и удалены в проходе по GROUP B (2026-11-08).

## Assets (задача, не проблема)
- Пользователь предоставил res/on.wav и res/off.wav; заменено в enable.wav/disable.wav (коммит `6e3596f`).
- Пользователь предоставил иконку: готовые записи 16–256 px с прозрачным фоном легли в app.ico (коммит `7b286a3`), заглушка заменена.
- Ассеты закрыты: финальных от пользователя больше не ждём.
- статус: fixed-removed
- источник: user 2026-11-07

## Stage 3 — review (signature, chain, tick, scanner_session, win_reader)

Пусто: S3-2..S3-6 исправлены и удалены в проходе по GROUP A (2026-11-08).

## Stage 4 — review (vital_math, vitals)

### S4-1: src/vital_math.cpp:11 — reserved_amount клампит отрицательный flat к 0 vs py truncation
- файл: src/vital_math.cpp:11
- суть: py `int(x + 0.999999)`truncate'т отрицательные к нулю, C клампит к 0; недостижимо (tick читает u32), «defensive»; добавить parity-заметку.
- статус: leave (паритет не реализуем integer-exact: py truncation'ит FLOAT `pct/10000*total + flat + 0.999999` к нулю,
  при отрицательной сумме это не ceil и зависит от double-округления; все поля tick читаются как u32 (chain.cpp block_u32),
  так что отрицательный flat недостижим. Clamp + заметка в vital_math.{h,cpp} + тест reserved_negative_flat_is_clamped_defensive.)
- источник: reviewer stage 4

## Stage 5 — review (README, SPEC v1.1, build.sh gate, .gitattributes)

S5-1..S5-5 исправлены и удалены в проходе по GROUP E1 (2026-11-08). S5-1 объединён с D2:
заглушка `src/core_placeholder.{h,cpp}` + CMake-entry + 2 stub-теста удалены; в
tests/test_main.cpp осталась version-консистентность без хардкода "1.0.0" (макросы
сравниваются между собой), bump VERSION 2.5.1 → ctest зелёный, zip
`POE2FlaskHelper-2.5.1-win64.zip` (D2-доказательство; S0-5: версия следует за project VERSION).

## Stage 5 — re-review (маjors M-1/M-2 закрыты)

R5-1..R5-4 исправлены и удалены в проходе по GROUP C (2026-11-08; измерение FindWindowW —
таблица в отчёте группы C: FindWindowW и EnumWindows+GetClassNameW возвращают один HWND на
hidden-окне). R5-5 исправлен и удалён в проходе по GROUP E1 (2026-11-08): build.sh гоняет
dist-exe smoke-run из временной копии staged-каталога (запуск + FindWindowW +
PostMessageW(WM_CLOSE) + rc 0, SPEC §9-2) перед созданием zip.

## Group A — re-review (core scanner minors)

GA-1, GA-2 исправлены и удалены в проходе по GROUP E1 (2026-11-08): thread-тест
`tests/scanner_thread_tests.cpp` (start→stop→start на реальном event, base+2/base+1/base+2/
base+1/base handle-count asserted) + метод измерения записан в src/scanner.cpp.

### GA-3: py read_cstring decode('ascii','replace') vs C++ raw-bytes
- файл: src/chain.cpp:63-81
- суть: практических расхождений для равенства "Life" нет (любой не-ASCII отклоняется обеими сторонами); действий нет.
- статус: leave (py decode('ascii','replace') и C++ raw-bytes дают один и тот же результат на
  равенстве "Life": py-replace вставляет U+FFFD (не-ASCII), C++ сохраняет raw-байт — обе стороны
  не равны "Life"; ASCII-имена совпадают побайтно. Функционального расхождения нет, паритет
  boundary-кейсов (S3-4) уже покрыт тестами. Действий нет.)
- источник: reviewer group A

## Group B — re-review (Stage-1 core minors)

BR-1, BR-2, BR-3, BR-4, BR-5, BR-7 исправлены и удалены в проходе по GROUP E2 (2026-11-08):
BR-1 — src/args.h задокументировал третью семью (кавычка, смежная с некавычечным текстом:
`app.exe"x"` → API `app.exe"x"`, split `app.exex`; пустая/whitespace command line: argvw n=1
empty vs split n=0), flag-паритет-формулировка сохранена; BR-2 — comment в
tests/args_tests.cpp приведён к фактическому count (15 agree + 2 pinned divergences +
0 flag-расхождений); BR-3 — SPEC §7 и README.txt (EN+RU): debug-запись часового сбоя видна
только при `logging.level: "debug"`; BR-4 — ACL/read-only-exe-dir вариант открыт комментарием
в tests/log_tests.cpp (тот же `open_failed_`, новой ACL-тест-записи нет); BR-5 — raw-byte pass-through
задокументирован в src/json.h (escape-сторона закрыта S1-1, валидация — за потребителем);
BR-7 — амеднт в SPEC §4.2 (прецедент §4.3, 2026-11-08), comment main.cpp:111-115 остаётся.

### BR-6: assert(false) в field() исчезает под Release NDEBUG
- файл: src/offsets.cpp:73-75
- суть: подтверждено NDEBUG; nullptr + caller-error делают ветку мёртвой; silent UB нет.
- статус: leave (под NDEBUG assert снят, но ветка мёртвая по конструкции: field() вызывается
  только с именами kKeys, caller обрабатывает nullptr явной error-строкой "internal: no field
  for key" — silent UB нет; assert = документация инварианта в debug-сборке, инвариант
  пинит offsets_key_field_mapping_is_injective. Действий нет.)
- источник: reviewer group B

## Group C — re-review (UI-доки)

CR-1, CR-2 исправлены и удалены в проходе по GROUP E2 (2026-11-08): src/tray.h
(header-comment про exit-хуки) разделил menu и dialog одной точной заметкой — внешний
WM_CLOSE дисмиссит оба #32770-объекта
(MessageBox «already running» исчезает, второй экземпляр выходит rc 0; popup-меню закрывается),
внешний BM_CLICK не дисмиссит ни один (диалогу и Button-ребёнку); недисмиссенное модальное меню
блокирует shutdown exit-хука (TrackPopupMenu consumes PostQuitMessage).

## Group D — re-review (tooling/build minors)

D1 исправлен и удалён в проходе по GROUP E1 (2026-11-08): name-offset = `key & 0xFFFF`;
tree-лист на dist zip печатает WAV-листья как `WAVE 102`, `WAVE 103` (и `GROUP_ICON 101`).
D2 исправлен и удалён вместе с S5-1 (заглушка удалена, версия проверяется по макросам).

## Group E1 — re-review (release/core minors)

E1-R1, E1-R2 исправлены и удалены в проходе по микро-группе F (2026-11-08): SPEC §3 root-строка
пополнена README.txt, tests-строка — test_tmp.{h,cpp} и session_fixture.h; build.sh 5b получил
комментарий-допущение «нет конкурентного экземпляра/параллельного build.sh».
