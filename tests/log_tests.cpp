// Logger tests: level filtering, exact line format, rotation naming, flush at
// shutdown (SPEC §7, plan.md §3 Stage 1). Deterministic via FakeClock, no sleeps.
#include "test.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "app.h"
#include "log.h"
#include "fakes/FakeClock.h"
#include "test_tmp.h"

using poe2::format_log_line;
using poe2::LogLevel;
using poe2::Logger;
using poe2::LogOptions;

// 2026-11-07 12:34:56.789 UTC, ms since epoch (verified against python datetime).
static const int64_t kMs = 1794054896789LL;

TEST(log_line_format_exact) {
    // Offset pinned to 0 so the expected strings are the UTC wall clock and the
    // test does not depend on the host timezone.
    CHECK_EQ(format_log_line(kMs, LogLevel::Info, "hello", 0),
             std::string("2026-11-07 12:34:56.789 [info] hello\n"));
    CHECK_EQ(format_log_line(kMs, LogLevel::Debug, "d", 0),
             std::string("2026-11-07 12:34:56.789 [debug] d\n"));
    CHECK_EQ(format_log_line(kMs, LogLevel::Warn, "w", 0),
             std::string("2026-11-07 12:34:56.789 [warn] w\n"));
    CHECK_EQ(format_log_line(kMs, LogLevel::Error, "e", 0),
             std::string("2026-11-07 12:34:56.789 [error] e\n"));
    // Epoch zero: 1970-01-01 00:00:00.000.
    CHECK_EQ(format_log_line(0, LogLevel::Info, "x", 0),
             std::string("1970-01-01 00:00:00.000 [info] x\n"));
    // Leap year 2024-02-29, and ms rounding down to .999.
    int64_t ms_2024 = 1709251199999LL;  // 2024-02-29 23:59:59.999 UTC
    CHECK_EQ(format_log_line(ms_2024, LogLevel::Info, "y", 0),
             std::string("2024-02-29 23:59:59.999 [info] y\n"));
}

TEST(log_line_format_local_timezone) {
    const int64_t h3 = 3 * 3600000LL, h1 = 3600000LL, h2 = 2 * 3600000LL, h5 = 5 * 3600000LL;
    // 17:00 UTC in a +3h zone prints as 20:00 (the SPEC §7 stamp is the user's clock).
    int64_t ms_1700 = 1794070800000LL;  // 2026-11-07 17:00:00.000 UTC
    CHECK_EQ(format_log_line(ms_1700, LogLevel::Info, "hello", h3),
             std::string("2026-11-07 20:00:00.000 [info] hello\n"));
    // Negative offset (west of UTC): same instant, earlier clock.
    CHECK_EQ(format_log_line(kMs, LogLevel::Info, "hello", -h5),
             std::string("2026-11-07 07:34:56.789 [info] hello\n"));
    // The offset rolls the stamp over midnight into the next day.
    int64_t ms_2330 = 1794094200000LL;  // 2026-11-07 23:30:00.000 UTC
    CHECK_EQ(format_log_line(ms_2330, LogLevel::Info, "z", h1),
             std::string("2026-11-08 00:30:00.000 [info] z\n"));
    // ... and back into the previous day / month with a negative offset.
    int64_t ms_dec1 = 1796086800000LL;  // 2026-12-01 01:00:00.000 UTC
    CHECK_EQ(format_log_line(ms_dec1, LogLevel::Info, "z", -h2),
             std::string("2026-11-30 23:00:00.000 [info] z\n"));
    int64_t ms_0200 = 1794016800000LL;  // 2026-11-07 02:00:00.000 UTC
    CHECK_EQ(format_log_line(ms_0200, LogLevel::Info, "z", -h3),
             std::string("2026-11-06 23:00:00.000 [info] z\n"));
    // A stamp that lands on the local epoch boundary keeps the leap day rule:
    // 2024-02-29 23:59:59.999 UTC + 1 ms = 2024-03-01 00:00:00.000 local.
    CHECK_EQ(format_log_line(1709251199999LL, LogLevel::Info, "y", 1),
             std::string("2024-03-01 00:00:00.000 [info] y\n"));
    // Pre-epoch local stamp clamps to the epoch (documented in log.h).
    CHECK_EQ(format_log_line(0, LogLevel::Info, "x", -h3),
             std::string("1970-01-01 00:00:00.000 [info] x\n"));
    // The level/message part is untouched by the offset.
    CHECK_EQ(format_log_line(ms_1700, LogLevel::Error, "boom", h3),
             std::string("2026-11-07 20:00:00.000 [error] boom\n"));
}

