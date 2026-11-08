// Hot-reload tests (plan-hot-reload.md §4, groups 1-3 of stage 1 + groups 4-5 of stage 2):
// stamp_changed, config_diff canonical order, the plan §2 live/restart table, every
// config_reload branch, ConfigSnapshot set/get + snapshot isolation, and live application
// through ScannerSession::step() on the fakes of session_fixture.h. Deterministic: no
// files, no POE, no Win32, no sleeps, no real time - stamps and text are plain values
// passed into the pure core, the scanner machine runs on FakeMemory/FakeProcess/FakeClock
// (the 5 s timer handler is main.cpp, plan §3).
#include "test.h"

#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "config_snapshot.h"
#include "scanner_ctl.h"
#include "scanner_session.h"
#include "session_fixture.h"
#include "state.h"
// FakeMemory/FakeProcess/synthetic_layout arrive through session_fixture.h - the direct
// includes were redundant (review minor m7, closed 2026-11-08).

using poe2::Config;
using poe2::config_defaults;
using poe2::config_diff;
using poe2::config_parse;
using poe2::config_reload;
using poe2::config_serialize;
using poe2::ConfigSnapshot;
using poe2::FileStamp;
using poe2::path_is_live;
using poe2::Phase;
using poe2::ReloadOutcome;
using poe2::ScannerCfg;
using poe2::ScannerCtl;
using poe2::stamp_changed;
using poe2::testing::Fix;

namespace {

// FileStamp baseline for the config_reload groups (plan §1a: mtime in 100 ns ticks + size):
// prev is the last stamp the app saw, moved stamps are built as kPrevTick + n. Named
// constants close the review minor m4 (the bare {100,500} literals).
constexpr uint64_t kPrevTick = 100, kPrevSize = 500;

Phase phase_of(const ScannerCtl& ctl) { return static_cast<Phase>(ctl.phase.load()); }

}  // namespace

// Group 1 (plan §4): the four stamp-change combinations + the zero baseline.
TEST(stamp_changed_matrix) {
    const FileStamp a{1000000000ULL, 500};  // LAST_WRITE ticks + size
    CHECK(!stamp_changed(a, a));            // identical stamp: nothing to do this cycle
    CHECK(stamp_changed(a, FileStamp{1000000001ULL, 500}));  // mtime only (save without edits)
    CHECK(stamp_changed(a, FileStamp{1000000000ULL, 501}));  // size only (edit inside one tick)
    CHECK(stamp_changed(a, FileStamp{1000000001ULL, 501}));  // both
    // The zero stamp (first-run baseline, no file read yet) behaves the same way.
    CHECK(!stamp_changed(FileStamp{0, 0}, FileStamp{0, 0}));
    CHECK(stamp_changed(FileStamp{0, 0}, FileStamp{0, 1}));
    CHECK(stamp_changed(FileStamp{0, 0}, FileStamp{1, 0}));
}

// Group 2: config_diff empty on equality, canonical order, symmetry, processNames as one path.
TEST(config_diff_empty_on_equal) {
    CHECK(config_diff(config_defaults(), config_defaults()).empty());
    Config c = config_defaults();
    c.sound.volume = 25;
    // serialize/parse round-trip is equality: no diff.
    CHECK(config_diff(c, config_parse(config_serialize(c)).cfg).empty());
    CHECK(config_diff(config_defaults(), c) == std::vector<std::string>{"sound.volume"});
}

