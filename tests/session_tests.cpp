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
    // MINOR_ISSUES S3-7: Scanner::stop() clears enabled BEFORE setting stop + waking,
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
    f.mem->put_u64(Synth::kGlobal, 0);       // chain fails at attach every poll
    f.session.step();
    CHECK(phase_of(f.ctl) == Phase::WaitingForGame);
    CHECK(f.log_text().find("Offsets mismatch: GameState") != std::string::npos);
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
    cfg.pollIntervalMs = 20;               // consumed by Deps as a copy at construction
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
