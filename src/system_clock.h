#pragma once
// Real clock for the exe (app layer). Core modules use IClock only (plan.md §1).

#include <windows.h>

#include "clock.h"

namespace poe2 {

struct SystemClock : IClock {
    int64_t now_ms() override {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER u;
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        // FILETIME ticks are 100 ns since 1601-01-01; 11644473600000 ms to 1970 epoch.
        return static_cast<int64_t>(u.QuadPart / 10000 - 11644473600000LL);
    }

    // Local zone offset for the log stamp (SPEC §7). GetDynamicTimeZoneInformation
    // gives the offset in effect right now (DST included); the logger takes it as a
    // snapshot per line, so a DST switch is picked up by the next line - fine for MVP.
    int64_t tz_offset_ms() override {
        DYNAMIC_TIME_ZONE_INFORMATION tzi;
        const DWORD id = GetDynamicTimeZoneInformation(&tzi);
        if (id == TIME_ZONE_ID_INVALID) return 0;  // no zone data: stamp stays UTC
        LONG bias = tzi.Bias;  // minutes to add to local time to get UTC
        if (id == TIME_ZONE_ID_DAYLIGHT) bias += tzi.DaylightBias;
        return -static_cast<int64_t>(bias) * 60000;  // flip to "local is east of UTC"
    }
};

}  // namespace poe2