TEST(config_diff_canonical_order_full) {
    Config b = config_defaults();
    b.toggleHotkey = "Ctrl+8";
    b.scanner.enabledOnStart = true;
    b.scanner.pollIntervalMs = 100;
    b.scanner.requireGameForeground = false;
    b.scanner.processNames = {"Game.exe"};
    b.sound.volume = 30;
    for (int i = 0; i < poe2::kVitalCount; ++i) {
        b.vitals[i].enabled = !b.vitals[i].enabled;
        b.vitals[i].key = std::string("F") + std::to_string(i + 1);
        b.vitals[i].threshold = i * 7 + 1;          // all differ from the defaults
        b.vitals[i].cooldownMs = (i == poe2::kRage) ? 1000 : 2000;  // default Rage is 2000
    }
    b.logging.level = "debug";
    b.logging.file = "logs/other.log";
    b.logging.maxSizeKb = 2048;
    b.logging.keepFiles = 5;
    b.configVersion = 2;
    const std::vector<std::string> want = {
        "hotkeys.toggleScanner",
        "scanner.enabledOnStart", "scanner.pollIntervalMs",
        "scanner.requireGameForeground", "scanner.processNames",
        "sound.volume",
        "vitals.Health.enabled", "vitals.Health.key",
        "vitals.Health.threshold", "vitals.Health.cooldownMs",
        "vitals.Mana.enabled", "vitals.Mana.key",
        "vitals.Mana.threshold", "vitals.Mana.cooldownMs",
        "vitals.EnergyShield.enabled", "vitals.EnergyShield.key",
        "vitals.EnergyShield.threshold", "vitals.EnergyShield.cooldownMs",
        "vitals.Ward.enabled", "vitals.Ward.key",
        "vitals.Ward.threshold", "vitals.Ward.cooldownMs",
        "vitals.Rage.enabled", "vitals.Rage.key",
        "vitals.Rage.threshold", "vitals.Rage.cooldownMs",
        "logging.level", "logging.file", "logging.maxSizeKb", "logging.keepFiles",
        "configVersion",
    };
    CHECK_EQ(config_diff(config_defaults(), b), want);
    CHECK_EQ(config_diff(b, config_defaults()), want);  // order is the table, not a/b direction
}

TEST(config_diff_partial_paths) {
    Config a = config_defaults();
    Config b = config_defaults();
    b.sound.volume = 25;
    b.vitals[poe2::kHealth].threshold = 60;
    // Canonical table order: sound.volume before vitals.
    const std::vector<std::string> want1{"sound.volume", "vitals.Health.threshold"};
    CHECK_EQ(config_diff(a, b), want1);
    Config c = config_defaults();
    c.scanner.processNames.push_back("Extra.exe");  // the vector is ONE path
    const std::vector<std::string> want2{"scanner.processNames"};
    CHECK_EQ(config_diff(a, c), want2);
}

// Group 2: the plan §2 table - live paths, restart-only, unknown.
TEST(path_is_live_table) {
    const std::vector<std::string> base_live = {
        "hotkeys.toggleScanner",
        "scanner.pollIntervalMs", "scanner.requireGameForeground", "scanner.processNames",
        "sound.volume",
        "logging.level", "logging.maxSizeKb", "logging.keepFiles",
    };
    std::vector<std::string> live = base_live;
    for (int i = 0; i < poe2::kVitalCount; ++i) {
        const std::string base = std::string("vitals.") + poe2::kVitalNames[i];
        for (const char* f : {"enabled", "key", "threshold", "cooldownMs"}) {
            live.push_back(base + "." + f);
        }
    }
    for (const auto& p : live) CHECK(path_is_live(p));
    // restart-only (plan §2): scanner.enabledOnStart, logging.file; configVersion is
    // "metadata, not applied" -> false (it lands in restart_changed as inert metadata).
    CHECK(!path_is_live("scanner.enabledOnStart"));
    CHECK(!path_is_live("logging.file"));
    CHECK(!path_is_live("configVersion"));
    // Unknown / section-level paths are false.
    for (const char* p : {"", "hotkeys", "scanner", "sound", "vitals", "logging",
                          "vitals.Health", "vitals.Fire.enabled", "scanner.pollIntervalsec",
                          "hotkeys.toggle", "logging.levelx"}) {
        CHECK(!path_is_live(p));
    }
}

// Group 3: config_reload branches.
TEST(config_reload_stamp_unchanged) {
    Config cur = config_defaults();
    FileStamp prev{kPrevTick, kPrevSize};
    // Valid text is present, the stamp did not move: the cycle does nothing.
    auto o = config_reload(cur, prev, prev, R"({"sound":{"volume":25}})", true);
    CHECK(!o.attempt);
    CHECK(!o.read_failed);
    CHECK(!o.json_broken);
    CHECK(!o.invalid);
    CHECK(o.error.empty());
    CHECK(o.live_changed.empty());
    CHECK(o.restart_changed.empty());
    CHECK(o.warnings.empty());
    CHECK(config_diff(o.next, cur).empty());  // next = current, untouched
}

