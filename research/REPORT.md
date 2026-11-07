# Схема сканирования памяти PathOfExile.exe (POE2Eye Helper, rev 33)

## 0. Артефакты исследования

```
research/
  code/rev33/          — полный C# код ауры POE2-Mem (ревизия 33, патч v0.5.5, 05.09.2026)
  code/rev32/          — C# код ауры (ревизия 17.07.2026 08:19, патч v0.5.4c)
  code/rev31/          — C# код ауры (ревизия 17.07.2026 08:18, тот же патч v0.5.4c)
  code/rev2025-09-16/  — срез из config.bak.7z, git commit e3c15f9 (начало 2025-09)
  code/rev2025-10-11/  — срез из config.bak.7z, git commit 4c14a56 (2025-10, офсеты под v0.4)
  rev31_rev32.diff     — пустой: rev31 и rev32 байт-в-байт идентичны
  rev32_rev33.diff     — полный diff кода rev32 → rev33 (57 строк, только офсеты)
  revisions/config     — распакованный config.bak.7z (git-репозиторий конфига пака, 75 коммитов)
  extract_code.py      — извлечение C#-кода из любого Script.json (любая ревизия)
  offsets_table.py     — сводная таблица смещений по всем извлечённым ревизиям
  verify_signature.py  — парсинг секций PE + поиск сигнатуры в PathOfExile.exe
  check_symbols.py     — Debug directory (CODEVIEW/PDB) + C++-маниглинг
  find_component_names.py / life_context.py — поиск имён компонентов в бинарнике
```

Скрипты, работающие с живым процессом, вынесены рядом в `scanner/` (`poe_scan.py`, `find_offsets.py`,
сохранённый расклад `offsets.json`, `README.md` с командами и опциями). Это сканер, а не исследование:
в `research/` остались анализ бинарника и код ауры. Выводы из живых прогонов — раздел 11.

Ревизии пака: `rev33` = «Fixed offsets for the latest patch v0.5.5 (2026-09-05)»,
`rev32` = «Fixed offsets for the latest patch v0.5.4c (2026-07-16)», `rev31` = загрузка 17.07.2026 08:18,
патч тот же (v0.5.4c). Все три на одном SDK 1.9.9774.0. В changelog пака самая свежая запись — `rev.29`
(«Fixed offsets for v0.5.4 + added dynamic offset resolution mechanism»); rev30/31/32 в changelog нет.

Исходник ауры **не защищён**: `data/release/config/auras/POE2-Mem/Script.json` содержит C#-файлы как
`{ "Name": "...", "Path": "", "Revision": 0, "Content": "..." }` — 36 файлов. Папки восстановлены по namespaces.

Весь код сканирования/чтения — это 32 файла. Ключевые:

| Файл | Роль |
|---|---|
| `CheatCartridge/GameHelper/TheGame.cs` | старт: скан сигнатуры → цепочка |
| `CheatCartridge/GameHelper/GameOffsets/Offsets.cs` | сигнатура(и) |
| `CheatCartridge/GameHelper/RemoteObjects/GameStates.cs` | разбор состояний игры |
| `CheatCartridge/GameHelper/RemoteObjects/States/InGameState.cs` | → зона |
| `CheatCartridge/GameHelper/RemoteObjects/States/InGameStateObjects/AreaInstance.cs` | → список игроков |
| `CheatCartridge/GameHelper/RemoteObjects/States/InGameStateObjects/Entity.cs` | компоненты сущности, поиск `Life` |
| `CheatCartridge/GameHelper/RemoteObjects/Components/Life.cs` | Health / Mana / EnergyShield |
| `CheatCartridge/GameHelper/GameOffsets/Objects/Components/VitalStruct.cs` | Total / Current / Reserved |
| `CheatCartridge/GameHelper/Natives/*.cs` | std::vector / bucket / wstring / tuple |
| `CheatCartridge/BotBrain.cs` | получение PID и IMemory (точка входа в память) |

## 1. Точка входа: откуда берётся базовый адрес

```csharp
var targetProcessId = WinActiveTrigger.ActiveWindow?.ProcessId;              // PID из активного окна
using var process = EyeAuras.Memory.LocalProcess.ByProcessId(targetProcessId.Value);
var moduleName = Path.GetFileNameWithoutExtension(process.ProcessName) + ".exe";
using var memory = process.MemoryOfModule(moduleName);   // IMemory с BaseAddress = base модуля
```

Реальный запуск (лог 2026-11-06 21:21:37, PID 11436):

```
Enumerated modules for PID 11436: count=138   [Process PID 11436 PathOfExile @ 0x7FF712410000]
Opening READ handle for PID 11436 PathOfExile
OpenProcess(READ) => 0x00005480
Static Addresses: { Key = GameStates, Value = 0x7FF7169FE348 }
```

- хэндл открывается **только на чтение** (`VM_READ`); записей в память нет ни в коде, ни в логе;
- все адреса считаются относительно **base модуля `PathOfExile.exe`** (`IMemory.BaseAddress`);
- `MemoryOfModule` выбирает именно главный модуль процесса из 138 перечисленных.

## 2. Сигнатура → статическая глобальная переменная GameStates

В коде ровно **одна** сигнатура (`Offsets.cs`):

```csharp
public static readonly IBytePattern[] Patterns =
{
    BytePattern.FromTemplate(
        "48 83 EC ?? 48 8B F1 33 ED 48 39 2D ^ ?? ?? ?? ??",
        nameof(GameStates)
    ),
};
```

Семантика шаблона EyeAuras:
- `??` = байт-маска (любой байт);
- `^` = **anchor** — позиция 4-байтового RIP-relative смещения внутри найденного совпадения;
- `MemoryUtils.GetOffsets(Memory, Offsets.Patterns)` сканирует память модуля и возвращает смещение anchor
  **относительно base**.

