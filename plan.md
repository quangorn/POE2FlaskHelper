# POE2FlaskHelper — план реализации MVP

Опирается на [`SPEC.md`](SPEC.md). Здесь только порядок работ, тестовая стратегия и
критерии готовности; описание поведения и конфигов — в спеке.

## 0. Порядок этапов

```
0 Каркас сборки + тестовый каркас
      └─► 1 Данные и логирование
              └─► 2 Трей, хоткей, звук
                      └─► 3 Сканер
                              └─► 4 Активация зелий
                                      └─► 5 Упаковка и приёмка
```

Каждый этап заканчивается работающим exe и отдельным коммитом.

## 1. Тестируемость: архитектурные требования

Логика отделена от ОС и потоков: чистые функции/классы принимают интерфейсы. В exe
подставляются реальные реализации, в тестах — фейковые.

```cpp
// src/mem/reader.h
struct IMemoryReader {
    virtual ~IMemoryReader() = default;
    virtual size_t read(uint64_t addr, void* buf, size_t n) = 0; // 0 = ошибка
    virtual uint64_t image_base() const = 0;
    virtual uint32_t image_size() const = 0;
};
struct IMemoryReaderFactory { virtual std::unique_ptr<IMemoryReader> open(uint32_t pid) = 0; };

// src/process.h
struct IProcessProvider {
    virtual std::vector<ProcessInfo> find(const std::vector<std::string>& names) = 0;
    virtual bool is_foreground(uint32_t pid) = 0;
    virtual bool alive(uint32_t pid) = 0;
};

// src/clock.h
struct IClock { virtual int64_t now_ms() = 0; };

// src/keys.h
struct IKeySender { virtual void press(uint16_t vk, bool extended) = 0; };
```

Тестируемые модули (без Win32-процессов и без реального времени):

- `signature.cpp` — `scan_signature(IMemoryReader&, base, size)`;
- `chain.cpp` — цепочка A–J и `tick()` на `IMemoryReader&` + `Layout`;
- `vitals.cpp` — `evaluate(samples, config, now_ms, state) -> actions`
  (порог, резервы, кулдаун);
- `hotkey_parse.cpp`, `keymap.cpp` — строки → VK/модификаторы/scan code;
- `scanner_session.cpp` — state machine с `step()`; поток только крутит `step()` и
  ждёт событие/таймер, тесты вызывают `step()` напрямую.

Сборка: общая статическая библиотека `poe2_core` с этой логикой; exe и тесты линкуют
один и тот же код.

## 2. Тестовая инфраструктура

- `tests/test.h` — свой мини-фреймворк (~100 строк): `CHECK`, `CHECK_EQ`, `REQUIRE`,
  регистрация тестов, отчёт, код возврата. GoogleTest/Catch2 не используем: спека
  запрещает лишние зависимости.
- `tests/fakes/`:
  - `FakeMemory` — регионы по адресам, короткие чтения на границе региона, счётчик
    чтений, инъекция отказа на N-м чтении;
  - `FakeClock` — `advance(ms)`;
  - `FakeProcess` — сценарии «игры нет / появилась / умерла / не в фокусе»;
  - `RecordingKeySender` — записывает `(vk, extended, время)`.
- `tests/synthetic_layout.cpp` — синтетический образ в духе
  `find_offsets.build_fake`: сигнатура по RVA, `GameStates`, `States[13]`,
  `AreaInstance`, `LocalPlayers`, Entity, ComponentLookUp с `"Life"`, Life-компонент
  и все пять Vital'ов. Без POE и без файла бинарника.
- CMake: `enable_testing()`, консольный `poe2_tests`,
  `add_test(NAME unit COMMAND poe2_tests)`.
- `build.sh`: после сборки `ctest --test-dir build --output-on-failure`; красные тесты
  останавливают сборку до упаковки.
- В dist тестовый exe не попадает; отдельный `--selftest` в боевом exe не нужен.

## 3. Этапы

### Этап 0. Каркас сборки + тестовый каркас

**Цель:** воспроизводимая сборка из cygwin и пустой запускаемый exe.

