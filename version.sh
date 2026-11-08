#!/usr/bin/env bash
# POE2FlaskHelper version control — SPEC §2.4. Native cygwin bash.
#
#   ./version.sh major "описание фичи" --en "English note"    — новая пользовательская фича: 1.1.0 -> 1.2.0
#   ./version.sh minor "описание фикса" --en "English note"   — фикс бага / мелкое изменение: 1.1.0 -> 1.1.1
#   ./version.sh series "описание" --en "English note"         — смена серии (2.0.0): только существенная
#                                            переработка архитектуры;
#   ./version.sh major "..." --en "..." --force                — documented-исключение правила 3 (SPEC §2.4).
#   ./version.sh check                       — гейт согласованности код<->версия, вызывает
#                                            build.sh перед упаковкой (exit 0 = ок).
#
# --en обязателен (2026-11-08, plan-release.md п. 8): заметка релиза пишется на английском и
# живёт в CHANGELOG.en.md — её берёт release.sh как body GitHub-релиза. Русская запись
# CHANGELOG.md остаётся.
#
# Format is 1.major.minor (S.major.minor): the leading digit is the series, always 1
# until a substantial architecture rework; major bumps on new features, minor on fixes.
# Single source of the number: project(POE2FlaskHelper VERSION ...) in CMakeLists.txt;
# it reaches build/version.h (--version, start log), VERSIONINFO, the manifest and the
# zip name (SPEC §2.2). The history lives in CHANGELOG.md. Commits and tags are manual:
# version.sh touches exactly CMakeLists.txt + CHANGELOG.md and never commits.
set -o pipefail

cd "$(dirname "$0")"

die() { echo "FATAL: $*" >&2; exit 1; }
warn() { echo "WARN: $*"; }

# The version line shape is the contract: version.sh edits exactly this one line. The
# caller checks for emptiness (die inside a command substitution would exit only the
# subshell). CRLF tolerance: .gitattributes keeps *.txt as CRLF in the working tree, so
# the CR is stripped before the contract match.
head_version() {
    tr -d '\r' < CMakeLists.txt \
        | sed -n 's/^project(POE2FlaskHelper VERSION \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\) LANGUAGES CXX RC)$/\1/p'
}

# ver_of <commit> — the project VERSION value in that commit (empty if the file/line is not there).
ver_of() {
    git show "$1:CMakeLists.txt" 2>/dev/null | tr -d '\r' \
        | sed -n 's/^project(POE2FlaskHelper VERSION \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\) LANGUAGES CXX RC)$/\1/p'
}

need_git() {
    command -v git > /dev/null 2>&1 \
        || die "git is not in PATH — the version gate works on the git history (SPEC §2.4)"
    git rev-parse --is-inside-work-tree > /dev/null 2>&1 \
        || die "not a git repository — the version gate needs the history (SPEC §2.4)"
    git rev-parse --verify HEAD > /dev/null 2>&1 \
        || die "no HEAD commit — the version gate needs the history (SPEC §2.4)"
}

# "Code of the project" (SPEC §2.4): everything that is compiled into the exe — src/ and
# the res/ assets/rc/manifest. Documents, tools, config data and tests do not bump the
# version: the shipped exe stays current. The README-in is a document (zip content, it is
# generated from @PROJECT_VERSION@), gen_assets.py is a build-time helper; the same for
# the Russian README-in and res/config.example.json (the first-run config is app-generated,
# the example is its reference text, added 2026-11-08, plan-release.md п. 5–6).
CODE_PATHS=(src res ":(exclude)res/README.txt.in" ":(exclude)res/README.ru.txt.in" ":(exclude)res/config.example.json" ":(exclude)res/gen_assets.py")

# version_commits — commits that changed the project VERSION value, newest first.
version_commits() {
    local c v pv
    for c in $(git log --format=%H -- CMakeLists.txt); do
        v=$(ver_of "$c")
        pv=$(ver_of "$c^")
        [ -n "$v" ] && [ "$v" != "$pv" ] && echo "$c"
    done
}

