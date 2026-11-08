// Shared ScannerSession test fixture (plan.md §2): the synthetic layout + fake OS
// wired through the exact Deps struct main.cpp builds. Used by session_tests.cpp
// (stage 3) and vital_session_tests.cpp (stage 4). Everything is deterministic:
// FakeClock time, FakeProcess scenarios, FakeMemory image, RecordingKeySender
// presses; the logger writes to a fresh temp dir, flush() puts lines on disk before
// log-content checks read them. No sleeps, no real time, no game.

#pragma once

#include <memory>
#include <string>

#include "config.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeMemory.h"
#include "fakes/FakeProcess.h"
#include "fakes/RecordingKeySender.h"
#include "log.h"
#include "scanner_ctl.h"
#include "scanner_session.h"
#include "state.h"
#include "synthetic_layout.h"
#include "test_tmp.h"

namespace poe2 {
namespace testing {

using Deps = ScannerSession::Deps;   // MSVC: a nested type alias, not a using-decl

struct Fix {
    std::shared_ptr<FakeMemory> mem;
    FakeProcess proc;
    FakeClock clock;
    std::string log_path;
    Logger logger;
    FakeMemFactory factory;
    ScannerCtl ctl;
    ScannerCfg cfg;
    Config full;                     // vitals section for evaluate (stage 4)
    RecordingKeySender sender;
    Deps deps;
    ScannerSession session;

    Fix(const std::string& name, ScannerCfg cfg_init,
        Config full_init = poe2::config_defaults(),
        LogLevel level = LogLevel::Debug)
        : mem(std::make_shared<FakeMemory>()),
          log_path(poe2::testing::tmp_dir(name).string() + "/app.log"),
          logger(log_path, clock, make_opts(level)),
          factory{},
          ctl{},
          cfg(cfg_init),
          full(full_init),
          sender(),
          deps{proc, factory, clock, logger, rev34(), cfg, full, sender},
          session(deps, ctl) {
        build_synthetic(*mem, rev34());
        factory.mem = mem;
        clock.set(100000);
        sender.clock = &clock;       // press timestamps come from the fake clock
    }

    static LogOptions make_opts(LogLevel level) {
        LogOptions o;
        o.min_level = level;
        return o;
    }

    // Write a Vital's Current field in the synthetic Life (the tick reads it live).
    void set_current(int vital, uint32_t current) {
        const Offsets& o = rev34();
        int64_t base = 0;
        switch (vital) {
            case kHealth: base = o.health_base(); break;
            case kMana: base = o.mana_base(); break;
            case kEnergyShield: base = o.es_base(); break;
            case kWard: base = o.ward_base(); break;
            default: base = o.rage_base(); break;
        }
        mem->put_u32(Synth::kLife + static_cast<uint64_t>(base + o.CurrentOff), current);
    }

    // SPEC §4.6 item 5 (added 2026-11-08): "the game is not in a zone" is exactly the
    // AreaInstance pointer going to zero (main menu, character selection, zone loading).
    void set_in_zone(bool in_zone) {
        mem->put_u64(Synth::kIngame + static_cast<uint64_t>(rev34().AreaInstanceData),
                     in_zone ? Synth::kArea : 0);
    }

    std::string log_text() {
        logger.flush();
        return poe2::testing::read_file(log_path);
    }

    int count(const std::string& needle) {
        const std::string t = log_text();
        int n = 0;
        size_t pos = 0;
        while ((pos = t.find(needle, pos)) != std::string::npos) {
            ++n;
            pos += needle.size();
        }
        return n;
    }
};

}  // namespace testing
}  // namespace poe2