TEST(config_reload_read_failed) {
    Config cur = config_defaults();
    cur.sound.volume = 25;
    FileStamp prev{kPrevTick, kPrevSize}, moved{kPrevTick + 1, kPrevSize + 1};
    auto o = config_reload(cur, prev, moved, "", false);  // file locked by an editor (plan §3 step 2)
    CHECK(o.read_failed);
    CHECK(!o.attempt);        // attempt needs the file READ, not just the stamp moved
    CHECK(!o.json_broken);
    CHECK(o.error.empty());
    CHECK(o.live_changed.empty() && o.restart_changed.empty() && o.warnings.empty());
    CHECK(config_diff(o.next, cur).empty());  // running config untouched
}

// Group 3 (review minor m3, closed 2026-11-08): the stamp moved, the file WAS read, and the
// text is EMPTY (file truncated to zero bytes). plan §3 step 2 is the failed-READ branch;
// this is the successful-read-of-empty-text branch: config_parse("") has no root, so the
// outcome is json_broken and the running config survives exactly like a broken file (plan §0).
TEST(config_reload_empty_text_is_broken) {
    Config cur = config_defaults();
    cur.sound.volume = 25;
    FileStamp prev{kPrevTick, kPrevSize}, moved{kPrevTick + 1, 0};  // size dropped to 0
    auto o = config_reload(cur, prev, moved, "", true);
    CHECK(o.attempt && o.json_broken && !o.read_failed);
    CHECK_EQ(o.error, std::string("line 1, column 1: unexpected end of input"));
    CHECK(config_diff(o.next, cur).empty());  // app keeps running on the current config
    CHECK(o.live_changed.empty() && o.restart_changed.empty() && o.warnings.empty());
}

TEST(config_reload_json_broken_keeps_current) {
    Config cur = config_defaults();
    cur.sound.volume = 25;  // NOT the defaults: the running config must survive a broken file
    FileStamp prev{kPrevTick, kPrevSize}, moved{kPrevTick + 1, 480};
    // Missing closing brace -> "unterminated object" at line 3, column 1 (json.cpp position).
    auto o = config_reload(cur, prev, moved, "{\n  \"scanner\": {\"pollIntervalMs\": 100\n", true);
    CHECK(o.attempt);
    CHECK(o.json_broken);
    CHECK(!o.read_failed);
    CHECK_EQ(o.error, std::string("line 3, column 1: unterminated object"));  // position included
    CHECK(config_diff(o.next, cur).empty());  // current kept, not reset to defaults
    CHECK(o.live_changed.empty() && o.restart_changed.empty() && o.warnings.empty());
    // Root not an object: valid JSON, not the schema -> broken too.
    auto n = config_reload(cur, prev, moved, "42", true);
    CHECK(n.attempt && n.json_broken);
    CHECK_EQ(n.error, std::string("root must be a JSON object"));
    CHECK(config_diff(n.next, cur).empty());
}

TEST(config_reload_warnings) {
    Config cur = config_defaults();
    FileStamp prev{kPrevTick, kPrevSize}, moved{kPrevTick + 1, 600};
    // Valid change: the change applies, no warnings (plan-release.md item 2 moved the
    // unknown-key case to the invalid branch, see config_reload_invalid_unknown_key).
    auto o = config_reload(cur, prev, moved, R"({"scanner":{"pollIntervalMs":100}})", true);
    CHECK(o.attempt && !o.json_broken && !o.invalid);
    CHECK_EQ(o.warnings.size(), 0u);
    CHECK_EQ(o.live_changed, std::vector<std::string>{"scanner.pollIntervalMs"});
    CHECK(o.restart_changed.empty());
    // Invalid value -> default = current: a success with warnings and an EMPTY diff.
    auto w = config_reload(cur, prev, moved, R"({"sound":{"volume":101}})", true);
    CHECK(w.attempt && !w.json_broken && !w.invalid);
    CHECK_EQ(w.warnings.size(), 1u);
    CHECK(w.live_changed.empty() && w.restart_changed.empty());
    CHECK_EQ(w.next.sound.volume, 100);  // SPEC §5: default used
}

