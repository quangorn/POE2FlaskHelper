#pragma once
// ConfigSnapshot (plan-hot-reload.md §1b): the live config channel from the UI thread to
// the scanner thread, on top of the existing UI <-> thread channel of ScannerCtl (SPEC §4.2).
// The UI thread installs a validated config with set() (the hot-reload handler in main.cpp),
// any reader takes it with get(). After set() the snapshot is immutable: the pointer is
// shared_ptr<const Config>, so a reader never sees a half-new config - get() hands out ONE
// whole snapshot, and readers of an older snapshot keep that object alive until their last
// shared_ptr dies. Lives inside ScannerCtl (src/scanner_ctl.h); poe2_core, Win32-free,
// testable without time.

#include <memory>
#include <mutex>

#include "config.h"

namespace poe2 {

class ConfigSnapshot {
public:
    // UI thread: install the next validated config. The snapshot the scanner thread is
    // reading stays alive for it - a set() never mutates a published config.
    void set(std::shared_ptr<const Config> next);

    // Scanner thread (and any reader): one consistent snapshot copy. Null only before the
    // first set() - main.cpp installs the startup config before the scanner thread starts.
    std::shared_ptr<const Config> get() const;

private:
    mutable std::mutex mtx_;  // mutable: get() is const and locks it (the Logger pattern)
    std::shared_ptr<const Config> cfg_;
};

}  // namespace poe2