TEST(log_level_names_round_trip) {
    LogLevel l;
    CHECK(poe2::level_from_string("debug", l) && l == LogLevel::Debug);
    CHECK(poe2::level_from_string("info", l) && l == LogLevel::Info);
    CHECK(poe2::level_from_string("warn", l) && l == LogLevel::Warn);
    CHECK(poe2::level_from_string("error", l) && l == LogLevel::Error);
    CHECK(!poe2::level_from_string("trace", l));
    CHECK(!poe2::level_from_string("", l));
    CHECK_EQ(std::string(poe2::level_name(LogLevel::Warn)), std::string("warn"));
}

TEST(log_file_stamp_is_local_time) {
    namespace fs = std::filesystem;
    fs::path dir = poe2::testing::tmp_dir("log_local_tz");
    poe2::testing::FakeClock clock;
    clock.set(kMs);
    clock.set_tz(3 * 3600000LL);  // explicit +3h: the expected string is deterministic
    {
        Logger log((dir / "logs" / "app.log").string(), clock, LogOptions{});
        log.log(LogLevel::Info, "local stamp");
        log.flush();
        CHECK(log.healthy());
        CHECK_EQ(poe2::testing::read_file(dir / "logs" / "app.log"),
                 std::string("2026-11-07 15:34:56.789 [info] local stamp\n"));
    }
}

TEST(log_level_filter) {
    namespace fs = std::filesystem;
    fs::path dir = poe2::testing::tmp_dir("log_filter");
    poe2::testing::FakeClock clock;
    clock.set(kMs);
    clock.set_tz(0);  // pinned: the format strings below are UTC-based
    LogOptions opts;
    opts.min_level = LogLevel::Info;
    opts.max_size_kb = 1024;
    opts.keep_files = 3;
    {
        Logger log((dir / "logs" / "app.log").string(), clock, opts);
        log.log(LogLevel::Debug, "secret debug value");  // below level: dropped
        log.log(LogLevel::Info, "info line");
        log.log(LogLevel::Warn, "warn line");
        log.log(LogLevel::Error, "error line");
        log.flush();
        CHECK(log.healthy());
        std::string text = poe2::testing::read_file(dir / "logs" / "app.log");
        CHECK(text.find("secret debug value") == std::string::npos);
        CHECK(text.find("[info] info line") != std::string::npos);
        CHECK(text.find("[warn] warn line") != std::string::npos);
        CHECK(text.find("[error] error line") != std::string::npos);
        CHECK_EQ(poe2::testing::count_lines(text), 3);
    }
}

