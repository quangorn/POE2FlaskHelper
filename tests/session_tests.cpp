// ScannerSession state machine tests (plan.md stage 3): waiting -> attach -> reading
// -> break -> waiting, game restart, scanner off with ZERO FakeMemory reads, the 5 s
// log rate limit. Everything is driven through step() with FakeProcess/FakeMemory/
// FakeClock; no sleeps, no real time, no game. The fixture lives in
// tests/session_fixture.h (shared with the stage 4 session tests).
#include "test.h"

#include <climits>
#include <string>

#include "config.h"
#include "scanner_ctl.h"
#include "scanner_session.h"
#include "session_fixture.h"
#include "state.h"
#include "synthetic_layout.h"
#include "test_tmp.h"
#include "version.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeMemory.h"
#include "fakes/FakeProcess.h"

using poe2::AppStatus;
using poe2::LogLevel;
using poe2::Logger;
using poe2::LogOptions;
using poe2::Phase;
using poe2::ScannerCfg;
using poe2::ScannerCtl;
using poe2::ScannerSession;
using poe2::testing::FakeClock;

namespace {

// SPEC §4.3 (amended 2026-11-08): tooltip line 1 = app name + project version.
std::string tip_head() { return std::string("POE2FlaskHelper " APP_VERSION_STR) + "\n"; }

}  // namespace
using poe2::testing::FakeMemFactory;
using poe2::testing::FakeMemory;
using poe2::testing::FakeProcess;
using poe2::testing::Fix;
using poe2::testing::Synth;
using poe2::testing::rev34;

namespace {

Phase phase_of(const ScannerCtl& ctl) { return static_cast<Phase>(ctl.phase.load()); }

}  // namespace

TEST(scanner_off_performs_zero_memory_reads) {
    Fix f("scanner_off_zero_reads", poe2::config_defaults().scanner);
    f.ctl.enabled.store(false);
    for (int i = 0; i < 12; ++i) f.session.step();   // the thread spins steps while off
    CHECK_EQ(f.mem->reads(), 0);          // SPEC §4.6 / acceptance criterion 7
    CHECK_EQ(f.proc.finds, 0);            // not even a process poll while off
    CHECK(phase_of(f.ctl) == Phase::ScannerOff);
    CHECK_EQ(f.session.step_delay_ms(), 500);
}

TEST(scanner_waiting_when_no_game) {
    Fix f("waiting_no_game", poe2::config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::WaitingForGame);
    CHECK_EQ(f.mem->reads(), 0);          // EnumProcesses only: no memory reads
    CHECK_EQ(f.proc.finds, 1);
    CHECK_EQ(f.session.step_delay_ms(), 1000);   // SPEC §4.6: game poll 1-2 s
    // The waiting line appears once, not once per poll.
    f.session.step();
    f.session.step();
    CHECK_EQ(f.count("waiting for game"), 1);
    CHECK_EQ(f.proc.finds, 3);
}

