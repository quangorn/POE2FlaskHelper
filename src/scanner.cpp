// Handle-count measurement method for the S3-2 wake-event fix (MINOR_ISSUES GA-2), so the
// "165 -> 154 handles" claim is reproducible. Sampler: GetProcessHandleCount(
// GetCurrentProcess(), &n) - the live kernel-handle count, no external tool needed.
// Sampling points (the GA-1 test tests/scanner_thread_tests.cpp runs exactly these, all
// samples taken after a logger flush so the async logger's file handle is a constant in
// every sample and the deltas are the scanner handles only):
//   base: idle, module off, before any start();
//   running: right after start() = base + 2 (the wake_ event + the thread handle);
//   stopped: right after stop() = base + 1 - only the event is left, S3-2 keeps wake_ open;
//   restart: right after the next start() = stopped + 1 - exactly the thread handle, never
//     a new event: the SAME wake_ is reused;
//   exit: after ~Scanner() (join + CloseHandle(wake_)) = base: nothing leaked.
// The auditor's numbers measured the same direction on the live app: a flat count across 5
// restart cycles (165 at idle, no growth per cycle) and a clean exit at 154 handles.
#include "scanner.h"

#include <chrono>

namespace poe2 {

Scanner::Scanner(ScannerSession::Deps deps, ScannerCtl& ctl)
    : session_(deps, ctl), ctl_(ctl) {}

Scanner::~Scanner() {
    stop();
    // MINOR_ISSUES S3-2: the wake event is the thread's only OS handle. Close it after
    // the join, so a long-running process releases it instead of holding it to exit.
    if (wake_) {
        CloseHandle(wake_);
        wake_ = nullptr;
    }
}

void Scanner::start() {
    if (running_.load()) return;
    ctl_.stop.store(false);
    if (!wake_) {
        // Auto-reset: one wake-up per signal (toggle/stop).
        wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    running_.store(true);
    thread_ = std::thread(&Scanner::loop, this);
}

void Scanner::loop() {
    while (!ctl_.stop.load()) {
        session_.step();
        if (ctl_.stop.load()) break;
        const int ms = session_.step_delay_ms();
        if (wake_) {
            WaitForSingleObject(wake_, static_cast<DWORD>(ms));
        } else {
            // Event creation cannot realistically fail here; the pacing stays a
            // timed wait, never a busy loop.
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
    }
    // MINOR_ISSUES S3-7: shutdown while attached must end with the detach info line.
    // stop() clears enabled before the stop flag, so this final step takes the
    // detach path (SPEC §4.6: zero reads when the module is off) and logs
    // "detached from PID n (scanner disabled)" before the thread is joined.
    session_.step();
}

void Scanner::set_enabled(bool on) {
    ctl_.enabled.store(on);
    if (running_.load() && wake_) SetEvent(wake_);   // the toggle takes effect at once
}

void Scanner::stop() {
    // S3-7 ordering: enabled is cleared BEFORE the stop flag, so the final loop step
    // sees the module off and writes the detach line when a reader was open.
    ctl_.enabled.store(false);
    ctl_.stop.store(true);
    if (wake_) SetEvent(wake_);   // wake the thread out of the poll timeout
    if (thread_.joinable()) thread_.join();
    running_.store(false);
}

}  // namespace poe2