TEST(log_rotation_keeps_names_per_spec) {
    namespace fs = std::filesystem;
    fs::path dir = poe2::testing::tmp_dir("log_rotation");
    poe2::testing::FakeClock clock;
    clock.set(kMs);
    clock.set_tz(0);  // pinned: the expected stamp below is UTC-based
    LogOptions opts;
    opts.min_level = LogLevel::Debug;
    opts.max_size_kb = 1;   // 1 KB cap
    opts.keep_files = 3;
    {
        Logger log((dir / "logs" / "app.log").string(), clock, opts);
        // Deterministic batches: every line is flushed before the next one.
        // Each line is exactly 36 bytes ("[info] Lxxx"), so 29 lines (1044 B)
        // cross the cap: rotations fire after L028, L057, L086.
        for (int i = 0; i < 100; ++i) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "L%03d", i);
            log.log(LogLevel::Info, buf);
            log.flush();
        }
        fs::path base = dir / "logs" / "app.log";
        CHECK(fs::exists(base));
        CHECK(fs::exists(dir / "logs" / "app.log.1"));
        CHECK(fs::exists(dir / "logs" / "app.log.2"));
        CHECK(!fs::exists(dir / "logs" / "app.log.3"));  // keepFiles = 3
        std::string cur = poe2::testing::read_file(base);
        std::string r1 = poe2::testing::read_file(dir / "logs" / "app.log.1");
        std::string r2 = poe2::testing::read_file(dir / "logs" / "app.log.2");
        CHECK_EQ(poe2::testing::count_lines(cur), 13);   // L087..L099
        CHECK_EQ(poe2::testing::count_lines(r1), 29);    // L058..L086
        CHECK_EQ(poe2::testing::count_lines(r2), 29);    // L029..L057
        CHECK(cur.find("L087") != std::string::npos && cur.find("L099") != std::string::npos
              && cur.find("L058") == std::string::npos);
        CHECK(r1.find("L058") != std::string::npos && r1.find("L086") != std::string::npos
              && r1.find("L057") == std::string::npos && r1.find("L087") == std::string::npos);
        CHECK(r2.find("L029") != std::string::npos && r2.find("L057") != std::string::npos
              && r2.find("L028") == std::string::npos && r2.find("L058") == std::string::npos);
        CHECK(r1.find("2026-11-07 12:34:56.789 [info]") != std::string::npos);
    }
}

TEST(log_rotation_across_restarts) {
    namespace fs = std::filesystem;
    fs::path dir = poe2::testing::tmp_dir("log_rotation_restarts");
    poe2::testing::FakeClock clock;
    clock.set(kMs);
    clock.set_tz(0);  // pinned for determinism
    LogOptions opts;
    opts.min_level = LogLevel::Debug;
    opts.max_size_kb = 1;
    opts.keep_files = 3;
    // Two separate Logger instances share the file (like two program runs).
    // Each run writes 20 lines of 36 bytes = 720 B - below the cap on its own.
    for (int run = 0; run < 2; ++run) {
        Logger log((dir / "logs" / "app.log").string(), clock, opts);
        for (int i = 0; i < 20; ++i) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "%c%03d", 'A' + run, i);
            log.log(LogLevel::Info, buf);
            log.flush();
        }
    }
    // Run 1: app.log = 720 B. Run 2 opens it, sees the size on disk, and rotates
    // as soon as the file passes 1024 B (the bug a per-process counter hides):
    // 720 + 9*36 = 1044 > 1024, so the rotation fires after the 9th line of run 2.
    std::string cur = poe2::testing::read_file(dir / "logs" / "app.log");
    std::string r1 = poe2::testing::read_file(dir / "logs" / "app.log.1");
    CHECK(fs::exists(dir / "logs" / "app.log"));
    CHECK(fs::exists(dir / "logs" / "app.log.1"));
    CHECK_EQ(poe2::testing::count_lines(cur), 11);  // B009..B019
    CHECK_EQ(poe2::testing::count_lines(r1), 29);   // A000..A018 + B000..B008
    CHECK(r1.find("A000") != std::string::npos && r1.find("B008") != std::string::npos
          && r1.find("B009") == std::string::npos);
    CHECK(cur.find("B009") != std::string::npos && cur.find("A000") == std::string::npos);
}

TEST(log_flush_at_shutdown) {
    namespace fs = std::filesystem;
    fs::path dir = poe2::testing::tmp_dir("log_shutdown");
    poe2::testing::FakeClock clock;
    clock.set(kMs);
    clock.set_tz(0);  // pinned: the expected stamp is UTC-based
    {
        Logger log((dir / "logs" / "app.log").string(), clock, LogOptions{});
        log.log(LogLevel::Info, "bye");  // no explicit flush: the destructor drains
    }  // destructor joins the writer thread here
    std::string text = poe2::testing::read_file(dir / "logs" / "app.log");
    CHECK_EQ(text, std::string("2026-11-07 12:34:56.789 [info] bye\n"));
}

