# Сканер памяти PathOfExile.exe

Работа с **живым процессом** игры: чтение HP/Mana/EnergyShield/Ward/Rage по цепочке смещений
(`poe_scan.py`) и поиск самих смещений по известным значениям (`find_offsets.py`).
Запись в память процесса отсутствует — только чтение.

Разбор цепочки адресации, кода ауры POE2-Mem и сравнение ревизий — в
[`../research/REPORT.md`](../research/REPORT.md). Здесь — только запуск и опции.

## Требования

Запускать **Windows-нативным** Python 3.x 64-bit, например
`C:\Users\<user>\AppData\Local\Programs\Python\Python3.12\python.exe`.
cygwin-python не подходит: у него нет `ctypes.WinDLL`
(скрипты сами перезапускаются на нативный интерпретатор). `pymem` не требуется.

Для live-прогона нужен запущенный клиент игры и персонаж в локации.

## `poe_scan.py` — чтение значений по цепочке

```
python poe_scan.py                              # найти PathOfExile*.exe → скан сигнатуры → цепочка → HP/MP/ES/Ward/Rage
python poe_scan.py --file                       # офлайн: сигнатура в файле бинарника (1 совпадение → VA 0x45EE348)
python poe_scan.py --rev rev32                  # смещения другой ревизии
python poe_scan.py --selftest                   # цепочка на синтетике, без процесса игры
python poe_scan.py --offs-file offsets.json --values   # только HP/Mana/ES, без трассировки
python poe_scan.py --pid 13996 \
    --offs HealthOff=0x1DC,TotalOff=0x34,CurrentOff=0x38,ManaOff=0x234,ESOff=0x274
```

Цепочка: `OpenProcess(VM_READ | QUERY_INFORMATION)` → `NtQueryInformationProcess` (PEB) →
`ImageBase` из PEB и `SizeOfImage` из PE-заголовка → скан образа regex'ом чанками по 1 МБ →
RIP-расчёт якоря → шаги (A)–(J): StdVector `{First,Last,End}`, bucket, `std::wstring` (SSO),
поиск `"Life"` по именам из ComponentLookUp, чтение Vital'ов, процент по той же формуле.

Опции:

