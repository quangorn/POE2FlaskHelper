#pragma once
// Tray icon + hidden top-level window (SPEC §4.3, §4.2). App layer: Win32 lives here,
// every state/label decision comes from poe2_core (state.cpp) and is tested there.
//
// Window class name: poe2::kTrayWindowClass = "POE2FlaskHelperTrayWindow".
// The window is top-level (parent NULL) and created WITHOUT WS_VISIBLE. FindWindowW
// does NOT find invisible windows, so a hidden window is located with:
//   EnumWindows + GetClassNameW (compare against kTrayWindowClass).
// That is the documented discovery path for this window.
//
// Programmatic hooks for a reviewer / smoke test (documented, reproducible):
//   HWND hwnd = FindWindowW(L"POE2FlaskHelperTrayWindow", nullptr);  // visible case
//   // hidden window: EnumWindows + GetClassNameW == kTrayWindowClass
//   PostMessageW(hwnd, WM_COMMAND, poe2::kCmdToggle /* 1001 */, 0);  // == the switch
//   PostMessageW(hwnd, WM_COMMAND, poe2::kCmdExit /* 1002 */, 0);    // == Exit
//   PostMessageW(hwnd, WM_CLOSE, 0, 0)  -> DestroyWindow -> WM_DESTROY -> exit
// Both hooks work cross-process. Measured 2026-11-08 (stage 5 acceptance): PostMessageW
// and SendMessageW of WM_COMMAND reach the WndProc from a separate process - kCmdToggle
// flipped the scanner ("scanner enabled" + tooltip written), kCmdExit exited with code 0
// and WM_CLOSE exited with code 0, each through the clean shutdown in main.cpp: scanner
// thread stop+join, sound off, UnregisterHotKey, NIM_DELETE, log flush.
//
// A right-click on the icon cannot be simulated as a reviewer hook. The tray callback
// message is WM_APP + 1 (a private message): Windows UIPI filters private messages sent
// from a lower integrity level to a higher one, so when the helper runs elevated (the
// game-as-admin case of SPEC §2.3) an external tool cannot post the callback and
// show_menu() is reachable only through the real shell callback. Measured 2026-11-08:
// between two processes of the same integrity the message IS delivered, so the hook
// depends on the integrity of both sides - never rely on it for a smoke test.
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

// Status timer id (SPEC §4.3 tooltip): main.cpp sets a 500 ms SetTimer on the tray
// window; WM_TIMER lands in the tray WndProc and calls TrayHost::on_timer, which
// merges the scanner thread's atomic snapshot into AppStatus (state.cpp).
constexpr UINT_PTR kStatusTimerId = 1;

// Tray dependencies injected by main.cpp (app layer): instance, logger, live state,
// the command handler that performs toggle/sound, and the timer handler that polls
// the scanner status snapshot.
struct TrayHost {
    HINSTANCE hInst;
    Logger& logger;
    AppStatus& status;
    std::function<void(TrayCmd)> on_cmd;
    std::function<void()> on_timer;
};

class Tray {
public:
    explicit Tray(TrayHost& host);
    ~Tray();  // defensive NIM_DELETE if the icon is still there (no orphan icon),
              // KillTimer, DestroyWindow, UnregisterClassW (class/window balance)
    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    // Registers the window class and creates the hidden window (WM_HOTKEY, tray
    // callbacks, menu commands and the status WM_TIMER land in its WndProc).
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
    bool class_registered_ = false;
};

}  // namespace poe2