Расчёт адреса глобальной переменной (`TheGame.cs`):

```csharp
var offsetDataValue  = Memory.Read<int>(Memory.BaseAddress + patternInfo.Value);   // disp32
var address = Memory.BaseAddress + patternInfo.Value + offsetDataValue + 0x04;      // RIP: end-of-disp + disp
```

Это стандартный RIP-relative: `48 39 2D disp32` (`cmp [rip+disp32], rbx`), адрес операнда =
`адрес_конца_disp + disp`.

Из примера: `GameStates static = base + 0x45EE348` (совпадение найдено на глубине ~4.6 МБ внутри образа модуля —
значит сканируется весь образ, а не сегмент). Это **единственная** точка, где нужен скан; дальше чистая
косвенная адресация.

## 3. Цепочка косвенной адресации (rev 33)

```
base (PathOfExile.exe)
  └─(A) статическая глобальная  GameStates*          ← сигнатура
       └─(B) GameState object                         +0x00
            ├─(C) CurrentStatePtr std::vector         +0x10
            │     текущее состояние = *(Last - 0x10)  ("2nd-last ptr")
            └─(D) States buffer                       +0x50, 13 записей по 16 байт
                  InGameState = States[4].X           (enum GameStateTypes.InGameState = 4)
                       └─(E) AreaInstanceData         InGameState +0x290
                            ├─(F) LocalPlayers        AreaInstance +0x5B8 std::vector<IntPtr>
                            │     Player entity = First[0]
                            └─ … (CurrentAreaHash +0x114, EntitiesCount +0x6F8 — для логов)

Player entity
  ├─(G) ItemStruct: VTablePtr +0x00, EntityDetailsPtr +0x08, ComponentListPtr std::vector +0x10
  ├─(H) IsValid @ +0x8C == 0xC (0x03 = invalid), Id @ +0x88
  ├─(I) EntityDetails: name std::wstring @ +0x08, ComponentLookUpPtr @ +0x28
  └─(J) ComponentLookUpStruct.ComponentsNameAndIndex @ +0x28 (bucket)
        элемент = { NamePtr: ptr, Index: int32, pad } (16 байт)
        имя читается как строка по NamePtr; нужен компонент "Life"

Life component (адрес = ComponentListPtr[Index])
  ├─ Header: StaticPtr +0x00, EntityPtr +0x08   (EntityPtr используется для проверки родителя)
  ├─ Health        @ +0x1A8
  ├─ Mana          @ +0x200
  └─ EnergyShield  @ +0x240

VitalStruct (внутри каждого Vital)
  VtablePtr +0x00, LifeComponentPtr +0x10, ReservedFlat +0x18, ReservedPercent +0x1C,
  TotalStatId +0x20, Total @ +0x34, Current @ +0x38

Процент: Unreserved = Total - (ceil(ReservedPercent/10000 * Total) + ReservedFlat)
         percent   = round(100 * Current / Unreserved)
```

Пример живых адресов из лога:

| Шаг | Адрес |
|---|---|
| base модуля | `0x7FF712410000` |
| GameStates static (A) | `0x7FF7169FE348` (`base + 0x45EE348`) |
| GameState object (B) | `0x3194EEB1A80` |
| InGameState (D/E) | `0x3194EE51210` |
| AreaInstance (F) | `0x319291B2000` |
| Player entity (G) | `0x7FF71592CF30` (`base + 0x351CF30`, внутри образа модуля) |

Важная деталь: адрес игрока лежит **внутри образа модуля** — структуры игрока в PoE2 статические,
а не в куче. Локальные игроки и зона — в нативной куче (`0x319…`).

### 3.1 Та же цепочка в rev 32 (v0.5.4c)

Отличаются только два верхних уровня; все нижние (`InGameState → Life → VitalStruct`) — те же числа.

```
GameStates* (та же сигнатура, тот же RIP-расчёт)
  └─*→ GameState object                      +0x00
       ├─ CurrentStatePtr std::vector        +0x08   (rev33: 0x10)
       │   текущее состояние = *(Last - 0x10)
       └─ States buffer                      +0x48   (rev33: 0x50), 13 записей по 16 байт
             InGameState = States[4].X
InGameState  +0x290 → AreaInstanceData                (не изменился)
AreaInstance +0x5A8 std::vector<IntPtr> LocalPlayers  (rev33: 0x5B8)
             Player entity = First[0]
Entity       +0x8C IsValid==0xC, +0x88 Id             (не изменились)
EntityDetails+0x28 ComponentLookUpPtr, bucket @+0x28  (не изменились)
Life         +0x1A8 Health, +0x200 Mana, +0x240 ES    (не изменились)
VitalStruct  +0x34 Total, +0x38 Current               (не изменились)
```

Служебные поля AreaInstance в rev32: `CurrentAreaLevel 0xC4`, `CurrentAreaHash 0x11C`, `EntitiesCount 0x6E8`.

### 3.2 Типы и размеры: Health / Mana — это структуры, а не числа

`LifeOffset` описывает Vital'ы как **встроенные записи**, значения HP/MP — это пара `int32`, а не float:

```csharp
[FieldOffset(0x1A8)] public VitalStruct Health;   // 60 байт
[FieldOffset(0x200)] public VitalStruct Mana;     // 60 байт
[FieldOffset(0x240)] public VitalStruct EnergyShield;
```

Vital (rev31/32/33) — 12 полей, размер записи `0x3C` = **60 байт**:

