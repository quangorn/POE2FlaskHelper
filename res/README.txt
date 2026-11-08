POE2 Flask Helper 1.0.0
=======================

A tray application for Windows x64. It reads HP/Mana/Energy Shield/Ward/Rage from the
memory of Path of Exile 2 (PathOfExile.exe), and when a value drops below your
threshold it sends the flask key to the game as if you pressed it yourself. It never
writes to game memory.

Requirements
------------
Windows x64 and the Visual C++ v14 x64 Redistributable
(https://aka.ms/vs/17/release/vc_redist.x64.exe). Nothing else is needed.

How to run
----------
Start POE2FlaskHelper.exe. The program lives entirely in the system tray and creates
no windows. On the first run it creates config.json next to the exe with default
settings. offsets.json is mandatory and must be placed next to the exe: if it is
missing, broken or incomplete, the program shows a message and exits with code 2.

Tray status (tooltip over the icon)
-----------------------------------
  Scanner off          - module switched off, the game memory is not read at all;
  Waiting for game     - module on, the game process is not found yet;
  In menu / loading    - module on, the game process is open, the game is not in a zone
   (main menu, character selection, zone loading); the handle stays open, the scanner
   polls once a second and reads no values;
  Attached (PID n)     - module on, the game values are being read;
  Admin rights required - OpenProcess denied: the game is started as administrator, so
   the helper must be started with the same rights (see "Game run as administrator").
   The status disappears on the next poll that opens the game successfully.
When the hotkey is taken by another program the tooltip appends "hotkey Ctrl+9 busy";
the program keeps working, the tray menu remains available.

Controls
--------
Ctrl+9 - global hotkey, switches the scanner module on and off. It works while the
game has the focus. Enabling plays enable.wav, disabling plays disable.wav.
Right-click the tray icon - a two-item menu:
  Enabled / Disabled - the switch; the text shows the current state, the checkmark
  marks the enabled module; clicking does the same as Ctrl+9.
  Exit - clean exit, the icon disappears from the tray.
Double-click on the icon does nothing.

Files (everything lives next to the exe, the current directory is ignored)
--------------------------------------------------------------------------
config.json  - settings: scanner switch at start (enabledOnStart), poll interval,
  requireGameForeground, process names, the Ctrl+9 hotkey, and for each Vital:
  enabled, key, threshold (1-100 %), cooldownMs. The file is read once at startup:
  edit it and restart the program to apply changes.
offsets.json - game offsets, mandatory. After a game patch the values stop matching
  and "Offsets mismatch" appears in the log. Fix: run find_offsets.py
  (python find_offsets.py --hp <HP> --mana <Mana> --es <ES> --current-hp <current HP>
  --tol 0 --save offsets.json, details in scanner/README.md) and put the new file
  next to the exe.
logs/app.log - UTF-8 log next to the exe, with rotation (logging.level,
  logging.maxSizeKb, logging.keepFiles in config.json). Start, exit, config warnings,
  hotkey registration, attach/detach, Offsets mismatch are written to the log. attach
  and detach are written once per game process, not once per poll: while the game is in
  the main menu / character selection / zone loading the log stays silent (status
  "In menu / loading", one line on entering it). Every
  flask key press is an info line ("Health 23% < 50%, key=1"), and so is the single
  line of a cooldown window in which the value stayed below the threshold ("EnergyShield
  80% < 80%, key=3 held: cooldown left 720 ms") - one such line per window, so you can
  see that the key was held back. Per-tick Vital values stay debug-only: at
  logging.level = info the log has presses and held lines, no value lines. The
  time zone lookup failure record (the log stamps fall back to UTC) is a debug line:
  it appears in the log only when logging.level is debug. At level debug, hovering the
  tray icon writes a burst of "tray callback event 512" lines (WM_MOUSEMOVE): normal
  noise, not a problem.

Game run as administrator
-------------------------
If the game is started as administrator, the helper must be started with the same
rights (right-click POE2FlaskHelper.exe -> Run as administrator). Otherwise Windows
UIPI blocks opening the game process and sending keys to it.

=======================
POE2 Flask Helper 1.0.0
=======================

Трей-приложение для Windows x64. Читает HP/Mana/Energy Shield/Ward/Rage из памяти
Path of Exile 2 (PathOfExile.exe) и при падении значения ниже порога отправляет игре
клавишу зелья, как будто её нажал сам игрок. Запись в память игры отсутствует.

Требования
----------
Windows x64 и Visual C++ v14 x64 Redistributable
(https://aka.ms/vs/17/release/vc_redist.x64.exe). Ничего больше не нужно.

Запуск
------
Запустите POE2FlaskHelper.exe. Программа живёт целиком в трее и не создаёт окон. При
первом запуске рядом с exe создаётся config.json со значениями по умолчанию.
offsets.json обязателен и кладётся рядом с exe: при отсутствии, повреждённом или неполном
файле программа показывает сообщение и выходит с кодом 2.

Статус в трее (тултип над иконкой)
----------------------------------
  Scanner off          - модуль выключен, память игры не читается вообще;
  Waiting for game     - модуль включён, процесс игры пока не найден;
  In menu / loading    - модуль включён, процесс игры открыт, игры в зоне нет (главное
   меню, выбор персонажа, загрузка зоны); хэндл держится открытым, сканер опрашивает
   раз в секунду и значений не читает;
  Attached (PID n)     - модуль включён, значения игры читаются;
  Admin rights required - OpenProcess отклонён: игра запущена от администратора, и хелпер
   нужно запускать с теми же правами (см. "Игра, запущенная от администратора").
   Статус исчезает на следующем опросе, где процесс игры открылся успешно.
При занятом хоткее тултип дополняется строкой "hotkey Ctrl+9 busy"; программа
продолжает работать, меню трея сохраняется.

Управление
---------
Ctrl+9 - глобальный хоткей, включает и выключает модуль сканирования. Работает, когда
фокус у игры. Включение сопровождается звуком enable.wav, выключение - disable.wav.
ПКМ по иконке - меню из двух пунктов:
  Enabled / Disabled - переключатель; текст показывает текущее состояние, галочка
  отмечает включённый модуль; клик делает то же, что Ctrl+9.
  Exit - корректный выход, иконка исчезает из трея.
Двойной клик по иконке ничего не делает.

Файлы (всё рядом с exe, текущая папка игнорируется)
-----------------------------------------------------
config.json  - настройки: включение сканера при старте (enabledOnStart), интервал
  опроса, requireGameForeground, имена процессов, хоткей Ctrl+9, и для каждого
  Vital'а: enabled, клавиша, порог (1-100 %), cooldownMs. Файл читается один раз на
  старте: чтобы применить изменения, отредактируйте его и перезапустите программу.
offsets.json - смещения игры, обязателен. После патча игры смещения перестают
  совпадать и в логе появляется "Offsets mismatch". Что делать: запустить
  find_offsets.py (python find_offsets.py --hp <HP> --mana <Mana> --es <ES>
  --current-hp <текущий HP> --tol 0 --save offsets.json, подробности в
  scanner/README.md) и положить новый файл рядом с exe.
logs/app.log - лог в UTF-8 рядом с exe, с ротацией (logging.level,
  logging.maxSizeKb, logging.keepFiles в config.json). В лог пишутся старт, выход,
  предупреждения конфига, регистрация хоткея, attach/detach, Offsets mismatch. attach и
  detach пишутся один раз на процесс игры, а не на каждый опрос: пока игра находится в
  главном меню / на экране выбора персонажа / на загрузке зоны, лог молчит (статус
  "In menu / loading", одна строка на вход). Каждое
  нажатие зелья — info-строка ("Health 23% < 50%, key=1"), и единственная строка окна
  кулдауна, в котором значение оставалось ниже порога ("EnergyShield 80% < 80%, key=3
  held: cooldown left 720 ms") — одна такая строка на окно, по ней видно, что клавиша
  была задержана. Значения Vital'ов на каждом тике остаются только на debug: при
  logging.level = info в логе есть нажатия и held-строки, но нет строк значений. Запись о
  сбое поиска часовой зоны (штампы тогда в UTC) — debug-строка: она видна в логе только
  при logging.level = debug. При уровне debug наведение курсора на иконку трея пишет
  пачку строк "tray callback event 512" (WM_MOUSEMOVE): это штатный шум, не проблема.

Игра, запущенная от администратора
----------------------------------
Если игра запущена от администратора, хелпер нужно запускать с теми же правами
(ПКМ по POE2FlaskHelper.exe -> Запуск от имени администратора). Иначе Windows UIPI
блокирует открытие процесса игры и отправку клавиш в неё.