TEST(scanner_attach_and_reading) {
    Fix f("attach_reading", poe2::config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.session.step();                       // no game yet
    f.proc.spawn(4242);
    f.session.step();                       // attach: OpenProcess + signature + chain
    CHECK(phase_of(f.ctl) == Phase::Attached);
    CHECK_EQ(f.ctl.pid.load(), 4242u);
    CHECK(f.mem->reads() > 0);
    CHECK(f.log_text().find("attached PID 4242") != std::string::npos);
    CHECK_EQ(f.session.step_delay_ms(), 50);   // pollIntervalMs in the reading phase

    // Reading: values are logged at debug only (SPEC §7).
    f.clock.advance(50);
    f.session.step();
    CHECK(f.log_text().find("tick: Health 4000/5000") != std::string::npos);
    CHECK_EQ(f.count("Health"), 1);        // one values line, no info-level duplication
    // Default config on the synthetic layout: nothing is below threshold, so the
    // RecordingKeySender stays empty (Health 4000/3888 unreserved >= 50%, key never sent).
    CHECK_EQ(f.sender.count(), 0);
}

TEST(scanner_open_process_failure_is_a_warning_and_waits) {
    Fix f("open_fail", poe2::config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.proc.spawn(777);
    f.factory.open_ok = false;
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::WaitingForGame);
    CHECK(f.log_text().find("OpenProcess failed for PID 777") != std::string::npos);
    // Throttled: a second failing open within 5 s does not add a line.
    f.session.step();
    CHECK_EQ(f.count("OpenProcess failed"), 1);
    f.clock.advance(5000);
    f.session.step();
    CHECK_EQ(f.count("OpenProcess failed"), 2);
}

TEST(open_access_denied_is_error_and_admin_status) {
    // SPEC §8: OpenProcess -> ERROR_ACCESS_DENIED => error in the log + the
    // "Admin rights required" tooltip. The fixture runs at the info level: warn/error
    // lines must survive it (SPEC §7), the debug chain lines must not.
    Fix f("admin_denied", poe2::config_defaults().scanner,
          poe2::config_defaults(), LogLevel::Info);
    f.ctl.enabled.store(true);
    f.proc.spawn(777);
    f.factory.open_ok = false;
    f.factory.open_err = poe2::kWinAccessDenied;   // injected ERROR_ACCESS_DENIED
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::AdminRequired);
    CHECK(f.log_text().find("[error] OpenProcess denied for PID 777 - "
                            "admin rights required") != std::string::npos);
    CHECK_EQ(f.count("[warn]"), 0);   // the deny is not downgraded to a warning
    // SPEC §7: at the info level debug lines are dropped, warn/error always written.
    CHECK_EQ(f.count("[debug]"), 0);
    // The UI sees it as the exact tooltip (SPEC §4.3/§8), merged like main.cpp does.
    AppStatus st;
    st.scanner_enabled = true;
    CHECK(poe2::apply_scanner_snapshot(st, phase_of(f.ctl), f.ctl.pid.load()));
    CHECK_EQ(poe2::tooltip_text(st), tip_head() + "Admin rights required");

    // Polling continues with the admin status, and the error line is rate-limited to
    // one per 5 s (SPEC §7 discipline for a repeating failure).
    f.session.step();
    f.session.step();
    CHECK_EQ(f.count("OpenProcess denied"), 1);
    CHECK(phase_of(f.ctl) == Phase::AdminRequired);
    f.clock.advance(5000);
    f.session.step();
    CHECK_EQ(f.count("OpenProcess denied"), 2);

    // SPEC §8 recovery: the next successful open replaces the admin status, the
    // tooltip returns to the normal states and no stale status sticks.
    f.factory.open_ok = true;
    f.factory.open_err = 0;
    f.session.step();                            // attach: OpenProcess + chain
    CHECK(phase_of(f.ctl) == Phase::Attached);
    CHECK_EQ(f.ctl.pid.load(), 777u);
    CHECK(poe2::apply_scanner_snapshot(st, phase_of(f.ctl), f.ctl.pid.load()));
    CHECK_EQ(poe2::tooltip_text(st), tip_head() + "Attached (PID 777)");
    CHECK(f.log_text().find("attached PID 777") != std::string::npos);
    CHECK_EQ(f.count("OpenProcess denied"), 2);  // the recovery adds no admin line
}

TEST(other_open_failures_keep_the_waiting_status) {
    // A non-ACCESS_DENIED open failure (PEB/PE read: kOpenReaderFail) keeps the SPEC §7
    // warning + "Waiting for game" tooltip - the admin tooltip is only for code 5.
    Fix f("open_other_fail", poe2::config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.proc.spawn(1001);
    f.factory.open_ok = false;
    f.factory.open_err = poe2::kOpenReaderFail;
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::WaitingForGame);
    CHECK(f.log_text().find("[warn] OpenProcess failed for PID 1001") != std::string::npos);
    CHECK_EQ(f.count("[error]"), 0);
    AppStatus st;
    st.scanner_enabled = true;
    CHECK(poe2::apply_scanner_snapshot(st, phase_of(f.ctl), f.ctl.pid.load()));
    CHECK_EQ(poe2::tooltip_text(st), tip_head() + "Waiting for game");
}