```
+0x00  VtablePtr            IntPtr   8
+0x08  UnknownStatId0       int32    4
+0x0C  UnknownStatId1       int32    4
+0x10  LifeComponentPtr     IntPtr   8   (back-pointer на Life-компонент)
+0x18  ReservedFlat         int32    4   (Clarity и т.п.)
+0x1C  ReservedPercent      int32    4   (процент ×100: 20.23% → 2023)
+0x20  TotalStatId          int32    4
+0x24  UnknownStatId2       int32    4
+0x28  UnknownStatId3       int32    4   (раньше читался как float регенерации)
+0x2C..0x33                —          8   (не моделируется)
+0x34  Total                int32    4   ← максимум
+0x38  Current              int32    4   ← текущее значение
```

Читаемые значения:

| Поле | Тип | байт |
|---|---|---|
| `Total` / `Current` (HP, MP, ES) | `int` (int32) | 4 |
| `ReservedFlat` / `ReservedPercent` / `TotalStatId` | `int` (int32) | 4 |
| `VtablePtr`, `LifeComponentPtr` | `IntPtr` | 8 |

Размеры чтений (`Marshal.SizeOf`, Pack=1, LayoutKind.Explicit):

```
VitalStruct      0x3C = 60 байт   (один Vital)
LifeOffset       0x27C = 636 байт (Header + Health + Mana + ES, один Read на весь компонент)
ComponentHeader  0x10 = 16 байт   (StaticPtr + EntityPtr)
```

Процент считается целым из целых: `ReservedTotal = ceil(ReservedPercent/10000 * Total) + ReservedFlat`,
`percent = round(100 * Current / (Total - ReservedTotal))`.

В исторических срезах (2025-09/2025-10) Vital был короче — `0x34` = 52 байта, регенерация читалась как
**float @ 0x28**, а back-pointer Vital'а стоял на `0x08`:

```
+0x00 VtablePtr(8)  +0x08 PtrToLifeComponent(8)  +0x10 ReservedFlat(int)  +0x14 ReservedPercent(int)
+0x28 Regeneration float(4)  +0x2C Total(int)  +0x30 Current(int)
```

В rev31/32/33 `Regeneration` из схемы вырезан: на его месте лежат целые stat-id, и автор прямо пишет,
что «статическая реверсия показала целые id, записываемые конструктором».

Шаживание Vital'ов внутри Life-компонента (**не равное**): `Health 0x1A8 → Mana 0x200` = `0x58` (88 байт),
`Mana 0x200 → EnergyShield 0x240` = `0x40` (64 байта). То есть Vital'ы — не массив фиксированной длины,
а последовательные под-объекты разного размера; helper из каждого читает только первые 60 байт.

## 4. Разбор нативных контейнеров (`Natives/*.cs`)

```csharp
StdVector   { First, Last, End }          // 24 байта; count = (Last-First)/sizeof(T)
StdBucket   { Data(StdVector), UnknownPtr, Capacity, pad…, pad…, pad… }   // bucket/unordered_set
StdTuple2D<IntPtr> { X, Y }               // 16 байт;_states buffer = 13 × StdTuple2D
StdWString  { Buffer, ReservedBytes, Length, pad, Capacity, pad }  // SSO: если Capacity<=8, строка в Buffer+ReservedBytes
```

`ReadStdVector` (`MemoryExtensions.cs`) — базовый примитив всей цепочки:

```csharp
var length = nativeContainer.Last - nativeContainer.First;
if (length <= 0 || length % typeSize != 0) return Array.Empty<T>();
return memory.Read<T>(nativeContainer.First, length / typeSize);
```

`ReadStdBucket` — то же, через `Data.First` + `Capacity`.

## 5. Как находится компонент `Life`

Список компонентов сущности лежит как **таблица имён**, а не фиксированных индексов:

```csharp
var lookupPtr      = Memory.Read<ComponentLookUpStruct>(entityDetails.ComponentLookUpPtr);
var namesAndIndexes = Memory.ReadStdBucket<ComponentNameAndIndexStruct>(lookupPtr.ComponentsNameAndIndex);
var entityComponent = Memory.ReadStdVector<IntPtr>(entityBase.ComponentListPtr);   // массив указателей

foreach (var ni in namesAndIndexes)
{
    var name = Memory.ReadString(ni.NamePtr);          // строка из памяти игры
    componentAddresses.TryAdd(name, entityComponent[ni.Index]);
}
```

`TryGetComponent<Life>` просто берёт ключ `"Life"` (имя класса совпадает с именем компонента в игре).
Есть защиты: `Capacity > 50` (`MaxComponentsInAnEntity`) → выход; индекс вне диапазона массива → пропуск.

## 6. Когда сканирование повторяется и что сбрасывается

- `TheGame.UpdateData()`: пока `States.Address == IntPtr.Zero` — полный путь (скан + цепочка);
  дальше только `InGameStateObject.UpdateData()` (лёгкий путь, скан не трогается).
- `GameStates.CleanUpData()` обнуляет `Address`, `myStaticObj`, `currentStateAddress`, список состояний;
  при нулевом `InGameStateObject.Address` — цепочка строится заново. Обнуление происходит при смене зоны /
  перезапуске клиента (в BotBrain цикл пересоздаёт `gameFactory` при смене PID).
- Каждый кадр перепроверяется валидность: `IsValid == 0xC` у сущности и `IsParentValid` (компонентный
  `EntityPtr` должен указывать на текущую сущность). Если нет — список компонентов строится заново.
- Цикл: `TargetFps` из конфига (у пользователя 61 → ~16 мс/кадр), за кадр один проход цепочки и один тик BT.

## 7. Сравнение ревизий

Сигнатура и вся структура цепочки **во всех срезах идентичны**; меняются только числовые смещения.
`rev 29` (по changelog пака) добавил «dynamic offset resolution mechanism» — это и есть переход на скан по
сигнатуре вместо статических офсетов.