| Опция | Значение |
|---|---|
| `--file` | скан сигнатуры по файлу бинарника (`EXE_PATH`), без процесса |
| `--rev NAME` | смещения ревизии (`rev34` по умолчанию — текущий патч, проверен живым прогоном; есть `rev33`, `rev32`, `rev2025-10-11`; rev34: LocalPlayers +8 и Vital'ы `Ward @+0x2E0`, `Rage @+0x298`) |
| `--offs K=0x..,…` | смещения подаются вручную: `HealthOff`, `TotalOff`, `CurrentOff`, `ManaOff`, `ESOff`, `WardOff`, `RageOff`, `BackPtrOff`, `ReservedOff`, `LocalPlayers` |
| `--offs-file FILE` | то же из JSON (те же ключи, что в `--offs`) |
| `--pid N` | PID процесса (иначе поиск по имени) |
| `--values` | подавляет трассировку шагов (A)–(J), печатает Vital'ы (HP/Mana/ES/Ward/Rage) |
| `--selftest` | прогон цепочки на синтетике `find_offsets.build_fake`, офлайн |

Скорости (замерены): одно `ReadProcessMemory` (4 байта) ~3 мкс; скан сигнатуры по 78 МБ образа 0.08 с.
В кадре (~16 мс при TargetFps 61) цепочка — ~10 чтений. Кучу целиком сканировать не нужно:
цепочка восстанавливается по смещениям (одно чтение глобальной GameStates → обход указателей, ~25 чтений).

Чтение значений одной командой, когда расклад найден и сохранён (скан не участвует):

```
python poe_scan.py --offs-file offsets.json --values
    Health @+0x1A8: Current=4000 Total=5000 ReservedFlat=100 ReservedPercent=2023 -> 103%
    Mana   @+0x200: Current=2000 Total=3000 ...
    EnergyShield @+0x240: ...
    Ward   @+0x2E0: Current=840 Total=840 ReservedFlat=0 ReservedPercent=0 -> 100%
    Rage   @+0x298: Current=15 Total=30 ReservedFlat=0 ReservedPercent=0 -> 50%
```

## `find_offsets.py` — поиск смещений по значениям

Инструмент «значения → смещения»: задаём известные максимумы текущего персонажа,
скрипт находит точные int32-совпадения в памяти и восстанавливает раскладку.

```
python find_offsets.py --hp 5000 --mana 3000 --es 1200
python find_offsets.py --hp 5000 --mana 3000 --es 1200 --current-hp 4000   # даёт и смещение Current
python find_offsets.py --pid 11436 ...
python find_offsets.py --selftest          # проверка алгоритма на синтетике (rev33 layout)
python find_offsets.py --bench 32          # замер стадий на синтетике заданного объёма (МБ)
python find_offsets.py --hp 7025 --mana 949 --es 182 --current-hp 6374 --tol 0 --save offsets.json
```

Опции:

| Опция | Значение |
|---|---|
| `--hp/--mana/--es N` | максимальные значения (Total) текущего персонажа |
| `--current-hp/--current-mana/--current-es N` | текущие значения; `--current-hp` даёт смещение `Current` сразу |
| `--pid N` | PID процесса (иначе поиск по имени) |
| `--anchor K=0x..,…` | зашитый якорь: `HealthOff` (Total Health внутри Life), `TotalOff` (Total внутри Vital), `BackPtrOff` (back-pointer Vital'а на Life), `ManaGap` (Health→Mana), `EsGap` (Mana→ES), `ReservedOff`. Пропущенные ключи берутся из зашитого `ANCHOR` |
| `--tol N` | допуск вокруг якоря (по умолчанию 8 байт; `--tol 0` — строго по якорю) |
| `--wide` | снять якорь: `TotalOff 0x20..0x8C`, `BackPtrOff {0x04..0x28}`, шаг Vital'ов не фиксируется — полный перебор для ревизий с другой раскладкой |
| `--min-score N` | не печатать кандидатов со скором ниже N (по умолчанию 5) |
| `--save FILE` | записать JSON расклада (только смещения, адреса кучи не сохраняются) |
| `--selftest` / `--bench МБ` | офлайн-проверка алгоритма и замер стадий |

Логика:

1. `VirtualQueryEx` → список committed-readable регионов; поиск `int32 == hp` по всем (`bytes.find`).
2. Для каждого совпадения перебираем гипотезы `TotalOff`: Vital-основа = `адрес − TotalOff`.
   Витал валидируется по структуре: vtable @+0x00, back-pointer Life @+0x10, `ReservedFlat` @+0x18,
   `ReservedPercent` @+0x1C ∈ [0, 10000].
3. По back-pointer получаем Life-компонент; внутри него ищем int32 == mana и == es → их смещения;
   каждый кандидат проверяется как Vital (тот же `TotalOff`, тот же back-pointer на Life).
4. Backtrace: ищем указатели на Life → находим std::vector `ComponentListPtr` (`First = site − 8k`),
   Entity = vector − 0x10, проверяем `IsValid == 0xC` / `Id`; затем указатели на Entity →
   std::vector `LocalPlayers` → смещение внутри AreaInstance.

Подтверждение кандидата — прямое чтение, а не скан 16 ГБ: `Life+0x08` — указатель на Entity,
`Entity+0x8C == 0xC`, компонентный вектор `Entity+0x10` содержит адрес Life, `Entity+0x88` — Id.
4–5 чтений на кандидата. Скоростная оценка `ReadProcessMemory` в этой среде: 728 МБ/с,
один проход по куче ~22 с, полный путь (4 прохода) ~1.5 мин. Путь скана — запасной
(если смещения Entity этого патча не совпадают с гипотезами rev33).

Значение `hp` встречается в памяти много раз (не только Total), поэтому отбор идёт по структурным
проверкам (back-pointer + ReservedPercent + совпадение смещений Mana/ES), а не по одному совпадению.
Кандидатов схлопует вывод: они группируются по сигнатуре смещений (адрес Life в сигнатуру не входит),
печатается `уникальных раскладов: N` и `Life-объектов k` для каждой группы. Шум двух типов:

1. **Сдвинутые семьи** (`TotalOff 0x2C..0x3C`, `HealthOff 0x1A0..0x1B0`) — одна игла Total Health,
   прочитанная с base-сдвигом; симметрия сдвига: проверки относительные, поэтому
   `(base+4, t+4, b+4)` удовлетворяет тем же условиям. Отличает их абсолютная проверка:
   vtable Vital'а обязана лежать внутри главного модуля (`ImageBase..ImageBase+SizeOfImage`).
   Убиваются скором: vtable вне модуля (−3), stat-id поля (−1), Reserved не по якорю (−1),
   основы Vital'ов кратны 8 (у сдвинутых семей немодульны). Строгий `--tol 0` их не порождает вовсе.
2. **Дубли Life-объектов одного расклада** — несколько объектов кучи с теми же значениями
   HP/Mana/ES (соседние Life-компоненты, монстры). Расклад идентичен, скор ниже.

Лучший расклад печатает готовую команду `poe_scan.py --offs HealthOff=…,TotalOff=…,ManaOff=…,ESOff=…,CurrentOff=…`
— смещения подаются как **позиции Total-значений внутри Life** (основа Vital = `Off − TotalOff`),
и для Mana/ES это `Off + TotalOff`.

Самопроверка на синтетике с rev33-раскладкой (`--selftest`) восстанавливает всё:

```
Total@0x34  Life @0x600000: Health@0x1DC (Vital-основа 0x1A8) Mana@0x234 ES@0x274 Current@0x38
ComponentListPtr vector @0x400010 (Entity+0x10), index 0 из 2, Entity @0x400000 IsValid=0xC Id=123
LocalPlayers vector @0x2005B8 index 0 count 1 (AreaInstance+0x5B8)
```

Замер стадий (`--bench`, каждая строка — полный прогон):

```
filler 32 МБ, HP-совпадений 129:  scan 0.01 с, discover 0.01 с, pointers 0.01 с, first_hits 0.00 с
filler 64 МБ, HP-совпадений 257:  scan 0.01 с, discover 0.02 с, pointers 0.02 с, first_hits 0.01 с
filler 128 МБ, HP-совпадений 513: scan 0.03 с, discover 0.03 с, pointers 0.04 с, first_hits 0.02 с
```

Объём × 4 ⇒ время × 4, число кандидатов и подтверждений постоянно — зависимость линейная.
Для реальных 10 ГБ экстраполяция ~30–40 с на все стадии (чтение памяти — dominant).

Практические правила live-прогона:

- указывать `--current-hp` (тогда Current получает +2 и `CurrentOff` находится сразу);
- значения Total читать в полном HP (Current == Total даёт лишние совпадения);
- кандидатов с наибольшим скором передавать в `poe_scan.py --offs HealthOff=…,TotalOff=…,…`.

## `find_vital.py` — разовый поиск смещения нового Vital'а (Ward/Rage/…)

Когда патч добавляет Vital'ы в Life (смещения HP/Mana/ES уже известны из `offsets.json`),
смещение ищется по значениям: цепочка (A)–(J) из `poe_scan.py` берёт Life-компонент локального
игрока, в окне `Life..Life+--span` ищутся целочисленные совпадения `int == max`, кандидат
проверяется как Vital (обязательный back-pointer на owner-компонент, vtable внутри главного
модуля, stat-id'ы, ReservedFlat/Percent; current проверяется в `CurrentOff`, +2 к скором).
Порядок стадий: окно Life → скан всех компонентов игрока (comp-вектор, и до, и после Life,
owner разрешается по CLU bucket) → куча.

```
python find_vital.py --label Ward --max 840 --current 840
python find_vital.py --label Rage --max 30 --current 15
python find_vital.py --label Spirit --max 949            # только max: печатается фактический Current
python find_vital.py --label Spirit --max 110 --types i16,u16
python find_vital.py --selftest      # синтетика: Rage 32-бит в Life, Spirit 16-бит в Position
```

Опции: `--types` (u32,i32,u16,i16,u64,i64 — по умолчанию все; int16 с нулевыми верхними
байтами неотличим от u32, offset один), `--span` (окно 0x1000), `--pid`, `--offs-file`,
`--rev`, `--heap` (пропустить Life/компоненты). Лучший кандидат печатает готовый JSON-ключ
`"LabelOff": N` и команду `poe_scan.py --offs LabelOff=…`. Vital вне Life-компонента —
печатается owner-компонент по имени и смещения Vital'а внутри него, Life-ключ не пишется.

## `offsets.json` — расклад, сохранённый сканером

```json
{
  "HealthOff": 476,    "TotalOff": 52,     "ManaOff": 564,   "ESOff": 628,
  "WardOff": 788,      "RageOff": 716,
  "BackPtrOff": 16,    "CurrentOff": 56,   "LocalPlayers": 1472,
  "Score": 9
}
```

`WardOff`/`RageOff` — Vital'ы, добавленные патчем 1.4.0 (позиции Total внутри Life, та же
семантика, что HealthOff/ManaOff/ESOff; основы @+0x2E0 и @+0x298, Life index 4). Найдены
сканером `find_vital.py`; Reserved у них нулевые.

Ключи — те же, что у `--offs`. `LocalPlayers` обязателен: без него цепочка не доходит до списка игроков.
Адреса кучи (`Life`, `Entity`, `AreaInstance`) в JSON не пишутся — они меняются каждый запуск.

Рабочий цикл (смещения находят один раз на патч, дальше скан не нужен):

```
python find_offsets.py --hp 7025 --mana 949 --es 182 --current-hp 6374 --tol 0 --save offsets.json
python poe_scan.py --offs-file offsets.json           # после рестарта: PID, ImageBase, цепочка — сами
```

`--area` из CLI убран: адрес AreaInstance определяется автоматически той же цепочкой, что в `poe_scan.py`
(сигнатура → `GameStates*` → `States[4]` → `+0x290`), поэтому `LocalPlayers` попадает в JSON сразу.

После сохранения повторный скан **не нужен**: `poe_scan.py` восстанавливает цепочку по сигнатуре
(одно чтение по `base+RVA`) и обходит кучу по указателям. Статичные и динамические величины
(смещения, RVA, `ImageBase`, адреса кучи, PID) разобраны в разделе 11 `../research/REPORT.md`.

`offsets_selftest.json` — расклад, найденный `--selftest` на синтетике; служит проверкой алгоритма,
а не данными текущего билда.

Синтетика расширена до GameState (`CurrentStatePtr @+0x10`, `States 13×16 @+0x50`,
`InGameState(4) → AreaInstance @+0x290`). Дефекты цепочки, найденные на этом прогоне,
разобраны в разделе 11 `../research/REPORT.md`.
