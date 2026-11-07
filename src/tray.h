#pragma once
// Tray icon + hidden top-level window (SPEC §4.3, §4.2). App layer: Win32 lives here,
// every state/label decision comes from poe2_core (state.cpp) and is tested there.
//
// Window class name: poe2::kTrayWindowClass = "POE2FlaskHelperTrayWindow".
// The window is top-level (parent NULL) and created WITHOUT WS_VISIBLE, so FindWindowW
// finds it while nothing is shown on screen.
//
// Programmatic exit for a reviewer / smoke test (documented, reproducible):
//   HWND hwnd = FindWindowW(L"POE2FlaskHelperTrayWindow", nullptr);
//   PostMessageW(hwnd, WM_COMMAND, poe2::kCmdExit /* 1002 */, 0);  // == clicking Exit
//   // equivalent: PostMessageW(hwnd, WM_CLOSE, 0, 0) -> DestroyWindow -> WM_DESTROY
// Both paths end the message loop (PostQuitMessage) and run the clean shutdown in
// main.cpp: scanner stub stop, sound off, UnregisterHotKey, NIM_DELETE, log flush.
//
// The menu item ids are pure core constants (src/state.cpp): kCmdToggle = 1001 flips
// the scanner exactly like the hotkey, kCmdExit = 1002 exits.
//
// No visible-window check: GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE == 0.

#include <windows.h>

#include <shellapi.h>  // NOTIFYICONDATAW for the member
#include <cstdint>
#include <functional>
#include <string>

#include "log.h"
#include "state.h"

namespace poe2 {

extern const wchar_t* const kTrayWindowClass;

// Tray dependencies injected by main.cpp (app layer): instance, logger, live state
// and the command handler that performs toggle/sound.
struct TrayHost {
    HINSTANCE hInst;
    Logger& logger;
    AppStatus& status;
    std::function<void(TrayCmd)> on_cmd;
};

class Tray {
public:
    explicit Tray(TrayHost& host);
    ~Tray();  // defensive NIM_DELETE if the icon is still there (no orphan icon)
    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    // Registers the window class and creates the hidden window (WM_HOTKEY, tray
    // callbacks and menu commands land in its WndProc).
    bool create();
    HWND hwnd() const { return hwnd_; }

    bool add_icon();      // NIM_ADD + NIM_SETVERSION(NOTIFYICON_VERSION_4), logged
    void update_tooltip();  // NIM_MODIFY with tooltip_text(status) (SPEC §4.3)
    void remove_icon();     // NIM_DELETE, logged; called from the shutdown path

private:
    static LRESULT CALLBACK wproc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void show_menu();  // exactly two rows: state-dependent toggle item + Exit (SPEC §4.3)

    NOTIFYICONDATAW nid_ = {};
    TrayHost& host_;
    HWND hwnd_ = nullptr;
    bool icon_added_ = false;
};

}  // namespace poe2
