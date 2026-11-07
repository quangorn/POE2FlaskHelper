#!/usr/bin/env bash
# POE2FlaskHelper build — SPEC §2.2. Native cygwin bash.
# vcvars64 -> VS cmake/ninja -> Release -> strip + --version check -> ctest -> dist zip.
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

if ! run_build; then
    echo "FATAL: cmake configure/build failed" >&2
    exit 1
fi

EXE=build/POE2FlaskHelper.exe
[ -f "$EXE" ] || { echo "FATAL: $EXE not produced" >&2; exit 1; }

# --- 3. strip + integrity check (SPEC §2.2, plan §5 risk 2) ---
# cygwin strip on MSVC PE can corrupt the file: if the exe does not run after
# strip, warn and relink without strip.
strip --strip-all "$EXE"
if objdump -h "$EXE" > /dev/null 2>&1 \
   && cmd.exe /c "$(cygpath -w "$EXE") --version"; then
    echo "strip ok"
else
    echo "WARN: strip corrupted the exe, relinking without strip"
    rm -f "$EXE"
    if ! run_build; then
        echo "FATAL: relink without strip failed" >&2
        exit 1
    fi
fi

# --- 4. tests: red tests stop the build before packaging (SPEC §2.2) ---
if ! ctest --test-dir build --output-on-failure; then
    echo "FATAL: tests failed, packaging skipped" >&2
    exit 1
fi

# --- 5. packaging ---
VERSION=$(cat build/version.txt)
STAGE="dist/POE2FlaskHelper-$VERSION-win64"
rm -rf dist
mkdir -p "$STAGE"
cp "$EXE" "$STAGE/POE2FlaskHelper.exe"
cp README.txt "$STAGE/README.txt"
cp config.json "$STAGE/config.json"
cp offsets.json "$STAGE/offsets.json"

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
