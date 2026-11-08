# minor-issues.md — отложенные мелочи hot-reload (рабочий документ, ведёт оркестратор)

Правила:
- сюда пишутся MINOR-находки ревьюверов по стадиям hot-reload (план: `plan-hot-reload.md`);
- MAJOR-находки здесь не живут: они закрываются сразу цикл-ревьювером;
- minor, который мешает следующей стадии или появляется повторно, исправляется сразу
  и удаляется отсюда;
- в конце проходим по файлу вместе с пользователем и решаем, что исправлять.

СТАТУСЫ: [ЗАКРЫТО] — исправлено по ходу стадий; [ОТКРЫТО] — ждёт обсуждения.

## Стадия 1 (ядро config: FileStamp/stamp_changed/config_diff/path_is_live/config_reload)

VERDICT: STAGE 1 OK, majors нет. Коммит `81c4ec0`, ctest 200/200, /W4 чисто.

- m1 [ЗАКРЫТО стадией 5] canonical-порядок diff (`sound.volume` до `vitals.*`) против примера
  плана §3 `(vitals.Health.threshold, sound.volume)` — пример в план-тексте переписан на
  canonical-порядок (m13-правка), handler печатает порядок ядра.
- m2 [ЗАКРЫТО стадией 4] configVersion в restart_changed без warn — handler main.cpp:434-442
  печатает warn только для logging.file/scanner.enabledOnStart; живой прогон Run D: правка
  configVersion 1→2 → `config.json reloaded (no changes)`, warn нет.
- m3 [ЗАКРЫТО 2b7fe5c] tests/reload_tests.cpp — нет теста stamp changed + read_ok=true + пустой текст
  (`config_parse("")` → `json_broken`). Дешёвый детерминированный кейс.
- m4 [ЗАКРЫТО 2b7fe5c] tests/reload_tests.cpp — магические литералы штампа `{100,500}`; стиль-only.

## Стадия 2 (ConfigSnapshot + ScannerSession + Logger::set_options)

VERDICT: STAGE 2 OK, majors нет. Коммит `76cb171`, ctest 208/208, /W4 чисто.

- m5 [ЗАКРЫТО 2b7fe5c] tests/session_tests.cpp:469 — устаревший комментарий `cfg.pollIntervalMs = 20;
  // consumed by Deps as a copy at construction` (Deps больше не несёт ScannerCfg-копию).
- m6 [ЗАКРЫТО стадией 4] main.cpp:231 метка «stage 3» — актуализирована при написании handler'а.
- m7 [ЗАКРЫТО 2b7fe5c] tests/reload_tests.cpp:20-24 — includes `fakes/FakeMemory.h`, `FakeProcess.h`,
  `synthetic_layout.h` избыточны (приходят через session_fixture.h).

## Стадия 3 (app-слой каркас: file_stamp, tray-таймер, WAVE-ресурс, state-id, CMake)

VERDICT: STAGE 3 OK, majors нет. Коммит `a57a6e2`, ctest 209/209, pe_res_check PASSED
(WAVE 102/103/104, lang 0x409), smoke rc=0. «LOWORD(lParam)» опровергнуто ревьювером:
WM_TIMER dispatch по wParam — документированный путь.

- m8 [ЗАКРЫТО стадией 4] config.h:76 метка «stage 2» → stage 3.
- m9 [ЗАКРЫТО стадией 4] tray.cpp:119 формулировка «handler body is empty» — актуализирована.
- m10 [ЗАКРЫТО 2b7fe5c] src/win_text.h + tray.cpp/file_stamp.cpp — третья копия `to_wide()` (репозиторий терпит
  per-TU дубликаты); informational.

## Стадия 4 (main.cpp: handler 5 с, хоткей, лог-строки, громкость из снимка)

VERDICT: STAGE 4 OK, majors нет. Коммит `cd0856e`, ctest 209/209, pe_res_check PASSED,
smoke rc=0, 12 живых прогонов A-G зелёные (битый JSON без MessageBox подтверждён
#32770-поллингом; hotkey restore мерен внешним RegisterHotKey; locked-файл warn раз в цикл
5.000 с; configVersion без warn).

- m11 [ЗАКРЫТО стадией 5] удалённый/неоткрываемый config.json даёт warn «locked by another
  process» — SPEC §7 теперь фиксирует тот же warn при удалённом файле (заметка).
- m12 [ЗАКРЫТО стадией 5] warn «sound resource failed (PlaySound), reload applied without
  sound» — включён в SPEC §7 enumeration + план §5 list (n4-правка).
- m13 [ЗАКРЫТО стадией 5 + n2] пример canonical-порядка в плане; residual-формулировка
  правлена (n2).

## Стадия 5 (документация + версия 1.2.0)