```
файл                   | поле            | rev2025-09-16 | rev2025-10-11 | rev31 | rev32 | rev33
-----------------------|-----------------|---------------|---------------|-------|-------|-------
GameStateOffset.cs     | CurrentStatePtr | 0x8           | 0x8           | 0x8   | 0x8   | 0x10
GameStateOffset.cs     | States          | 0x48          | 0x48          | 0x48  | 0x48  | 0x50
GameStateBuffer.cs     | TOTAL_STATES    | 12            | 12            | 13    | 13    | 13
InGameStateOffset.cs   | AreaInstanceData| 0x290         | 0x290         | 0x290 | 0x290 | 0x290
InGameStateOffset.cs   | ZoneSwitchCounter| 0x46C        | 0x46C         | 0x56C | 0x56C | 0x56C
InGameStateOffset.cs   | MsElapsed       | —             | 0x328         | 0x400 | 0x400 | 0x400
AreaInstanceOffsets.cs | LocalPlayers    | 0xA00         | 0xA08         | 0x5A8 | 0x5A8 | 0x5B8
AreaInstanceOffsets.cs | CurrentAreaLevel| 0xB4          | 0xBC          | 0xC4  | 0xC4  | 0xBC
AreaInstanceOffsets.cs | CurrentAreaHash | 0xF4          | 0xFC          | 0x11C | 0x11C | 0x114
AreaInstanceOffsets.cs | EntitiesCount   | 0xB58         | 0xB60         | 0x6E8 | 0x6E8 | 0x6F8
EntityOffsets.cs       | Id              | 0x80          | 0x80          | 0x88  | 0x88  | 0x88
EntityOffsets.cs       | IsValid         | 0x84          | 0x84          | 0x8C  | 0x8C  | 0x8C
LifeOffset.cs          | Health          | 0x1A8         | 0x1A8         | 0x1A8 | 0x1A8 | 0x1A8
LifeOffset.cs          | Mana            | 0x1F8         | 0x1F8         | 0x200 | 0x200 | 0x200
LifeOffset.cs          | EnergyShield    | 0x230         | 0x230         | 0x240 | 0x240 | 0x240
VitalStruct.cs         | Total           | 0x2C          | 0x2C          | 0x34  | 0x34  | 0x34
VitalStruct.cs         | Current         | 0x30          | 0x30          | 0x38  | 0x38  | 0x38
VitalStruct.cs         | ReservedPercent | 0x14          | 0x14          | 0x1C  | 0x1C  | 0x1C
VitalStruct.cs         | ReservedFlat    | 0x10          | 0x10          | 0x18  | 0x18  | 0x18
```

### 7.0 rev 31 = rev 32 (код байт-в-байт идентичен)

`diff -ru code/rev31 code/rev32` → **пустой** (`rev31_rev32.diff`, 0 строк). rev31 загружена 17.07.2026 в
08:18:34, rev32 — в 08:19:58, то есть через минуту: rev32 пересобрана/перезалит без изменений исходника.
Смещения обеих — под v0.5.4c. Отличия от исторических срезов у них общие: `ZoneSwitchCounter 0x46C → 0x56C`,
`Entity.Id 0x80 → 0x88`, `Entity.IsValid 0x84 → 0x8C`, `Life.Mana 0x1F8 → 0x200`, `Life.ES 0x230 → 0x240`,
`Vital.Total 0x2C → 0x34`, `Vital.Current 0x30 → 0x38`, `ReservedFlat 0x10 → 0x18`, `ReservedPercent 0x14 → 0x1C`,
число состояний 12 → 13, `LocalPlayers` переехал с `0xA08` на `0x5A8`.

### 7.1 rev 32 → rev 33 (единственные отличия)

Полный diff (`rev32_rev33.diff`) затрагивает **только 2 файла и 6 смещений**; весь остальной код, включая
сигнатуру, BotBrain, Life/Vital/Entity и компонентный поиск — байт в байт идентичен.

```
GameStateOffset.CurrentStatePtr  0x08 → 0x10   (+8)
GameStateOffset.States           0x48 → 0x50   (+8)
AreaInstance.CurrentAreaLevel    0xC4 → 0xBC   (-8)
AreaInstance.CurrentAreaHash     0x11C → 0x114 (-8)
AreaInstance.LocalPlayers        0x5A8 → 0x5B8 (+0x10)   ← по этой дорожке находится Player entity
AreaInstance.EntitiesCount       0x6E8 → 0x6F8 (+0x10)
```

Смысл: в v0.5.5 GameState-структура расширилась на 8 байт в начало (поэтому `CurrentStatePtr` и `States`
уехали на +8), а в AreaInstance часть полей сдвинулась назад на 8, часть вперёд на 16. Число состояний игры
(13) и индекс `InGameState = 4` не изменились.

Смещения, по которым читаются HP/MP/ES (`LifeOffset` и `VitalStruct`), между rev32 и rev33 **не менялись** —
то есть для rev33 автору хватило правок на двух верхних уровнях цепочки (GameState / AreaInstance).

### 7.1.1 Текущий патч (проверено живым прогоном 2026-11-07, `rev34` в `scanner/poe_scan.py`)

От rev33 отличается **только** одно поле: `AreaInstance.LocalPlayers 0x5B8 → 0x5C0` (+8).
Все остальные звенья цепочки совпали: сигнатура → `GameStates` (base+0x45EE348), `CurrentStatePtr 0x10`,
`States 0x50`, `InGameState = States[4]`, `AreaInstanceData 0x290`, `IsValid 0x8C`, `Id 0x88`,
`Life 0x1A8/0x200/0x240`, `Vital.Total 0x34`, `Vital.Current 0x38`. Живые значения:
`Health 6733/7025`, `Mana 659/949`, `EnergyShield 182/182`.

