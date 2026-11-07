// Stage 4 scanner-thread wiring proofs (plan.md "Этап 4", deliverable 6): attach ->
// tick on the synthetic layout -> values below threshold -> RecordingKeySender presses
// with the right vk/extended, cooldown between ticks through the session's own state,
// foreground flip via FakeProcess stops presses, chain break stops presses, and at log
// level "info" no press/value lines appear. All through ScannerSession::step() with
// the fake provider/memory/clock; no game, no sleeps.
#include "test.h"

#include <string>

#include "config.h"
#include "scanner_ctl.h"
#include "scanner_session.h"
#include "session_fixture.h"
#include "state.h"
#include "synthetic_layout.h"
#include "fakes/FakeMemory.h"
#include "fakes/FakeProcess.h"

using poe2::Config;
using poe2::LogLevel;
using poe2::Phase;
using poe2::ScannerCfg;
using poe2::ScannerCtl;
using poe2::kHealth;
using poe2::kMana;
using poe2::kVitalCount;
using poe2::testing::Fix;
using poe2::testing::Synth;
using poe2::testing::rev34;

namespace {

Phase phase_of(const ScannerCtl& ctl) { return static_cast<Phase>(ctl.phase.load()); }

// Attach the game (one step after spawn) and drop Health's Current below the default
// 50% of unreserved 3888. Health key is "1" (VK '1'), Mana key is "2".
void attach_and_drop_hp(Fix& f, uint32_t current) {
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    f.session.step();                    // attach
    REQUIRE(phase_of(f.ctl) == Phase::Attached);
    f.set_current(kHealth, current);
}

}  // namespace

TEST(session_presses_fire_on_valid_ticks) {
    Fix f("s4_fire", poe2::config_defaults().scanner);
    attach_and_drop_hp(f, 1500);         // 1500*100=150000 < 50*3888=194400
    f.session.step();                    // first tick after attach: fires, no arming
    REQUIRE(f.sender.count() == 1);
    CHECK_EQ(f.sender.recs[0].vk, static_cast<uint16_t>('1'));   // Health key "1"
    CHECK_EQ(f.sender.recs[0].extended, false);
    CHECK_EQ(f.sender.recs[0].t, f.clock.now_ms());              // stamped on this tick
    CHECK(f.log_text().find("Health 39% < 50%, key=1") != std::string::npos);
    // The value line is the debug values line of the same tick (SPEC §7).
    CHECK(f.log_text().find("tick: Health 1500/5000") != std::string::npos);
}

TEST(session_cooldown_between_ticks_and_foreground_flip) {
    Fix f("s4_cooldown_fg", poe2::config_defaults().scanner);
    attach_and_drop_hp(f, 1000);
    f.session.step();                    // tick 1: press
    CHECK_EQ(f.sender.count(), 1);

    f.clock.advance(50);                 // 50 ms < 1000: next tick must not press
    f.session.step();
    CHECK_EQ(f.sender.count(), 1);
    CHECK_EQ(f.count("key=1"), 1);       // no extra debug press line either

    f.clock.advance(950);                // exactly 1000 ms from the last send
    f.session.step();
    CHECK_EQ(f.sender.count(), 2);

    f.proc.set_foreground(false);        // SPEC §4.8/§9 criterion 9: keys stop
    f.clock.advance(1000);               // cooldown is ready, foreground is not
    f.session.step();
    CHECK_EQ(f.sender.count(), 2);
    CHECK_EQ(f.count("key=1"), 2);

    f.proc.set_foreground(true);         // back in foreground: presses resume
    f.clock.advance(1000);
    f.session.step();
    CHECK_EQ(f.sender.count(), 3);
    CHECK_EQ(f.count("key=1"), 3);
}

TEST(session_no_presses_when_chain_broke) {
    Fix f("s4_chain_break", poe2::config_defaults().scanner);
    attach_and_drop_hp(f, 1000);         // values ready to fire
    f.mem->fail_all_reads(true);         // process handle dies: the tick cannot read
    f.proc.kill();                       // the game vanished
    f.session.step();                    // tick fails -> back to waiting
    CHECK(phase_of(f.ctl) == Phase::WaitingForGame);
    CHECK_EQ(f.sender.count(), 0);       // invalid values: zero presses (SPEC §4.7)
    CHECK_EQ(f.count("key="), 0);
}

