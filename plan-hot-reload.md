# POE2FlaskHelper — план: hot-reload `config.json` (5 с цикл) + звук релоада

Дополнение к [`plan.md`](plan.md) (правило §4 «Конфиг — только на старте» этим планом
амендится). Опись поведения — [`SPEC.md`](SPEC.md), список отложенного — [`backlog.md`](backlog.md).
План от 2026-11-08. Реализация: версия **1.2.0** (`./version.sh major`, SPEC §2.4).

## 0. Механика (желаемое поведение)

- При старте приложение читает `config.json` и запоминает штамп файла: `LAST_WRITE` time + размер.
- Каждые 5 с проверяет, изменился ли штамп. Если изменился — перечитывает файл и валидирует.
- Битый JSON — error в лог, **без MessageBox**, приложение работает на текущем конфиге; 5 с цикл
  сохраняется (файл может починиться позже).
- Валидный JSON — применяется на лету, играется `reload.wav`. На старте звук не играется.
- Применяются только живые параметры (таблица §2); остальные — после рестарта, с warn/info.

Принятые решения (подтверждены пользователем 2026-11-08):
- живой список — **полный** (§2);
- ассет: `res/reload.wav` предоставлен (26540 B, PCM16 mono 44100 Hz, 0.300 с);
  `res/enable.wav` / `res/disable.wav` подрезаны (0.633 с / 0.387 с);
- звук — **только на успешный parse+apply** (битый JSON и заблокированный файл = тишина;
  пустой diff = успех, info-строка + звук).

## 1. Ядро (`poe2_core`, чистое, тестируется без Win32)

### `src/config.{h,cpp}`

- `struct FileStamp { uint64_t mtime; uint64_t size; }` — mtime в 100 ns тиках (`LAST_WRITE`);
- `bool stamp_changed(const FileStamp& prev, const FileStamp& cur)` — изменился, если mtime **или**
  size отличается (правка «в пределах одного тика» с тем же mtime ловится по размеру; сохранение без
  правок меняет mtime);
- `std::vector<std::string> config_diff(const Config& a, const Config& b)` — список путей:
  `sound.volume`, `vitals.Health.threshold`, `scanner.pollIntervalMs`, `hotkeys.toggleScanner`, …;
- `bool path_is_live(const std::string& path)` — таблица живых/после-рестарта путей (§2);
- `ReloadOutcome config_reload(const Config& current, const FileStamp& prev, const FileStamp& cur,
  const std::string& text, bool read_ok)`, все ветки:

```cpp
struct ReloadOutcome {
    bool attempt;        // штамп изменился и файл прочитан
    bool read_failed;    // штамп изменился, чтение не удалось (файл заблокирован)
    bool json_broken;    // битый JSON / корень не объект -> текущий конфиг сохраняется
    std::string error;   // строка разбора (позиция), для error-лога
    Config next;         // валидный разбор (defaults + warnings, правила SPEC §5)
    std::vector<std::string> live_changed;      // пути, применяемые сейчас
    std::vector<std::string> restart_changed;   // пути, требующие рестарта
    std::vector<std::string> warnings;          // правила SPEC §5 (unknown/invalid)
};
```

### Новый `src/config_snapshot.{h,cpp}` (в `poe2_core`)

`ConfigSnapshot` = `std::mutex` + `std::shared_ptr<const Config>`; методы
`set(std::shared_ptr<const Config>)`, `get() -> std::shared_ptr<const Config>`. Живёт внутри
`ScannerCtl` (существующий канал UI ↔ поток, `src/scanner_ctl.h`).

### `src/scanner_session.{h,cpp}`

- `Deps`: поля `ScannerCfg cfg` и `Config full` заменяются на `const ConfigSnapshot* live`;
- `step()` снимает снимок **один раз за шаг** (целостный снимок в пределах тика, гонок нет);
- `step_delay_ms()` — живой `scanner.pollIntervalMs`; `tick_step()` — живой
  `requireGameForeground` + секция `vitals` для `evaluate()`; `attach_step()`/`menu_step()` — живой
  `scanner.processNames`.

### `src/log.{h,cpp}`

- `void Logger::set_options(LogOptions)` под `mtx_`;
- `opts_` читается под мьютексом: в `log()` (`min_level`) и в `worker()` по каждому batch
  (`max_size_kb`, `keep_files`) — сейчас читается без lock.

## 2. Список параметров: живое применение

