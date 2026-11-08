#!/usr/bin/env bash
# POE2FlaskHelper build — SPEC §2.2. Native cygwin bash.
# vcvars64 -> VS cmake/ninja -> Release -> version gates (SPEC §2.4: ./version.sh check,
# --version stdout, PE resources) -> strip + integrity -> ctest -> dist zip.
set -o pipefail

cd "$(dirname "$0")"
ROOT_WIN=$(cygpath -w "$PWD")

# --- 1. find vcvars64.bat: vswhere first, standard BT2022 path as fallback ---
VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
VCVARS_WIN=""
if [ -f "$VSWHERE" ]; then
    found=$("$VSWHERE" -latest -products '*' -property installationPath 2>/dev/null | head -1 | tr -d '\r')
    if [ -n "$found" ]; then
        vcv="$(cygpath -u "$found")/VC/Auxiliary/Build/vcvars64.bat"
        [ -f "$vcv" ] && VCVARS_WIN=$(cygpath -w "$vcv")
    fi
fi
if [ -z "$VCVARS_WIN" ]; then
    VCVARS_WIN='C:\Program Files (x86)\Microsoft Visual Studio\2022\BT2022\VC\Auxiliary\Build\vcvars64.bat'
fi
if [ ! -f "$(cygpath -u "$VCVARS_WIN")" ]; then
    echo "FATAL: vcvars64.bat not found ($VCVARS_WIN)" >&2
    exit 1
fi

# VS cmake + ninja live next to the found VS installation.
VS_ROOT="$(dirname "$(dirname "$(dirname "$(dirname "$(cygpath -u "$VCVARS_WIN")")")")")"
CMAKE_BIN_WIN=$(cygpath -w "$VS_ROOT/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin")
NINJA_BIN_WIN=$(cygpath -w "$VS_ROOT/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja")

# vswhere -latest -products '*' can pick a VS SKU without the CMake/Ninja components
# (S0-9): then the cmd below fails with a bare "cmake: command not found". Check the
# binaries here so the failure names the missing component.
if [ ! -f "$(cygpath -u "$CMAKE_BIN_WIN")/cmake.exe" ]; then
    echo "FATAL: cmake.exe missing from $CMAKE_BIN_WIN - the VS found by vswhere has no 'CMake tools for Windows' component; install it or point build.sh at a VS that ships it (BT2022)" >&2
    exit 1
fi
if [ ! -f "$(cygpath -u "$NINJA_BIN_WIN")/ninja.exe" ]; then
    echo "FATAL: ninja.exe missing from $NINJA_BIN_WIN - the VS found by vswhere has no Ninja; install the 'CMake tools for Windows' component" >&2
    exit 1
fi

# --- 2. configure + build inside a temporary .cmd run via cmd.exe /c ---
run_build() {
    local tmp
    tmp=$(mktemp /tmp/poe2_build_XXXXXX.cmd)
    {
        echo '@echo off'
        echo "call \"$VCVARS_WIN\""
        echo "set \"PATH=$CMAKE_BIN_WIN;$NINJA_BIN_WIN;%PATH%\""
        echo "cd /d \"$ROOT_WIN\""
        echo 'cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1'
        echo 'cmake --build build || exit /b 1'
    } > "$tmp"
    # NOTE: this cygwin keeps "//c" literal and cmd then goes interactive;
    # plain "/c" passes through correctly (tested).
    cmd.exe /c "$(cygpath -w "$tmp")"
    local rc=$?
    rm -f "$tmp"
    return $rc
}

# Full clean build on every run (added 2026-11-08): ninja + localized cl does not
# track /showIncludes header dependencies (the deps arrive as Russian "Примечание:
# включение файла"), so an incremental build can keep stale objects and ship stale
# binaries - measured live (a modified sound.cpp was not recompiled, the exe kept the
# old behavior). The sources are small, a full rebuild costs tens of seconds.
rm -rf build

if ! run_build; then
    echo "FATAL: cmake configure/build failed" >&2
    exit 1
fi

EXE=build/POE2FlaskHelper.exe
[ -f "$EXE" ] || { echo "FATAL: $EXE not produced" >&2; exit 1; }

