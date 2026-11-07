// Real IProcessProvider (SPEC §4.6/§4.8, app layer): EnumProcesses +
// QueryFullProcessImageNameW for discovery, GetForegroundWindow +
// GetWindowThreadProcessId for the foreground check, OpenProcess(limited) for
// liveness. Compiled into the exe only; the interface is in src/game_process.h.
// NOTE: the header is NOT named process.h: the UCRT ships <process.h>, and a
// src/process.h in the include path shadows it for <thread>'s own include,
// breaking _beginthreadex.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <psapi.h>

#include <string>
#include <vector>

#include "game_process.h"

namespace poe2 {

namespace {

std::string to_utf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::string basename_lower(const std::string& path) {
    size_t base = path.find_last_of("\\/");
    std::string n = base == std::string::npos ? path : path.substr(base + 1);
    for (char& c : n) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return n;
}

}  // namespace

std::vector<ProcessInfo> Win32ProcessProvider::find(const std::vector<std::string>& names) {
    std::vector<DWORD> pids(1024);
    DWORD need = 0;
    if (!EnumProcesses(pids.data(), static_cast<DWORD>(pids.size() * sizeof(DWORD)), &need)) {
        return {};
    }
    std::vector<ProcessInfo> out;
    const DWORD count = need / static_cast<DWORD>(sizeof(DWORD));
    for (DWORD i = 0; i < count; ++i) {
        const DWORD pid = pids[i];
        if (!pid) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) continue;   // protected process: not our game, cannot be read anyway
        wchar_t buf[1024];
        DWORD sz = 1024;
        if (QueryFullProcessImageNameW(h, 0, buf, &sz)) {
            const std::string path = to_utf8(buf);
            for (const auto& name : names) {
                if (exe_name_match(path, name)) {
                    out.push_back(ProcessInfo{pid, basename_lower(path), path});
                    break;
                }
            }
        }
        CloseHandle(h);
    }
    return out;
}

bool Win32ProcessProvider::is_foreground(uint32_t pid) {
    HWND w = GetForegroundWindow();
    if (!w) return false;
    DWORD fg = 0;
    GetWindowThreadProcessId(w, &fg);
    return fg == pid;
}

bool Win32ProcessProvider::alive(uint32_t pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                           static_cast<DWORD>(pid));
    if (!h) return false;
    CloseHandle(h);
    return true;
}

}  // namespace poe2