| Параметр | Живое | Механика применения |
|---|---|---|
| `vitals.*.enabled` / `key` / `threshold` / `cooldownMs` | ✅ | следующий тик `evaluate()` (≤ `pollIntervalMs`) |
| `scanner.pollIntervalMs` | ✅ | `step_delay_ms()` читает живой снимок |
| `scanner.requireGameForeground` | ✅ | следующий `tick_step()` |
| `scanner.processNames` | ✅ | следующий опрос `IProcessProvider::find()` |
| `sound.volume` | ✅ | `play_wav` масштабирует PCM16 на каждое проигрывание (шов SPEC §4.5) |
| `logging.level` / `maxSizeKb` / `keepFiles` | ✅ | `Logger::set_options()` |
| `hotkeys.toggleScanner` | ✅ | main-поток: `UnregisterHotKey` + `RegisterHotKey` |
| `scanner.enabledOnStart` | ❌ | старт-семантика: info «applies after restart» |
| `logging.file` | ❌ | перезапуск writer-потока = риск потерять reload-строки: warn «applies after restart» |
| `configVersion` | — | метаданные, не применяется |

## 3. App-слой

- Новый `src/file_stamp.{h,cpp}` (exe): `bool file_stamp(const std::string& path, FileStamp&)` —
  `CreateFileW(FILE_SHARE_READ | FILE_SHARE_WRITE)` + `GetFileTime(LAST_WRITE)` + `GetFileSize`;
  чтение текста — существующий `read_text_file`.
- `src/tray.{h,cpp}`: `constexpr UINT_PTR kConfigTimerId = 2`, `TrayHost::on_config_timer`
  (`WM_TIMER`), `KillTimer(hwnd_, kConfigTimerId)` при выходе.
- `src/res_ids.h`: `#define ID_RELOAD_WAV 104`; `res/app.rc`: строка
  `ID_RELOAD_WAV WAVE RELOAD_WAV_PATH`; `res/res_paths.rc.in`:
  `#define RELOAD_WAV_PATH "@CMAKE_CURRENT_SOURCE_DIR@/res/reload.wav"`.
- `src/state.{h,cpp}`: `constexpr uint16_t kReloadWavResId = ID_RELOAD_WAV` (+ тест маппинга).
- `CMakeLists.txt`: `src/config_snapshot.cpp` в `poe2_core`; `src/file_stamp.cpp` в exe;
  `tests/reload_tests.cpp` в `poe2_tests`.

### Handler 5 с таймера в `src/main.cpp`

1. `file_stamp(config.json)`: штамп не изменился → выход.
2. Штамп изменился, чтение не удалось → warn `config.json is locked by another process, retrying
   next cycle`; штамп **не** обновляем (следующая проверка повторит).
3. `config_reload(live_cfg, prev_stamp, cur_stamp, text, read_ok)`:
   - `json_broken` → **error**-строка `config.json is broken (...) - current config kept`, MessageBox
     нет, штамп обновляем (та же битая версия логируется один раз), 5 с цикл сохраняется;
   - валидный → порядок: `live.set()` → `logger.set_options(log_options(next))` (reload-строки
     пишутся уже при новом уровне) → перерегистрация хоткея, если менялся `toggleScanner` → info-строка
     изменений → warn для restart-only полей → `play_wav(kReloadWavResId, hInst, volume, logger)`;
   - пустой diff → info `config.json reloaded (no changes)` + звук.
4. Хоткей: `UnregisterHotKey` → `RegisterHotKey(new)`. Занято → warn и возврат старой комбинации
   (`status.hotkey_busy = true`, `hotkey_text` = старый, tooltip refresh). Успех → info
   `hotkey <x> re-registered`, `status.hotkey_text` = новый, tooltip refresh.
5. Строки лога (формы зафиксированы дословно в SPEC §7):
   - info: `config.json reloaded (<live_changed>)` — пути в **canonical-порядке таблицы
     ядра** (`config_diff`: `sound.volume` идёт до `vitals.*`; план §2 задаёт набор
     путей); пример `config.json reloaded (sound.volume, vitals.Health.threshold)`
     — уже в canonical-порядке, показывает ФОРМУ строки;
   - info при пустом diff: `config.json reloaded (no changes)`;
   - warn: parse-warning'и по правилам SPEC §5 (`unknown key ...`, `invalid ... default used`);
   - warn: `logging.file changed - applies after restart`, `scanner.enabledOnStart changed -
     applies after restart`;
   - error: `config.json is broken (line 3, column 5: ...), current config kept`;
   - warn: `config.json is locked by another process, retrying next cycle`;
   - warn: `hotkey Ctrl+8 is taken by another process, kept Ctrl+9`;
   - warn: `sound resource failed (PlaySound), reload applied without sound`
     (шов SPEC §4.5 на reload-пути, SPEC §7).
6. `toggle_scanner(...)` берёт громкость из живого снимка: `live.get().sound.volume` (шов SPEC §4.5);
   `SetTimer(tray.hwnd(), kConfigTimerId, 5000, nullptr)` сразу после `tray.create()`; первая
   проверка — через 5 с после старта.
