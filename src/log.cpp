#include "log.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace poe2 {

const char* level_name(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        case LogLevel::Error: return "error";
    }
    return "info";  // unreachable
}

bool level_from_string(const std::string& s, LogLevel& out) {
    if (s == "debug") { out = LogLevel::Debug; return true; }
    if (s == "info") { out = LogLevel::Info; return true; }
    if (s == "warn") { out = LogLevel::Warn; return true; }
    if (s == "error") { out = LogLevel::Error; return true; }
    return false;
}

namespace {

bool is_leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }

// Civil date from days since 1970-01-01. Simple year loop: a few dozen iterations
// per log line - trivially cheap, and impossible to get subtly wrong.
void civil_from_days(int64_t z, int& y, unsigned& m, unsigned& d) {
    int64_t days = z;
    y = 1970;
    while (days >= (is_leap(y) ? 366 : 365)) {
        days -= (is_leap(y) ? 366 : 365);
        ++y;
    }
    static const unsigned mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned midx = 0;
    unsigned len = mdays[midx] + (midx == 1 && is_leap(y) ? 1 : 0);
    while (days >= len) {
        days -= len;
        ++midx;
        len = mdays[midx] + (midx == 1 && is_leap(y) ? 1 : 0);
    }
    m = midx + 1;
    d = static_cast<unsigned>(days) + 1;
}

}  // namespace

std::string format_log_line(int64_t ms, LogLevel lvl, const std::string& msg,
                            int64_t tz_offset_ms) {
    // UTC -> local: the user's clock, not the machine's epoch time (SPEC §7).
    const int64_t local = ms + tz_offset_ms;
    if (local < 0) {
        // Local stamp would fall before the epoch (negative offset near 1970):
        // clamp to the epoch rather than printing a pre-epoch civil date.
        const std::string head = "1970-01-01 00:00:00.000";
        return head + " [" + level_name(lvl) + "] " + msg + "\n";
    }
    const int64_t sec = local / 1000;
    const int millis = static_cast<int>(local % 1000);
    const int64_t day = sec / 86400;
    int64_t tod = sec - day * 86400;
    int y; unsigned m, d;
    civil_from_days(day, y, m, d);
    char head[32];
    std::snprintf(head, sizeof head, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  y, m, d, static_cast<int>(tod / 3600), static_cast<int>((tod / 60) % 60),
                  static_cast<int>(tod % 60), millis);
    std::string line = head;
    line += " [";
    line += level_name(lvl);
    line += "] ";
    line += msg;
    line += '\n';
    return line;
}

Logger::Logger(std::string path, IClock& clock, LogOptions opts)
    : path_(std::move(path)), clock_(clock), opts_(opts) {
    thread_ = std::thread(&Logger::worker, this);
}

Logger::~Logger() {
    {
        std::lock_guard lk(mtx_);
        stop_ = true;
    }
    cv_work_.notify_all();
    if (thread_.joinable()) thread_.join();  // drains the queue, joins
}

void Logger::log(LogLevel lvl, const std::string& msg) {
    bool wake = false;
    {
        std::lock_guard lk(mtx_);
        // opts_ is read under the mutex (plan-hot-reload.md §1b): set_options() can change
        // min_level at any time, the pre-reload lock-free read raced with it.
        if (lvl >= opts_.min_level) {
            // The stamp is local time: the zone offset is read from the clock per line
            // (SPEC §7). SystemClock snapshots the zone as it is right now; FakeClock uses
            // an explicit offset so tests stay deterministic.
            queue_.push_back(format_log_line(clock_.now_ms(), lvl, msg, clock_.tz_offset_ms()));
            wake = true;
        }
    }
    if (wake) cv_work_.notify_one();
}

void Logger::set_options(LogOptions opts) {
    // The whole LogOptions is swapped under the mutex: readers see one complete set of
    // options, never a mix of a new level with an old rotation threshold.
    std::lock_guard lk(mtx_);
    opts_ = opts;
}

void Logger::flush() {
    std::unique_lock lk(mtx_);
    cv_flush_.wait(lk, [this] { return queue_.empty() && !pending_; });
}

bool Logger::healthy() const {
    std::lock_guard lk(mtx_);
    return !open_failed_;
}

void Logger::worker() {
    std::ofstream f;
    size_t bytes = 0;
    namespace fs = std::filesystem;
    const fs::path log_path(path_);
    const fs::path dir = log_path.parent_path();
    const std::string base = log_path.filename().string();

    while (true) {
        std::vector<std::string> batch;
        LogOptions opts;
        {
            std::unique_lock lk(mtx_);
            cv_work_.wait(lk, [this] { return stop_ || !queue_.empty(); });
            batch.assign(queue_.begin(), queue_.end());
            queue_.clear();
            pending_ = !batch.empty();
            // Hot-reload (plan-hot-reload.md §1b): ONE options copy per batch under the
            // mutex, so a set_options() between batches changes max_size_kb / keep_files
            // for the next write. The pre-reload code read opts_ outside the lock.
            opts = opts_;
        }
        if (!batch.empty()) {
            // One threshold per batch (plan-hot-reload.md §1b), so a rotation change
            // applies to the very next write.
            const size_t max_bytes = static_cast<size_t>(opts.max_size_kb) * 1024;
            if (!f.is_open() && !open_failed_) {
                std::error_code ec;
                if (!dir.empty()) fs::create_directories(dir, ec);
                f.open(log_path, std::ios::binary | std::ios::app);
                if (!f.is_open()) {
                    std::lock_guard lk(mtx_);
                    open_failed_ = true;
                } else {
                    // The rotation budget is the size of the file on disk, not the bytes
                    // this process has written: a log that keeps growing across restarts
                    // must rotate on the next run too (SPEC §7).
                    std::error_code sec;
                    const auto sz = fs::file_size(log_path, sec);
                    bytes = sec ? 0 : static_cast<size_t>(sz);
                }
            }
            if (f.is_open()) {
                for (const auto& line : batch) {
                    f << line;
                    bytes += line.size();
                }
                f.flush();
                if (bytes > max_bytes) {
                    // Rotation (SPEC §7): app.log.(keep-1) is dropped, names shift up.
                    f.close();
                    std::error_code ec;
                    if (opts.keep_files > 1) {
                        // app.log.(keep-1) is dropped, then names shift up by one,
                        // then app.log becomes app.log.1 and a fresh app.log is opened.
                        fs::remove(dir / (base + "." + std::to_string(opts.keep_files - 1)), ec);
                        for (int i = opts.keep_files - 2; i >= 1; --i) {
                            fs::path older = dir / (base + "." + std::to_string(i));
                            if (fs::exists(older, ec)) {
                                fs::rename(older, dir / (base + "." + std::to_string(i + 1)), ec);
                            }
                        }
                        fs::rename(log_path, dir / (base + ".1"), ec);
                    } else {
                        fs::remove(log_path, ec);  // keep_files == 1: truncate by recreate
                    }
                    f.open(log_path, std::ios::binary | std::ios::app);
                    bytes = 0;
                }
            }
            {
                std::lock_guard lk(mtx_);
                pending_ = false;
            }
            cv_flush_.notify_all();
        }
        if (stop_) break;  // queue drained before the loop exits on stop
    }
    if (f.is_open()) f.close();
}

}  // namespace poe2
