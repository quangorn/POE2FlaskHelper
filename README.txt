POE2 Flask Helper 1.0.0 — Stage 0 stub
======================================

[EN] This build is a non-functional skeleton: it has no tray icon and no scanner.
Requires: Windows x64 and the Visual C++ v14 x64 Redistributable
(https://aka.ms/vs/17/release/vc_redist.x64.exe).
POE2FlaskHelper.exe --version prints the version and exits 0.
On start the program creates config.json next to the exe (defaults) and writes
logs/app.log in UTF-8, rotating app.log -> app.log.1 -> ... (logging.maxSizeKb,
logging.keepFiles in config.json). offsets.json must be placed next to the exe by
find_offsets.py - a missing or invalid offsets.json exits with code 2.
Use --batch for non-interactive runs: no message boxes, all messages go to app.log.
Full English/Russian README comes in the final release (SPEC §2.3).

[RU] Эта сборка — незапускаемый каркас: нет трея и сканера.
Требуются: Windows x64 и Visual C++ v14 x64 Redistributable
(https://aka.ms/vs/17/release/vc_redist.x64.exe).
POE2FlaskHelper.exe --version печатает версию и выходит с кодом 0.
При запуске рядом с exe создаётся config.json (значения по умолчанию), логи пишутся
в UTF-8 в logs/app.log с ротацией app.log -> app.log.1 -> ... (logging.maxSizeKb,
logging.keepFiles в config.json). offsets.json кладётся рядом с exe из find_offsets.py —
при отсутствии или невалидности код выхода 2.
Для неинтерактивного запуска используйте --batch: сообщение не показывается,
всё пишется в app.log.
Полный двуязычный README — в финальном релизе (SPEC §2.3).