# python is not part of the SPEC §2 toolchain; the PE-resource check (step 3b), the
# composition gate and the zip writer below are python-based. Fail here with a clear
# message instead of a bare "python: command not found" at the very end of the build
# (S0-1) — the guard is moved up because the version gates use python too.
if ! command -v python > /dev/null 2>&1; then
    echo "FATAL: python not in PATH - packaging (zip composition gate) and the PE-resource check need it; build not packaged" >&2
    exit 1
fi

# --- 2c. version gates (SPEC §2.4): the committed code and the committed version agree,
# and the built exe carries that version. Rules live in ./version.sh check (single source).
VERSION=$(cat build/version.txt)
HEAD_VER=$(tr -d '\r' < CMakeLists.txt | sed -n 's/^project(POE2FlaskHelper VERSION \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\) LANGUAGES CXX RC)$/\1/p')
[ -n "$HEAD_VER" ] || { echo "FATAL: CMakeLists.txt has no 'project(POE2FlaskHelper VERSION x.y.z LANGUAGES CXX RC)' line (SPEC §2.4 contract)" >&2; exit 1; }
if [ "$VERSION" != "$HEAD_VER" ]; then
    echo "FATAL: build/version.txt ($VERSION) != project(... VERSION) in CMakeLists.txt ($HEAD_VER) - re-run cmake configure (res/version.h.in, res/README.txt.in, res/app.manifest.in come from project VERSION)" >&2
    exit 1
fi
./version.sh check || { echo "FATAL: version gate failed (SPEC §2.4), packaging skipped" >&2; exit 1; }

# --- 3. strip + integrity check (SPEC §2.2, plan §5 risk 2) ---
# cygwin strip on MSVC PE can corrupt the file: if the exe does not run after
# strip, warn and relink without strip.
strip --strip-all "$EXE"
if objdump -h "$EXE" > /dev/null 2>&1; then
    VER_OUT=$(cmd.exe /c "$(cygpath -w "$EXE") --version" | tr -d '\r')
    rc=$?
else
    VER_OUT=""; rc=1
fi
if [ $rc -ne 0 ]; then
    echo "WARN: strip corrupted the exe, relinking without strip"
    rm -f "$EXE"
    if ! run_build; then
        echo "FATAL: relink without strip failed" >&2
        exit 1
    fi
    VER_OUT=$(cmd.exe /c "$(cygpath -w "$EXE") --version" | tr -d '\r')
    rc=$?
    [ $rc -ne 0 ] && { echo "FATAL: relink without strip produced an exe that does not run" >&2; exit 1; }
fi
# SPEC §2.4: --version prints exactly the project version. print_version prints only when
# AttachConsole succeeds (SPEC §2.3 "если есть консоль"), so an empty stdout is a WARN and
# the assertion is skipped; a mismatch is red.
if [ -z "$VER_OUT" ]; then
    echo "WARN: --version printed nothing (no parent console) - stdout version assertion skipped"
elif [ "$VER_OUT" = "POE2FlaskHelper $VERSION" ]; then
    echo "strip ok: --version prints $VERSION"
else
    echo "FATAL: --version printed '$VER_OUT', expected 'POE2FlaskHelper $VERSION' (SPEC §2.4)" >&2
    exit 1
fi

# --- 3b. the exe's PE resources carry the same version (SPEC §2.2 resources, §2.4):
# research/pe_res_check.py asserts assemblyIdentity and VERSIONINFO against project VERSION.
python research/pe_res_check.py "$EXE" "$VERSION" > /dev/null \
    || { echo "FATAL: PE resources of $EXE do not carry version $VERSION (SPEC §2.4), packaging skipped" >&2; exit 1; }
echo "PE resources ok: VERSIONINFO + manifest carry $VERSION"

# --- 4. tests: red tests stop the build before packaging (SPEC §2.2) ---
if ! ctest --test-dir build --output-on-failure; then
    echo "FATAL: tests failed, packaging skipped" >&2
    exit 1
fi

# --- 5. packaging ---
STAGE="dist/POE2FlaskHelper-$VERSION-win64"
rm -rf dist
mkdir -p "$STAGE"
cp "$EXE" "$STAGE/POE2FlaskHelper.exe"
# The zip README is generated by configure_file from res/README.txt.in (SPEC §2.4): the
# number in the user document cannot drift from project(... VERSION).
cp build/README.txt "$STAGE/README.txt"
cp config.json "$STAGE/config.json"
cp offsets.json "$STAGE/offsets.json"