7. First-run: штамп берётся после записи дефолтного `config.json`.

Шов SPEC §4.2: «никакого файлового I/O на главном потоке» во время message loop — нужна датированная
поправка по прецеденту BR-7: 5 с проверка читает ~1 КБ (`GetFileTime` + `ReadFile`), UI не блокируется.

## 4. Тесты (`ctest`, детерминированные: без POE / sleep / реального времени)

- `stamp_changed`: одинаковый штамп / только mtime / только size / оба;
- `config_diff`: пустой diff, порядок путей; `path_is_live`: живые vs restart-only;
- `config_reload`: unchanged / `read_failed` / `json_broken` (текущий конфиг) / warning'и / success,
  включая последовательность «битый → следующий валидный»;
- `ConfigSnapshot`: set/get, изоляция снимка, несколько читателей видят один целостный снимок;
- живое применение через `ScannerSession::step()` (`FakeMemory`/`FakeProcess`/`FakeClock`):
  - порог → press-строка с новым порогом;
  - `key` → новый VK у `RecordingKeySender`;
  - `pollIntervalMs` → `step_delay_ms()`;
  - `processNames` → attach по новому имени;
  - `requireGameForeground: false` → отправок нет;
- `Logger::set_options` — смена уровня и ротации в середине работы;
- reload-WAV id в тесте маппинга (`state_tests.cpp`).

Новые/правки файлов тестов: `tests/reload_tests.cpp` (новый TU), `tests/config_tests.cpp`,
`tests/state_tests.cpp`, `tests/log_tests.cpp`, `tests/session_fixture.h`, `tests/session_tests.cpp`,
`tests/vital_session_tests.cpp`.

## 5. Документация

- **SPEC.md**: §5 — переписать «Читается один раз на старте» (hot-reload правила, живой список,
  поведение при битом JSON в runtime без MessageBox); §4.2 — поправка про 5 с I/O на main-потоке;
  §4.5 — новый WAV + звук при релоаде и не на старте; §7 — reload-строки (info/warn/error);
  §8 — строка «битый `config.json` в runtime»; §9 — критерий приёмки hot-reload; §3 — новые файлы
  (`config_snapshot.*`, `file_stamp.*`, `reload_tests.cpp`) и длительности ассетов
  (0.300 / 0.633 / 0.387 с).
- **plan.md** §4: правило «Конфиг и offsets — только на старте» → amendment (config hot-reload
  сделан, `offsets.json` остаётся старт-only, его перезагрузка живёт в backlog окна настройки).
- **backlog.md**: hot-reload вынести из backlog; окно настройки остаётся отложенным.
- **res/README.txt.in**: раздел о правке `config.json` на лету (English + Russian).
- **CHANGELOG.md** + версия: `./version.sh major "config.json hot reload + reload.wav"` → 1.2.0
  (код менялся, гейт §2.4 проходит).

## 6. Ручной чеклист (live)

1. Правка `vitals.Health.threshold` → ≤5 с info-строка изменений, играет `reload.wav`, срабатывание
   по новому порогу.
2. Битый JSON (убрать скобку/запятую) → error в логе, MessageBox нет, сканер живёт на текущем
   конфиге; починка → применение + звук; 5 с цикл не ломается; повторной строки про ту же битую
   версию нет.
3. `hotkeys.toggleScanner` → новый хоткей переключает сканер, tooltip обновлён; занятая комбинация →
   warn, старая сохранена.
4. `sound.volume: 30` → следующие проигрывания тише, включая сам звук релоада.
5. `logging.level: "debug"` → `tick:`-строки видны; `maxSizeKb: 1` → ротация на следующей записи.
6. `logging.file` / `scanner.enabledOnStart` → warn/info «applies after restart», применяется после
   рестарта.
7. Старт приложения — звука нет; `reload.wav` играется только на релоад.
8. Файл заблокирован редактором (exclusive lock) → warn один раз за цикл, попытка на следующем цикле.
9. Открытое popup-меню (`TrackPopupMenu` modal loop) может задержать таймер-проверку — известный
   пункт, фиксируется в чеклисте.
10. `./build.sh` зелёный: `/W4` без предупреждений, `ctest` зелёный, strip-целостность,
    `pe_res_check.py` (новый WAVE-ресурс в дереве), zip собран, smoke-запуск + `WM_CLOSE` выход rc 0.

## 7. Границы плана

- `offsets.json` hot-reload не входит (остаётся в backlog окна настройки);
- окно настройки конфига через UI не делается;
- UI-слой остаётся на главном потоке, сканер — на рабочем потоке (SPEC §4.2);
- системная громкость не трогается; WAV играется через `PlaySound` из ресурсов exe.
