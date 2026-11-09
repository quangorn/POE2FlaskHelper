#!/usr/bin/env bash
# POE2FlaskHelper GitHub release — SPEC §2.2 (upload rule of 2026-11-08), plan-release.md п. 8.
# Native cygwin bash. Called by build.sh after packaging: upload by default;
# --no-upload (passed through by build.sh) prints the skip notice and exits 0.
#
# Tag: v<version>-poe2-<gamever> (the poe2 part comes from the GameVersion key of
# offsets.json, written first by find_offsets.py --save). Title and notes are English.
# Notes body: the CHANGELOG.md entry of the version (English file, rename 2026-11-09); when
# a release of the SAME exe
# version already exists (offsets-only release for a new game patch), the body is the
# offsets-update sentence. Re-upload of an existing tag: message + non-zero exit.
set -o pipefail

cd "$(dirname "$0")"

die() { echo "FATAL: $*" >&2; exit 1; }
warn() { echo "WARN: $*"; }

NO_UPLOAD=""
for a in "$@"; do
    [ "$a" = "--no-upload" ] && NO_UPLOAD=1
done

[ -f build/version.txt ] || die "build/version.txt is missing — run build.sh first"
VERSION=$(tr -d '\r' < build/version.txt)
[ -n "$VERSION" ] || die "build/version.txt is empty"

# The game patch version: string GameVersion, the FIRST key of offsets.json (SPEC §6).
GAMEVER=$(python -c "import json;v=json.load(open('offsets.json',encoding='utf-8')).get('GameVersion');print(v if isinstance(v,str) else '')" 2>/dev/null)

if [ -n "$GAMEVER" ]; then
    ZIP="dist/POE2FlaskHelper-$VERSION-poe2-$GAMEVER-win64.zip"
    TAG="v$VERSION-poe2-$GAMEVER"
    TITLE="POE2FlaskHelper $VERSION (POE2 $GAMEVER)"
else
    warn "no GameVersion in offsets.json — plain archive name and tag (no poe2 part)"
    ZIP="dist/POE2FlaskHelper-$VERSION-win64.zip"
    TAG="v$VERSION"
    TITLE="POE2FlaskHelper $VERSION"
fi
[ -f "$ZIP" ] || die "release archive $ZIP is missing — run build.sh first"

if [ -n "$NO_UPLOAD" ]; then
    echo "release NOT uploaded (--no-upload): tag $TAG, zip $ZIP ready for a manual publish"
    exit 0
fi

# gh CLI: PATH first, the standard install as fallback (verified 2026-11-08: 2.103.3,
# account quangorn, remote git@github.com:quangorn/POE2FlaskHelper.git).
GH=""
if command -v gh > /dev/null 2>&1; then
    GH=gh
elif [ -x "/c/Program Files/GitHub CLI/gh.exe" ]; then
    GH="/c/Program Files/GitHub CLI/gh.exe"
elif [ -x "/c/Program Files/GitHub CLI/gh" ]; then
    GH="/c/Program Files/GitHub CLI/gh"
fi
[ -n "$GH" ] || die "gh not found (PATH, fallback C:/Program Files/GitHub CLI) — install GitHub CLI or use build.sh --no-upload"

REPO="quangorn/POE2FlaskHelper"

# The same tag must not be re-uploaded: the tag carries version AND poe2 patch, so a
# repeat means nothing new (SPEC §2.2, plan-release.md п. 8).
if "$GH" release view "$TAG" --repo "$REPO" > /dev/null 2>&1; then
    echo "release $TAG already exists on $REPO — nothing to upload (a new release needs a new version or a new poe2 patch)" >&2
    exit 1
fi

# Offsets-only release (version.sh check is green on unchanged code, the poe2 version is
# new): a release of the SAME exe version already exists -> the notes are the offsets
# sentence, and CHANGELOG.md needs no entry for it (CHANGELOG.md intro). Detection
# runs BEFORE the body-required check below: this is the only scenario the plan describes
# for an unchanged-code release, and a missing English entry must not kill it.
# Dots of $VERSION are escaped — a plain dot in a regex is a wildcard (NIT R1-10).
VRE=$(printf '%s' "$VERSION" | sed 's/\./\\./g')
OFFSETS_ONLY=""
if [ -n "$GAMEVER" ] && "$GH" release list --repo "$REPO" --limit 100 2>/dev/null \
        | grep -qE "^v$VRE-poe2-"; then
    OFFSETS_ONLY=1
fi

# English notes body: the CHANGELOG.md entry of this version (added by version.sh).
# $2 is the version token of the newest-first heading "## <version> — <date> — <class>",
# compared as an exact string (no wildcard dots).
BODY=$(awk -v v="$VERSION" '
    /^## / { take = ($2 == v) }
    take && !/^## / { print }
' CHANGELOG.md | tr -d '\r' | sed -e '1{/^$/d}' | tr '\n' ' ')

if [ -n "$OFFSETS_ONLY" ]; then
    BODY="Updated memory offsets for POE2 $GAMEVER, program code unchanged."
elif [ -z "$BODY" ]; then
    die "CHANGELOG.md has no entry for version $VERSION — bump with ./version.sh major|minor \"desc\" --en \"English note\""
fi

"$GH" release create "$TAG" "$ZIP" --repo "$REPO" --title "$TITLE" --notes "$BODY" \
    || die "gh release create failed (tag $TAG)"
echo "release published: $TAG on $REPO (zip $ZIP)"