VERDICT: STAGE 5 OK, majors нет. Коммиты `f463627` (docs) + `080fbb1` (release 1.2.0),
полный ./build.sh зелёный ревьювера: 0 warnings, version gate ok 1.2.0, strip --version
prints 1.2.0, pe_res_check PASSED, ctest 209/209, smoke rc=0, zip
dist/POE2FlaskHelper-1.2.0-win64.zip composition ровно 4 файла, README 1.2.0.

- n1 [ЗАКРЫТО] SPEC.md:618-619 — ложная атрибуция canonical-порядка «plan §2: там
  sound.volume до vitals.*»; правка: порядок закрепляет config_diff+тесты, plan §2 = набор путей.
- n2 [ЗАКРЫТО] plan-hot-reload.md:117-119 — m13-заметка описывала несуществующий пример
  `(vitals.*, sound.*)`; правка: пример canonical, показывает форму.
- n3 [ЗАКРЫТО 2b7fe5c] SPEC.md:15-21 — датированный блок дополнения v1.2 в шапке не перечисляет
  §2.3 (README-раздел, amended в том же коммите).
- n4 [ЗАКРЫТО] plan-hot-reload.md §5 list строки лога без warn «sound resource failed»
  (повторение m12); строка добавлена.

## Раунд закрытия 1.2.1 (рецензент HEAD `12bae2b`)

VERDICT: ROUND OK, majors нет. Коммиты `2b7fe5c` + `ec05103` (1.2.1); рецензент перепрогнал
clean build 0 warnings, ctest 210/210, pe_res_check PASSED на 1.2.1, smoke rc=0, живая
проверка win_text refactor (tooltip/menu/reload через to_wide, warn'ов нет).

- n5 [ЗАКРЫТО acafa5a] SPEC §3 дерево не перечисляло src/win_text.h — добавлено датированно.
- n6 [ЗАКРЫТО acafa5a] длинные строки reload_tests.cpp:272 (121) / session_tests.cpp:469 (122)
  — перенесены в бюджет ~112.
- n7 [ЗАКРЫТО acafa5a] win_text.h wording — упомянуты обе reverse-копии (main.cpp, game_process.cpp).
- n8 [ЗАКРЫТО acafa5a] WIN32_LEAN_AND_MEAN в win_text.h под #ifndef-защитой.

Закрытие: коммит `acafa5a` + релиз `9a148e3` (1.2.1 -> 1.2.2, version.sh minor), полный
./build.sh зелёный: ctest 210/210, strip --version prints 1.2.2, pe_res_check PASSED 1.2.2,
smoke rc=0, zip dist/POE2FlaskHelper-1.2.2-win64.zip.

Раунд закрытия 1.2.0-миноров: коммит `2b7fe5c` + релиз `ec05103` (1.2.0 -> 1.2.1), полный
./build.sh зелёный: ctest 210/210, strip --version prints 1.2.1, pe_res_check PASSED 1.2.1,
smoke rc=0, zip dist/POE2FlaskHelper-1.2.1-win64.zip.

## Раунд закрытия 1.2.2 (рецензент HEAD `092a33d`)

VERDICT: ROUND OK, majors нет. Коммиты `acafa5a` + `9a148e3` (1.2.2); рецензент перепрогнал
полный ./build.sh: 0 warnings, ctest 210/210, pe_res_check PASSED 1.2.2, smoke rc=0, живая
reload-проверка volume 25->60 (info-строка, warn'ов 0), дерево/wording/бюджет строк подтверждены.

- n9 [ЗАКРЫТО] `src/win_text.h:5-6` — wording «command line/version path» не привязан к uses
  (to_utf8 вызывается только на GetModuleFileNameW -> Paths) — переформулировано.
- n10 [ЗАКРЫТО] `SPEC.md:23` — §3/win_text.h был приписан к датированному блоку «выпуск 1.2.0»,
  а landed в 1.2.1/1.2.2 — вынесено в отдельную датированную строку «minors закрытия, выпуски 1.2.1/1.2.2».
- n11 [ЗАКРЫТО] `minor-issues.md:79-81` — при переписывании записи закрытия 1.2.1 выпали
  «strip --version prints 1.2.1» и zip 1.2.1 — восстановлены отдельной строкой.
- n12 [ЗАКРЫТО] `minor-issues.md:83-87` — бэктики вокруг хешей в новых статусах, стиль
  рассинхронизирован с прежними записями — унифицировано.

Info (не finding, в бюджет): бюджет ~112 выполняется для src/tests (max 111); преэксистинг
res/app.rc max 119, res/app.manifest.in max 121 — раундом не тронуты.

Закрытие: коммит 705250a (docs+src) + релиз d6e9284 (1.2.2 -> 1.2.3, version.sh minor),
полный ./build.sh зелёный: ctest 210/210, strip --version prints 1.2.3, pe_res_check
PASSED 1.2.3, smoke rc=0, zip dist/POE2FlaskHelper-1.2.3-win64.zip.
## ИТОГО ОТКРЫТО: нет
