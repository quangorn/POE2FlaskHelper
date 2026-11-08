// config.json validation tests (SPEC §5, plan.md §3 Stage 1).
#include "test.h"

#include <string>

#include "app.h"
#include "config.h"
#include "test_tmp.h"

using poe2::Config;
using poe2::config_defaults;
using poe2::config_parse;
using poe2::config_serialize;

static bool cfg_eq(const Config& a, const Config& b) {
    if (a.configVersion != b.configVersion || a.toggleHotkey != b.toggleHotkey) return false;
    if (a.scanner.enabledOnStart != b.scanner.enabledOnStart
        || a.scanner.pollIntervalMs != b.scanner.pollIntervalMs
        || a.scanner.requireGameForeground != b.scanner.requireGameForeground
        || a.scanner.processNames != b.scanner.processNames) return false;
    for (int i = 0; i < poe2::kVitalCount; ++i) {
        const poe2::VitalCfg& x = a.vitals[i];
        const poe2::VitalCfg& y = b.vitals[i];
        if (x.enabled != y.enabled || x.key != y.key
            || x.threshold != y.threshold || x.cooldownMs != y.cooldownMs) return false;
    }
    return a.logging.level == b.logging.level && a.logging.file == b.logging.file
        && a.logging.maxSizeKb == b.logging.maxSizeKb && a.logging.keepFiles == b.logging.keepFiles
        && a.sound.volume == b.sound.volume;
}

static const char* kSpecConfig = R"(
{
  "configVersion": 1,
  "hotkeys": { "toggleScanner": "Ctrl+9" },
  "scanner": {
    "enabledOnStart": false,
    "pollIntervalMs": 50,
    "requireGameForeground": true,
    "processNames": ["PathOfExile.exe", "PathOfExile_x64.exe", "PathOfExileSteam.exe", "PathOfExile_KG.exe"]
  },
  "sound": { "volume": 25 },
  "vitals": {
    "Health":       { "enabled": true,  "key": "1", "threshold": 50, "cooldownMs": 1000 },
    "Mana":         { "enabled": true,  "key": "2", "threshold": 40, "cooldownMs": 1000 },
    "EnergyShield": { "enabled": false, "key": "3", "threshold": 35, "cooldownMs": 1000 },
    "Ward":         { "enabled": false, "key": "4", "threshold": 30, "cooldownMs": 1000 },
    "Rage":         { "enabled": false, "key": "5", "threshold": 20, "cooldownMs": 2000 }
  },
  "logging": { "level": "info", "file": "logs/app.log", "maxSizeKb": 1024, "keepFiles": 3 }
})";

TEST(config_spec_file_defaults) {
    auto r = config_parse(kSpecConfig);
    CHECK(!r.jsonBroken);
    CHECK_EQ(r.warnings.size(), 0u);
    const Config& c = r.cfg;
    CHECK_EQ(c.configVersion, 1);
    CHECK_EQ(c.toggleHotkey, std::string("Ctrl+9"));
    CHECK_EQ(c.scanner.pollIntervalMs, 50);
    CHECK(c.scanner.enabledOnStart == false);
    CHECK(c.scanner.requireGameForeground == true);
    CHECK_EQ(c.scanner.processNames.size(), 4u);
    CHECK_EQ(c.scanner.processNames[0], std::string("PathOfExile.exe"));
    CHECK_EQ(c.vitals[poe2::kHealth].threshold, 50);
    CHECK(c.vitals[poe2::kHealth].enabled == true);
    CHECK_EQ(c.vitals[poe2::kHealth].key, std::string("1"));
    CHECK_EQ(c.vitals[poe2::kMana].threshold, 40);
    CHECK_EQ(c.vitals[poe2::kEnergyShield].threshold, 35);
    CHECK_EQ(c.vitals[poe2::kWard].threshold, 30);
    CHECK_EQ(c.vitals[poe2::kRage].threshold, 20);
    CHECK_EQ(c.vitals[poe2::kRage].cooldownMs, 2000);
    CHECK_EQ(c.logging.level, std::string("info"));
    CHECK_EQ(c.logging.file, std::string("logs/app.log"));
    CHECK_EQ(c.logging.maxSizeKb, 1024);
    CHECK_EQ(c.logging.keepFiles, 3);
    CHECK_EQ(c.sound.volume, 25);  // the repo config.json value (SPEC §5)
}

TEST(config_missing_keys_defaults) {
    auto r = config_parse("{}");
    CHECK(!r.jsonBroken);
    CHECK_EQ(r.warnings.size(), 0u);  // missing sections are silent, defaults apply
    CHECK(cfg_eq(r.cfg, config_defaults()));  // all sections untouched = defaults
    auto one = config_parse(R"({"scanner":{"pollIntervalMs":100}})");
    CHECK_EQ(one.warnings.size(), 0u);
    CHECK_EQ(one.cfg.scanner.pollIntervalMs, 100);
    CHECK_EQ(one.cfg.vitals[poe2::kHealth].threshold, 50);  // rest stays default
}

