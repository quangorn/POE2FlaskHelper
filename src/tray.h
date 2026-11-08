#pragma once
// Tray icon + hidden top-level window (SPEC §4.3, §4.2). App layer: Win32 lives here,
// every state/label decision comes from poe2_core (state.cpp) and is tested there.
//
// Window class name: poe2::kTrayWindowClass = "POE2FlaskHelperTrayWindow".
// The window is top-level (parent NULL) and created WITHOUT WS_VISIBLE. FindWindowW
// DOES find invisible windows (documented in the FindWindow Remarks: "FindWindow
// searches invisible windows, e.g. message windows"), so a hidden window is located
// directly with FindWindowW(L"POE2FlaskHelperTrayWindow", nullptr). Measured
// 2026-11-08 on the live app (WS_VISIBLE bit == 0, IsWindowVisible == 0): FindWindowW
// and EnumWindows + GetClassNameW == kTrayWindowClass return the same HWND.
//
// Programmatic hooks for a reviewer / smoke test (documented, reproducible):
//   HWND hwnd = FindWindowW(L"POE2FlaskHelperTrayWindow", nullptr);  // finds the hidden window
//   PostMessageW(hwnd, WM_COMMAND, poe2::kCmdToggle /* 1001 */, 0);  // == the switch
//   PostMessageW(hwnd, WM_COMMAND, poe2::kCmdExit /* 1002 */, 0);    // == Exit
//   PostMessageW(hwnd, WM_CLOSE, 0, 0)  -> DestroyWindow -> WM_DESTROY -> exit
// Both hooks work cross-process. Measured 2026-11-08 (stage 5 acceptance): PostMessageW
// and SendMessageW of WM_COMMAND reach the WndProc from a separate process - kCmdToggle
// flipped the scanner ("scanner enabled" + tooltip written), kCmdExit exited with code 0
// and WM_CLOSE exited with code 0, each through the clean shutdown in main.cpp: scanner
// thread stop+join, sound off, UnregisterHotKey, NIM_DELETE, log flush.
// Exit hooks do NOT complete shutdown while a modal menu is open: the TrackPopupMenu
// modal loop consumes PostQuitMessage, so an undismissed popup menu blocks the exit
// hook (the smoke gate of build.sh step 5b posts WM_CLOSE only when no menu is open).
// Measured 2026-11-08: WM_COMMAND kCmdExit(1002) under an open menu logged "exit
// requested (tray Exit menu)" and the app stayed in popup-menu mode; after the menu was
// dismissed the request never completed, and WM_CLOSE exited with rc 0.
// Dialog and menu are two different cases, measured 2026-11-08 from a separate process:
//   - external PostMessageW(WM_CLOSE) DOES dismiss the #32770 "already running" dialog:
//     it vanishes and the second instance exits with rc 0;
//   - external BM_CLICK to the #32770 dialog does NOT dismiss it (PostMessage and
//     SendMessage: the dialog stays alive, the instance stays alive); BM_CLICK to the
//     dialog's default Button child DOES - the dialog vanishes, rc 0.
// The popup menu is not a #32770 dialog. Measured 2026-11-08 from a separate process, in
// a clean session with no dialog open: EnumWindows sees the menu as the "#32768" window
// inside the app process - GetWindowThreadProcessId(menu) returns the app's pid and one of
// its thread ids. The menu has no #32770 handle, so an external tool cannot grab it with
// FindWindowW(L"#32770"): #32770 is a dialog class, not a menu class, and it only shows up
// if a dialog from another probe is open (measured: with a second-instance #32770 dialog
// up, EnumWindows returns that dialog during a menu pop and FindWindowW(L"#32770") returns
// it - so any absolute "no #32770 window" claim holds only in a clean session).
// The popup menu is enumerable from an external process BOTH ways, measured 2026-11-08
// from a separate process in a clean session: EnumWindows (above) gives the "#32768"
// window - GetWindowThreadProcessId returns the app pid and the app UI thread tid - and
// EnumThreadWindows of that same UI thread lists the menu too: under the TrackPopupMenu
// modal loop it returns 5 windows (#32768, SysShadow, POE2FlaskHelperTrayWindow,
// MSCTFIME UI, IME; the baseline before the pop is 2 - tray window and IME), and after
// Escape the menu drops out of that list (count 3: tray window, MSCTFIME UI, IME).
// GetGUIThreadInfo(that UI tid) from the external process returns TRUE in popup-menu
// mode: flags 0x14 (GUI_INMENUMODE | GUI_POPUPMENUMODE), and menu state is kept in the
// member named hwndMenuOwner (not "hwndMenu") - it equals the tray window handle, which
// is exactly the owner argument show_menu passes to TrackPopupMenu. After Escape the same
// call returns TRUE with flags 0x0 and hwndMenuOwner = NULL. Caveat measured in two
// environments: in one, GetGUIThreadInfo returned FALSE from a console probe that had
// created no window (gle=0); in the other, the same call worked from a window-less probe
// (ret=TRUE, correct popup-mode data). Creating the probe's own window (desktop attach)
// is a safe precaution, not a requirement. The reproducible external dismissal is
// WM_KEYDOWN VK_ESCAPE posted to the tray window queue: the menu closes, TrackPopupMenu
// returns (app.log "popup menu shown"), the consumed exit request does not complete, and
// a later WM_CLOSE exits rc 0.
// show_menu() calls SetForegroundWindow(hwnd_) itself, then TrackPopupMenu. Measured
// 2026-11-08: every posted right-click gesture popped the menu with no external
// SetForegroundWindow call at all. LockSetForegroundWindow(LSFW_LOCK) from an external
// process returned TRUE, and the gesture under the lock still popped the menu with no warn
// - external foreground locking does not prevent the pop (LSFW_LOCK/LSFW_UNLOCK are the
// documented constants; LSFW_GLOBAL is not one). The only reproducible TrackPopupMenu
// failure is a nested gesture posted while a modal loop is already open: the app logs warn
// "popup menu failed (TrackPopupMenu, gle=1446, foreground=1)" - its own
// SetForegroundWindow returned 1, so foreground was not the cause - and no second menu
// appears. An external tool that owns the foreground first can hand it to the tray window;
// that helps, it is not required.
//
// A right-click on the icon CAN be simulated as a reviewer hook between processes of
// the same integrity level. The tray callback message is WM_APP + 1 (a private message):
// Windows UIPI filters messages above WM_USER sent from a lower integrity level to a
// higher one (documented in the ChangeWindowMessageFilterEx Remarks), so when the helper
// runs elevated (the game-as-admin case of SPEC §2.3) an external medium-IL tool cannot
// post the callback and show_menu() is reachable only through the real shell callback.
// Measured 2026-11-08: between two processes of the same integrity WM_APP + 1 IS
// delivered and the menu pops - the hook works, but depends on the integrity of both
// sides. The gesture is PostMessageW(hwnd, WM_APP + 1, 0, 0x7B): measured, the menu pops
// on this gesture with no external SetForegroundWindow call (show_menu() calls
// SetForegroundWindow(hwnd_) itself, then TrackPopupMenu); an external tool that owns the
// foreground first can hand it to the tray window, which helps, it is not required.
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

// Config timer id (plan-hot-reload.md §3): the 5 s config.json stamp check runs on the
// same hidden window; WM_TIMER calls TrayHost::on_config_timer. Stage 3 landed the id,
// the dispatch and the KillTimer pair; stage 4 (main.cpp) sets the 5000 ms SetTimer and
// assigns the handler body that reads the stamp, reloads the config and plays reload.wav.
constexpr UINT_PTR kConfigTimerId = 2;

// Tray dependencies injected by main.cpp (app layer): instance, logger, live state,
// the command handler that performs toggle/sound, the timer handler that polls
// the scanner status snapshot, and the config timer handler of the 5 s hot-reload
// cycle (plan-hot-reload.md §3).
struct TrayHost {
    HINSTANCE hInst;
    Logger& logger;
    AppStatus& status;
    std::function<void(TrayCmd)> on_cmd;
    std::function<void()> on_timer;
    std::function<void()> on_config_timer;
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
