// Win32 file-stamp probe (plan-hot-reload.md §3, exe layer): CreateFileW with
// FILE_SHARE_READ | FILE_SHARE_WRITE so an editor that keeps the file open is still
// readable, then GetFileTime(LAST_WRITE) + GetFileSizeEx. dwDesiredAccess = 0 asks for
// no access rights (GetFileTime/GetFileSizeEx work on a plain handle), so the probe
// takes no lock of its own. FILETIME's unit is the 100 ns tick poe2::FileStamp::mtime
// stores (src/config.h), so the conversion is a plain ULARGE_INTEGER read - no scaling.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

#include "file_stamp.h"
#include "win_text.h"  // shared to_wide (single exe-layer implementation, minor m10)

namespace poe2 {

bool file_stamp(const std::string& path, FileStamp& out) {
    out = FileStamp{};  // a failed probe leaves no half-read stamp behind
    const HANDLE h = CreateFileW(to_wide(path).c_str(), 0,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    // Checked one after the other, never in an && chain: a short-circuit leaves the second
    // variable uninitialized for the code below (measured: C4701 on the first build).
    FILETIME last_write;
    if (!GetFileTime(h, nullptr, nullptr, &last_write)) {
        CloseHandle(h);
        return false;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size)) {
        CloseHandle(h);
        return false;
    }
    CloseHandle(h);  // the probe holds nothing: metadata only, one short open per cycle
    ULARGE_INTEGER t;
    t.LowPart = last_write.dwLowDateTime;
    t.HighPart = last_write.dwHighDateTime;
    out.mtime = static_cast<uint64_t>(t.QuadPart);  // 100 ns ticks, config.h FileStamp
    out.size = static_cast<uint64_t>(size.QuadPart);
    return true;
}

}  // namespace poe2
