#include "config.h"

#include <algorithm>
#include <cstdio>

#include "json.h"
#include "keys.h"

namespace poe2 {

const char* const kVitalNames[kVitalCount] = {"Health", "Mana", "EnergyShield", "Ward", "Rage"};

namespace {

// Validation ranges (SPEC §5; upper bounds are sanity caps).
constexpr int kThresholdMin = 1, kThresholdMax = 100;
constexpr int kCooldownMin = 0, kCooldownMax = 3600000;
constexpr int kPollMin = 10, kPollMax = 1000;
constexpr int kSizeKbMin = 1, kSizeKbMax = 100000;
constexpr int kKeepMin = 1, kKeepMax = 100;
constexpr int kVersionMin = 1, kVersionMax = 100;
constexpr int kVolumeMin = 0, kVolumeMax = 100;

void warn(std::vector<std::string>& w, const std::string& msg) { w.push_back(msg); }

// Field fetch: missing -> default (silent); invalid -> warning + default.
int int_field(const Json& obj, const char* key, int lo, int hi, int def,
              std::vector<std::string>& w, const std::string& path) {
    const Json* n = obj.find(key);
    if (!n) return def;
    if (n->is_int() && n->as_int() >= lo && n->as_int() <= hi) {
        return static_cast<int>(n->as_int());
    }
    char buf[256];
    std::snprintf(buf, sizeof buf, "invalid \"%s\" (allowed %d..%d), default %d used",
                  path.c_str(), lo, hi, def);
    warn(w, buf);
    return def;
}

bool bool_field(const Json& obj, const char* key, bool def,
               std::vector<std::string>& w, const std::string& path) {
    const Json* n = obj.find(key);
    if (!n) return def;
    if (n->is_bool()) return n->as_bool();
    warn(w, "invalid \"" + path + "\", default used");
    return def;
}

std::string string_field(const Json& obj, const char* key, const char* def,
                         std::vector<std::string>& w, const std::string& path) {
    const Json* n = obj.find(key);
    if (!n) return def;
    if (n->is_string() && !n->as_string().empty()) return n->as_string();
    warn(w, "invalid \"" + path + "\", default used");
    return def;
}

// Report members of obj not in known[] as unknown-key warnings.
void check_unknown(const Json& obj, const char* const* known, size_t count,
                   const std::string& prefix, std::vector<std::string>& w) {
    for (const auto& kv : obj.members()) {
        bool found = false;
        for (size_t i = 0; i < count; ++i) {
            if (kv.first == known[i]) { found = true; break; }
        }
        if (!found) {
            warn(w, "unknown key \"" + prefix + kv.first + "\" ignored");
        }
    }
}

// Live paths of the plan §2 table, complete enumeration (restart-only and unknown paths
// return false by absence, so configVersion - "metadata, not applied" - is not in the list).
const std::vector<std::string>& live_paths() {
    static const std::vector<std::string> table = [] {
        std::vector<std::string> t;
        t.push_back("hotkeys.toggleScanner");
        t.push_back("scanner.pollIntervalMs");
        t.push_back("scanner.requireGameForeground");
        t.push_back("scanner.processNames");
        t.push_back("sound.volume");
        for (size_t i = 0; i < kVitalCount; ++i) {
            for (const char* field : {"enabled", "key", "threshold", "cooldownMs"}) {
                t.push_back(std::string("vitals.") + kVitalNames[i] + "." + field);
            }
        }
        t.push_back("logging.level");
        t.push_back("logging.maxSizeKb");
        t.push_back("logging.keepFiles");
        return t;
    }();
    return table;
}

}  // namespace

Config config_defaults() {
    Config c;
    c.configVersion = 1;
    c.toggleHotkey = "Ctrl+9";
    c.scanner.enabledOnStart = false;
    c.scanner.pollIntervalMs = 50;
    c.scanner.requireGameForeground = true;
    c.scanner.processNames = {"PathOfExile.exe", "PathOfExile_x64.exe",
                              "PathOfExileSteam.exe", "PathOfExile_KG.exe"};
    c.sound.volume = 100;
    c.vitals[kHealth] = {true, "1", 50, 1000};
    c.vitals[kMana] = {true, "2", 40, 1000};
    c.vitals[kEnergyShield] = {false, "3", 35, 1000};
    c.vitals[kWard] = {false, "4", 30, 1000};
    c.vitals[kRage] = {false, "5", 20, 2000};
    c.logging.level = "info";
    c.logging.file = "logs/app.log";
    c.logging.maxSizeKb = 1024;
    c.logging.keepFiles = 3;
    return c;
}

ConfigResult config_parse(const std::string& text) {
    ConfigResult r;
    r.cfg = config_defaults();
    auto pr = Json::parse(text);
    if (!pr.ok) {
        r.jsonBroken = true;
        char buf[256];
        std::snprintf(buf, sizeof buf, "line %d, column %d: %s",
                      pr.error.line, pr.error.col, pr.error.msg.c_str());
        r.parseError = buf;
        return r;
    }
    if (!pr.value.is_object()) {
        r.jsonBroken = true;
        r.parseError = "root must be a JSON object";
        return r;
    }
    const Json& root = pr.value;

    r.cfg.configVersion = int_field(root, "configVersion", kVersionMin, kVersionMax,
                                    1, r.warnings, "configVersion");

    // hotkeys: only toggleScanner is used (SPEC §4.4). Missing section = defaults, silent.
    static const char* const kHotkeyKeys[] = {"toggleScanner"};
    const Json* hot = root.find("hotkeys");
    if (hot && !hot->is_object()) {
        warn(r.warnings, "invalid \"hotkeys\", defaults used");
    } else if (hot) {
        check_unknown(*hot, kHotkeyKeys, 1, "hotkeys.", r.warnings);
        const Json* hk = hot->find("toggleScanner");
        if (hk) {
            if (hk->is_string()) {
                auto res = parse_hotkey(hk->as_string());
                if (res.ok) r.cfg.toggleHotkey = hk->as_string();
                else {
                    warn(r.warnings, "invalid \"hotkeys.toggleScanner\" (" + res.error
                                     + "), default \"Ctrl+9\" used");
                }
            } else {
                warn(r.warnings, "invalid \"hotkeys.toggleScanner\", default \"Ctrl+9\" used");
            }
        }
    }

    // scanner. Missing section = defaults, silent.
    static const char* const kScannerKeys[] = {"enabledOnStart", "pollIntervalMs",
                                               "requireGameForeground", "processNames"};
    const Json* sc = root.find("scanner");
    if (sc && !sc->is_object()) {
        warn(r.warnings, "invalid \"scanner\", defaults used");
    } else if (sc) {
        check_unknown(*sc, kScannerKeys, 4, "scanner.", r.warnings);
        r.cfg.scanner.enabledOnStart = bool_field(*sc, "enabledOnStart", false,
                                                  r.warnings, "scanner.enabledOnStart");
        r.cfg.scanner.pollIntervalMs = int_field(*sc, "pollIntervalMs", kPollMin, kPollMax,
                                                  50, r.warnings, "scanner.pollIntervalMs");
        r.cfg.scanner.requireGameForeground = bool_field(*sc, "requireGameForeground", true,
                                                         r.warnings, "scanner.requireGameForeground");
        const Json* pn = sc->find("processNames");
        if (pn) {
            bool ok = pn->is_array() && !pn->array_items().empty();
            if (ok) {
                for (const auto& el : pn->array_items()) {
                    if (!el.is_string() || el.as_string().empty()) { ok = false; break; }
                }
            }
            if (ok) {
                r.cfg.scanner.processNames.clear();
                for (const auto& el : pn->array_items()) {
                    r.cfg.scanner.processNames.push_back(el.as_string());
                }
            } else {
                warn(r.warnings, "invalid \"scanner.processNames\", defaults used");
            }
        }
    }

    // sound. Missing section = defaults, silent (SPEC §5, backlog "Громкость WAV").
    static const char* const kSoundKeys[] = {"volume"};
    const Json* sn = root.find("sound");
    if (sn && !sn->is_object()) {
        warn(r.warnings, "invalid \"sound\", defaults used");
    } else if (sn) {
        check_unknown(*sn, kSoundKeys, 1, "sound.", r.warnings);
        r.cfg.sound.volume = int_field(*sn, "volume", kVolumeMin, kVolumeMax,
                                       100, r.warnings, "sound.volume");
    }

    // vitals. Missing section = defaults, silent.
    const Json* vt = root.find("vitals");
    if (vt && !vt->is_object()) {
        warn(r.warnings, "invalid \"vitals\", defaults used");
    } else if (vt) {
        for (size_t i = 0; i < kVitalCount; ++i) {
            const std::string name = kVitalNames[i];
            const Json* v = vt->find(name);
            if (!v) continue;  // missing vital block -> its defaults, silent
            if (!v->is_object()) {
                warn(r.warnings, "invalid \"vitals." + name + "\", defaults used");
                continue;
            }
            static const char* const kVitalKeys[] = {"enabled", "key", "threshold", "cooldownMs"};
            check_unknown(*v, kVitalKeys, 4, "vitals." + name + ".", r.warnings);
            VitalCfg& d = r.cfg.vitals[i];
            d.enabled = bool_field(*v, "enabled", d.enabled, r.warnings, "vitals." + name + ".enabled");
            d.threshold = int_field(*v, "threshold", kThresholdMin, kThresholdMax,
                                    d.threshold, r.warnings, "vitals." + name + ".threshold");
            d.cooldownMs = int_field(*v, "cooldownMs", kCooldownMin, kCooldownMax,
                                     d.cooldownMs, r.warnings, "vitals." + name + ".cooldownMs");
            const Json* key = v->find("key");
            if (key) {
                if (key->is_string()) {
                    auto res = parse_key(key->as_string());
                    if (res.ok) d.key = key->as_string();
                    else {
                        warn(r.warnings, "invalid \"vitals." + name + ".key\" (" + res.error
                                         + "), default \"" + d.key + "\" used");
                    }
                } else {
                    warn(r.warnings, "invalid \"vitals." + name + ".key\", default used");
                }
            }
        }
        // unknown vital names (S1-8: one name table, kVitalNames, not a second literal)
        check_unknown(*vt, kVitalNames, kVitalCount, "vitals.", r.warnings);
    }

    // logging. Missing section = defaults, silent.
    static const char* const kLogKeys[] = {"level", "file", "maxSizeKb", "keepFiles"};
    const Json* lg = root.find("logging");
    if (lg && !lg->is_object()) {
        warn(r.warnings, "invalid \"logging\", defaults used");
    } else if (lg) {
        check_unknown(*lg, kLogKeys, 4, "logging.", r.warnings);
        const Json* lvl = lg->find("level");
        if (lvl) {
            LogLevel out;
            if (lvl->is_string() && level_from_string(lvl->as_string(), out)) {
                r.cfg.logging.level = lvl->as_string();
            } else {
                warn(r.warnings, "invalid \"logging.level\", default \"info\" used");
            }
        }
        r.cfg.logging.file = string_field(*lg, "file", "logs/app.log",
                                          r.warnings, "logging.file");
        r.cfg.logging.maxSizeKb = int_field(*lg, "maxSizeKb", kSizeKbMin, kSizeKbMax,
                                            1024, r.warnings, "logging.maxSizeKb");
        r.cfg.logging.keepFiles = int_field(*lg, "keepFiles", kKeepMin, kKeepMax,
                                            3, r.warnings, "logging.keepFiles");
    }

    static const char* const kRootKeys[] = {"configVersion", "hotkeys", "scanner",
                                            "sound", "vitals", "logging"};
    check_unknown(root, kRootKeys, 6, "", r.warnings);
    return r;
}

std::string config_serialize(const Config& cfg) {
    Json root = Json::object();
    root.add("configVersion", Json::integer(cfg.configVersion));

    Json hk = Json::object();
    hk.add("toggleScanner", Json::string(cfg.toggleHotkey));
    root.add("hotkeys", std::move(hk));

    Json sc = Json::object();
    sc.add("enabledOnStart", Json::boolean(cfg.scanner.enabledOnStart));
    sc.add("pollIntervalMs", Json::integer(cfg.scanner.pollIntervalMs));
    sc.add("requireGameForeground", Json::boolean(cfg.scanner.requireGameForeground));
    Json pn = Json::array();
    for (const auto& name : cfg.scanner.processNames) {
        pn.push_back(Json::string(name));
    }
    sc.add("processNames", std::move(pn));
    root.add("scanner", std::move(sc));

    Json snd = Json::object();
    snd.add("volume", Json::integer(cfg.sound.volume));
    root.add("sound", std::move(snd));

    Json vitals = Json::object();
    for (size_t i = 0; i < kVitalCount; ++i) {
        Json v = Json::object();
        const VitalCfg& d = cfg.vitals[i];
        v.add("enabled", Json::boolean(d.enabled));
        v.add("key", Json::string(d.key));
        v.add("threshold", Json::integer(d.threshold));
        v.add("cooldownMs", Json::integer(d.cooldownMs));
        vitals.add(kVitalNames[i], std::move(v));
    }
    root.add("vitals", std::move(vitals));

    Json lg = Json::object();
    lg.add("level", Json::string(cfg.logging.level));
    lg.add("file", Json::string(cfg.logging.file));
    lg.add("maxSizeKb", Json::integer(cfg.logging.maxSizeKb));
    lg.add("keepFiles", Json::integer(cfg.logging.keepFiles));
    root.add("logging", std::move(lg));

    return root.serialize(true);
}

LogOptions log_options(const Config& cfg) {
    LogOptions o;
    LogLevel lvl;
    o.min_level = level_from_string(cfg.logging.level, lvl) ? lvl : LogLevel::Info;
    o.max_size_kb = cfg.logging.maxSizeKb;
    o.keep_files = cfg.logging.keepFiles;
    return o;
}

// ---- Hot-reload core (plan-hot-reload.md §1a) ----

bool stamp_changed(const FileStamp& prev, const FileStamp& cur) {
    return prev.mtime != cur.mtime || prev.size != cur.size;
}

std::vector<std::string> config_diff(const Config& a, const Config& b) {
    std::vector<std::string> out;
    // Canonical order (plan §2 table): each path emitted once, in the header order.
    auto add = [&](std::string path, bool changed) {
        if (changed) out.push_back(std::move(path));
    };
    add("hotkeys.toggleScanner", a.toggleHotkey != b.toggleHotkey);
    add("scanner.enabledOnStart", a.scanner.enabledOnStart != b.scanner.enabledOnStart);
    add("scanner.pollIntervalMs", a.scanner.pollIntervalMs != b.scanner.pollIntervalMs);
    add("scanner.requireGameForeground",
        a.scanner.requireGameForeground != b.scanner.requireGameForeground);
    add("scanner.processNames", a.scanner.processNames != b.scanner.processNames);
    add("sound.volume", a.sound.volume != b.sound.volume);
    for (size_t i = 0; i < kVitalCount; ++i) {
        const std::string base = std::string("vitals.") + kVitalNames[i];
        const VitalCfg& x = a.vitals[i];
        const VitalCfg& y = b.vitals[i];
        add(base + ".enabled", x.enabled != y.enabled);
        add(base + ".key", x.key != y.key);
        add(base + ".threshold", x.threshold != y.threshold);
        add(base + ".cooldownMs", x.cooldownMs != y.cooldownMs);
    }
    add("logging.level", a.logging.level != b.logging.level);
    add("logging.file", a.logging.file != b.logging.file);
    add("logging.maxSizeKb", a.logging.maxSizeKb != b.logging.maxSizeKb);
    add("logging.keepFiles", a.logging.keepFiles != b.logging.keepFiles);
    add("configVersion", a.configVersion != b.configVersion);
    return out;
}

bool path_is_live(const std::string& path) {
    const std::vector<std::string>& t = live_paths();
    return std::find(t.begin(), t.end(), path) != t.end();
}

ReloadOutcome config_reload(const Config& current, const FileStamp& prev, const FileStamp& cur,
                            const std::string& text, bool read_ok) {
    ReloadOutcome o;
    o.next = current;  // every non-success branch keeps the running config (plan §0)
    if (!stamp_changed(prev, cur)) return o;  // nothing happened this cycle: all empty
    if (!read_ok) {  // file locked (exclusive editor): silent, main.cpp warns and retries
        o.read_failed = true;
        return o;  // attempt needs the file READ, not just the stamp moving
    }
    o.attempt = true;
    auto r = config_parse(text);
    if (r.jsonBroken) {  // app keeps running on current; the error line is for the log
        o.json_broken = true;
        o.error = r.parseError;  // "line N, column M: msg" (config_parse position format)
        return o;  // next stays = current; diff and warnings stay empty
    }
    o.next = std::move(r.cfg);  // defaults + warnings per SPEC §5
    o.warnings = std::move(r.warnings);
    for (const std::string& path : config_diff(current, o.next)) {
        if (path_is_live(path)) o.live_changed.push_back(path);
        else o.restart_changed.push_back(path);  // restart-only + inert metadata
    }
    return o;
}

}  // namespace poe2
