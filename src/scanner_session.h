#pragma once
// ScannerSession (SPEC §4.6, plan.md §1): the state machine
// waiting -> attach -> reading, driven by an explicit step() with no sleeps in the
// core. The thread layer (src/scanner.cpp, app layer) calls step() and waits on an
// event for step_delay_ms(). All OS interaction is injected:
// IProcessProvider, IMemoryReaderFactory, IClock, Logger.
//
// Caches (SPEC §4.6): global_addr, the actual LocalPlayers offset (+-0x40 window),
// life_index. Enabled flag and stop request come through ScannerCtl atomics.
// While the module is OFF the step performs ZERO memory reads (SPEC §4.6).

#include <cstdint>
#include <memory>

#include "chain.h"
#include "clock.h"
#include "config.h"
#include "game_process.h"
#include "keys.h"
#include "log.h"
#include "mem/reader.h"
#include "offsets.h"
#include "scanner_ctl.h"
#include "state.h"   // Phase values carried in ctl_.phase
#include "vitals.h"   // evaluate() + VitalSendState (stage 4)

namespace poe2 {

// SPEC §7 log rate limit: this kind of message is logged at most once per window_ms.
// last starts at INT64_MIN so the first occurrence always logs. Pure, testable.
bool log_throttle(int64_t now_ms, int64_t window_ms, int64_t& last_ms);

class ScannerSession {
public:
    struct Deps {
        IProcessProvider& proc;
        IMemoryReaderFactory& factory;
        IClock& clock;
        Logger& log;
        const Offsets& offs;
        ScannerCfg cfg;
        Config full;         // stage 4: the vitals section for evaluate()
        IKeySender& sender;  // stage 4: presses go through the injected sender
    };

    ScannerSession(Deps deps, ScannerCtl& ctl);

    // One machine step: discover/attach/tick/poll as the phase dictates. No sleeps.
    void step();

    // How long the thread should wait (event with timeout) before the NEXT step:
    // SPEC §4.6 - game poll 1-2 s while waiting, pollIntervalMs while reading.
    int step_delay_ms() const;

    uint32_t pid() const { return pid_; }        // attached PID (0 = not attached)
    const ChainCache& cache() const { return cache_; }

private:
    void publish(Phase p, uint32_t pid);
    void detach_info(const std::string& reason);
    void attach_step();
    void tick_step();

    Deps deps_;
    ScannerCtl& ctl_;
    std::unique_ptr<IMemoryReader> reader_;
    ChainCache cache_;
    uint32_t pid_ = 0;
    // The last PID we opened: chain caches belong to it. detach_info() zeroes pid_,
    // so re-opening the SAME live process must not wipe the signature/global caches
    // (SPEC §4.6 item 4: chain loss keeps the caches, a new process clears them).
    uint32_t last_opened_pid_ = 0;
    Phase last_logged_phase_ = Phase::ScannerOff;
    int64_t last_loss_ms_ = -1;       // chain-loss info throttle (SPEC §7, 5 s)
    int64_t last_mismatch_ms_ = -1;   // Offsets mismatch warn throttle
    int64_t last_open_ms_ = -1;       // OpenProcess warn throttle
    int64_t last_admin_ms_ = -1;      // ERROR_ACCESS_DENIED error throttle (SPEC §8)
    // Stage 4: the per-Vital cooldown timers live in the scanner thread (SPEC §4.2),
    // never in the UI. evaluate() updates them on every sent press.
    VitalSendState vitals_state_;
};

}  // namespace poe2