TEST(config_extra_keys_warn_ignored) {
    auto r = config_parse(R"({"foo":1,"bar":2,
        "hotkeys":{"x":1},"logging":{"y":2},"vitals":{"Fire":{}}})");
    CHECK(!r.jsonBroken);
    CHECK_EQ(r.warnings.size(), 5u);
    // Values stay at defaults: unknown keys must not change anything.
    CHECK_EQ(r.cfg.scanner.pollIntervalMs, 50);
    CHECK_EQ(r.cfg.logging.maxSizeKb, 1024);
    bool all_unknown = true;
    for (const auto& w : r.warnings) {
        if (w.find("unknown key") == std::string::npos) all_unknown = false;
    }
    CHECK(all_unknown);
}

TEST(config_invalid_values_reset_to_default) {
    auto r = config_parse(R"({
        "scanner":{"pollIntervalMs":9},
        "hotkeys":{"toggleScanner":"9"},
        "vitals":{"Health":{"threshold":0},"Mana":{"threshold":101},
                  "EnergyShield":{"cooldownMs":-1,"key":"Ctrl+3"}}})");
    CHECK(!r.jsonBroken);
    CHECK_EQ(r.cfg.scanner.pollIntervalMs, 50);   // 9 out of 10..1000 -> default
    CHECK_EQ(r.cfg.toggleHotkey, std::string("Ctrl+9"));  // no modifier -> default
    CHECK_EQ(r.cfg.vitals[poe2::kHealth].threshold, 50);  // 0 out of 1..100
    CHECK_EQ(r.cfg.vitals[poe2::kMana].threshold, 40);    // 101 out of 1..100
    CHECK_EQ(r.cfg.vitals[poe2::kEnergyShield].cooldownMs, 1000);
    CHECK_EQ(r.cfg.vitals[poe2::kEnergyShield].key, std::string("3"));
    CHECK_EQ(r.warnings.size(), 6u);  // poll, hotkey, threshold x2, cooldown, key
}

TEST(config_boundary_values_valid) {
    auto r = config_parse(R"({
        "scanner":{"pollIntervalMs":10},
        "vitals":{"Health":{"threshold":1,"cooldownMs":0},
                  "Mana":{"threshold":100}}})");
    CHECK(!r.jsonBroken);
    CHECK_EQ(r.warnings.size(), 0u);
    CHECK_EQ(r.cfg.scanner.pollIntervalMs, 10);
    CHECK_EQ(r.cfg.vitals[poe2::kHealth].threshold, 1);
    CHECK_EQ(r.cfg.vitals[poe2::kHealth].cooldownMs, 0);
    CHECK_EQ(r.cfg.vitals[poe2::kMana].threshold, 100);
    auto hi = config_parse(R"({"scanner":{"pollIntervalMs":1000}})");
    CHECK_EQ(hi.warnings.size(), 0u);
    CHECK_EQ(hi.cfg.scanner.pollIntervalMs, 1000);
}

TEST(config_type_mismatch_defaults) {
    auto r = config_parse(R"({"scanner":{"pollIntervalMs":"50","enabledOnStart":1},
        "logging":{"maxSizeKb":0,"keepFiles":0,"level":"trace"}})");
    CHECK(!r.jsonBroken);
    CHECK_EQ(r.cfg.scanner.pollIntervalMs, 50);
    CHECK_EQ(r.cfg.logging.maxSizeKb, 1024);
    CHECK_EQ(r.cfg.logging.keepFiles, 3);
    CHECK_EQ(r.cfg.logging.level, std::string("info"));
    CHECK_EQ(r.warnings.size(), 5u);
}

// S1-9: a JSON Real in an integer field ({"threshold":50.0}) is not valid for the
// current implementation: SPEC §5 lists only range/type rules, int_field requires
// is_int(), so the float takes the warning + default path. Pinned here so the
// behavior cannot silently change.
TEST(config_float_at_int_field_rejected) {
    auto r = config_parse(R"({
        "configVersion":2.0,
        "scanner":{"pollIntervalMs":100.0},
        "vitals":{"Mana":{"threshold":50.0,"cooldownMs":2000.0}},
        "logging":{"maxSizeKb":2048.0,"keepFiles":5.0}})");
    CHECK(!r.jsonBroken);  // valid JSON, the fields are just not integers
    CHECK_EQ(r.warnings.size(), 6u);  // configVersion, poll, threshold, cooldown, maxSizeKb, keepFiles
    CHECK_EQ(r.cfg.configVersion, 1);        // 2.0 -> default
    CHECK_EQ(r.cfg.scanner.pollIntervalMs, 50);
    CHECK_EQ(r.cfg.vitals[poe2::kHealth].threshold, 50);   // untouched
    CHECK_EQ(r.cfg.vitals[poe2::kMana].threshold, 40);     // 50.0 -> default
    CHECK_EQ(r.cfg.vitals[poe2::kMana].cooldownMs, 1000);  // 2000.0 -> default
    CHECK_EQ(r.cfg.logging.maxSizeKb, 1024);
    CHECK_EQ(r.cfg.logging.keepFiles, 3);
    // The warning is the standard invalid-value text, with the allowed range.
    bool all_invalid = true;
    for (const auto& w : r.warnings) {
        if (w.find("invalid") == std::string::npos) all_invalid = false;
        if (w.find("default") == std::string::npos) all_invalid = false;
    }
    CHECK(all_invalid);

    // Exponent form is also a Real: rejected the same way (no int coercion).
    auto e = config_parse(R"({"scanner":{"pollIntervalMs":1e2}})");
    CHECK_EQ(e.warnings.size(), 1u);
    CHECK_EQ(e.cfg.scanner.pollIntervalMs, 50);

    // An integer value in the same field is accepted: the rule is type, not spelling.
    auto i = config_parse(R"({"scanner":{"pollIntervalMs":100}})");
    CHECK_EQ(i.warnings.size(), 0u);
    CHECK_EQ(i.cfg.scanner.pollIntervalMs, 100);
}