// plan-release.md item 2 (SPEC §5 runtime amendment): an unknown key rejects the whole
// config; the app keeps running on the CURRENT config (the keep rule of broken JSON),
// and the same invalid version logs once - main.cpp advances the stamp on the handled
// version, so the next cycle sees an unchanged stamp and repeats nothing.
TEST(config_reload_invalid_unknown_key_keeps_current) {
    Config cur = config_defaults();
    cur.sound.volume = 25;  // NOT the defaults: the running config must survive
    FileStamp prev{kPrevTick, kPrevSize}, moved{kPrevTick + 1, 600};
    auto o = config_reload(cur, prev, moved, R"({"scanner":{"pollIntervalMs":100,"foo":1}})", true);
    CHECK(o.attempt && !o.json_broken && !o.read_failed);
    CHECK(o.invalid);
    CHECK_EQ(o.unknown_keys, std::vector<std::string>{"scanner.foo"});
    CHECK(config_diff(o.next, cur).empty());  // current kept, not the parsed change
    CHECK(o.live_changed.empty() && o.restart_changed.empty() && o.warnings.empty());
    // Stamp advances to the handled invalid version (main.cpp): the same text on the same
    // stamp is a no-op cycle, the config stays.
    auto o2 = config_reload(o.next, moved, moved, R"({"scanner":{"pollIntervalMs":100,"foo":1}})", true);
    CHECK(!o2.attempt && !o2.invalid && !o2.json_broken);
    CHECK(config_diff(o2.next, cur).empty());
    // The user fixes the key: the new stamp parses, the change applies on the current config.
    // The fixed text omits sound.volume, so the parse takes the SPEC §5 default 100 - the
    // canonical diff carries both changed paths (scanner.pollIntervalMs before sound.volume).
    FileStamp fixed{kPrevTick + 2, 590};
    auto o3 = config_reload(o.next, moved, fixed, R"({"scanner":{"pollIntervalMs":100}})", true);
    CHECK(o3.attempt && !o3.invalid);
    const std::vector<std::string> want_live{"scanner.pollIntervalMs", "sound.volume"};
    CHECK_EQ(o3.live_changed, want_live);
    CHECK_EQ(o3.next.scanner.pollIntervalMs, 100);
    CHECK_EQ(o3.next.sound.volume, 100);
}

TEST(config_reload_success_splits_live_restart) {
    Config cur = config_defaults();
    FileStamp prev{kPrevTick, kPrevSize}, moved{kPrevTick + 1, 700};
    const std::string text = R"({
        "configVersion": 2,
        "hotkeys": { "toggleScanner": "Ctrl+8" },
        "scanner": { "enabledOnStart": true, "pollIntervalMs": 100,
                     "requireGameForeground": false },
        "sound": { "volume": 30 },
        "vitals": { "Health": { "threshold": 60 } },
        "logging": { "level": "debug", "file": "logs/other.log",
                     "maxSizeKb": 2048, "keepFiles": 5 }
    })";
    auto o = config_reload(cur, prev, moved, text, true);
    CHECK(o.attempt && !o.json_broken && !o.read_failed && !o.invalid);
    CHECK(o.error.empty());
    CHECK_EQ(o.warnings.size(), 0u);
    // Canonical order preserved inside each subset (plan §2 table).
    const std::vector<std::string> want_live{
        "hotkeys.toggleScanner", "scanner.pollIntervalMs", "scanner.requireGameForeground",
        "sound.volume", "vitals.Health.threshold",
        "logging.level", "logging.maxSizeKb", "logging.keepFiles"};
    const std::vector<std::string> want_restart{
        "scanner.enabledOnStart", "logging.file", "configVersion"};
    CHECK_EQ(o.live_changed, want_live);
    CHECK_EQ(o.restart_changed, want_restart);
    // next is the full parsed config: restart fields are present for the post-restart read too.
    CHECK_EQ(o.next.scanner.pollIntervalMs, 100);
    CHECK_EQ(o.next.vitals[poe2::kHealth].threshold, 60);
    CHECK_EQ(o.next.toggleHotkey, std::string("Ctrl+8"));
}

TEST(config_reload_empty_diff_is_success) {
    Config cur = config_defaults();
    // save without edits: same size, mtime moved
    FileStamp prev{kPrevTick, kPrevSize}, moved{kPrevTick + 1, kPrevSize};
    auto o = config_reload(cur, prev, moved, config_serialize(cur), true);
    CHECK(o.attempt && !o.json_broken && !o.read_failed && !o.invalid);  // plan §0: empty diff = success
    CHECK(o.live_changed.empty() && o.restart_changed.empty());
    CHECK(o.warnings.empty());
    CHECK(config_diff(o.next, cur).empty());
}

