#include "config_snapshot.h"

namespace poe2 {

void ConfigSnapshot::set(std::shared_ptr<const Config> next) {
    // The whole snapshot is swapped under the mutex: readers see the old pointer or the
    // new one, never a mix of fields (plan-hot-reload.md §1b - the config is immutable
    // after set, so the swap is the only shared state).
    std::lock_guard lk(mtx_);
    cfg_ = std::move(next);
}

std::shared_ptr<const Config> ConfigSnapshot::get() const {
    // The shared_ptr copy is taken under the mutex; the pointed-to Config is const and
    // never mutated, so reading through the copy needs no lock at all.
    std::lock_guard lk(mtx_);
    return cfg_;
}

}  // namespace poe2
