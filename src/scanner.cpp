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
}

void Scanner::set_enabled(bool on) {
    ctl_.enabled.store(on);
    if (running_.load() && wake_) SetEvent(wake_);   // the toggle takes effect at once
}

void Scanner::stop() {
    ctl_.stop.store(true);
    if (wake_) SetEvent(wake_);   // wake the thread out of the poll timeout
    if (thread_.joinable()) thread_.join();
    running_.store(false);
}

}  // namespace poe2
