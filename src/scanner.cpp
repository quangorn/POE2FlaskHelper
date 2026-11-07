#include "scanner.h"

#include <chrono>

namespace poe2 {

Scanner::Scanner(ScannerSession::Deps deps, ScannerCtl& ctl)
    : session_(deps, ctl), ctl_(ctl) {}

Scanner::~Scanner() { stop(); }

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