### 7.2 Как читать «дорожки» в комментариях

Автор записывает путь к полю как цепочку дельт: `0x588 -> 0x5A0 (+0x18) -> 0x5A8 (+8) -> 0x5B8 (+0x10)`.
Это история поиска поля по патчам: каждое звено — значение смещения в предыдущей ревизии и шаг сдвига.
По ним видно, какие структуры расширялись и в каком направлении, и это удобный ориентир для самостоятельной
реверсии следующей ревизии.

Наблюдения по всей истории:
- смещения Vital'ов (`Health` не изменился, `Mana`/`ES` сдвинулись на +8) и `Total`/`Current` (+8) —
  в новые патчи добавлялось поле в начало структур;
- `LocalPlayers` переехал с `0xA08` на `0x5B8` (перестройка AreaInstance);
- число состояний выросло с 12 до 13 (в enum GameStateTypes тоже), индекс `InGameState = 4` не менялся.

### 7.3 Порядок Vital'ов внутри Life-компонента

Порядок **`Health → Mana → EnergyShield` во всех пяти срезах одинаков**; `Health` лежит по одному адресу
`0x1A8` всегда. Меняются только адреса `Mana`/`ES` и шаги между ними:

```
Health 0x1A8 | Mana 0x1F8 | ES 0x230     rev2025-09-16, rev2025-10-11   (шаги 0x50=80, 0x38=56)
Health 0x1A8 | Mana 0x200 | ES 0x240     rev31, rev32, rev33            (шаги 0x58=88, 0x40=64)
```

Причина постоянства порядка: Vital'ы — поля C++-объекта Life-компонента, создаваемые конструктором в одном
и том же порядке; добавление новых полей двигает адреса следующих (`Mana`, `ES`), но не меняет очередность.
Сам адрес Life-компонента при этом не фиксирован — он берётся из `ComponentListPtr[Index]`, где индекс приходит
из таблицы ComponentLookUp по имени `"Life"`, поэтому смена порядка компонентов в игре скрипту не страшна.

### 7.4 Сигнатура: одинакова во всех срезах

`Offsets.cs` — **байт-в-байт идентичен** во всех пяти срезах (2025-09-16, 2025-10-11, rev31, rev32, rev33):
одна сигнатура, тот же RIP-расчёт. Меняется только `disp32`, который читается из памяти в рантайме, поэтому
одно и то же «тело» паттерна (`48 83 EC ?? 48 8B F1 33 ED 48 39 2D ^ ?? ?? ?? ??`) работает под v0.4, v0.5.4c
и v0.5.5. В бинарнике v0.5.5 у него ровно одно совпадение (`VA 0x10E955`), так что запас по уникальности есть.

Сигнатура введена в `rev.29` («dynamic offset resolution mechanism») и с тех пор не правилась — все
последующие ревизии меняли только числовые offsets.

## 8. Откуда скрипт знает имена полей: символов в бинарнике нет

Проверка `D:\Games\Path of Exile 2\PathOfExile.exe` (78 МБ, PE32+):

**Debug directory (4 записи):**
```
[0] CODEVIEW  sig='RSDS'  PDB = F:\buildslave\build\poe2_production\source\
                        build_outputs\Release_Fastx64\PathOfExile_x64x64.pdb
            GUID 2B57E4DB-3E68-AE4D-A7EE-108D40A02F44, age 1
[1] type 12   [2] VMWindows (GCTL — Guard CF table)   [3] VMLinux (пусто)
```
Ссылка на PDB есть, самого PDB в поставке нет → локально символов (имён полей/классов/функций) нет.

**Что всё-таки осталось в бинарнике:** 3751 уникальный MSVC-маниглинг `??…` (таблица Guard CF, в `.data`,
VA ~`0x4452803`+), например:

```
??ProcessDamagePackageInternal@Actor@Components@Game@@MEAA_NAEAUDamagePackage@Actions@@…
??Update@Impl@Entity2@@QEAAXXZ@
??OnFrame@PreGameState@States@@UEAAX_JM@Z@
??SimulationEventReceived@ClientPlayer@Components@Game@@UEAAXAEBUSimulationEventPacket@…
```

Они дают имена классов игры (`Components@Game`, `States`, `Actions`, `Buffs`), но **ничего про layout
структур**: это имена функций, ни одно не содержит смещения поля. По ним можно находить код, который поле
пишет, но само число всё равно приходится получать из листинга.

**Имена компонентов скрипт читает из памяти игры**, а не из бинарника (`Entity.cs`):

```csharp
var lookup = Memory.Read<ComponentLookUpStruct>(entityDetails.ComponentLookUpPtr);
var namesAndIndexes = Memory.ReadStdBucket<ComponentNameAndIndexStruct>(lookup.ComponentsNameAndIndex);
foreach (var ni in namesAndIndexes)                       // элемент 16 байт: {NamePtr, Index, pad}
    componentAddresses.TryAdd(Memory.ReadString(ni.NamePtr), entityComponent[ni.Index]);
```

`Memory.ReadString(ni.NamePtr)` — строка лежит в игровой куче (адрес вида `0x319…`). Ключ для поиска
компонента зашит в скрипте как имя C#-класса: `typeof(T).Name` → `"Life"`.

Доказательства, что статической таблицы имён компонентов в exe нет:
- строка `Life\0` встречается **2 раза** (VA `0x3228275`, `0x34DFF1C`); вокруг `0x34DFF1C` — dat-имена
  (`on_life_threshold`, `trigger_method`, `l_mana_percentage`, `start_delay`, `next_only_path`), то есть
  это пул имён dat-схем, а не список компонентов;
