#pragma once
// Time source interface (plan.md §1): core modules get time through IClock,
// tests inject FakeClock. The real SystemClock lives in the app layer (src/system_clock.h).

#include <cstdint>

namespace poe2 {

struct IClock {
    virtual ~IClock() = default;
    // Milliseconds since 1970-01-01 00:00:00 UTC.
    virtual int64_t now_ms() = 0;
    // Offset of the local time zone from UTC in milliseconds (MSK +3h = 10800000,
    // zones west of UTC are negative). Log lines are stamped with local time
    // (SPEC §7): local_ms = now_ms() + tz_offset_ms(). The offset is read per line
    // (a fixed snapshot for the line being formatted is enough for MVP - a DST change
    // shows up on the next line), so tests can pin it explicitly.
    virtual int64_t tz_offset_ms() = 0;
};

}  // namespace poe2