TEST(admin_status_clears_when_the_game_disappears) {
    // SPEC §8: the admin status is a state, not a latch. If the elevated game process
    // goes away, the next poll reports the normal "waiting" status again.
    Fix f("admin_gone", poe2::config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.proc.spawn(777);
    f.factory.open_ok = false;
    f.factory.open_err = poe2::kWinAccessDenied;
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::AdminRequired);

    f.proc.kill();                 // the game closes
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::WaitingForGame);
    AppStatus st;
    st.scanner_enabled = true;
    CHECK(poe2::apply_scanner_snapshot(st, phase_of(f.ctl), f.ctl.pid.load()));
    CHECK_EQ(poe2::tooltip_text(st), tip_head() + "Waiting for game");

    // And a game that becomes openable later attaches normally.
    f.proc.spawn(888);
    f.factory.open_ok = true;
    f.factory.open_err = 0;
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::Attached);
    CHECK_EQ(poe2::tooltip_text(st), tip_head() + "Waiting for game");
    CHECK(poe2::apply_scanner_snapshot(st, phase_of(f.ctl), f.ctl.pid.load()));
    CHECK_EQ(poe2::tooltip_text(st), tip_head() + "Attached (PID 888)");
}

TEST(scanner_chain_loss_goes_back_to_waiting_and_reuses_cache) {
    Fix f("chain_loss", poe2::config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    f.session.step();                        // attach (signature scan, full chain)
    REQUIRE(phase_of(f.ctl) == Phase::Attached);
    const int attach_reads = f.mem->reads();

    // The game enters a state where the entity is invalid (zone change / death).
    f.mem->put_u8(Synth::kEntity + static_cast<uint64_t>(rev34().EntityIsValid), 0xB);
    f.session.step();                        // tick fails => back to the process poll
    CHECK(phase_of(f.ctl) == Phase::WaitingForGame);
    CHECK(f.log_text().find("chain lost (PID 4242): EntityValid") != std::string::npos);

    // The process is alive, the game is back in a valid state: re-open. global_addr,
    // off_lp and life_index are cached, so NO signature rescan runs (one signature
    // pass alone costs 8 chunk reads).
    f.mem->reset_reads();
    f.mem->put_u8(Synth::kEntity + static_cast<uint64_t>(rev34().EntityIsValid), 0xC);
    f.session.step();                        // re-attach from the caches: no chain walk
    CHECK(phase_of(f.ctl) == Phase::Attached);
    CHECK_EQ(f.mem->reads(), 0);            // no signature rescan, no chain re-walk
    f.session.step();                        // the tick runs the cached chain path
    // The cached chain is 11 reads (pinned by tick_read_budget_and_cache_reuse); a
    // signature pass alone would add 8 chunk reads, so this pins "no rescan".
    CHECK_EQ(f.mem->reads(), 11);

    // Loss logs are rate-limited to once per 5 s (SPEC §7): this loss is inside the
    // window of the first, so the count stays 1.
    f.mem->put_u8(Synth::kEntity + static_cast<uint64_t>(rev34().EntityIsValid), 0xB);
    f.session.step();                        // tick fails again
    CHECK_EQ(f.count("chain lost"), 1);
    f.mem->put_u8(Synth::kEntity + static_cast<uint64_t>(rev34().EntityIsValid), 0xC);
    f.session.step();                        // attached again
    REQUIRE(phase_of(f.ctl) == Phase::Attached);
    f.mem->put_u8(Synth::kEntity + static_cast<uint64_t>(rev34().EntityIsValid), 0xB);
    f.clock.advance(5000);                   // the window expires
    f.session.step();                        // tick fails: the line logs again
    CHECK_EQ(f.count("chain lost"), 2);
    CHECK(attach_reads >= 8 + 11);          // the first attach scanned the image
}

TEST(scanner_menu_state_is_quiet_and_keeps_the_handle) {
    // SPEC §4.6 item 5 / SPEC §8 (added 2026-11-08): the game running in the main menu
    // or on the character selection screen is a normal status. Before this rule the
    // session detached and re-opened the process every poll: three info lines per
    // second, the tray tooltip flickering between "Attached (PID n)" and
    // "Waiting for game", an OpenProcess+CloseHandle cycle per second.
    Fix f("menu_quiet", poe2::config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    f.set_in_zone(false);
    f.session.step();                    // OpenProcess + signature; the chain stops at AreaInstance = 0
    CHECK(phase_of(f.ctl) == Phase::InMenu);
    CHECK_EQ(f.ctl.pid.load(), 0u);      // the UI label carries no PID (SPEC §4.3)
    CHECK_EQ(f.session.step_delay_ms(), 1000);   // SPEC §4.6 item 5: the 1 s cadence
    AppStatus st;
    st.scanner_enabled = true;
    CHECK(poe2::apply_scanner_snapshot(st, phase_of(f.ctl), f.ctl.pid.load()));
    CHECK_EQ(poe2::tooltip_text(st), tip_head() + "In menu / loading");

    const int attached_reads = f.mem->reads();
    for (int i = 0; i < 12; ++i) f.session.step();   // twelve menu polls
    CHECK(phase_of(f.ctl) == Phase::InMenu);
    // The whole point of the fix: the state logs ONE line on entry and stays silent.
    CHECK_EQ(f.count("in menu / loading"), 1);
    CHECK_EQ(f.count("attached PID 4242"), 1);
    CHECK_EQ(f.count("detached from"), 0);
    CHECK_EQ(f.count("waiting for game"), 0);   // the process IS found: not "waiting"
    CHECK_EQ(f.count("chain lost"), 0);         // SPEC §7: the menu is not a chain loss
    CHECK_EQ(f.factory.opens, 1);               // one OpenProcess for the whole menu run
    CHECK_EQ(f.proc.finds, 13);                 // one process poll per step
    // ~3 reads per menu poll: GameStates, States[4], AreaInstance.
    CHECK_EQ(f.mem->reads() - attached_reads, 12 * 3);
}

TEST(scanner_menu_to_zone_logs_one_transition_and_resumes_reading) {
    Fix f("menu_to_zone", poe2::config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    f.set_in_zone(false);
    f.session.step();
    REQUIRE(phase_of(f.ctl) == Phase::InMenu);

    f.set_in_zone(true);                        // the player entered the zone
    f.session.step();                           // menu_step: the chain resolves on the open handle
    CHECK(phase_of(f.ctl) == Phase::Attached);
    CHECK_EQ(f.ctl.pid.load(), 4242u);
    CHECK_EQ(f.count("in zone (PID 4242)"), 1);   // one info line for the transition
    CHECK_EQ(f.count("attached PID 4242"), 1);     // NO second attach line: the handle was reused
    CHECK_EQ(f.factory.opens, 1);
    CHECK_EQ(f.session.step_delay_ms(), 50);       // reading cadence (pollIntervalMs)
    f.session.step();                            // the reading tick
    CHECK(f.log_text().find("tick: Health 4000/5000") != std::string::npos);
    CHECK_EQ(f.count("in zone (PID 4242)"), 1);
}

TEST(scanner_zone_to_menu_is_one_line_not_a_detach_cycle) {
    Fix f("zone_to_menu", poe2::config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    f.session.step();                           // attach (signature scan, full chain)
    REQUIRE(phase_of(f.ctl) == Phase::Attached);

    f.set_in_zone(false);                       // leaving the zone (menu / transition)
    f.mem->reset_reads();
    f.session.step();                           // the tick fails at AreaInstance = 0
    CHECK(phase_of(f.ctl) == Phase::InMenu);
    CHECK_EQ(f.count("in menu / loading"), 1);
    CHECK_EQ(f.count("detached from"), 0);
    CHECK_EQ(f.count("chain lost"), 0);
    CHECK_EQ(f.factory.opens, 1);
    CHECK_EQ(f.mem->reads(), 3);                // GameStates + States[4] + AreaInstance
    for (int i = 0; i < 6; ++i) f.session.step();
    CHECK(phase_of(f.ctl) == Phase::InMenu);
    CHECK_EQ(f.count("in menu / loading"), 1);   // the polls stay silent (SPEC §7)
    CHECK_EQ(f.factory.opens, 1);
}

TEST(scanner_menu_death_and_restart_rescans_signature) {
    Fix f("menu_death", poe2::config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    f.set_in_zone(false);
    f.session.step();
    REQUIRE(phase_of(f.ctl) == Phase::InMenu);

    f.proc.kill();                               // the player quit from the menu
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::WaitingForGame);
    CHECK(f.log_text().find("detached from PID 4242 (game closed in menu)") != std::string::npos);
    CHECK(f.log_text().find("process closed (PID 4242) - waiting for restart") != std::string::npos);
    CHECK_EQ(f.count("waiting for game"), 1);

    // SPEC §4.6 item 4: a restarted game is a new process, the signature is rescanned.
    f.proc.spawn(5000);
    f.mem->reset_reads();
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::InMenu);
    CHECK(f.log_text().find("attached PID 5000") != std::string::npos);
    CHECK(f.mem->reads() >= 8);                 // the chunk scan ran again
    CHECK_EQ(f.count("in menu / loading"), 2);    // one line per phase entry
    CHECK_EQ(f.factory.opens, 2);
}

TEST(scanner_menu_real_chain_failure_still_detaches) {
    // The menu classification must not hide genuine problems (SPEC §8): a chain break
    // that is NOT a zero zone pointer still goes through the detach + loss line path.
    Fix f("menu_real_failure", poe2::config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    f.set_in_zone(false);
    f.session.step();
    REQUIRE(phase_of(f.ctl) == Phase::InMenu);

    f.set_in_zone(true);                         // the zone is back, the entity is not valid
    f.mem->put_u8(Synth::kEntity + static_cast<uint64_t>(rev34().EntityIsValid), 0xB);
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::WaitingForGame);
    CHECK(f.log_text().find("chain lost (PID 4242): EntityValid") != std::string::npos);
    CHECK_EQ(f.count("detached from PID 4242"), 1);
    CHECK_EQ(f.count("in menu / loading"), 1);
    CHECK_EQ(f.factory.opens, 1);
}

TEST(scanner_process_death_clears_cache_and_rescans_on_restart) {
    Fix f("restart", poe2::config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    f.session.step();                        // attach (signature scan)
    REQUIRE(phase_of(f.ctl) == Phase::Attached);

    // The process dies: the tick fails, alive() is false, the cache must be cleared.
    f.mem->fail_all_reads(true);
    f.proc.kill();
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::WaitingForGame);
    CHECK(f.log_text().find("process closed (PID 4242)") != std::string::npos);

    // The game restarts under a new PID: signature rescanned, full chain rerun.
    f.mem->fail_all_reads(false);
    f.proc.spawn(5000);
    f.mem->reset_reads();
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::Attached);
    CHECK_EQ(f.ctl.pid.load(), 5000u);
    CHECK(f.mem->reads() >= 8);            // the chunk scan ran again
    CHECK(f.log_text().find("attached PID 5000") != std::string::npos);
}

TEST(scanner_disable_mid_reading_stops_all_reads) {
    Fix f("disable_mid", poe2::config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    f.session.step();                        // attach
    f.session.step();                        // tick
    REQUIRE(phase_of(f.ctl) == Phase::Attached);
    const int before = f.mem->reads();
    f.ctl.enabled.store(false);
    f.session.step();                        // detach, phase off
    CHECK(phase_of(f.ctl) == Phase::ScannerOff);
    CHECK(f.log_text().find("detached from PID 4242 (scanner disabled)") != std::string::npos);
    for (int i = 0; i < 5; ++i) f.session.step();
    CHECK_EQ(f.mem->reads(), before);        // zero further reads
    CHECK_EQ(f.proc.finds, 1);               // the process poll is not running while off
}

TEST(shutdown_while_attached_logs_detach) {
    // Review S3-7 (fixed 2026-11-08): Scanner::stop() clears enabled BEFORE setting stop + waking,
    // and the loop runs one final step() after the stop flag. That final step must
    // take the detach path and log the line. This test drives exactly that order on
    // the session: enabled=false, stop=true, then the last step.
    Fix f("shutdown_detach", poe2::config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    f.session.step();                        // attach
    REQUIRE(phase_of(f.ctl) == Phase::Attached);
    const int attached_reads = f.mem->reads();

    f.ctl.enabled.store(false);              // stop() order: enabled first
    f.ctl.stop.store(true);                  // then the stop flag
    f.session.step();                        // the loop's final step
    CHECK(f.log_text().find("detached from PID 4242 (scanner disabled)") != std::string::npos);
    CHECK(phase_of(f.ctl) == Phase::ScannerOff);
    CHECK_EQ(f.mem->reads(), attached_reads);   // detach path: zero further reads
}

TEST(offsets_mismatch_logged_once_per_5s) {
    Fix f("mismatch_throttle", poe2::config_defaults().scanner);
    f.ctl.enabled.store(true);
    f.proc.spawn(4242);
    // A genuine offsets problem, SPEC §8: IsValid must be 0xC. (A zero GameStates
    // pointer is the menu state, SPEC §4.6 item 5, and is not a mismatch - see
    // scanner_menu_state_is_quiet.)
    f.mem->put_u8(Synth::kEntity + static_cast<uint64_t>(rev34().EntityIsValid), 0xB);
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::WaitingForGame);
    CHECK(f.log_text().find("Offsets mismatch: EntityValid") != std::string::npos);
    f.session.step();
    f.session.step();
    CHECK_EQ(f.count("Offsets mismatch"), 1);   // throttled inside the 5 s window
    f.clock.advance(5000);
    f.session.step();
    CHECK_EQ(f.count("Offsets mismatch"), 2);
}

TEST(log_throttle_is_pure_and_deterministic) {
    // SPEC §7: at most one line per 5 s window; the clock is injected, so the test
    // drives the window without any real time.
    int64_t last = INT64_MIN;
    CHECK(poe2::log_throttle(1000, 5000, last));       // first call always logs
    CHECK(!poe2::log_throttle(4999, 5000, last));      // 3999 ms inside the window
    CHECK(!poe2::log_throttle(5001, 5000, last));      // 4001 ms: still inside
    CHECK(poe2::log_throttle(6000, 5000, last));       // 5000 ms: the window expired
    CHECK(!poe2::log_throttle(9999, 5000, last));      // 3999 ms after the new line
    CHECK(poe2::log_throttle(11000, 5000, last));      // 5000 ms again
}

TEST(step_delay_follows_the_phase_and_config) {
    ScannerCfg cfg = poe2::config_defaults().scanner;
    cfg.pollIntervalMs = 20;   // lands in the fixture's startup snapshot
    // (ConfigSnapshot is the Deps channel, session_fixture.h:62-64)
    Fix f("step_delay", cfg);
    CHECK_EQ(f.session.step_delay_ms(), 500);          // scanner off
    f.ctl.enabled.store(true);
    CHECK_EQ(f.session.step_delay_ms(), 1000);         // waiting, no game yet
    f.session.step();                                  // process poll, still no game
    CHECK_EQ(f.session.step_delay_ms(), 1000);
    f.proc.spawn(4242);
    f.session.step();                                  // attach
    CHECK_EQ(f.session.step_delay_ms(), cfg.pollIntervalMs);   // reading phase
    f.ctl.enabled.store(false);
    f.session.step();                                  // detach
    CHECK_EQ(f.session.step_delay_ms(), 500);          // off again
}

TEST(snapshot_merge_into_app_status) {
    AppStatus st;                        // disabled: ScannerOff, pid 0
    // Disabled: any thread snapshot lands as ScannerOff and changes nothing.
    CHECK(!poe2::apply_scanner_snapshot(st, Phase::Attached, 4242));
    CHECK(st.phase == Phase::ScannerOff);
    st.scanner_enabled = true;
    CHECK(poe2::apply_scanner_snapshot(st, Phase::WaitingForGame, 0));
    CHECK(st.phase == Phase::WaitingForGame);
    CHECK(poe2::apply_scanner_snapshot(st, Phase::Attached, 4242));
    CHECK(st.phase == Phase::Attached);
    CHECK_EQ(st.pid, 4242u);
    CHECK(!poe2::apply_scanner_snapshot(st, Phase::Attached, 4242));   // same: no change
    // Toggling off while the thread is attached: the snapshot must read as off,
    // pid cleared (the main-thread view never shows a stale scanner state).
    st.scanner_enabled = false;
    CHECK(poe2::apply_scanner_snapshot(st, Phase::Attached, 4242));
    CHECK(st.phase == Phase::ScannerOff);
    CHECK_EQ(st.pid, 0u);
}