TEST(session_no_presses_when_game_foreground_lost_at_attach_time) {
    // The game is never in foreground: every tick runs, values stay below, and the
    // RecordingKeySender stays empty for the whole session.
    Fix f("s4_no_fg", poe2::config_defaults().scanner);
    f.proc.set_foreground(false);
    attach_and_drop_hp(f, 1000);
    for (int i = 0; i < 8; ++i) {
        f.session.step();
        f.clock.advance(200);
    }
    CHECK(phase_of(f.ctl) == Phase::Attached);
    CHECK_EQ(f.sender.count(), 0);
    CHECK_EQ(f.count("key="), 0);
}

TEST(session_presses_when_foreground_not_required) {
    ScannerCfg cfg = poe2::config_defaults().scanner;
    cfg.requireGameForeground = false;   // SPEC §4.8: gate is config-controlled
    Fix f("s4_fg_optional", cfg);
    f.proc.set_foreground(false);
    attach_and_drop_hp(f, 1000);
    f.session.step();                    // tick with the game in background
    CHECK_EQ(f.sender.count(), 1);       // presses continue
    CHECK_EQ(f.sender.recs[0].vk, static_cast<uint16_t>('1'));
}

TEST(session_info_level_hides_presses_and_values) {
    // SPEC §7 + acceptance criterion 8: at level "info" the debug press and value
    // lines must not appear; the press itself still happens (RecordingKeySender).
    Fix f("s4_info_level", poe2::config_defaults().scanner,
          poe2::config_defaults(), LogLevel::Info);
    attach_and_drop_hp(f, 1000);
    f.session.step();                    // fires a press
    CHECK_EQ(f.sender.count(), 1);       // presses continue at info level
    const std::string t = f.log_text();
    CHECK(t.find("key=") == std::string::npos);         // no press lines
    CHECK(t.find("tick:") == std::string::npos);        // no values lines
    CHECK(t.find("Health 26%") == std::string::npos);   // no press line in any form
    CHECK(t.find("Health 1000/5000") == std::string::npos);
    CHECK(t.find("attached PID 4242") != std::string::npos);   // info lines stay
}

TEST(session_independent_vital_timers_over_ticks) {
    // Health (cd 1000, key "1") and Mana (cd 1000, key "2") both below threshold:
    // one press each per eligible tick, timers independent of each other.
    Config full = poe2::config_defaults();
    Fix f("s4_multi_vital", poe2::config_defaults().scanner, full);
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    f.session.step();                    // attach
    REQUIRE(phase_of(f.ctl) == Phase::Attached);
    f.set_current(kHealth, 1000);        // Health below 50% of unreserved 3888
    f.set_current(kMana, 100);           // Mana below 40% of unreserved 1500

    f.session.step();                    // both fire on the same tick
    CHECK_EQ(f.sender.count(), 2);
    CHECK_EQ(f.sender.recs[0].vk, static_cast<uint16_t>('1'));
    CHECK_EQ(f.sender.recs[1].vk, static_cast<uint16_t>('2'));

    f.clock.advance(500);                // half cooldown: no press
    f.session.step();
    CHECK_EQ(f.sender.count(), 2);

    f.clock.advance(500);                // exactly 1000 from the last send: both fire
    f.session.step();
    CHECK_EQ(f.sender.count(), 4);

    // Mana only is below the threshold now: its timer runs, Health is quiet.
    f.set_current(kHealth, 4000);        // back above threshold
    f.clock.advance(1000);
    f.session.step();                    // Mana fires (its own timer), Health not
    CHECK_EQ(f.sender.count(), 5);
    CHECK_EQ(f.sender.recs[4].vk, static_cast<uint16_t>('2'));
}

TEST(session_no_presses_while_scanner_disabled) {
    Fix f("s4_off", poe2::config_defaults().scanner);
    f.proc.spawn(4242);
    f.session.step();                    // attach (enabled=false: detach path, stays off)
    CHECK(phase_of(f.ctl) == Phase::ScannerOff);
    CHECK_EQ(f.mem->reads(), 0);         // module off: zero reads (SPEC §4.6)
    f.session.step();
    CHECK_EQ(f.sender.count(), 0);
}
