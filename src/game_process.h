#pragma once
// IProcessProvider (plan.md §1): game process discovery and liveness/foreground
// checks. Win32-free interface; the real implementation is app layer
// (src/game_process.cpp: EnumProcesses + QueryFullProcessImageNameW +
// GetForegroundWindow). Tests inject FakeProcess.

#include <cstdint>
#include <string>
#include <vector>

namespace poe2 {

struct ProcessInfo {
    uint32_t pid = 0;
    std::string exe_name;   // lowercase basename, e.g. "pathofexile.exe"
    std::string exe_path;   // full path as the OS reports it
};

struct IProcessProvider {
    virtual ~IProcessProvider() = default;
    // Processes whose exe basename matches `names` case-insensitively (SPEC §4.6).
    virtual std::vector<ProcessInfo> find(const std::vector<std::string>& names) = 0;
    // SPEC §4.8: keys are sent only when the foreground window belongs to pid;
    // consumed by stage 4 evaluate under scanner.requireGameForeground.
    virtual bool is_foreground(uint32_t pid) = 0;
    // Is the process still alive (SPEC §4.6 item 4 - game restart detection).
    virtual bool alive(uint32_t pid) = 0;
};

// Real provider (app layer, src/game_process.cpp): EnumProcesses +
// QueryFullProcessImageNameW, GetForegroundWindow + GetWindowThreadProcessId,
// OpenProcess(limited) for liveness. Declared in this Win32-free header so poe2_core
// and main.cpp can reference the type; the implementation compiles into the exe.
// The header is deliberately NOT named process.h: that name is taken by the UCRT
// <process.h>, which <thread> includes for _beginthreadex, and a src/process.h
// earlier on the include path shadows it.
struct Win32ProcessProvider : IProcessProvider {
    std::vector<ProcessInfo> find(const std::vector<std::string>& names) override;
    bool is_foreground(uint32_t pid) override;
    bool alive(uint32_t pid) override;
};

// ASCII case-insensitive "basename equals" check, shared by the real provider and
// FakeProcess (pure, no Win32).
inline bool exe_name_match(const std::string& exe_path, const std::string& name) {
    size_t base = exe_path.find_last_of("\\/");
    const size_t start = base == std::string::npos ? 0 : base + 1;
    const size_t len = exe_path.size() - start;
    if (len != name.size()) return false;
    for (size_t i = 0; i < len; ++i) {
        char a = exe_path[start + i], b = name[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
        if (a != b) return false;
    }
    return true;
}

}  // namespace poe2