TEST(log_file_is_utf8_lf) {
    namespace fs = std::filesystem;
    fs::path dir = poe2::testing::tmp_dir("log_utf8");
    poe2::testing::FakeClock clock;
    clock.set(kMs);
    clock.set_tz(0);  // pinned: the expected stamp is UTC-based
    {
        Logger log((dir / "logs" / "app.log").string(), clock, LogOptions{});
        // 65-byte line (longer than any fixed 64-byte snprintf buffer) must be
        // written complete.
        log.log(LogLevel::Info, "config.json created with defaults");
        log.flush();
        std::string text = poe2::testing::read_file(dir / "logs" / "app.log");
        CHECK_EQ(text, std::string("2026-11-07 12:34:56.789 [info] config.json created with defaults\n"));
        CHECK_EQ(text.find('\r'), std::string::npos);  // LF only, no CRLF translation
    }
}

// Plan-hot-reload.md §4 group 6 (stage 2, §1b): Logger::set_options() mid-run. The level
// change filters the very next line, and the rotation threshold + keepFiles naming apply to
// the next batch - opts_ is read under mtx_ in log() and once per batch in worker(), so a
// reload changes the behaviour while the writer thread is live. Deterministic: FakeClock,
// one line per batch (flush between), no sleeps.
TEST(log_set_options_midrun_level_and_rotation) {
    namespace fs = std::filesystem;
    fs::path dir = poe2::testing::tmp_dir("log_set_options");
    poe2::testing::FakeClock clock;
    clock.set(kMs);
    clock.set_tz(0);  // pinned: the expected stamps below are UTC-based
    LogOptions opts;
    opts.min_level = LogLevel::Info;
    opts.max_size_kb = 1024;
    opts.keep_files = 3;
    Logger log((dir / "logs" / "app.log").string(), clock, opts);

    // Pre-change: the debug line is below the level and is dropped.
    log.log(LogLevel::Debug, "dropped debug line");
    log.log(LogLevel::Info, "I000");
    log.flush();
    std::string t = poe2::testing::read_file(dir / "logs" / "app.log");
    CHECK(t.find("dropped debug line") == std::string::npos);
    CHECK_EQ(poe2::testing::count_lines(t), 1);

    // logging.level change (plan §2): set_options applies to the very next line.
    opts.min_level = LogLevel::Debug;
    log.set_options(opts);
    log.log(LogLevel::Debug, "debug after set");
    log.flush();
    t = poe2::testing::read_file(dir / "logs" / "app.log");
    CHECK(t.find("debug after set") != std::string::npos);
    CHECK_EQ(poe2::testing::count_lines(t), 2);   // 36 + 48 = 84 bytes on disk

    // maxSizeKb/keepFiles change mid-run: the 1 KB cap fires on the next batch, and the
    // rotation follows the NEW keepFiles (2), not the startup 3.
    // 84 + 27*36 = 1056 > 1024 -> the rotation cuts after R026.
    opts.max_size_kb = 1;
    opts.keep_files = 2;
    log.set_options(opts);
    for (int i = 0; i < 32; ++i) {   // R000..R031, one line per batch
        char buf[8];
        std::snprintf(buf, sizeof buf, "R%03d", i);
        log.log(LogLevel::Info, buf);
        log.flush();
    }
    fs::path base = dir / "logs" / "app.log";
    CHECK(fs::exists(base));
    CHECK(fs::exists(dir / "logs" / "app.log.1"));
    CHECK(!fs::exists(dir / "logs" / "app.log.2"));   // the new keepFiles = 2
    std::string r1 = poe2::testing::read_file(dir / "logs" / "app.log.1");
    std::string cur = poe2::testing::read_file(base);
    CHECK_EQ(poe2::testing::count_lines(r1), 29);     // I000 + the debug line + R000..R026
    CHECK(r1.find("I000") != std::string::npos && r1.find("debug after set") != std::string::npos);
    CHECK(r1.find("R000") != std::string::npos && r1.find("R026") != std::string::npos
          && r1.find("R027") == std::string::npos);
    CHECK_EQ(poe2::testing::count_lines(cur), 5);     // R027..R031
    CHECK(cur.find("R027") != std::string::npos && cur.find("R031") != std::string::npos
          && cur.find("R026") == std::string::npos);
    // The new level stays in force after the rotation.
    log.log(LogLevel::Debug, "DBG tail");
    log.flush();
    CHECK(poe2::testing::read_file(base).find("DBG tail") != std::string::npos);
    CHECK_EQ(poe2::testing::count_lines(poe2::testing::read_file(base)), 6);
}