code_diff() {  # committed code changes in <range> (name-only)
    git diff --name-only "$1"..HEAD -- "${CODE_PATHS[@]}"
}
code_dirty() {  # uncommitted code changes (working tree + staged + untracked)
    git status --porcelain -- "${CODE_PATHS[@]}"
}

case "${1:-}" in

check)  # ------------------------- build.sh gate (SPEC §2.4) -------------------------
    need_git
    ver=$(head_version)
    [ -n "$ver" ] || die "CMakeLists.txt has no 'project(POE2FlaskHelper VERSION x.y.z LANGUAGES CXX RC)' line — the contract shape is broken by hand (SPEC §2.4)"
    mapfile -t bumps < <(version_commits)
    if [ ${#bumps[@]} -eq 0 ]; then
        warn "ни один коммит истории не менял project(... VERSION) — гейт код<->версия пропускается"
        exit 0
    fi
    since=${bumps[0]}
    short=$(git rev-parse --short "$since")
    if [ ${#bumps[@]} -gt 1 ]; then
        prev=${bumps[1]}
        # rule 1/2: code committed after the version was set requires a new bump.
        after=$(code_diff "$since")
        if [ -n "$after" ]; then
            die "код проекта менялся после подъёма версии $ver (коммит $short): версия поднимается — major при новой фиче, minor при фиксе (SPEC §2.4); примеры: $(echo "$after" | head -3 | tr '\n' ' ')"
        fi
        # rule 3: the bump must be justified by code changes between the previous bump and this one.
        if [ -z "$(git diff --name-only "$prev".."$since" -- "${CODE_PATHS[@]}")" ]; then
            die "версия $ver поднята без изменений кода (src/ + res/) между подъёмами $short и $(git rev-parse --short "$prev") — правило 3, SPEC §2.4"
        fi
    else
        warn "первый релиз: version.sh только что задал номер, гейт код<->версия действует с следующего подъёма (SPEC §2.4)"
    fi
    n=$(code_dirty | wc -l)
    [ "$n" -gt 0 ] && warn "есть незакоммиченные правки кода ($n объектов): собранному exe соответствует версия, которая будет закоммичена после них"
    echo "version gate ok: $ver (подъём в $short, код<->версия согласованы)"
    exit 0
    ;;

major|minor|series)  # ------------------------- bump -------------------------
    CLASS=$1
    DESC="" EN="" FORCE=""
    shift
    while [ $# -gt 0 ]; do
        case "$1" in
            # `shift 2` fails when --en is the last argument ($# == 1), so the loop never
            # advances (R1-3). A missing value dies with the usage line.
            --en) [ $# -ge 2 ] || die "--en требует значение: --en \"English release note\""
                  EN="$2"; shift 2 ;;
            --force) FORCE=1; shift ;;
            *)      [ -z "$DESC" ] && DESC="$1"; shift ;;
        esac
    done
    [ -n "$DESC" ] || die "нужно описание записи CHANGELOG: ./version.sh $CLASS \"описание\" --en \"English note\" [--force]"
    [ -n "$EN" ] || die "нужна англоязычная заметка релиза: --en \"English release note\" (body GitHub-релиза, plan-release.md п. 8)"
    [ -f CHANGELOG.md ] || die "CHANGELOG.md отсутствует (история версий, SPEC §2.4)"
    [ -f CHANGELOG.en.md ] || die "CHANGELOG.en.md отсутствует (английские заметки релизов, SPEC §2.4)"
    need_git
    dirty=$(git status --porcelain -- CMakeLists.txt CHANGELOG.md CHANGELOG.en.md)
    [ -z "$dirty" ] || die "CMakeLists.txt/CHANGELOG.md/CHANGELOG.en.md имеют незакоммиченные правки — сначала коммит, потом подъём версии"

    ver=$(head_version)
    [ -n "$ver" ] || die "CMakeLists.txt has no 'project(POE2FlaskHelper VERSION x.y.z LANGUAGES CXX RC)' line — version.sh edits exactly that line, its shape is the contract (SPEC §2.4)"
    read -r S MA MI <<<"$(echo "$ver" | tr '.' ' ')"
    case "$CLASS" in
        major)  NEW="$S.$((MA + 1)).0" ;;
        minor)  NEW="$S.$MA.$((MI + 1))" ;;
        series) NEW="$((S + 1)).0.0" ;;
    esac

    mapfile -t bumps < <(version_commits)
    if [ ${#bumps[@]} -gt 0 ]; then
        # rule 3 at bump time: the code must have changed since the last version change.
        if [ -z "$(code_diff "${bumps[0]}")" ] && [ -z "$(code_dirty)" ]; then
            if [ -n "$FORCE" ]; then
                warn "правило 3 (код не менялся) обойдено --force — запись CHANGELOG должна объяснять, почему версия поднимается"
            else
                die "код проекта (src/ + res/) не менялся с подъёма версии $(git rev-parse --short "${bumps[0]}") — по правилу 3 (SPEC §2.4) exe актуален и версию поднимать нельзя; исключение: --force"
            fi
        fi
    else
        warn "первый подъём номера: правила проверятся с следующего (история пустая по VERSION)"
    fi

    # sed rewrites the prefix of the contract line; the trailing CR of the CRLF line is
    # left intact, so the file keeps its .gitattributes line endings.
    sed -i 's/^project(POE2FlaskHelper VERSION [0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]* LANGUAGES CXX RC)/project(POE2FlaskHelper VERSION '"$NEW"' LANGUAGES CXX RC)/' CMakeLists.txt
    tr -d '\r' < CMakeLists.txt | grep -q '^project(POE2FlaskHelper VERSION '"$NEW"' LANGUAGES CXX RC)$' \
        || die "CMakeLists.txt не содержит новую версию $NEW — правка не выполнена"

    # CHANGELOG entries are newest-first: the entry goes before the first version heading.
    # cp (not mv) keeps the target's permissions — cygwin mv cannot preserve them here.
    # The English entry of CHANGELOG.en.md is the release-note body of release.sh (plan-release.md
    # п. 8): the same newest-first shape, added from the --en description.
    # awk writes LF, so the entry of a CRLF-dominant file gets mixed EOL (R1-11). The dominant
    # EOL of each target is read BEFORE the rewrite and applied to the whole file after it.
    MD_CRLF=0; grep -q $'\r' CHANGELOG.md && MD_CRLF=1
    EN_CRLF=0; grep -q $'\r' CHANGELOG.en.md && EN_CRLF=1
    DATE=$(date +%Y-%m-%d)
    normalize() {  # rewrite the whole file with the target's dominant eol (LF content + CR)
        tmp=$(mktemp)
        tr -d '\r' < "$1" | sed 's/$/\r/' > "$tmp" && cp "$tmp" "$1" && rm -f "$tmp"
    }
    awk -v head="## $NEW — $DATE — $CLASS" -v desc="$DESC" '
        !done && /^## [0-9]/ { print head; print ""; print desc; print ""; done=1 }
        { print }
        END { if (!done) { print head; print ""; print desc } }
    ' CHANGELOG.md > "$tmp" && cp "$tmp" CHANGELOG.md && rm -f "$tmp"
    [ "$MD_CRLF" = 1 ] && normalize CHANGELOG.md
    tmp=$(mktemp)
    awk -v head="## $NEW — $DATE — $CLASS" -v desc="$EN" '
        !done && /^## [0-9]/ { print head; print ""; print desc; print ""; done=1 }
        { print }
        END { if (!done) { print head; print ""; print desc } }
    ' CHANGELOG.en.md > "$tmp" && cp "$tmp" CHANGELOG.en.md && rm -f "$tmp"
    [ "$EN_CRLF" = 1 ] && normalize CHANGELOG.en.md

    echo "$ver -> $NEW (записи в CHANGELOG.md и CHANGELOG.en.md)"
    echo "далее: ./build.sh (гейт код<->версия + имя zip + release), коммит CMakeLists.txt CHANGELOG.md CHANGELOG.en.md"
    ;;

*)
    die "usage: ./version.sh major|minor|series \"описание\" --en \"English note\" [--force]   |   ./version.sh check (SPEC §2.4)"
    ;;
esac
