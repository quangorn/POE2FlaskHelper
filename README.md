# POE2FlaskHelper

Исследование памяти `PathOfExile.exe` (Path of Exile 2) и сканер чтения HP/Mana/ES/Ward/Rage.

## Структура

- [`scanner/`](scanner/README.md) — работа с живым процессом: чтение значений по
  цепочке смещений (`poe_scan.py`), поиск смещений HP/Mana/ES (`find_offsets.py`) и
  новых Vital'ов Ward/Rage/… (`find_vital.py`).
  Только чтение, запись в память отсутствует.
- [`research/REPORT.md`](research/REPORT.md) — разбор цепочки адресации, кода ауры
  POE2-Mem (Cheat Engine), сравнение ревизий.
  - `code/` — извлечённый C#-код ауры по ревизиям
  - `extract_code.py`, `offsets_table.py`, `verify_signature.py`, `check_symbols.py` — инструменты анализа
  - `revisions/` — распакованный сторед конфига пака (в репозиторий не входит, см. `.gitignore`)

## Требования

Windows-нативный Python 3.x 64-bit. `pymem` не требуется. Подробности запуска и опции —
в [`scanner/README.md`](scanner/README.md).
