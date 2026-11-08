# POE2FlaskHelper — release notes (English)

The English version history of `1.major.minor` bumps (the rules are in `CHANGELOG.md`).
Entries are added by `./version.sh major|minor|series "desc" --en "English note"`
(newest-first) and serve as the body of the GitHub releases published by `release.sh`
(SPEC §2.4). The Russian history lives in `CHANGELOG.md`.

Releases before 1.3.0 have no English entry (the upload flow appeared in 1.3.0). For an
offsets-only release (same exe version, new game patch) `release.sh` writes its own
body and needs no entry here.
## 1.3.0 — 2026-11-08 — major

Strict config validation: an unknown key makes config.json invalid — startup shows a message box and exits, runtime keeps the current config. Press/held log lines print '% <='. Vitals are logged at info level on the first fetch. config.json is generated on first run (no longer shipped in the archive). The game patch version (GameVersion) is written into offsets.json and carried in the archive name. English and Russian user documents. GitHub release upload, disable with --no-upload.