# --- 5b. dist-exe smoke run (SPEC §9-2 "запуск из распакованного архива") ---
# The --version check in step 3 covers the version path only; the full launch + clean
# exit of the packaged exe is exercised here, in a TEMP COPY of the staged dir (the
# staged dir must stay the exact SPEC composition - running the exe there would add
# logs/ and fail the composition gate). Headless-safe: the app creates no windows, the
# documented hook of src/tray.h finds the hidden tray window by class
# ("POE2FlaskHelperTrayWindow") and posts WM_CLOSE -> DestroyWindow -> WM_DESTROY ->
# exit code 0 through the clean shutdown of main.cpp. No game needed, no sleeps in the
# build: a short poll loop with a fixed cap is the gate, not a unit test. Red => the
# build stops before the zip is written. ASSUMPTION (E1-R2, measured 2026-11-08): the
# single-instance mutex and FindWindowW are session-global, so two concurrent build.sh
# runs (or a running app instance) collide - run build.sh serially, close any live
# POE2FlaskHelper before packaging.
SMOKE_DIR=$(mktemp -d /tmp/poe2_smoke_XXXXXX)
cp "$STAGE/POE2FlaskHelper.exe" "$STAGE/config.json" "$STAGE/offsets.json" "$SMOKE_DIR/"
python - "$SMOKE_DIR" <<'PYEOF'
import ctypes, pathlib, subprocess, sys, time

smoke = pathlib.Path(sys.argv[1])
u = ctypes.CDLL("C:/Windows/System32/user32.dll")   # cygwin python loads the Windows DLL
u.FindWindowW.restype = ctypes.c_void_p
u.FindWindowW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p]
u.PostMessageW.restype = ctypes.c_bool
u.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_size_t]

proc = subprocess.Popen([str(smoke / "POE2FlaskHelper.exe")])
hwnd, polls = 0, 0
for i in range(50):                       # startup is one-shot: 5 s cap, 100 ms poll
    hwnd = u.FindWindowW("POE2FlaskHelperTrayWindow", None) or 0   # src/tray.h hook
    polls = i
    if hwnd:
        break
    time.sleep(0.1)
if not hwnd:
    proc.kill()
    sys.exit("FATAL: smoke run - the hidden tray window class was not found in 5 s")
if not u.PostMessageW(hwnd, 0x10, 0, 0):  # WM_CLOSE -> clean shutdown (tray.h documented hook)
    proc.kill()
    sys.exit("FATAL: smoke run - PostMessageW(WM_CLOSE) was not delivered")
rc = proc.wait(timeout=10)
if rc != 0:
    sys.exit(f"FATAL: smoke run exited with rc={rc}, expected 0 (SPEC §9-2)")
print(f"smoke ok: launched from a staged copy, tray window in {polls} polls, WM_CLOSE exit rc=0")
PYEOF
[ $? -eq 0 ] || { rm -rf "$SMOKE_DIR"; exit 1; }
rm -rf "$SMOKE_DIR"

python - "$STAGE" "$VERSION" <<'PYEOF'
import pathlib, sys, zipfile
stage, version = sys.argv[1], sys.argv[2]
# SPEC §2.2 / plan §4 DoD: exactly exe + config.json + offsets.json + README.txt.
expected = {"POE2FlaskHelper.exe", "config.json", "offsets.json", "README.txt"}
names = {p.name for p in pathlib.Path(stage).iterdir()}
if names != expected:
    print(f"FATAL: dist staging is not the SPEC composition; expected {sorted(expected)}, "
          f"found {sorted(names)} - zip skipped", file=sys.stderr)
    sys.exit(1)
zip_path = f"dist/POE2FlaskHelper-{version}-win64.zip"
with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
    for p in sorted(pathlib.Path(stage).iterdir()):
        zf.write(p, f"POE2FlaskHelper-{version}-win64/{p.name}")
print(f"created {zip_path}")
PYEOF
[ $? -eq 0 ] || exit 1

echo "BUILD OK: dist/POE2FlaskHelper-$VERSION-win64.zip"
