#pragma once
// FakeProcess (plan.md §2): the scenarios a scanner session needs -
// "no game / game appeared / game died / game not in foreground". find() counts its
// calls so a test can prove the session polls, and the memory read counter proves
// that "no game" runs do not touch memory.

#include <cstdint>
#include <string>
#include <vector>

#include "game_process.h"

namespace poe2 {
namespace testing {

struct FakeProcess : IProcessProvider {
    std::vector<ProcessInfo> games;   // the "alive game processes" list
    bool foreground = true;           // the foreground window belongs to the game
    int finds = 0;                    // find() call counter

    std::vector<ProcessInfo> find(const std::vector<std::string>& names) override {
        ++finds;
        std::vector<ProcessInfo> out;
        for (const auto& g : games) {
            for (const auto& n : names) {
                if (exe_name_match(g.exe_path, n)) { out.push_back(g); break; }
            }
        }
        return out;
    }

    bool is_foreground(uint32_t pid) override {
        return foreground && alive(pid);
    }

    bool alive(uint32_t pid) override {
        for (const auto& g : games) {
            if (g.pid == pid) return true;
        }
        return false;
    }

    void spawn(uint32_t pid, const std::string& exe = "C:/Games/Path of Exile 2/PathOfExile.exe") {
        games.push_back(ProcessInfo{pid, "pathofexile.exe", exe});
    }

    void kill() { games.clear(); }   // game process died (player quit / crash)
    void set_foreground(bool fg) { foreground = fg; }
};

// TU anchor for the FakeProcess module.
void fake_process_anchor();

}  // namespace testing
}  // namespace poe2
