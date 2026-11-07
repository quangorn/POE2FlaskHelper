#pragma once
// Global hotkey binding (SPEC §4.4). String parsing and validation live in poe2_core
// (keys.cpp, tests/hotkey_tests.cpp); the Win32 RegisterHotKey call is app layer.
// WM_HOTKEY is delivered to the hidden tray window (see src/tray.h).

#include <windows.h>

#include "keys.h"

namespace poe2 {

// Hotkey id used for RegisterHotKey; WM_HOTKEY carries it in wParam.
constexpr uint32_t kToggleHotkeyId = 1;

// Returns false when the combo is already taken by another process. The caller logs a
// warning, marks status.hotkey_busy (tooltip status) and the app keeps running
// (SPEC §4.4).
bool register_toggle_hotkey(HWND hwnd, const Hotkey& hk);
void unregister_toggle_hotkey(HWND hwnd);

}  // namespace poe2