Работы:

- `CMakeLists.txt`: C++20, x64, `WIN32`, `/MD`, флаги и линковка из SPEC §2,
  `build/version.txt`, `app.rc`, цели `poe2_core`, exe, `poe2_tests`, `ctest`.
- `build.sh`: `vswhere` → `vcvars64.bat` → VS-cmake/ninja → Release → `strip
  --strip-all` → проверка `--version` → `ctest` → `dist/...zip` (SPEC §2.2).
- `src/main.cpp`: `WinMain`, single-instance mutex, `--version` с `AttachConsole`,
  выход; без окна и трея.
- `res/`: заглушки `app.ico`, `enable.wav`, `disable.wav`, `app.rc` (иконка,
  VERSIONINFO, манифест DPI).
- `tests/test.h`, `tests/test_main.cpp`, один тривиальный тест.

**Тесты:** самопроверка фреймворка (проходит/падает, код возврата), `ctest` видит
тест.

**Проверка:** `./build.sh` зелёный без предупреждений `/W4`; `ctest` зелёный; zip
содержит exe + `README.txt`; `POE2FlaskHelper.exe --version` → код 0;
`dumpbin /dependents` — только system DLL + VC runtime.

### Этап 1. Данные и логирование

**Цель:** конфиги читаются и валидируются, логи пишутся, смертельные ошибки
отрабатывают.

Работы:

- `src/json.cpp`: парсер/сериализатор (объекты, массивы, UTF-8, числа, bool, null;
  ошибка с позицией).
- `src/log.cpp`: асинхронный лог — очередь + отдельный поток записи/ротации; уровни
  `debug/info/warn/error`; flush и join при выходе. Главный поток не делает файловый
  I/O (SPEC §4.2).
- `src/config.cpp`: схема, defaults, строгая валидация, warning'и, создание
  `config.json` при первом запуске (SPEC §5).
- `src/offsets.cpp`: обязательный полный набор ключей, `Off − TotalOff`,
  `ReservedOff → ReservedPercent`; отсутствие/битый файл → лог + MessageBox + код 2
  (SPEC §6).