- поиск 32-битных указателей, равных смещению строки `Life` (`0x34DF31C`) — **0 совпадений**: на строки
  никто статически не ссылается, таблица ComponentLookUp собирается в рантайме из dat-данных;
- кластерного окна (4 КБ), где стояло бы ≥4 имени компонентов (`Life`, `Position`, `Rotation`, `Twitch`,
  `Flee`, `Deployed`), в бинарнике **не найдено**;
- имена в `Content.ggpk` не читались: ggpack v3 (magic `GGPK`, version 3, PDIR v1) хранит индекс как
  xxhash-таблицу (имён в индексе нет), данные сжаты — plaintext-имён в файле нет.

Итог по схеме знаний скрипта:

| Что нужно | Откуда берётся |
|---|---|
| базовый адрес модуля | `IMemory.BaseAddress` (`Process.Module32` / `MemoryOfModule`) |
| адрес статической `GameStates*` | **единственный** скан по сигнатуре + RIP-расчёт (подтверждено: 1 совпадение, file `0x10DD55`, VA `0x10E955`, disp `+72219107` → `0x45EE348` = живой лог) |
| имена компонентов (`"Life"`) | строки из памяти игры (ComponentLookUp), ключ = имя C#-класса |
| числовые FieldOffset (`0x1A8`, `0x5B8`, …) | захардкожены в исходнике, получены ручной реверсией («дорожки» дельт в комментариях) |
| индекс состояния (`InGameState = 4`) | enum `GameStateTypes` зашит в код |

## 9. Как добавить две другие ревизии

Ревизии пака берутся из EyeAuras share `S20241213181401RaWLZLE430e6` (rev 32, rev 31 —
«Fixed offsets for the latest patch v0.5.5 / v0.5.4c»). Для каждой ревизии нужен её `Script.json`:

1. Распаковать пак ревизии и запустить его (он создаст `data/release/config/auras/POE2-Mem/Script.json`),
   либо достать `Script.json` из архива пака/`config.bak` этой ревизии.
2. `python extract_code.py <путь>/Script.json code/revNNN`
3. `python offsets_table.py` — таблица обновится автоматически (она обходит все `code/rev*`).

Срез из локального `config.bak.7z` уже распакован в `revisions/config`, история:
`git -C revisions/config log --oneline -- auras/POE2-Mem/Script.json`
(75 коммитов «Periodic save», 2025-09-16 … 2025-10-17).

## 10. Что останется сделать, если живые адреса HP/Mana/ES уже найдены

Коротко: **адресов из одного запуска недостаточно**. Живые адреса — это снимок; скрипт, который переживает
перезапуск и патч, требует двух вещей:

1. **цепочку смещений** (все `+0x…`), выраженных относительно base модуля и друг друга;
2. **сигнатуру-якорь** для единственного глобального указателя (`GameStates*`), потому что его абсолютный
   адрес меняется и от ASLR (запуск к запуску), и от патча.

Процедура, по которой это делается:

| Шаг | Что получается | Метод |
|---|---|---|
| 1. base модуля | `IMemory.BaseAddress` (`OpenProcess(READ)` + выбор главного модуля) | тривиально |
| 2. Все найденные адреса → относительные | `off = addr − base`; кучные адреса (`0x319…`) **не** переносятся, только по цепочке | вычитание base |
| 3. Подъём вверх по указателям | для каждого объекта найти, кто на него ссылается (поиск значения указателя по памяти / xref в статике) | скан памяти записями `ptr == объект` |
| 4. Выход на глобальную переменную в образе модуля | адрес в `.data` → его смещение от base; этот адрес и есть «статический», но он ASLR-плавает | цепочка xref до переменной в `.data` |
| 5. Сигнатура для якоря | последовательность байт вокруг `cmp [rip+disp], reg`, маскируя volatile-байты (`??`), якорь `^` на позиции disp; обязательна проверка, что совпадение **ровно одно** (у пака: 1 совпадение) | disasm + уникальность паттерна |
| 6. RIP-расчёт | `target = base + anchorOffset + disp32 + 4` (проверено: VA `0x10E955`, disp `+72219107` → `0x45EE348`) | арифметика |
| 7. Смещения внутри структур | `GameState+0x10/0x50`, `InGameState+0x290`, `AreaInstance+0x5B8`, `Entity+0x8C/0x88`, `EntityDetails+0x28`, `Life+0x1A8/0x200/0x240`, `Vital+0x34/0x38` | delta-diff: менять HP/MP в игре и смотреть, какой int32 изменился |
| 8. Семантика контейнеров | `std::vector {First,Last,End}`, bucket `{Data, Capacity}`, `std::wstring` SSO; индекс состояния (`InGameState = 4`); `IsValid == 0xC` как sentinel валидности | чтение структур + сравнение с реально растущими массивами |
| 9. Поиск компонента по имени | таблица ComponentLookUp `{NamePtr, Index}`; ключ `"Life"` (имя класса) | читается из памяти в рантайме |
| 10. Readers и цикл | PID → хэндл на чтение → скан → цепочка → кадр; проверки `IsValid`/`IsParentValid`, лимит `Capacity > 50`; пересборка при `Address == 0` (смена зоны/перезапуск клиента) | код из `TheGame.cs`, `GameStates.cs`, `MemoryExtensions.cs` |
| 11. Обслуживание по патчам | после обновления игры: заново прогнать цепочку в живом процессе, пересчитать числа, обновить сигнатуру если код изменился; вести «дорожки» дельт (`0x5A8 → 0x5B8 (+0x10)`) | сравнение с прошлой ревизией |

