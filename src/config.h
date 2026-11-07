#pragma once
// config.json schema, defaults, strict validation (SPEC §5). Pure module: parsing
// takes text, returns values + warnings; MessageBox/UI decisions live in main.cpp.

#include <array>
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

}  // namespace poe2
