#pragma once
// config.json schema, defaults, strict validation (SPEC §5). Pure module: parsing
// takes text, returns values + warnings; MessageBox/UI decisions live in main.cpp.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "log.h"

namespace poe2 {

struct VitalCfg {
    bool enabled = false;
    std::string key;      // plain key without modifiers (SPEC §5)
    int threshold = 50;   // 1..100
    int cooldownMs = 1000;
};

struct ScannerCfg {
    bool enabledOnStart = false;
    int pollIntervalMs = 50;         // 10..1000
    bool requireGameForeground = true;
    std::vector<std::string> processNames;
};

struct SoundCfg {
    int volume = 100;  // percent 0..100 (SPEC §5, backlog "Громкость WAV")
};

struct LogCfg {
    std::string level = "info";      // debug | info | warn | error
    std::string file = "logs/app.log";
    int maxSizeKb = 1024;
    int keepFiles = 3;
};

// Vital order matches SPEC §5: Health, Mana, EnergyShield, Ward, Rage.
enum VitalIndex { kHealth = 0, kMana, kEnergyShield, kWard, kRage, kVitalCount };
extern const char* const kVitalNames[kVitalCount];

struct Config {
    int configVersion = 1;
    std::string toggleHotkey = "Ctrl+9";
    ScannerCfg scanner;
    SoundCfg sound;
    std::array<VitalCfg, kVitalCount> vitals;
    LogCfg logging;
};

Config config_defaults();

struct ConfigResult {
    bool jsonBroken = false;    // bad JSON / root not object -> error log + MessageBox, run on defaults
    std::string parseError;     // JSON parse position included
    Config cfg;                 // defaults already applied; invalid fields reset to defaults
    std::vector<std::string> warnings;  // unknown/invalid keys (SPEC §5 rules)
};

// text: raw config.json content. Missing keys -> defaults; unknown keys -> warning
// + ignore; invalid values -> warning + default.
ConfigResult config_parse(const std::string& text);

// Pretty serialization in the SPEC §5 layout (first-run file creation, round-trip).
std::string config_serialize(const Config& cfg);

// Logger options derived from a validated config.
LogOptions log_options(const Config& cfg);

// ---- Hot-reload core (plan-hot-reload.md §1a; the 5 s timer loop lives in main.cpp) ----
// All pure: stamps and text are plain values passed in, so the branches are testable
// without files, Win32 or time. Stamp bookkeeping (when to advance prev) is main.cpp's job.

// File stamp: LAST_WRITE time + size. mtime is in 100 ns ticks (the FILETIME unit the
// app layer reads with GetFileTime; src/file_stamp.* is the Win32 side, stage 3 of the plan).
struct FileStamp {
    uint64_t mtime = 0;
    uint64_t size = 0;
};

// Changed if mtime OR size differs (plan §1a): a save-without-edits moves mtime; an edit
// inside one 100 ns tick keeps mtime and is caught by size.
bool stamp_changed(const FileStamp& prev, const FileStamp& cur);

// Paths that differ between two configs, in the canonical order of plan §2 (deterministic,
// one path per value, log-friendly): hotkeys.toggleScanner, scanner.{enabledOnStart,
// pollIntervalMs, requireGameForeground, processNames}, sound.volume, then
// vitals.<Name>.{enabled, key, threshold, cooldownMs} in kVitalNames order (Health, Mana,
// EnergyShield, Ward, Rage), logging.{level, file, maxSizeKb, keepFiles}, configVersion.
// Empty vector when the configs are equal; processNames is one path (the whole vector).
std::vector<std::string> config_diff(const Config& a, const Config& b);

// The live/restart table of plan §2. True = applies on the fly (next tick / re-register /
// set_options); false = restart-only (scanner.enabledOnStart, logging.file) and every
// other path, including the metadata configVersion (never applied - it lands in
// restart_changed as inert metadata when it differs). Unknown paths are false.
bool path_is_live(const std::string& path);

// One 5 s cycle decision, all branches of plan §1a.
struct ReloadOutcome {
    bool attempt = false;        // stamp changed and the file was read
    bool read_failed = false;    // stamp changed, reading failed (file locked by an editor)
    bool json_broken = false;    // bad JSON / root not object -> current config kept
    std::string error;           // parse string (position), for the error log
    Config next;                 // validated parse (defaults + warnings, SPEC §5 rules)
    std::vector<std::string> live_changed;     // paths to apply now (plan §2)
    std::vector<std::string> restart_changed;  // paths that need a restart
    std::vector<std::string> warnings;         // SPEC §5 rules (unknown/invalid)
};

// current: the running config; prev/cur: the file stamp before/after this cycle; text: raw
// file content (used only when read_ok). Branches: stamp unchanged -> attempt=false, all
// empty, next=current; read_ok=false -> read_failed=true, attempt stays false, next=current
// (main.cpp keeps prev, the next cycle repeats the read); broken JSON -> json_broken=true,
// error with the parse position, next=current (the app keeps running on the current config,
// plan §0); valid -> next=config_parse result, diff split into live_changed / restart_changed
// in canonical order, parse warnings reported.
ReloadOutcome config_reload(const Config& current, const FileStamp& prev, const FileStamp& cur,
                            const std::string& text, bool read_ok);

}  // namespace poe2