- Пути от каталога exe (`app.cpp` / `paths`).
- Расширенный `offsets.json` для rev34 (константы цепочки + Vital'ы), совместимый с
  `poe_scan.py --offs-file`.

**Тесты:**

- JSON: примитивы, вложенность, UTF-8 и escape, целые/дробные, битый ввод с позицией,
  round-trip serialize, неизвестные поля.
- Config: defaults, отсутствующие/лишние ключи, невалидные значения → default,
  границы `threshold`/`cooldownMs`/`pollIntervalMs`.
- Hotkey-парсер: `Ctrl+9`, `Shift+Alt+F5`, регистр, мусор, хоткей без модификатора
  (для toggle — ошибка, для Vital — норма).
- Offsets: полный набор, `Off − TotalOff`, `ReservedOff → ReservedPercent`,
  отсутствующий/нечисловой ключ → ошибка, лишние ключи игнорируются, совместимость с
  реальным `offsets.json`.
- Log: фильтр уровней, формат строки, ротация имён на временном каталоге, flush при
  остановке.

**Проверка:** нет конфига → создаётся дефолтный; битый JSON → defaults + сообщение;
нет `offsets.json` → exit 2 и запись в лог; невалидные пороги/хоткеи → warning +
default.

### Этап 2. Трей, хоткей, звук

**Цель:** приложение живёт в трее и управляется без окон.

Работы:

- скрытое top-level окно + message loop (для `WM_HOTKEY` и колбэков трея);
- `src/tray.cpp`: `Shell_NotifyIcon`, тултип со статусом, ПКМ-меню из двух пунктов с
  галочкой, `NIM_DELETE` при выходе (SPEC §4.3);
- `src/hotkeys.cpp`: разбор `"Ctrl+9"` → модификаторы+VK, `RegisterHotKey`, обработка
  занятой комбинации, валидация «модификатор или F-клавиша» (SPEC §4.4);
- `src/sound.cpp`: `PlaySound` из ресурсов, async, два WAV (SPEC §4.5);
- выход: остановка потока сканера (заглушка), закрытие лога, удаление иконки.

**Тесты:**

- парсер хоткея и таблица «строка → VK/modifiers»;
- чистая функция состояния меню: `(scanner_enabled) → label + checked`;
- маппинг «состояние → WAV resource id»;
- регрессия: тесты этапа 1 остаются зелёными.

**Проверка (ручной чеклист):** иконка и тултип; меню из двух пунктов; галочка
меняется; `Ctrl+9` работает при фокусе на игре; играют оба WAV; второй экземпляр не
стартует; `Exit` завершает процесс без «висящих» иконок.

### Этап 3. Сканер

**Цель:** чтение HP/Mana/ES/Ward/Rage из живого процесса.

Работы:

- `src/process.cpp`: `EnumProcesses` + `QueryFullProcessImageNameW`,
  `OpenProcess(VM_READ|QUERY_INFORMATION)`, PEB, `ImageBase`, `SizeOfImage`,
  foreground по PID.
- `src/mem/`: `IMemoryReader` + реализация на `ReadProcessMemory`.
- `src/scanner_session.cpp`: состояния `ожидание → подключение → чтение`, управление
  atomic + событие с таймаутом (без busy-wait), join при выходе.
- `src/scanner.cpp`: поток-обёртка над сессией.
- `src/signature.cpp`, `src/chain.cpp`: скан чанками по 1 МБ, `global_addr`, цепочка
  A–J, кэши `global_addr` / фактический `LocalPlayers` (±0x40) / `life_index`
  (SPEC §4.6).
- `tick()`: ~10 чтений, проверки `IsValid == 0xC`, границ векторов, `capacity <= 50`.
- Переподключение после рестарта игры; при выключенном модуле — ни одного
  `ReadProcessMemory`.
- Логи: attach/detach, `Offsets mismatch`, потеря цепочки не чаще раза в 5 с;
  значения — только `debug`.

**Тесты:**

- `scan_signature` на синтетическом образе: ровно одно совпадение, корректный
  `global_addr`, маска `??`, совпадение на границе чанка, несколько совпадений,
  пустой модуль.
- `chain`: happy path (Life + index) и каждая ветка отказа — `GameState=0`, короткое
  чтение, `LocalPlayers` сдвинут на ±8 (окно ±0x40), `IsValid != 0xC`, пустой
  comp-вектор, нет `"Life"` в CLU, `capacity > 50`, `life_index` за границей.
- `tick`: значения всех Vital'ов, кэши `global_addr`/`off_lp`/`life_index`, перескан
  при невалидном кэше, короткие чтения, «процесс закрыт».
- state machine через `ScannerSession::step()` + `FakeProcess` + `FakeClock`:
  ожидание → attach → чтение → разрыв → ожидание, рестарт игры, scanner off →
  счётчик чтений `FakeMemory` не растёт.
- формула процента: flat+percent+ceil, `unreserved <= 0`, границы округления,
  сравнение «ровно порог» vs «на единицу ниже».

**Проверка (live, ручная):** значения совпадают с
`python scanner/poe_scan.py --offs-file offsets.json --values`; attach/detach по
`Ctrl+9`; рестарт игры → перескан; выход из игры → `Waiting for game`; при
выключенном сканере в Process Monitor нет чтения памяти.

### Этап 4. Активация зелий

**Цель:** клавиши уходят в игру по порогу и кулдауну.

Работы:

- `src/keys.cpp`: `SendInput`, VK + scan code (`MapVirtualKey`), extended-флаги,
  keydown/keyup (SPEC §4.8).
- `src/vitals.cpp`: формула с резервами (`ceil`, `unreserved`), сравнение
  `current*100 < threshold*unreserved` без округления, кулдаун от последней отправки
  (SPEC §4.7).
- Условие foreground-окна при `requireGameForeground`.
- Debug-лог нажатия: `Health 23% < 50%, key=1`.
- Отправка только при включённом модуле и валидных значениях.

**Тесты** (`evaluate()` + `FakeClock` + `RecordingKeySender`):

- срабатывание ниже порога, отсутствие при равенстве порогу;
- кулдаун: повтор ровно через `cooldownMs`, не раньше, независимые таймеры у разных
  Vital'ов;
- `enabled: false` игнорируется;
- несколько Vital'ов ниже порога на одном тике;
- foreground=false → ни одного нажатия; игра пропала → нажатий нет;
- `keymap`: буквы/цифры/F1–F24/NumPad, extended-флаги, запрет модификаторов у
  клавиш Vital'ов;
- нажатие не дублируется без нового тика.

**Проверка (live, ручная):** в игре клавиша срабатывает ниже порога; повтор не чаще
`cooldownMs`; после поднятия значения — тишина; сворачивание игры / фокус в другом
окне — отправок нет; Mana с резервом (процент от unreserved) и Ward/Rage при
включении; на `info` нажатий в логе нет.

### Этап 5. Упаковка и приёмка

**Цель:** пройти критерии приёмки SPEC §9 целиком.

Работы:

- финальные `app.ico`, `enable.wav`, `disable.wav` (ассеты предоставляет
  пользователь; до этого — заглушки).
- `README.txt`: English + Russian в одном файле (запуск, трей, конфиги/лог,
  `Offsets mismatch`, VC++ Redist).
- `dist/`: дефолтный `config.json` (сканер выключен) + полный `offsets.json`.
- чистая сборка из свежего клона под cygwin, проверка strip (`objdump -h`,
  отсутствие символов), состава архива и зависимостей.
- прогон чеклиста 1–11 из SPEC §9.

**Тесты:** полный `ctest` на чистой сборке; проверка, что тестов и PDB в zip нет.

**Проверка:** распакованный архив запускается на машине только с VC++ v14 x64; все
11 пунктов выполнены.

## 4. Сквозные правила

- Definition of Done этапа:
  1. `/W4` без предупреждений;
  2. все unit-тесты этапа зелёные в `ctest`, тесты детерминированные (без `sleep`,
     сети, POE, зависимости от времени суток);
  3. новые ветки логики, включая ошибки, покрыты тестами;
  4. ручной чеклист этапа пройден;
  5. `build.sh` собирает пакет только при зелёных тестах.
- Один этап — один коммит (сообщения в стиле репозитория).
- Сканер никогда не вызывает UI и GDI; UI никогда не читает память и не пишет файлы
  (лог асинхронный).
- Конфиг и offsets — только на старте; горячая перезагрузка появится вместе с окном
  настройки (backlog).
- Живые проверки с игрой остаются ручными и в CI не входят: unit-тесты гоняют только
  `FakeMemory`/`FakeProcess`/`FakeClock`.

## 5. Риски

1. **UIPI/права:** если игра запущена от администратора, `SendInput` и `OpenProcess`
   из обычного процесса не сработают — нужен запуск хелпера с теми же правами.
   Отработать в этапах 3–4, в README указать.
2. **strip:** cygwin-бинutils на MSVC-PE может испортить файл — проверка `--version`
   после strip, иначе сборка без strip с предупреждением.
3. **Полный `offsets.json`:** собрать константы цепочки rev34, не сломав совместимость
   с `poe_scan.py --offs-file`.
4. **Ассеты:** WAV и иконка нужны к этапу 5; до этого заглушки, чтобы не блокировать
   разработку.
5. **Mock vs реальность:** синтетика может разойтись с живым процессом — компенсируем
   ручным live-чеклистом на каждом этапе.

## 6. Что потребует правки SPEC (v1.1, при реализации)

- §3 — новые файлы `src/mem/`, `src/clock.h`, `src/scanner_session.*`, `tests/`,
  разделение `poe2_core`/exe;
- новый раздел «Тестируемость» с перечнем интерфейсов и правилом «ядро не знает про
  Win32-процессы и реальное время»;
- §9 — критерий «`ctest` зелёный, сборка без тестов невозможна».