// Plan §4 group 3: the broken -> repaired sequence the 5 s cycle produces (plan §3 step 3).
TEST(config_reload_broken_then_valid) {
    Config cur = config_defaults();
    FileStamp base{1000, 500}, broken{1001, 480};
    auto o1 = config_reload(cur, base, broken, "{\n  \"scanner\": {\"pollIntervalMs\": 100\n", true);
    CHECK(o1.json_broken);
    CHECK(config_diff(o1.next, cur).empty());  // the app keeps running on the current config
    // main.cpp advances prev to the broken stamp: the same broken version logs ONE error line,
    // the 5 s cycle is kept (plan §3).
    auto o2 = config_reload(o1.next, broken, broken, "{\n  \"scanner\": {\"pollIntervalMs\": 100\n", true);
    CHECK(!o2.attempt && !o2.json_broken && !o2.read_failed);
    CHECK(o2.error.empty());
    // The user repairs the file: prev = the broken stamp, cur = the new stamp -> success.
    FileStamp fixed{1002, 486};
    auto o3 = config_reload(o1.next, broken, fixed, R"({"scanner":{"pollIntervalMs":100}})", true);
    CHECK(o3.attempt && !o3.json_broken);
    CHECK(o3.error.empty());
    CHECK_EQ(o3.live_changed, std::vector<std::string>{"scanner.pollIntervalMs"});
    CHECK_EQ(o3.next.scanner.pollIntervalMs, 100);
}

// ---- Group 4 (plan §4): ConfigSnapshot (plan-hot-reload.md §1b) ----

TEST(config_snapshot_set_get_isolation) {
    ConfigSnapshot snap;
    Config a = config_defaults();
    a.sound.volume = 25;
    auto p1 = std::make_shared<const Config>(a);
    snap.set(p1);
    auto r1 = snap.get();
    CHECK(r1.get() == p1.get());           // the reader gets the same object, not a copy
    CHECK_EQ(r1->sound.volume, 25);
    // Snapshot isolation: the source Config keeps changing, the published snapshot does not.
    a.sound.volume = 90;
    CHECK_EQ(snap.get()->sound.volume, 25);
    // A new set() publishes a new snapshot; a reader of the old one keeps it whole.
    Config b = *snap.get();
    b.vitals[poe2::kHealth].threshold = 60;
    auto p2 = std::make_shared<const Config>(b);
    snap.set(p2);
    CHECK_EQ(snap.get()->vitals[poe2::kHealth].threshold, 60);
    CHECK_EQ(r1->vitals[poe2::kHealth].threshold, 50);   // the old snapshot survives the set
    const std::vector<std::string> one_path{"vitals.Health.threshold"};
    CHECK_EQ(config_diff(*r1, *snap.get()), one_path);
    // Immutability after set(): the published pointer is shared_ptr<const Config>, so no
    // writer can reach through a reader's copy - the app has no writable view of a snapshot.
}

TEST(config_snapshot_readers_see_one_whole_snapshot) {
    ConfigSnapshot snap;
    snap.set(std::make_shared<const Config>(config_defaults()));
    // A paired change (plan §2 moves scanner.pollIntervalMs and the vitals threshold
    // together): every reader after the set() sees BOTH fields of ONE snapshot, never a
    // mix of the old interval with the new threshold.
    Config paired = config_defaults();
    paired.scanner.pollIntervalMs = 100;
    paired.vitals[poe2::kHealth].threshold = 60;
    auto whole = std::make_shared<const Config>(paired);
    snap.set(whole);
    for (int i = 0; i < 4; ++i) {
        auto r = snap.get();   // four readers, one and the same snapshot
        CHECK(r.get() == whole.get());
        CHECK_EQ(r->scanner.pollIntervalMs, 100);
        CHECK_EQ(r->vitals[poe2::kHealth].threshold, 60);
    }
    // Before any set() the channel is empty: the session guards a null snapshot.
    ConfigSnapshot fresh;
    CHECK(fresh.get() == nullptr);
}

// ---- Group 5 (plan §4): live application through ScannerSession::step() ----
// The config reaches the session through ConfigSnapshot inside ScannerCtl - the exact
// channel main.cpp installs (plan §3 handler order: live.set() first), driven here by
// the fixture's set_live(). Every check is a step()/step_delay_ms() call on the fakes:
// no thread, no sleep, no game.

