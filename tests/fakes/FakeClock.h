#pragma once
// FakeClock (plan.md §2): deterministic time source for logger/vital tests.
// No sleeps, no real time: tests set the clock and read exactly the injected value.

#include "clock.h"

namespace poe2 {
namespace testing {

struct FakeClock : IClock {
    int64_t t = 0;
    int64_t tz = 0;  // zone offset from UTC in ms, pinned by the test
    FakeClock() = default;
    void set(int64_t ms) { t = ms; }
    void advance(int64_t ms) { t += ms; }
    // Explicit offset so log-format tests never depend on the host timezone.
    void set_tz(int64_t offset_ms) { tz = offset_ms; }
    int64_t now_ms() override { return t; }
    int64_t tz_offset_ms() override { return tz; }
};

// TU anchor for the FakeClock module (S1-6): behavior is fully inline above, the
// anchor lives in tests/fakes/FakeClock.cpp.
void fake_clock_anchor();

}  // namespace testing
}  // namespace poe2
