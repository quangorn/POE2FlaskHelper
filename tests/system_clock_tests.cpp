// SystemClock (the app-layer IClock, SPEC §7): the zone-offset snapshot and the S1-12
// one-time lookup-failure flag. Deterministic: the flag is checked against the same
// Win32 call the clock uses, so no sleeps and no assumption about the host timezone.
#include "test.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

#include "log.h"
#include "system_clock.h"

TEST(system_clock_zone_flag) {
    poe2::SystemClock clock;
    // Fresh clock: no lookup has happened, so no failure is recorded (the flag is not
    // a stale "true" that would make main.cpp log the debug line on every run).
    CHECK(!clock.tz_lookup_failed());

    DYNAMIC_TIME_ZONE_INFORMATION tzi;
    const DWORD id = GetDynamicTimeZoneInformation(&tzi);
    const int64_t off = clock.tz_offset_ms();

    // The flag mirrors the API result exactly: TIME_ZONE_ID_INVALID -> stamp stays UTC
    // and the flag is set for the one-time debug record.
    CHECK_EQ(clock.tz_lookup_failed(), id == TIME_ZONE_ID_INVALID);
    if (id == TIME_ZONE_ID_INVALID) {
        CHECK_EQ(off, 0);
    } else {
        LONG bias = tzi.Bias;
        if (id == TIME_ZONE_ID_DAYLIGHT) bias += tzi.DaylightBias;
        CHECK_EQ(off, -static_cast<int64_t>(bias) * 60000);
        CHECK(!clock.tz_lookup_failed());  // a valid zone never sets the flag
    }

    // Sticky once set, and a second lookup does not clear it (one-time record).
    if (clock.tz_lookup_failed()) {
        clock.tz_offset_ms();
        CHECK(clock.tz_lookup_failed());
    }
}

// The exact startup debug line main.cpp writes once after the logger starts (S1-12):
// pinned as text, and pinned in the SPEC §7 line format at debug level with the zone
// offset forced to 0, so the stamp is deterministic.
TEST(system_clock_utc_debug_message) {
    const std::string msg = poe2::tz_utc_debug_message();
    CHECK_EQ(msg, std::string("time zone lookup failed (TIME_ZONE_ID_INVALID), log stamps are UTC"));
    CHECK_EQ(poe2::format_log_line(0, poe2::LogLevel::Debug, msg, 0),
             std::string("1970-01-01 00:00:00.000 [debug] time zone lookup failed "
                         "(TIME_ZONE_ID_INVALID), log stamps are UTC\n"));
}

// now_ms() must be the system clock in ms since the epoch: compared with the same
// FILETIME arithmetic done independently here (no sleeps, a tight bound).
TEST(system_clock_now_ms_matches_filetime) {
    poe2::SystemClock clock;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    const int64_t api = static_cast<int64_t>(u.QuadPart / 10000 - 11644473600000LL);
    const int64_t got = clock.now_ms();
    CHECK(got >= api && got <= api + 2000);  // same clock read twice, no sleep in between
}
