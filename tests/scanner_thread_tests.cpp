// GA-1: thread-level test of the Scanner start -> stop -> start cycle. This is the real
// worker thread (src/scanner.cpp, compiled into poe2_tests - see the CMakeLists note) on
// real Win32 events, with the fakes of tests/session_fixture.h for everything the session
// touches (FakeProcess/FakeMemory/FakeClock/RecordingKeySender). No sleeps, no game, no UI.
//
// The point under test: start() creates the wake_ event once and stop() keeps it open
// (MINOR_ISSUES S3-2 closes it only in ~Scanner), so the second start() reuses the same
// event and the toggle wakes the restarted thread at once.
//
// GA-2 handle-count method (recorded in src/scanner.cpp): every sample is
// GetProcessHandleCount(GetCurrentProcess()) taken at fixed points after a logger flush,
// so the async logger's own file handle is present in every sample and the deltas are the
// scanner handles only: baseline = idle before any start; running = after start;
// stopped = after the join; restart = after the second start; exit = after ~Scanner().
// Measured here (all runs, 2026-11-08): base+2 / base+1 / base+2 / base+1 / base -
// event + thread handle at start, the event alone between cycles, nothing at exit.
//
// Determinism: the thread is woken through the event by start/stop/set_enabled, and the
// test spins on the ctl_.phase atomic with a fixed wall-clock cap - a thread that fails to
// publish is a loud FAIL, never a hang. No sleep calls.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "test.h"

#include <cstdint>

#include "config.h"
#include "scanner.h"
#include "scanner_ctl.h"
#include "session_fixture.h"
#include "state.h"

using poe2::LogLevel;
using poe2::Phase;
using poe2::Scanner;
using poe2::ScannerCtl;
using poe2::testing::Fix;

namespace {

Phase phase_of(const ScannerCtl& ctl) { return static_cast<Phase>(ctl.phase.load()); }

// Bounded spin on the atomic snapshot the worker publishes (start/stop/toggle SetEvent,
// so the publish lands within milliseconds). The spin yields with SwitchToThread - no
// sleep calls - and gives up at a fixed wall-clock cap, so a thread that fails to publish
// is a loud deterministic FAIL, never a hang.
bool wait_phase(const ScannerCtl& ctl, Phase want) {
    LARGE_INTEGER t0, freq, now;
    QueryPerformanceCounter(&t0);
    QueryPerformanceFrequency(&freq);
    for (int round = 0; round < 2000000; ++round) {
        if (phase_of(ctl) == want) return true;
        if ((round & 31) == 0) {
            SwitchToThread();   // let the worker run; not a sleep
            QueryPerformanceCounter(&now);
            if (now.QuadPart - t0.QuadPart > freq.QuadPart * 2) return false;  // 2 s cap
        }
    }
    return false;
}

// The snapshot atomics are published phase-first, then pid (scanner_session.cpp
// publish()), so a test that reads pid right after the phase lands can see the old pid.
// Same bounded spin, no sleeps.
bool wait_pid(const ScannerCtl& ctl, uint32_t want) {
    for (int round = 0; round < 2000000; ++round) {
        if (ctl.pid.load() == want) return true;
        if ((round & 31) == 0) SwitchToThread();
    }
    return false;
}

// GA-2 sampling point: the live kernel-handle count of this process.
uint32_t handle_count() {
    DWORD n = 0;
    GetProcessHandleCount(GetCurrentProcess(), &n);
    return n;
}

}  // namespace

TEST(scanner_start_stop_start_reuses_the_wake_event) {
    Fix f("scanner_thread_cycle", poe2::config_defaults().scanner);
    f.proc.spawn(4242);

    uint32_t base = 0, running = 0, stopped = 0, restart = 0, exit_cnt = 0;
    int reads_attached = 0;

    {
        Scanner sc(f.deps, f.ctl);
        CHECK(!sc.running());
        CHECK(phase_of(f.ctl) == Phase::ScannerOff);

        // GA-2 baseline: the logger has opened its log file (flush puts the line on disk),
        // so from this point every sample carries the same constant logger handle and the
        // deltas below are the scanner handles only.
        f.logger.log(LogLevel::Info, "scanner thread cycle baseline");
        f.log_text();
        base = handle_count();

        sc.start();                       // creates wake_ + spawns the worker
        CHECK(sc.running());
        running = handle_count();
        CHECK_EQ(running, base + 2);      // the wake event + the thread handle are open

        sc.set_enabled(true);             // SetEvent wakes the loop: attach runs at once
        REQUIRE(wait_phase(f.ctl, Phase::Attached));
        REQUIRE(wait_pid(f.ctl, 4242));   // pid lands right after the phase (publish order)
        CHECK_EQ(f.ctl.pid.load(), 4242u);
        CHECK_EQ(f.factory.opens, 1);
        CHECK(f.mem->reads() > 0);        // attached: real reads through FakeMemory
        reads_attached = f.mem->reads();

        sc.stop();                        // join; S3-2 keeps wake_ open for the restart
        CHECK(!sc.running());
        CHECK(phase_of(f.ctl) == Phase::ScannerOff);
        REQUIRE(wait_pid(f.ctl, 0));      // the detached snapshot: pid back to 0
        CHECK_EQ(f.ctl.pid.load(), 0u);
        CHECK_EQ(f.count("detached from PID 4242 (scanner disabled)"), 1);
        stopped = handle_count();
        CHECK_EQ(stopped, base + 1);      // the thread handle closed, the event stayed open

        sc.start();                       // second start: the SAME wake_ event is reused
        CHECK(sc.running());
        restart = handle_count();
        CHECK_EQ(restart, stopped + 1);   // exactly the new thread handle: no new event
        sc.set_enabled(true);
        REQUIRE(wait_phase(f.ctl, Phase::Attached));   // the reused event wakes it at once
        REQUIRE(wait_pid(f.ctl, 4242));
        CHECK_EQ(f.ctl.pid.load(), 4242u);
        CHECK_EQ(f.factory.opens, 2);     // re-attached: the factory opened twice
        CHECK(f.mem->reads() > reads_attached);
        CHECK_EQ(f.count("attached PID 4242"), 2);

        sc.stop();
    }   // ~Scanner(): join + CloseHandle(wake_)

    exit_cnt = handle_count();
    CHECK_EQ(exit_cnt, base);             // GA-2 exit point: back to the baseline, nothing leaked
}
