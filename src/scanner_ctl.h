#pragma once
// Control shared between the UI thread and the scanner thread (SPEC §4.2):
// enable + stop request flow in, the phase/pid snapshot flows out. Values are plain
// atomics, each side writes whole values only - the scanner thread never touches UI,
// the UI thread never reads memory. phase carries poe2::Phase values (src/state.h)
// so the main thread can copy the snapshot straight into AppStatus.
//
// Hot-reload stage 2 (plan-hot-reload.md §1b): ConfigSnapshot is added here because
// ScannerCtl is the existing UI <-> thread channel. The UI thread installs a validated
// config with snapshot.set() (the 5 s hot-reload handler in main.cpp), the scanner
// thread reads it with snapshot.get() - one whole immutable config per read, the same
// rule as the atomics above (each side writes whole values only).

#include <atomic>
#include <cstdint>

#include "config_snapshot.h"

namespace poe2 {

struct ScannerCtl {
    std::atomic<bool> enabled{false};       // scanner module on/off (hotkey, tray menu)
    std::atomic<bool> stop{false};          // shutdown request: the thread must leave its loop
    std::atomic<int> phase{0};             // poe2::Phase value, written by the scanner thread
    std::atomic<uint32_t> pid{0};          // attached PID (0 when not attached)
    ConfigSnapshot snapshot;               // the live config: UI set(), scanner get()
};

}  // namespace poe2
