#include "scanner_session.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <string>

#include "vital_math.h"

namespace poe2 {

namespace {

// SPEC §4.6 item 1: the process poll runs every 1-2 s while waiting for the game.
constexpr int kGamePollMs = 1000;
// SPEC §7: chain-loss / Offsets mismatch messages at most once per 5 s.
constexpr int64_t kLogWindowMs = 5000;
// Idle wait while the module is off (no reads, no enumeration; the event wakes the
// thread on toggle/stop, this is the fallback period).
constexpr int kOffDelayMs = 500;

std::string hx(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%llX", static_cast<unsigned long long>(v));
    return buf;
}

// Debug value line (SPEC §7: vital values are debug-level only).
std::string values_line(const TickResult& t) {
    std::string s = "tick: ";
    for (int i = 0; i < kVitalCount; ++i) {
        const VitalVals& v = t.vals[i];
        const int64_t unres = unreserved_amount(v.total, v.flat, v.pct);
        const int pr = unres > 0 ? static_cast<int>((100 * v.current + unres / 2) / unres) : 0;
        s += std::string(kVitalNames[i]) + " " + std::to_string(v.current) + "/" +
             std::to_string(v.total) + " (" + std::to_string(pr) + "%, reserved " +
             std::to_string(reserved_amount(v.total, v.flat, v.pct)) + ")";
        if (i + 1 < kVitalCount) s += " | ";
    }
    return s;
}

std::string phase_name(Phase p) {
    switch (p) {
        case Phase::ScannerOff: return "off";
        case Phase::WaitingForGame: return "waiting";
        default: return "attached";
    }
}

// SPEC §7 debug press line, exact format: "Health 23% < 50%, key=1".
std::string press_line(const VitalAction& a) {
    return std::string(kVitalNames[a.vital]) + " " + std::to_string(a.percent) +
           "% < " + std::to_string(a.threshold) + "%, key=" + a.key;
}

}  // namespace

bool log_throttle(int64_t now_ms, int64_t window_ms, int64_t& last_ms) {
    if (last_ms != INT64_MIN && now_ms - last_ms < window_ms) return false;
    last_ms = now_ms;
    return true;
}

ScannerSession::ScannerSession(Deps deps, ScannerCtl& ctl)
    : deps_(deps), ctl_(ctl) {
    last_loss_ms_ = INT64_MIN;
    last_mismatch_ms_ = INT64_MIN;
    last_open_ms_ = INT64_MIN;
    ctl_.phase.store(static_cast<int>(Phase::ScannerOff));
    ctl_.pid.store(0);
}

void ScannerSession::publish(Phase p, uint32_t pid) {
    ctl_.phase.store(static_cast<int>(p));
    ctl_.pid.store(p == Phase::Attached ? pid : 0);
    if (p != last_logged_phase_) {
        last_logged_phase_ = p;
        if (p == Phase::WaitingForGame) {
            // SPEC §7 info: the waiting status is a state change, logged once per
            // entry (the poll itself stays silent).
            deps_.log.log(LogLevel::Info, "waiting for game (no configured process found)");
        }
    }
}

void ScannerSession::detach_info(const std::string& reason) {
    if (!reader_) return;
    deps_.log.log(LogLevel::Info, "detached from PID " + std::to_string(pid_) +
                                  " (" + reason + ")");
    reader_.reset();
    pid_ = 0;
}

void ScannerSession::attach_step() {
    const auto found = deps_.proc.find(deps_.cfg.processNames);
    if (found.empty()) {
        publish(Phase::WaitingForGame, 0);
        return;
    }
    const uint32_t pid = found[0].pid;

    if (!reader_) {
        reader_ = deps_.factory.open(pid);
        if (!reader_) {
            // SPEC §8: OpenProcess failure is a warning; the poll continues.
            if (log_throttle(deps_.clock.now_ms(), kLogWindowMs, last_open_ms_)) {
                deps_.log.log(LogLevel::Warn, "OpenProcess failed for PID " +
                                              std::to_string(pid) +
                                              " (admin rights needed?), retrying");
            }
            publish(Phase::WaitingForGame, 0);
            return;
        }
        deps_.log.log(LogLevel::Info,
                      "attached PID " + std::to_string(pid) + ", ImageBase " +
                      hx(reader_->image_base()) + ", SizeOfImage " +
                      hx(reader_->image_size()));
        if (pid != last_opened_pid_) cache_.clear();   // a new process => signature rescanned
        last_opened_pid_ = pid;
        pid_ = pid;
    }

    if (!cache_.valid()) {
        const FullChainResult r = full_chain(*reader_, deps_.offs, cache_);
        if (!r.ok) {
            // SPEC §8: offsets do not fit -> "Offsets mismatch" status; warn level,
            // at most once per 5 s (SPEC §7).
            if (log_throttle(deps_.clock.now_ms(), kLogWindowMs, last_mismatch_ms_)) {
                deps_.log.log(LogLevel::Warn, "Offsets mismatch: " +
                                              std::string(chain_step_name(r.fail.step)) +
                                              " - " + r.fail.detail);
            }
            detach_info("chain did not resolve at attach");
            cache_.clear();
            publish(Phase::WaitingForGame, 0);
            return;
        }
        // SPEC §7: chain steps are debug-level.
        deps_.log.log(LogLevel::Debug,
                      "chain OK: global_addr " + hx(cache_.global_addr) +
                      ", LocalPlayers +" + hx(static_cast<uint64_t>(cache_.off_lp)) +
                      ", life index " + std::to_string(cache_.life_index));
    }
    publish(Phase::Attached, pid_);
}

void ScannerSession::tick_step() {
    const TickResult t = tick(*reader_, deps_.offs, cache_);
    if (t.ok) {
        deps_.log.log(LogLevel::Debug, values_line(t));
        // Stage 4 (SPEC §4.7/§4.8): the presses go out on this tick, from the
        // scanner thread only. requireGameForeground is folded into the foreground
        // bool by the session: is_foreground is asked on the scanner thread via
        // IProcessProvider. Invalid values never reach evaluate (tick !ok below).
        const bool fg = !deps_.cfg.requireGameForeground ||
                        deps_.proc.is_foreground(pid_);
        const std::vector<VitalAction> actions =
            evaluate(t.vals, deps_.full, fg, deps_.clock.now_ms(), vitals_state_);
        for (const VitalAction& a : actions) {
            deps_.sender.press(a.vk, a.extended);
            // SPEC §7: each press is a debug line; nothing of this appears at info.
            deps_.log.log(LogLevel::Debug, press_line(a));
        }
        return;
    }
    // SPEC §4.6 item 4: chain break => back to the process poll. The log is
    // rate-limited to once per 5 s (SPEC §7).
    if (log_throttle(deps_.clock.now_ms(), kLogWindowMs, last_loss_ms_)) {
        deps_.log.log(LogLevel::Info, "chain lost (PID " + std::to_string(pid_) + "): " +
                                      std::string(chain_step_name(t.fail.step)) +
                                      " - " + t.fail.detail);
    }
    const uint32_t pid = pid_;
    detach_info(t.fail.detail);
    if (!deps_.proc.alive(pid)) {
        // Game process died: caches are dead with it, the signature is rescanned on
        // the next attach (SPEC §4.6: rescan after a game restart).
        cache_.clear();
        deps_.log.log(LogLevel::Info, "process closed (PID " + std::to_string(pid) +
                                      ") - waiting for restart");
    }
    publish(Phase::WaitingForGame, 0);
}

void ScannerSession::step() {
    if (!ctl_.enabled.load() || ctl_.stop.load()) {
        // SPEC §4.6: with the module off, not a single ReadProcessMemory happens.
        detach_info("scanner disabled");
        cache_.clear();
        ctl_.phase.store(static_cast<int>(Phase::ScannerOff));
        ctl_.pid.store(0);
        last_logged_phase_ = Phase::ScannerOff;
        return;
    }
    const Phase cur = static_cast<Phase>(ctl_.phase.load());
    if (cur == Phase::Attached) {
        tick_step();
    } else {
        attach_step();
    }
}

int ScannerSession::step_delay_ms() const {
    if (!ctl_.enabled.load()) return kOffDelayMs;
    const Phase cur = static_cast<Phase>(ctl_.phase.load());
    if (cur == Phase::Attached) {
        return std::clamp(deps_.cfg.pollIntervalMs, 10, 1000);
    }
    return kGamePollMs;
}

}  // namespace poe2
