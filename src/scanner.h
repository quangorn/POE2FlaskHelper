#pragma once
// Scanner worker thread (SPEC §4.2, plan.md stage 3): the thread layer owns the
// session, sleeps on an event with the poll timeout (no busy-wait), joins at
// shutdown. Control to the thread: std::atomic flags in ScannerCtl; main thread and
// thread communicate only through them.
//
// The thread never calls UI/GDI (no PlaySound, no SendMessage, SPEC §4.2): sounds
// stay in the main layer on the toggle path. The main thread never reads memory.
//
// App layer: uses Win32 events, compiled into the exe only.

#include <windows.h>

#include <atomic>
#include <memory>
#include <thread>

#include "chain.h"
#include "scanner_ctl.h"
#include "scanner_session.h"

namespace poe2 {

class Scanner {
public:
    Scanner(ScannerSession::Deps deps, ScannerCtl& ctl);
    ~Scanner();   // stop() + join: no thread left running after the destructor
    Scanner(const Scanner&) = delete;
    Scanner& operator=(const Scanner&) = delete;

    void start();  // spawns the worker (idempotent while running)
    void stop();   // stop request + wake + join (idempotent)
    bool running() const { return running_.load(); }

    // UI -> thread: enable flag; wakes the loop so a toggle takes effect immediately.
    void set_enabled(bool on);
    bool enabled() const { return ctl_.enabled.load(); }

    // Status snapshot for the main thread (SPEC §4.3 tooltip).
    Phase phase() const { return static_cast<Phase>(ctl_.phase.load()); }
    uint32_t pid() const { return ctl_.pid.load(); }

private:
    void loop();

    ScannerSession session_;
    ScannerCtl& ctl_;
    std::thread thread_;
    HANDLE wake_ = nullptr;   // auto-reset event: toggle/stop wakes the poll timeout
    std::atomic<bool> running_{false};
    bool joined_ = false;
};

}  // namespace poe2