Что **не** нужно: искать абсолютные адреса HP/Mana/ES и хардкодить их — они живут в куче, меняются при
каждом входе в зону; не нужно и искать имена полей в бинарнике (их там нет, раздел 8).

Что нужно помимо памяти (вне этой схемы): окно/PID-детект, ввод (кнопки, отправка клавиш), конфиг порогов
и UI — это отдельный слой, в исследование не входит.

Итоговый ответ на вопрос: **нужны оба** — и цепочка смещений (относительно base), и паттерн для глобального
якоря. Только «адрес hp/mana/es» даёт рабочий скрипт ровно до первого перезапуска клиента.

## 11. Сканер: живые прогоны и их выводы

Инструменты (`scanner/poe_scan.py`, `scanner/find_offsets.py`), команды запуска, все опции
(`--file`, `--rev`, `--offs`, `--offs-file`, `--pid`, `--values`, `--anchor`, `--tol`, `--wide`,
`--min-score`, `--save`, `--selftest`, `--bench`) и формат `offsets.json` разобраны в
[`../scanner/README.md`](../scanner/README.md). Здесь — только результаты исследования.

### 11.1 Прототип на чистом Python годен

Реализовано: `OpenProcess(VM_READ | QUERY_INFORMATION)` → `NtQueryInformationProcess` (PEB) →
`ImageBase` из PEB и `SizeOfImage` из PE-заголовка в памяти → скан образа regex'ом чанками по 1 МБ →
RIP-расчёт якоря → цепочка (A)–(J): StdVector `{First,Last,End}`, bucket, `std::wstring` (SSO),
поиск `"Life"` по именам из ComponentLookUp, чтение Vital'ов, процент по той же формуле. Запись в
память отсутствует. Ограничения: Windows-нативный интерпретатор (cygwin-сборка не даёт `ctypes.WinDLL`)
и 64-битный (для корректных `c_void_p`); `pymem` не требуется. Для live-теста нужен запущенный клиент.

Замеренные скорости:

```
одно ReadProcessMemory (4 байта)    ~3 мкс
скан сигнатуры по 78 МБ образа       0.08 c
ReadProcessMemory в этой среде       728 МБ/с
```

Вывод: Python полностью подходит. Единственный скан — раз на запуск; в кадре (~16 мс при TargetFps 61)
цепочка — ~10 чтений, что в Python укладывается легко. Кучу целиком сканировать не нужно: цепочка
восстанавливается по смещениям (сигнатура → одно чтение глобальной GameStates → обход указателей,
~25 чтений).

### 11.2 Сложность «значения → смещения» (проверена бенчмарком)

Все проходы по памяти линейны, квадратичных переборов нет:

- `scan_value` — один проход по committed-регионам, `bytes.find` (O(M));
- `discover` — на каждое HP-совпадение окно читается один раз (кэш), позиции Mana/ES находятся regexом
  в этом окне, затем гипотезы `TotalOff × BackPtrOff` (27 × 8, константа) и группировка позиций
  словарём по значению back-pointer. Перебор пар Mana×ES заменён на O(|mpos| + |epos|);
- `scan_pointers` — все Life-значения (и все Entity) ищутся одной regex-альтернативой за один проход,
  а не по проходу на каждого кандидата;
- `collect_first_hits` — гипотезы `First = site - 8k` (k ≤ 50) собираются сканом диапазона по общему
  старшему префиксу адресов (один `bytes.find`), а не regex из |sites| × 51 игл;
- `find_vector_at` — 51 проверок на каждый site, доступ к значениям через индекс регионов
  (`ProcIndex`, бинарный поиск + клиппинг чтений), без скана памяти внутри циклов.

Замер (каждая строка — полный прогон стадий):

```
filler 32 МБ, HP-совпадений 129:  scan 0.01 с, discover 0.01 с, pointers 0.01 с, first_hits 0.00 с
filler 64 МБ, HP-совпадений 257:  scan 0.01 с, discover 0.02 с, pointers 0.02 с, first_hits 0.01 с
filler 128 МБ, HP-совпадений 513: scan 0.03 с, discover 0.03 с, pointers 0.04 с, first_hits 0.02 с
```

Объём × 4 ⇒ время × 4, число кандидатов (8) и подтверждений (8) постоянно: зависимость линейная.
Для реальных 10 ГБ экстраполяция ~30–40 с на все стадии (чтение памяти — dominant). Один проход по
куче ~22 с, полный путь (4 прохода) ~1.5 мин — из-за этого путь скана кучи объявлен запасным, а
кандидат подтверждается прямым чтением (`confirm_direct`): `Life+0x08` — указатель на Entity,
`Entity+0x8C == 0xC`, компонентный вектор `Entity+0x10` содержит адрес Life, `Entity+0x88` — Id.
4–5 чтений на кандидата.

### 11.3 Живой прогон: якорь найден, природа «множества вариантов»

Первые живые прогоны (локации разные, персонаж: max HP 7009, мана 949, ES 182):

```
совпадений int32 == 7009 (max HP): 832 / 915
окон успешно прочитано: 831..915          # клиппинг по границе региона работает
кандидатов Vital/Life-раскладки: 16 / 32
  Life @0x233C6D00030: Health Vital +0x1A8 (Total@+0x1DC), Mana Vital +0x200, ES Vital +0x240
     | внутри Vital: Total@+0x34, back-pointer@+0x10, Reserved+0x14
```

Ключевое: **Mana и ES проверяются** — кандидату нужно, чтобы значения 949 и 182 лежали в позициях
`ManaVital+TotalOff` и `ESVital+TotalOff`, чей back-pointer ведёт на тот же Life. Все 16/32 кандидатов
дают одно и то же ядро (`Health +0x1A8`, `Mana +0x200`, `ES +0x240`, `TotalOff 0x34`, back-pointer `0x10`)
— это раскладка rev33, она подтверждена живыми данными.

