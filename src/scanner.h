#pragma once
// Scanner stub for stage 2 (plan.md §3 stage 2: "остановка потока сканера (заглушка)").
// The real worker thread arrives in stage 3 (SPEC §4.6): it will own OpenProcess,
// all ReadProcessMemory calls, SendInput and the cooldown timers (SPEC §4.2).
// The stub keeps the exact start/stop contract that main.cpp already wires, so stage
// 3 swaps the implementation without touching the tray/hotkey/sound layer.

#include <atomic>

namespace poe2 {

class ScannerStub {
public:
    void start() { running_.store(true); ++starts_; }
    void stop()  { running_.store(false); ++stops_; }   // idempotent
    bool running() const { return running_.load(); }

    // Counters are touched on the main thread only (tests verify start/stop pairing).
    int starts() const { return starts_; }
    int stops() const { return stops_; }

private:
    std::atomic<bool> running_{false};
    int starts_ = 0, stops_ = 0;
};

}  // namespace poe2
