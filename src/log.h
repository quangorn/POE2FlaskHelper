#pragma once
// Asynchronous logger (SPEC §4.2, §7): the main thread only appends lines to an
// in-memory queue; a writer thread does all file I/O, so disk stalls never block
// the UI. UTF-8 file, rotation app.log -> app.log.1 -> ... (SPEC §7).
// The clock is injected as IClock so the core is testable without Win32 time.

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "clock.h"

namespace poe2 {

// One log message level (SPEC §7: info / warn / error; debug is used by future stages).
enum class LogLevel { Debug, Info, Warn, Error };

const char* level_name(LogLevel lvl);
// "debug"/"info"/"warn"/"error" -> level (config.json logging.level, SPEC §5).
bool level_from_string(const std::string& s, LogLevel& out);

// Formats one line in the SPEC §7 form (split out so the format is testable
// without a file): 2026-11-07 12:34:56.789 [info] message
// The stamp is LOCAL time, not UTC (SPEC §7): tz_offset_ms is the zone offset from
// UTC in ms (+3h = 10800000, negative west of UTC), taken as a fixed snapshot for
// this line - DST changes land on the next line, which is enough for the app. Stamps
// below the epoch clamp to the epoch.
std::string format_log_line(int64_t ms, LogLevel lvl, const std::string& msg,
                            int64_t tz_offset_ms);

struct LogOptions {
    LogLevel min_level = LogLevel::Info;  // messages below it are dropped
    int max_size_kb = 1024;               // rotation threshold (SPEC §7), from config
    int keep_files = 3;                   // app.log + app.log.1 + ... (SPEC §7)
};

// path: full log file path (main.cpp builds it from the exe directory, SPEC §4.1).
// The writer thread starts in the constructor; the destructor drains the queue,
// joins the thread and closes the file.
class Logger {
public:
    Logger(std::string path, IClock& clock, LogOptions opts);
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(LogLevel lvl, const std::string& msg);
    // Blocks until everything queued so far is on disk (used at shutdown and in tests).
    void flush();
    // False when the writer thread could not open the log file (SPEC §8).
    bool healthy() const;

private:
    void worker();  // writer thread: all file I/O and rotation lives here

    std::string path_;
    IClock& clock_;
    LogOptions opts_;

    std::thread thread_;
    mutable std::mutex mtx_;  // mutable: healthy() is const and locks it
    std::condition_variable cv_work_;   // wakes the writer on queued messages / stop
    std::condition_variable cv_flush_;  // wakes flush() when the queue is drained
    std::deque<std::string> queue_;
    bool stop_ = false;
    bool pending_ = false;              // a batch is being written
    bool open_failed_ = false;          // file open failed: no further I/O attempts
};

}  // namespace poe2