Причина «вариантов» — **симметрия сдвига**: проверки относительные (позиция значения → base = позиция − t,
back-pointer в base + b), поэтому `(base+4, t+4, b+4)` удовлетворяет тем же условиям. Семейство из 7
кандидатов — это одна раскладка, записанная со сдвигом на 8 байт. Отличает их только абсолютная проверка:
**vtable Vital'а обязана лежать внутри главного модуля** (`ImageBase..ImageBase+SizeOfImage`, берётся из PEB).
Для сдвинутых семейств «vtable» читается из середины структуры (куча) и не попадает в диапазон модуля.

По этому живому якорю `discover` свёрнут к узкому режиму (по умолчанию):
`TotalOff 0x30..0x38`, `BackPtrOff 0x08..0x14`, шаг Health→Mana `0x48..0x68`, шаг Mana→ES `0x30..0x4C`,
плюс vtable внутри модуля и заголовок Life. На самопроверке узкий режим выдаёт единственный лидер
(score 7) — ровно rev33: Health Vital `+0x1A8`, Mana `+0x200`, ES `+0x240`, Total `+0x34`, Current `+0x38`,
back-pointer `+0x10`. Строгий якорь (`--tol 0`) даёт единственный кандидат (score 12); `--wide` возвращает
полный перебор и воспроизводит сдвинутые семьи, как на живом скане. Самопроверка проверяет оба режима.

«Много кандидатов» схлопываются выводом: кандидаты группируются по сигнатуре смещений (адрес Life в
сигнатуру не входит), печатается `уникальных раскладов: N` и `Life-объектов k` для каждой группы; строки
со скором ниже порога не печатаются. Шум двух типов:

1. **Сдвинутые семьи** (`TotalOff 0x2C..0x3C`, `HealthOff 0x1A0..0x1B0`): одна и та же игла Total
   Health прочитана с base-сдвигом. Убиваются скором: vtable Vital'а вне модуля (−3), stat-id поля (−1),
   Reserved не по якорю (−1), и **основы Vital'ов кратны 8** — первое поле Vital'а vtable-указатель,
   выравнивание объекта 8 байт; у сдвинутых семей основы `0x1AC/0x1A4` немодульны (за каждую модульную
   +3: скор настоящего расклада 12 против 8 у сдвинутых). Строгий `--tol 0` их не порождает вовсе.
2. **Дубли Life-объектов одного расклада**: несколько объектов кучи с теми же значениями HP/Mana/ES
   (соседние Life-компоненты, монстры). Расклад идентичен, скор ниже (Current не найден).

Значение `hp` встречается в памяти много раз (не только Total), поэтому отбор идёт по структурным
проверкам (back-pointer + ReservedPercent + совпадение смещений Mana/ES), а не по одному совпадению.
Самопроверка на синтетической памяти с rev33-раскладкой восстанавливает всё (`Health@0x1DC`,
`Mana@0x234`, `ES@0x274`, `Current@0x38`, `ComponentListPtr` vector `Entity+0x10`,
`LocalPlayers` `AreaInstance+0x5B8`).

### 11.4 Дефекты цепочки, найденные самопроверкой

Самопроверка расширенной синтетики (`CurrentStatePtr @+0x10`, `States 13×16 @+0x50`,
`InGameState(4) → AreaInstance @+0x290`) показала три реальных дефекта реализации цепочки:

- Entity-чанка читалась 24 байтами (`mem.read(player, 0x18)`), а `ComponentListPtr` std::vector
  занимает `+0x10..0x28` — нужно 40 байт;
- wstring имени EntityDetails читался 24 байтами, формат `<2Qi4i` (36 байт, лишнее `i`)
  — исправлено на 32 байта и `<2Q4i`;
- `FakeMem` не имел `rd_u64/rd_u32/rd_u8/rd_u16`, из-за чего цепочка на синтетике не шла.

### 11.5 Что статично, что нет: чтение после рестарта без скана

Смещения — константы компиляции билда, адреса кучи — нет:

| величина | статична? | где берётся |
|---|---|---|
| структурные смещения (`TotalOff`, `CurrentOff`, `HealthOff`, `ManaOff`, `ESOff`, `BackPtrOff`, `ReservedOff`, `LocalPlayers`, `States`, `CurrentStatePtr`, `AreaInstanceData`, `EntityIsValid`, `CLU_Bucket`) | да, до патча игры | находят один раз, сохраняются |
| GameStates RVA (`base+0x45EE348`) и смещение сигнатуры (`file 0x10DD55`) | да, до патча | скан сигнатуры по бинарнику (`poe_scan.py --file`) |
| `ImageBase` | меняет каждый запуск (ASLR) | PEB (`NtQueryInformationProcess`), читают при каждом запуске |
| адреса Life/Entity/AreaInstance/GameState | меняет каждый запуск | не сохраняют: цепочка читает их из указателей |
| PID | меняет каждый запуск | `EnumProcesses` + `QueryFullProcessImageNameW` |

То есть после нахождения смещений повторный скан **не нужен**: `poe_scan.py` восстанавливает цепочку
по сигнатуре (одно чтение по `base+RVA`) и обходит кучу по указателям. `--save FILE` записывает
расклад в `scanner/offsets.json` (только смещения, адреса кучи не сохраняются), `--offs-file FILE`
читает его теми же ключами. `LocalPlayers` попадает в JSON сразу: `--area` убран из CLI, адрес
AreaInstance определяется автоматически той же цепочкой (сигнатура → `GameStates*` → `States[4]` → `+0x290`).