TEST(config_broken_json_runs_on_defaults) {
    auto r = config_parse("{ config.json");
    CHECK(r.jsonBroken);
    CHECK(!r.parseError.empty());
    CHECK(cfg_eq(r.cfg, config_defaults()));  // defaults, untouched
    auto nonobject = config_parse("42");
    CHECK(nonobject.jsonBroken);  // valid JSON, not the schema
}

TEST(config_sound_volume) {
    // SPEC §5 rules for sound.volume (backlog "Громкость WAV").
    auto r = config_parse(R"({"sound":{"volume":25}})");
    CHECK(!r.jsonBroken);
    CHECK_EQ(r.warnings.size(), 0u);
    CHECK_EQ(r.cfg.sound.volume, 25);
    auto def = config_parse("{}");  // missing section is silent, default 100
    CHECK_EQ(def.cfg.sound.volume, 100);
    auto bad = config_parse(R"({"sound":{"volume":101}})");     // out of 0..100
    CHECK_EQ(bad.cfg.sound.volume, 100);
    CHECK_EQ(bad.warnings.size(), 1u);
    auto type = config_parse(R"({"sound":{"volume":"25"}})");   // string is not an int
    CHECK_EQ(type.cfg.sound.volume, 100);
    CHECK_EQ(type.warnings.size(), 1u);
    auto zero = config_parse(R"({"sound":{"volume":0}})");      // 0 is a valid boundary
    CHECK_EQ(zero.warnings.size(), 0u);
    CHECK_EQ(zero.cfg.sound.volume, 0);
    auto unk = config_parse(R"({"sound":{"mute":true}})");      // unknown key of the section
    CHECK_EQ(unk.warnings.size(), 1u);
    CHECK_EQ(unk.cfg.sound.volume, 100);
    auto sec = config_parse(R"({"sound":5})");                  // section is not an object
    CHECK_EQ(sec.warnings.size(), 1u);
    CHECK_EQ(sec.cfg.sound.volume, 100);
    // Round-trip: sound survives serialize (the first-run file layout).
    Config c = config_defaults();
    c.sound.volume = 25;
    auto rt = config_parse(config_serialize(c));
    CHECK_EQ(rt.warnings.size(), 0u);
    CHECK(cfg_eq(rt.cfg, c));
}

TEST(config_first_run_creates_default_file) {
    namespace fs = std::filesystem;
    fs::path dir = poe2::testing::tmp_dir("config_first_run");
    fs::path cfg = dir / "config.json";
    // Same calls as main.cpp first-run path: serialize defaults, write, read back.
    std::string def = config_serialize(config_defaults()) + "\n";
    CHECK(poe2::write_text_file(cfg.string(), def));
    CHECK(fs::exists(cfg));
    std::string text;
    CHECK(poe2::read_text_file(cfg.string(), text));
    auto r = config_parse(text);
    CHECK(!r.jsonBroken);
    CHECK_EQ(r.warnings.size(), 0u);
    CHECK_EQ(r.cfg.scanner.pollIntervalMs, 50);
    CHECK_EQ(r.cfg.toggleHotkey, std::string("Ctrl+9"));
    // Round-trip: serialized defaults re-parse to the identical config.
    CHECK_EQ(config_serialize(r.cfg), config_serialize(config_defaults()));
}

TEST(config_serialize_round_trip_modified) {
    Config c = config_defaults();
    c.toggleHotkey = "Shift+Alt+F5";
    c.scanner.pollIntervalMs = 100;
    c.vitals[poe2::kHealth].threshold = 7;
    c.logging.level = "debug";
    auto r = config_parse(config_serialize(c));
    CHECK(!r.jsonBroken);
    CHECK_EQ(r.warnings.size(), 0u);
    CHECK_EQ(r.cfg.toggleHotkey, std::string("Shift+Alt+F5"));
    CHECK_EQ(r.cfg.scanner.pollIntervalMs, 100);
    CHECK_EQ(r.cfg.vitals[poe2::kHealth].threshold, 7);
    CHECK_EQ(r.cfg.logging.level, std::string("debug"));
}
