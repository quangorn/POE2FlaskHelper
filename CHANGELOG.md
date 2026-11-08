# POE2FlaskHelper — история версий

Формат номера — `1.major.minor` (ведущая цифра — серия, всегда `1`):

- **major** — новая пользовательская фича: `1.1.0 -> 1.2.0` (последняя цифра сбрасывается в 0);
- **minor** — фикс бага или небольшое изменение поведения: `1.1.0 -> 1.1.1`;
- **смена серии** — `1.x.y -> 2.0.0`: только существенная переработка архитектуры;
- **версия не поднимается, если код проекта не менялся** — exe актуален. «Код проекта» =
  `src/` и `res/` ассеты/rc/manifest (всё, что линкуется в exe). Версию не поднимают
  правки `tests/`, документации (`SPEC.md`, `plan.md`, `backlog.md`, `README.md`,
  `res/README.txt.in`), `research/`, `scanner/`, `build.sh`, `version.sh`,
  `config.json`, `offsets.json`, `res/gen_assets.py`.

Единственный источник номера — `project(POE2FlaskHelper VERSION ...)` в `CMakeLists.txt`
(SPEC §2.2): он попадает в `build/version.h` (`--version`, лог старта), VERSIONINFO,
манифест, генерируемый `README.txt` и имя архива. Поднимается только через `./version.sh`:

```
./version.sh major "описание фичи"
./version.sh minor "описание фикса"
./version.sh series "описание"      # смена серии: 1.x.y -> 2.0.0
./version.sh check        # гейт согласованности код<->версия, вызывает build.sh
```

`version.sh` правит `CMakeLists.txt` и эту таблицу (записи newest-first), коммит и сборка
вручную; автокоммита и git-тегов нет. Правила и гейт — SPEC §2.4.

## 1.1.0 — 2026-11-08 — major

звук из конфига: sound.volume 0..100 (PCM16-масштабирование при каждом проигрывании, SND_MEMORY) + фикс тултипа трея NIF_SHOWTIP при NOTIFYICON_VERSION_4

## 1.0.0 — 2026-11-08 — первый релиз

MVP по SPEC v1.1, этапы 0–5 плана плюс live-тесты L1/M1 (173 теста, `ctest` зелёный):

- трей с пунктами Enabled/Disabled (галочка) и Exit, глобальный Ctrl+9 с обработкой
  занятой комбинации, `enable.wav` / `disable.wav` (PCM16), пользовательская иконка;
- async-лог с ротацией и local-штампами, `config.json` / `offsets.json` по SPEC §5–§6,
  скан сигнатуры чанками 1 МБ, цепочка адресации A–J, Vital'ы HP / Mana / Energy Shield /
  Ward / Rage, `evaluate()` с порогом, кулдауном от последней отправки и foreground-gate,
  SendInput с scan code и extended-флагами;
- состояние In menu / loading (`Phase::InMenu`) убирает info-спам главного меню,
  held-строка окна кулдауна пишется один раз за окно;
- путь Admin: error-лог + тултип «Admin rights required» с троттлом 5 с и восстановлением;
- сборка: `build.sh` (vcvars64 → VS cmake/ninja → Release → strip + целостность →
  `ctest` → zip), smoke-запуск staged-копии, zip-гейт состава.

Состав архива: `POE2FlaskHelper.exe` + `config.json` + `offsets.json` + `README.txt`.
Коммиты `213bf35` (этап 0) … `c499602` (live-тесты M1).
