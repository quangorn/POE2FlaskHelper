# MINOR_ISSUES — накопленный список minor-проблем

Формат записи:

```
## Stage N — <краткая суть>
- файл: src/... (строка/функция)
- суть: что не так
- статус: open | fixed-removed | live
- источник: reviewer stage N / live-checklist stage N
```

Правила:
- Major-проблемы сюда НЕ пишутся — они чинятся сразу в цикле ревью.
- Minor, мешающий следующему этапу, или появившийся повторно — исправляется
  сразу и удаляется из этого файла.
- В конце проекта — совместный проход по оставшимся записям.

---

## Stage 0 — review (build.sh, CMake, main.cpp, res/, tests/)

### S0-1: build.sh:90 — упаковка через python без проверки наличия
- файл: build.sh:90
- суть: cygwin `python` не входит в тулчейн SPEC §2; если его нет — сборка падает в самом конце с «command not found».
- статус: open
- источник: reviewer stage 0

### S0-2: CMakeLists.txt:14 — CMAKE_RC_FLAGS без кавычек
- файл: CMakeLists.txt:14
- суть: `-I ${CMAKE_BINARY_DIR}` ломается при пробелах в пути проекта; строка избыточна — include poe2_core уже транслируется в app.rc.
- статус: open
- источник: reviewer stage 0

### S0-3: CMakeLists.txt:29-30 — дефолтные лины CMake в линк-лайне
- файл: CMakeLists.txt:29-30
- суть: advapi32/ole32/uuid и т.д. от дефолта CMAKE_LINK_LIBRARY_FLAG; в exe не попадают (dumpbin чист), SPEC §2 формально говорит «advapi32 не используется». Комментарий, чтобы никто не паниковал.
- статус: open
- источник: reviewer stage 0

### S0-4: CMakeLists.txt:18 — /MANIFEST:NO отсутствует в списке линк-флагов SPEC §2
- файл: CMakeLists.txt:18
- суть: флаг необходим (манифест внедряет app.rc, линковочный дублировал бы); добавить комментарий над флагом.
- статус: open
- источник: reviewer stage 0

### S0-5: res/app.manifest:3 — версия assemblyIdentity захардкожена
- файл: res/app.manifest:3
- суть: version="1.0.0.0" zestareет при bump VERSION; генерить из .in с @PROJECT_VERSION@.
- статус: open
- источник: reviewer stage 0

### S0-6: src/main.cpp:15 — has_version_flag ищет подстроку во всей command line
- файл: src/main.cpp:15
- суть: любой аргумент/путь, содержащий `--version`, триггерит режим. На этапе 0 безвредно; ужесточить при появлении реальных аргументов.
- статус: open
- источник: reviewer stage 0

### S0-7: tests/test.h:80-83 — статическая регистрация тестов
- файл: tests/test.h:80-83
- суть: при разбиении тестов по TУ порядок статик-инита неопределён, объекты из статик-либа могут выкидываться. Держать тесты в exe, не в либе; заметку в заголовок.
- статус: open
- источник: reviewer stage 0

### S0-8: tests/test.h:35-40 — CHECK_EQ не печатает значения
- файл: tests/test.h:35-40
- суть: при падении не видно lhs/rhs — тяжело дебажить. Печатать значения для арифметических типов.
- статус: open
- источник: reviewer stage 0

### S0-9: build.sh:12-13 — vswhere -latest может выбрать VS без CMake/Ninja
- файл: build.sh:12-13
- суть: тогда cmake «command not found» внутри cmd; проверить наличие cmake.exe и FATAL с внятным сообщением.
- статус: open
- источник: reviewer stage 0

### S0-10: README.txt:4,10-13 — внутреннее противоречие «незапускаемый каркас» vs «--version печатает»
- файл: README.txt
- суть: формулировка EN/RU про не запускаемый каркас contradicts строке про --version. Косметика, переформулировать при финальном README (этап 5).
- статус: open
- источник: reviewer stage 0

### S0-11: res/app.rc:1-2 — лишний #include <windows.h>
- файл: res/app.rc:1-2
- суть: VERSIONINFO его не требует; убрать.
- статус: open
- источник: reviewer stage 0

### S0-12: tests/test_main.cpp:42-44 — exit-path run_all() проверен только через probe-счётчики
- файл: tests/test_main.cpp:42-44
- суть: тест не может самозапустить run_all(); честная запись в комментарии есть. Действий нет.
- статус: open (no-action)
- источник: reviewer stage 0

### S0-13: res/gen_assets.py:29 — AND-маска all-zero
- файл: res/gen_assets.py:29
- суть: прозрачность только через alpha 32bpp; для современных оболочек корректно. Действий нет.
- статус: open (no-action)
- источник: reviewer stage 0

### S0-14: build.sh:4,63 — set -u отсутствует; rc strip игнорируется (за ним guard)
- файл: build.sh:4,63
- суть: намеренно; действий нет.
- статус: open (no-action)
- источник: reviewer stage 0