TEST(live_threshold_change_makes_a_press_line_with_the_new_threshold) {
    Fix f("live_threshold", config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    f.session.step();                       // attach
    REQUIRE(phase_of(f.ctl) == Phase::Attached);
    f.set_current(poe2::kHealth, 2000);     // 51% of unreserved 3888: above the default 50%
    f.session.step();                       // the tick runs: nothing is below, no press
    REQUIRE(f.sender.count() == 0);

    Config c = f.live();
    c.vitals[poe2::kHealth].threshold = 60;   // 2000*100 < 60*3888: below the new gate
    f.set_live(c);
    f.session.step();                       // plan §2: the next evaluate() uses the snapshot
    CHECK_EQ(f.sender.count(), 1);
    CHECK(f.log_text().find("Health 51% <= 60%, key=1\n") != std::string::npos);
}

TEST(live_key_change_gives_the_new_vk_to_the_sender) {
    Fix f("live_key", config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    f.session.step();                       // attach
    REQUIRE(phase_of(f.ctl) == Phase::Attached);
    f.set_current(poe2::kHealth, 1000);     // 26% < 50%: ready to fire
    f.session.step();
    REQUIRE(f.sender.count() == 1);
    CHECK_EQ(f.sender.recs[0].vk, static_cast<uint16_t>('1'));

    Config c = f.live();
    c.vitals[poe2::kHealth].key = "4";      // the Health flask moves to key "4"
    f.set_live(c);
    f.clock.advance(1000);                  // the cooldown is ready: the next tick fires
    f.session.step();
    CHECK_EQ(f.sender.count(), 2);
    CHECK_EQ(f.sender.recs[1].vk, static_cast<uint16_t>('4'));   // the new VK reaches the sender
    CHECK(f.log_text().find(", key=4\n") != std::string::npos);  // plan §7 press line carries it
}

TEST(live_poll_interval_change_moves_step_delay_ms) {
    ScannerCfg cfg = config_defaults().scanner;
    cfg.pollIntervalMs = 50;
    Fix f("live_poll", cfg);
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    f.session.step();                       // attach: the reading phase
    REQUIRE(phase_of(f.ctl) == Phase::Attached);
    CHECK_EQ(f.session.step_delay_ms(), 50);

    Config c = f.live();
    c.scanner.pollIntervalMs = 250;         // plan §2: step_delay_ms() reads the live value
    f.set_live(c);
    CHECK_EQ(f.session.step_delay_ms(), 250);
    c.scanner.pollIntervalMs = 5000;        // out of the SPEC §5 range: clamped to 1000
    f.set_live(c);
    CHECK_EQ(f.session.step_delay_ms(), 1000);
}

TEST(live_process_names_change_attaches_by_the_new_name) {
    ScannerCfg cfg = config_defaults().scanner;
    cfg.processNames = {"NotTheGame.exe"};  // the spawned game does not match this list
    Fix f("live_names", cfg);
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);                     // PathOfExile.exe
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::WaitingForGame);   // plan §2: the next find() uses the list
    CHECK_EQ(f.proc.finds, 1);

    Config c = f.live();
    c.scanner.processNames = {"PathOfExile.exe"};      // case-insensitive basename match
    f.set_live(c);
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::Attached);
    CHECK_EQ(f.ctl.pid.load(), 4242u);
    CHECK(f.log_text().find("attached PID 4242") != std::string::npos);
}

TEST(live_require_game_foreground_change_moves_the_send_gate) {
    Fix f("live_fg", config_defaults().scanner);   // requireGameForeground = true
    f.proc.set_foreground(false);                  // the game is in the background
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    f.session.step();                              // attach
    REQUIRE(phase_of(f.ctl) == Phase::Attached);
    f.set_current(poe2::kHealth, 1000);            // 26% < 50%: ready to fire
    f.session.step();
    CHECK_EQ(f.sender.count(), 0);   // SPEC §4.8: the gate sends nothing to a background game

    Config c = f.live();
    c.scanner.requireGameForeground = false;       // plan §2: the next tick sees the gate off
    f.set_live(c);
    f.session.step();
    CHECK_EQ(f.sender.count(), 1);                 // presses resume with the gate off
    CHECK_EQ(f.sender.recs[0].vk, static_cast<uint16_t>('1'));

    // And back the other way: the gate on stops the sends on the next tick again.
    Config d = f.live();
    d.scanner.requireGameForeground = true;
    f.set_live(d);
    f.clock.advance(1000);                         // cooldown ready, foreground is not
    f.session.step();
    CHECK_EQ(f.sender.count(), 1);
}
