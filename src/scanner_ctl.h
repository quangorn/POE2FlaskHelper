#pragma once
// Control shared between the UI thread and the scanner thread (SPEC §4.2):
// enable + stop request flow in, the phase/pid snapshot flows out. Values are plain
// atomics, each side writes whole values only - the scanner thread never touches UI,
// the UI thread never reads memory. phase carries poe2::Phase values (src/state.h)
// so the main thread can copy the snapshot straight into AppStatus.

#include <atomic>
#include <cstdint>

namespace poe2 {

struct ScannerCtl {
    std::atomic<bool> enabled{false};       // scanner module on/off (hotkey, tray menu)
    std::atomic<bool> stop{false};          // shutdown request: the thread must leave its loop
    std::atomic<int> phase{0};             // poe2::Phase value, written by the scanner thread
    std::atomic<uint32_t> pid{0};          // attached PID (0 when not attached)
};

}  // namespace poe2
