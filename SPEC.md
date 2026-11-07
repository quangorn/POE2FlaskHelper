# POE2FlaskHelper — спецификация v1.0

Дата: 2026-11-07. Статус: MVP зафиксирован, реализация не начата.

## 1. Назначение и границы

Трей-приложение для Windows x64: читает HP/Mana/ES/Ward/Rage из памяти
`PathOfExile.exe` и при падении процента ниже порога отправляет игре клавишу зелья —
так, как будто игрок нажал её сам.

Входит в MVP:

- иконка в трее, автозапуск в свёрнутом виде (без окон);
- один глобальный хоткей — включение/выключение модуля сканирования;
- звуковое подтверждение включения и выключения (WAV из ресурсов);
- чтение Vital'ов по полному раскладу `offsets.json`;
- активация зелий с порогом и кулдауном на каждый Vital;
- JSON-конфиг и лог рядом с исполняемым файлом;
- сборка `build.sh` из cygwin и portable zip.

Не входит в MVP: любые окна (включая placeholder), редактор конфига, оверлей,
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
| Библиотеки | `user32 kernel32 shell32 psapi gdi32 winmm` |
| Ресурсы | `app.rc`: иконка, VERSIONINFO, манифест (per-monitor DPI v2), `enable.wav`, `disable.wav` |

`advapi32` не используется: реестр, Event Log, права доступа и криптография в MVP не нужны.

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
4. Strip: `strip --strip-all` по exe, затем проверка целостности PE и запуск
   `POE2FlaskHelper.exe --version` (код возврата 0). Если strip недоступен или
   проверка не прошла — предупреждение и сборка без strip.
5. Упаковка: `dist/POE2FlaskHelper-<version>-win64/` = `POE2FlaskHelper.exe`,
   `config.json`, `offsets.json`, `README.txt`; zip-архив
   `dist/POE2FlaskHelper-<version>-win64.zip`. Только zip, 7z не используется.

Версия берётся из `project(... VERSION ...)` и попадает в VERSIONINFO и имя архива
(через сгенерированный `build/version.txt`).

### 2.3 `README.txt` в архиве

Один файл, двуязычный: сначала English, затем Russian. Содержание: запуск, статус
в трее, где лежат `config.json`, `offsets.json` и лог, что делать при
`Offsets mismatch`, требование VC++ v14 x64 Redistributable.

## 3. Структура проекта

```
build.sh  CMakeLists.txt  README.md  SPEC.md  backlog.md
src/main.cpp        WinMain, single instance, message loop, --version
src/app.{h,cpp}     состояние приложения, связка модулей
src/config.{h,cpp}  загрузка, валидация, значения по умолчанию
src/json.{h,cpp}    минимальный JSON: разбор и сериализация
src/log.{h,cpp}     потокобезопасный файловый лог, ротация
src/tray.{h,cpp}    иконка, контекстное меню из двух пунктов
src/hotkeys.{h,cpp} RegisterHotKey, разбор строк вида "Ctrl+9"
src/scanner.{h,cpp} рабочий поток: чтение памяти и отправка клавиш
src/keys.{h,cpp}    SendInput
src/process.{h,cpp} поиск PID, ImageBase, SizeOfImage, foreground check
src/sound.{h,cpp}   PlaySound из ресурсов
res/app.ico res/app.rc res/enable.wav res/disable.wav
config.json offsets.json
```

Внешние ассеты, которые нужно предоставить: `res/enable.wav`, `res/disable.wav`
(короткие, < 1 с) и `res/app.ico`. До их появления используются заглушки.

## 4. Поведение

### 4.1 Запуск и завершение

- Named mutex `Local\POE2FlaskHelper.SingleInstance`: второй экземпляр показывает
  сообщение и выходит.
- Все пути строятся от каталога exe (`GetModuleFileNameW`); текущая рабочая
  директория игнорируется — важно при запуске из cygwin.
- Нет `config.json` — создаётся файл со значениями по умолчанию.
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

### 4.3 Трей

- Иконка и тултип: `Scanner off` / `Waiting for game` / `Attached (PID n)`.
- ПКМ — ровно два пункта: `Enable scanner` / `Disable scanner` (галочка отражает
  состояние) и `Exit`.
- Двойной клик по иконке — без действия.

### 4.4 Хоткей

- Один глобальный: `hotkeys.toggleScanner`, по умолчанию `Ctrl+9`.
- `RegisterHotKey`; работает и когда фокус у игры.
- Валидация при загрузке: обязателен модификатор или F-клавиша.
- Хоткей занят — warning в лог и статус в тултипе, приложение продолжает работать.

### 4.5 Звук

- Включение сканера — `enable.wav`, выключение — `disable.wav`; срабатывает и на
  хоткей, и на пункт меню.
