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
// SPEC §4.6 item 5 (added 2026-11-08): in the menu / loading the same 1 s cadence
// applies - the handle is kept open, so a step costs one process poll + ~3 reads.
constexpr int kMenuPollMs = 1000;
// SPEC §7: chain-loss / Offsets mismatch messages at most once per 5 s.
constexpr int64_t kLogWindowMs = 5000;
// Idle wait while the module is off (no reads, no enumeration; the event wakes the
// thread on toggle/stop, this is the fallback period).
constexpr int kOffDelayMs = 500;
// Fallback poll interval for the case no snapshot has been installed yet. main.cpp
// installs the startup config before scanner.start() (plan-hot-reload.md §3), so this
// value is unreachable in the app; the SPEC §5 default keeps the cadence legal.
constexpr int kDefaultPollMs = 50;

std::string hx(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%llX", static_cast<unsigned long long>(v));
    return buf;
}

// Debug value line (SPEC §7: vital values are debug-level only). The percent is
// rounded half-even, exactly like poe_scan.py's round() on the live line.
std::string values_line(const TickResult& t) {
    std::string s = "tick: ";
    for (int i = 0; i < kVitalCount; ++i) {
        const VitalVals& v = t.vals[i];
        const int64_t unres = unreserved_amount(v.total, v.flat, v.pct);
        const int pr = percent_round(v.current, unres);
        s += std::string(kVitalNames[i]) + " " + std::to_string(v.current) + "/" +
             std::to_string(v.total) + " (" + std::to_string(pr) + "%, reserved " +
             std::to_string(reserved_amount(v.total, v.flat, v.pct)) + ")";
        if (i + 1 < kVitalCount) s += " | ";
    }
    return s;
}

// SPEC §4.6: the admin-denied OpenProcess is a state the poll leaves automatically.
std::string phase_name(Phase p) {
    switch (p) {
        case Phase::ScannerOff: return "off";
        case Phase::WaitingForGame: return "waiting";
        case Phase::InMenu: return "menu";
        case Phase::AdminRequired: return "admin-required";
        default: return "attached";
    }
}

// SPEC §7 (amended 2026-11-08, plan-release.md item 3): "Health 23% <= 50%, key=1". The
// comparison is strict (current*100 < threshold*unreserved), the printed percent is the
// half-even rounded one, so a rounded value CAN equal the threshold while the press is
// legal - "<=" keeps the line consistent with the fact.
std::string press_line(const VitalAction& a) {
    return std::string(kVitalNames[a.vital]) + " " + std::to_string(a.percent) +
           "% <= " + std::to_string(a.threshold) + "%, key=" + a.key;
}

// SPEC §7 (amended 2026-11-08): the ONE info line of a cooldown window where a Vital
// stayed below the threshold. Distinguishable from the press line by the " held:" tail,
// so tests count press lines as ", key=N\n" and held lines as " held: ".
std::string held_line(const VitalWait& w) {
    return std::string(kVitalNames[w.vital]) + " " + std::to_string(w.percent) +
           "% <= " + std::to_string(w.threshold) + "%, key=" + w.key +
           " held: cooldown left " + std::to_string(w.remaining_ms) + " ms";
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
    last_admin_ms_ = INT64_MIN;
    ctl_.phase.store(static_cast<int>(Phase::ScannerOff));
    ctl_.pid.store(0);
}

void ScannerSession::publish(Phase p, uint32_t pid) {
    const Phase prev = last_logged_phase_;
    ctl_.phase.store(static_cast<int>(p));
    ctl_.pid.store(p == Phase::Attached ? pid : 0);
    if (p == last_logged_phase_) return;
    last_logged_phase_ = p;
    // plan-release.md item 4 (SPEC §7): entering the menu closes the "first values at
    // info" right - the next zone entry logs its first tick at info again.
    if (p == Phase::InMenu) first_values_logged_ = false;
    // SPEC §7 (amended 2026-11-08): phase entries are state changes, logged ONCE per
    // entry - the polls themselves stay silent. The menu line carries the PID because
    // the handle is open on it; the tooltip label carries none (SPEC §4.3).
    if (p == Phase::WaitingForGame) {
        // SPEC §7 info: the waiting status is a state change, logged once per
        // entry (the poll itself stays silent). Only for the true "no process".
        deps_.log.log(LogLevel::Info, "waiting for game (no configured process found)");
    } else if (p == Phase::InMenu) {
        deps_.log.log(LogLevel::Info, "in menu / loading (PID " + std::to_string(pid) +
                                      ") - not in a zone");
    } else if (p == Phase::Attached && prev == Phase::InMenu) {
        // The handle was already open in the menu: this line marks entering the zone.
        deps_.log.log(LogLevel::Info, "in zone (PID " + std::to_string(pid) + ")");
    }
}