// S1-5: the log-open failure branch (src/log.cpp:162-164, open_failed_ = true; the line
// numbers moved with the hot-reload set_options work of stage 2, plan-hot-reload.md §1b). The
// log path sits under a REGULAR FILE, so the directory cannot be created and the file
// open fails. The logger reports healthy() == false, drops the lines, and every later
// log()/flush() must return without stalling or crashing (main.cpp:130-136 depends on
// this being a clean, non-blocking path: it logs the error and shows the MessageBox).
// BR-4 (2026-11-08): the third real-world variant of SPEC §8 - a read-only exe
// directory, i.e. an ACL that denies CreateFile write access on logs/app.log - reaches
// exactly this same open_failed_ = true branch: the open syscall fails, the writer
// thread never retries, healthy() is false and flush() returns. The two tests below
// exercise the branch through the filesystem shape (path blocked by a regular file /
// path is a directory); an ACL scenario needs a prepared ACL on a real path and is
// deliberately not exercised - it adds no new code path, so it is documented as
// equivalent to the tests here.
TEST(log_open_failure_is_unhealthy) {
    namespace fs = std::filesystem;
    fs::path dir = poe2::testing::tmp_dir("log_open_failure");
    {
        std::ofstream f(dir / "logs");  // "logs" is a file, not a directory
        f << "not a directory";
        f.close();
    }
    REQUIRE(fs::is_regular_file(dir / "logs"));
    poe2::testing::FakeClock clock;
    clock.set(kMs);
    clock.set_tz(0);
    {
        Logger log((dir / "logs" / "app.log").string(), clock, LogOptions{});
        log.log(LogLevel::Info, "first line");
        log.flush();                      // returns: the open failed once, no retries
        CHECK(!log.healthy());
        log.log(LogLevel::Error, "second line");
        log.flush();                      // must not stall on a file that never opens
        CHECK(!log.healthy());
    }  // destructor joins the writer thread: no crash
    // The blocker file is untouched and no log file exists.
    CHECK_EQ(poe2::testing::read_file(dir / "logs"), std::string("not a directory"));
    CHECK(!fs::exists(dir / "logs" / "app.log"));
}

// The same branch from the other side: the log path IS a directory, so opening it as
// a file fails. Healthy() must be false and flush() must return.
TEST(log_path_is_directory_is_unhealthy) {
    namespace fs = std::filesystem;
    fs::path dir = poe2::testing::tmp_dir("log_path_dir");
    fs::create_directories(dir / "app.log");  // a directory where the log file goes
    REQUIRE(fs::is_directory(dir / "app.log"));
    poe2::testing::FakeClock clock;
    clock.set(kMs);
    clock.set_tz(0);
    {
        Logger log((dir / "app.log").string(), clock, LogOptions{});
        log.log(LogLevel::Info, "line");
        log.flush();
        CHECK(!log.healthy());
    }
    CHECK(fs::is_directory(dir / "app.log"));  // the directory survives untouched
}