- `PlaySound(MAKEINTRESOURCE(...), hInst, SND_RESOURCE | SND_ASYNC | SND_NODEFAULT)`;
  UI не блокируется.
- Ошибка ресурса — warning, переключение происходит без звука.

### 4.6 Сканер

Состояния:

1. `ожидание процесса` — `EnumProcesses` + `QueryFullProcessImageNameW`, опрос 1–2 с;
2. `подключение` — `OpenProcess(VM_READ | QUERY_INFORMATION)`, PEB через
   `NtQueryInformationProcess`, `ImageBase` из `PEB+0x10`, `SizeOfImage` из
   PE-заголовка; скан сигнатуры по образу модуля чанками по 1 МБ; полная цепочка A–J;
3. `чтение` — `tick()` каждые `pollIntervalMs`, около 10 чтений на тик;
4. разрыв цепочки или смерть процесса — назад в `ожидание процесса`; после рестарта
   игры сигнатура сканируется заново.

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
  и debug-запись в лог;
- пока значение остаётся ниже порога, повтор каждые `cooldownMs`.

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

Лежит рядом с exe. Читается один раз на старте; изменения применяются после
перезапуска.

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

Правила разбора:

- неизвестные ключи — warning в лог, игнорируются;
- отсутствующие — значения по умолчанию;
- невалидные (`threshold` вне 1–100, `cooldownMs < 0`, `pollIntervalMs` вне 10–1000,
  неизвестная клавиша, хоткей без модификатора) — warning и значение по умолчанию;
- битый JSON — error в лог, MessageBox, работа на значениях по умолчанию;
- ключи Vital'ов пишутся одиночными клавишами без модификаторов (буквы, цифры,
  F1–F24, NumPad).

## 6. `offsets.json`

Лежит рядом с `config.json`, обязателен. Содержит полный расклад, чтобы в exe не
было зашитых смещений. Имена и семантика совместимы с `poe_scan.py --offs-file`:

```json
{
  "CurrentStatePtr": 16, "States": 80, "TOTAL": 13, "AreaInstanceData": 656,
  "LocalPlayers": 1472, "EntityId": 136, "EntityIsValid": 140,
  "EntityDetailsName": 8, "EntityDetailsLookup": 40, "CLU_Bucket": 40,
  "TotalOff": 52, "CurrentOff": 56, "ReservedOff": 28, "ReservedFlat": 24,
  "HealthOff": 476, "ManaOff": 564, "ESOff": 628, "WardOff": 788, "RageOff": 716,
  "BackPtrOff": 16, "Score": 9
}
```

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
  (не чаще раза в 5 с), fatal-ошибки.
- **debug**: значения Vital'ов на каждом тике, каждое нажатие
  (`Health 23% < 50%, key=1`), тайминги тика, шаги цепочки.
- **warn/error**: конфиг, ресурсы, `OpenProcess`, несовпадение смещений.

При уровне `info` одиночные изменения значений и нажатия клавиш не пишутся.

## 8. Обработка ошибок

| Ситуация | Поведение |
|---|---|
| `OpenProcess` → `ERROR_ACCESS_DENIED` | error в лог, тултип `Admin rights required` |
| Игра не запущена / меню / не залогинен | штатный статус, опрос продолжается |
| Смещения не подходят | статус `Offsets mismatch`, подсказка про `find_offsets.py` |
| Каталог exe только для чтения | сообщение и выход |
| Отсутствующий/битый WAV | warning, работа без звука |
| Нет/битый `offsets.json` | error + MessageBox + выход с кодом 2 |

## 9. Критерии приёмки MVP

1. `./build.sh` из cygwin собирает x64 Release без предупреждений `/W4`, делает
   strip и создаёт `dist/POE2FlaskHelper-<version>-win64.zip` с exe, `config.json`,
   `offsets.json`, `README.txt`.
2. Запуск из распакованного архива; требуется только VC++ v14 x64 Redistributable.
3. Старт в трее без окон; второй экземпляр не стартует.
4. ПКМ по трею: ровно `Enable/Disable scanner` (с галочкой) и `Exit`.
5. `Ctrl+9` переключает сканер при фокусе на игре; включение и выключение
   сопровождаются соответствующим WAV.
6. Нет `offsets.json` → error в лог, MessageBox, выход с кодом 2; чтений памяти нет.
7. При включённом сканере — attach и значения; при выключенном — ни одного
   `ReadProcessMemory`.
8. Vital ниже порога → клавиша в игре не чаще кулдауна; при `info` нажатий и
   значений в логе нет, при `debug` есть.
9. Игра не в фокусе — клавиши не отправляются.
10. `config.json`, `offsets.json`, `logs/` — рядом с exe независимо от cwd.
11. `README.txt` двуязычный (English + Russian) в одном файле.