void ScannerSession::detach_info(const std::string& reason) {
    if (!reader_) return;
    deps_.log.log(LogLevel::Info, "detached from PID " + std::to_string(pid_) +
                                  " (" + reason + ")");
    reader_.reset();
    pid_ = 0;
    // SPEC §4.7 (amended 2026-11-08): the cooldown timers and the one-line-per-window
    // flags are per-session. A detach closes them: the new attach starts fresh, so the
    // "one held line per window" counting never carries over from a previous session,
    // and SPEC §4.7's no-arming rule (fire on the first tick after attach) holds.
    vitals_state_ = VitalSendState{};
    // plan-release.md item 4 (SPEC §7): the first values line is per-session too - the
    // new attach (or the next zone entry after a detach) gets its own info values line.
    first_values_logged_ = false;
}

void ScannerSession::attach_step(const Config& cfg) {
    const auto found = deps_.proc.find(cfg.scanner.processNames);
    if (found.empty()) {
        publish(Phase::WaitingForGame, 0);
        return;
    }
    const uint32_t pid = found[0].pid;

    if (!reader_) {
        uint32_t err = 0;
        reader_ = deps_.factory.open(pid, &err);
        if (!reader_) {
            if (err == kWinAccessDenied) {
                // SPEC §8: OpenProcess -> ERROR_ACCESS_DENIED is an ERROR line and the
                // "Admin rights required" tooltip. The game is elevated, the helper is
                // not. The poll continues; a successful open later clears the status.
                if (log_throttle(deps_.clock.now_ms(), kLogWindowMs, last_admin_ms_)) {
                    deps_.log.log(LogLevel::Error, "OpenProcess denied for PID " +
                                                   std::to_string(pid) +
                                                   " - admin rights required");
                }
                publish(Phase::AdminRequired, 0);
                return;
            }
            // SPEC §7: any other OpenProcess failure is a warning; the poll continues.
            if (log_throttle(deps_.clock.now_ms(), kLogWindowMs, last_open_ms_)) {
                deps_.log.log(LogLevel::Warn, "OpenProcess failed for PID " +
                                              std::to_string(pid) +
                                              " (code " + std::to_string(err) +
                                              "), retrying");
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
            if (r.not_in_zone) {
                // SPEC §4.6 item 5 (added 2026-11-08): the game process is openable and
                // the signature resolved, only the zone pointers are zero (main menu /
                // character selection / zone loading). SPEC §8 calls this a normal
                // status, so NO detach, no warn, no chain-loss line: the handle and
                // global_addr stay alive and menu_step() polls on the 1 s cadence.
                pid_ = pid;
                publish(Phase::InMenu, pid);
                return;
            }
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

void ScannerSession::tick_step(const Config& cfg) {
    const TickResult t = tick(*reader_, deps_.offs, cache_);
    if (t.ok) {
        // plan-release.md item 4 (SPEC §7 amendment, 2026-11-08): the FIRST successful tick
        // of the session logs its values line at info, so a live default-level run shows
        // what the scanner reads right after attach / zone entry; every later tick stays
        // debug-only (the level rules of §7, criterion §9-8 unchanged for the steady state).
        if (!first_values_logged_) {
            first_values_logged_ = true;
            deps_.log.log(LogLevel::Info, values_line(t));
        } else {
            deps_.log.log(LogLevel::Debug, values_line(t));
        }
        // Stage 4 (SPEC §4.7/§4.8): the presses go out on this tick, from the
        // scanner thread only. requireGameForeground is folded into the foreground
        // bool by the session: is_foreground is asked on the scanner thread via
        // IProcessProvider. Invalid values never reach evaluate (tick !ok below).
        // Hot-reload (plan-hot-reload.md §1b): the gate and the whole vitals section
        // come from the snapshot step() took for this tick, so a threshold/key/cooldown
        // change is applied on the next tick, within one pollIntervalMs.
        const bool fg = !cfg.scanner.requireGameForeground ||
                        deps_.proc.is_foreground(pid_);
        std::vector<VitalWait> waits;
        const std::vector<VitalAction> actions =
            evaluate(t.vals, cfg, fg, deps_.clock.now_ms(), vitals_state_, &waits);
        for (const VitalAction& a : actions) {
            deps_.sender.press(a.vk, a.extended);
            // SPEC §7 (amended 2026-11-08): each press is an INFO line (live tests read
            // the log at the default level, so presses must be visible there).
            deps_.log.log(LogLevel::Info, press_line(a));
        }
        for (const VitalWait& w : waits) {
            // One info line per Vital per cooldown window: evaluate() marks exactly one
            // record of each window with first=true.
            if (w.first) deps_.log.log(LogLevel::Info, held_line(w));
        }
        return;
    }
    // SPEC §4.6 item 5 (added 2026-11-08): leaving the zone (menu, character selection,
    // zone loading) is a normal status, not the chain break of SPEC §4.6 item 4. The
    // handle and global_addr stay alive, the reading cadence switches to the menu poll,
    // and publish() writes ONE line on the entry. A real chain break still detaches.
    if (t.not_in_zone) {
        publish(Phase::InMenu, pid_);
        return;
    }
    // SPEC §4.6 item 4: chain break => back to the process poll.
    chain_break(t.fail);
}

void ScannerSession::chain_break(const ChainFail& fail) {
    // A REAL chain break (the menu state is not one, SPEC §4.6 item 5): the loss line
    // is rate-limited to once per 5 s (SPEC §7), then the session goes back to the
    // process poll.
    if (log_throttle(deps_.clock.now_ms(), kLogWindowMs, last_loss_ms_)) {
        deps_.log.log(LogLevel::Info, "chain lost (PID " + std::to_string(pid_) + "): " +
                                      std::string(chain_step_name(fail.step)) +
                                      " - " + fail.detail);
    }
    const uint32_t pid = pid_;
    detach_info(fail.detail);
    if (!deps_.proc.alive(pid)) {
        // Game process died: caches are dead with it, the signature is rescanned on
        // the next attach (SPEC §4.6: rescan after a game restart).
        cache_.clear();
        deps_.log.log(LogLevel::Info, "process closed (PID " + std::to_string(pid) +
                                      ") - waiting for restart");
    }
    publish(Phase::WaitingForGame, 0);
}

void ScannerSession::menu_step(const Config& cfg) {
    // SPEC §4.6 item 5 (added 2026-11-08): the handle and global_addr are alive and the
    // game is not in a zone. The step re-checks the process first, so a game that
    // closed or restarted while in the menu leaves the state cleanly.
    // Hot-reload (plan-hot-reload.md §1b): the process name list is the snapshot's -
    // a new scanner.processNames takes effect on the next poll.
    const auto found = deps_.proc.find(cfg.scanner.processNames);
    if (found.empty()) {
        const uint32_t pid = pid_;
        detach_info("game closed in menu");
        cache_.clear();   // SPEC §4.6: the caches die with the process
        deps_.log.log(LogLevel::Info, "process closed (PID " + std::to_string(pid) +
                                      ") - waiting for restart");
        publish(Phase::WaitingForGame, 0);
        return;
    }
    if (found[0].pid != pid_) {
        // A restarted game (new PID): its signature is rescanned by attach_step.
        detach_info("game restarted in menu (PID " + std::to_string(pid_) + " -> " +
                    std::to_string(found[0].pid) + ")");
        cache_.clear();
        attach_step(cfg);
        return;
    }
    const TickResult t = tick(*reader_, deps_.offs, cache_);
    if (t.ok) {
        // The zone is ready: reading resumes on the open handle, with the ONE
        // zone-entry info line from publish() (SPEC §7).
        publish(Phase::Attached, pid_);
        return;
    }
    if (t.not_in_zone) {
        // Still menu / loading: stay in the phase, no line, no re-open (SPEC §4.6 item 5).
        publish(Phase::InMenu, pid_);
        return;
    }
    chain_break(t.fail);
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
    // Hot-reload (plan-hot-reload.md §1b): ONE snapshot copy per step. The UI thread
    // installs a new config at any time (set() swaps the shared_ptr under its mutex),
    // so one get() makes attach/tick/menu and evaluate() run on one complete immutable
    // config: a step never mixes the old pollIntervalMs with a new threshold, no race.
    const std::shared_ptr<const Config> snap = deps_.live->get();
    if (!snap) return;   // no snapshot installed yet (main.cpp sets it before start())
    const Phase cur = static_cast<Phase>(ctl_.phase.load());
    if (cur == Phase::Attached) {
        tick_step(*snap);
    } else if (cur == Phase::InMenu) {
        menu_step(*snap);
    } else {
        attach_step(*snap);
    }
}

int ScannerSession::step_delay_ms() const {
    if (!ctl_.enabled.load()) return kOffDelayMs;
    const Phase cur = static_cast<Phase>(ctl_.phase.load());
    if (cur == Phase::Attached) {
        // Hot-reload (plan-hot-reload.md §1b): the live scanner.pollIntervalMs, so a
        // reload changes the reading cadence on the next wait. This call is a separate
        // thread action outside step(), so it takes its own snapshot copy.
        const std::shared_ptr<const Config> snap = deps_.live->get();
        return std::clamp(snap ? snap->scanner.pollIntervalMs : kDefaultPollMs, 10, 1000);
    }
    // SPEC §4.6 item 5: the menu cadence is the process-poll cadence (1-2 s).
    if (cur == Phase::InMenu) return kMenuPollMs;
    return kGamePollMs;
}

}  // namespace poe2
