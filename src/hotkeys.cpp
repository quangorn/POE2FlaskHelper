#include "hotkeys.h"

namespace poe2 {

// mod::kAlt/kCtrl/kShift/kWin match MOD_ALT/MOD_CONTROL/MOD_SHIFT/MOD_WIN
// (the values keys.cpp parses for RegisterHotKey).
bool register_toggle_hotkey(HWND hwnd, const Hotkey& hk) {
    // MOD_NOREPEAT (0x4000): holding Ctrl+9 down must deliver ONE WM_HOTKEY, not a
    // key-repeat stream of toggles + WAVs (review S2-3, fixed 2026-11-08).
    constexpr uint32_t kModNoRepeat = 0x4000;
    return RegisterHotKey(hwnd, static_cast<int>(kToggleHotkeyId),
                          static_cast<UINT>(hk.mods | kModNoRepeat),
                          static_cast<UINT>(hk.vk)) != 0;
}

void unregister_toggle_hotkey(HWND hwnd) {
    if (hwnd) UnregisterHotKey(hwnd, static_cast<int>(kToggleHotkeyId));
}

}  // namespace poe2
