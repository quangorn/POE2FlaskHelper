#ifndef POE2_WIN_TEXT_H
#define POE2_WIN_TEXT_H
// UTF-8 -> wide conversion for the exe layer: ONE implementation shared by tray.cpp and
// file_stamp.cpp (the third per-TU copy of stage 3, review minor m10, closed 2026-11-08).
// main.cpp's to_utf8 is the reverse direction (wide -> UTF-8, GetModuleFileNameW -> Paths)
// and stays in its TU; game_process.cpp carries the same reverse-direction copy for
// QueryFullProcessImageNameW — both reverse copies are out of this header's forward scope.
// Header-only inline: no new exe TU, no CMake change.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace poe2 {

inline std::wstring to_wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        w.data(), n);
    return w;
}

}  // namespace poe2

#endif  // POE2_WIN_TEXT_H
