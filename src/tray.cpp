#include "tray.h"

#include <shellapi.h>   // Shell_NotifyIconW, NOTIFYICONDATA

#include "hotkeys.h"  // kToggleHotkeyId (the id registered on this window)

namespace poe2 {

// Tray callback message. WM_APP range so it cannot collide with system messages.
constexpr UINT kTrayMsg = WM_APP + 1;

const wchar_t* const kTrayWindowClass = L"POE2FlaskHelperTrayWindow";

namespace {

std::wstring to_wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        w.data(), n);
    return w;
}

void copy_wide(const std::wstring& src, wchar_t* dst, size_t cap) {
    size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
    for (size_t i = 0; i < n; ++i) dst[i] = src[i];
    dst[n] = L'\0';
}

}  // namespace

Tray::Tray(TrayHost& host) : host_(host) {
    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.uID = 1;  // one icon per process
}

Tray::~Tray() {
    if (icon_added_) remove_icon();
    if (hwnd_) {
        KillTimer(hwnd_, kStatusTimerId);  // status timer must not outlive the window
        DestroyWindow(hwnd_);              // NIM_DELETE already ran; window teardown
    }
    if (class_registered_) {
        // Balanced against RegisterClassW in create() (review S2-4, fixed 2026-11-08).
        UnregisterClassW(kTrayWindowClass, host_.hInst);
        class_registered_ = false;
    }
    hwnd_ = nullptr;
}

bool Tray::create() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = &Tray::wproc;
    wc.hInstance = host_.hInst;
    wc.hIcon = LoadIconW(host_.hInst, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kTrayWindowClass;
    if (!RegisterClassW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            host_.logger.log(LogLevel::Error, "cannot register the tray window class");
            return false;
        }
    } else {
        class_registered_ = true;   // ~Tray pairs this with UnregisterClassW
    }
    // No WS_VISIBLE: a hidden top-level window. FindWindowW still finds it
    // (FindWindow Remarks: "FindWindow searches invisible windows") - see tray.h.
    hwnd_ = CreateWindowExW(0, kTrayWindowClass, L"POE2FlaskHelper", 0, 0, 0, 0, 0,
                            nullptr, nullptr, host_.hInst, this);
    if (!hwnd_) {
        host_.logger.log(LogLevel::Error, "cannot create the hidden tray window");
        return false;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    return true;
}

LRESULT CALLBACK Tray::wproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Tray* self = reinterpret_cast<Tray*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self) return self->handle(msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT Tray::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_HOTKEY:
            // hotkeys.cpp registers kToggleHotkeyId on this window (SPEC §4.4).
            if (wp == static_cast<WPARAM>(kToggleHotkeyId)) host_.on_cmd(TrayCmd::Toggle);
            return 0;
        case kTrayMsg: {
            const UINT ev = LOWORD(lp);  // NOTIFYICON_VERSION_4: event in the low word
            // One right-click under NOTIFYICON_VERSION_4 delivers WM_RBUTTONDOWN(0x204),
            // WM_RBUTTONUP(0x205) and WM_CONTEXTMENU(0x7B). The menu fires ONLY on
            // WM_CONTEXTMENU - the v4-recommended right-click signal. Firing on
            // WM_RBUTTONUP as well popped the menu twice per one right-click.
            const bool menu = (ev == WM_CONTEXTMENU);
            // Hover/left/double-click/no-op are debug: a hover burst is ~50 events and
            // must not spam the info log (SPEC §7, review S2-2, fixed 2026-11-08).
            host_.logger.log(LogLevel::Debug, "tray callback event " + std::to_string(ev));
            if (menu) {
                // Actionable: only this event is an info line (SPEC §7 info list).
                host_.logger.log(LogLevel::Info, "tray callback: right-click menu request");
                show_menu();  // SPEC §4.3: one state-dependent toggle row + Exit
            }
            // Double click on the icon does nothing (SPEC §4.3); left click too.
            return 0;
        }
        case WM_TIMER:
            if (wp == static_cast<WPARAM>(kStatusTimerId)) {
                if (host_.on_timer) host_.on_timer();  // scanner status -> tooltip merge
            }
            return 0;
        case WM_COMMAND: {
            TrayCmd c = tray_cmd_from_id(LOWORD(wp));  // routing is pure core state.cpp
            if (c == TrayCmd::Exit) {
                host_.logger.log(LogLevel::Info, "exit requested (tray Exit menu)");
                PostQuitMessage(0);  // ends the message loop -> clean shutdown in main.cpp
                return 0;
            }
            if (c != TrayCmd::None) host_.on_cmd(c);
            return 0;
        }
        case WM_CLOSE:  // documented reviewer path: WM_CLOSE == Exit
            host_.logger.log(LogLevel::Info, "exit requested (WM_CLOSE)");
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void Tray::show_menu() {
    HMENU m = CreatePopupMenu();
    if (!m) return;
    // SPEC §4.3 (amended 2026-11-07): exactly two rows - the state-dependent toggle
    // item (text + checkmark from state.cpp) and Exit.
    const MenuItem tg = tray_toggle_item(host_.status.scanner_enabled);
    AppendMenuW(m, MF_STRING | (tg.checked ? MF_CHECKED : 0),
                static_cast<UINT>(tg.cmd), to_wide(tg.label).c_str());
    const MenuItem ex = tray_exit_item();
    AppendMenuW(m, MF_STRING, static_cast<UINT>(ex.cmd), to_wide(ex.label).c_str());

    // Log the built menu so a reviewer can verify labels/checkmarks from app.log.
    host_.logger.log(LogLevel::Info,
                     "menu: " + tg.label + "[" + std::string(tg.checked ? "x" : "") + "]"
                     " Exit[" + std::string(ex.checked ? "x" : "") + "]");
    POINT pt;
    GetCursorPos(&pt);
    // SetForegroundWindow before TrackPopupMenu: required for the menu to close on
    // the click outside (Windows behaviour for tray menus).
    BOOL fg = SetForegroundWindow(hwnd_);
    // No TPM_NONOTIFY: the menu then sends WM_COMMAND with the item id to hwnd_
    // contractually, routed by tray_cmd_from_id in handle(). No TPM_RETURNCMD:
    // TrackPopupMenu is modal and the app handles WM_COMMAND from the menu fine.
    BOOL ok = TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    if (ok) {
        host_.logger.log(LogLevel::Info, "popup menu shown: " + tg.label + " / Exit");
    } else {
        host_.logger.log(LogLevel::Warn, "popup menu failed (TrackPopupMenu, gle="
                        + std::to_string(GetLastError()) + ", foreground=" + std::to_string(fg) + ")");
    }
    DestroyMenu(m);
}

bool Tray::add_icon() {
    nid_.hWnd = hwnd_;  // Shell_NotifyIcon needs the notification window (NIM_ADD fails without it)
    // NIF_SHOWTIP: NOTIFYICON_VERSION_4 suppresses the standard tooltip by default
    // (NOTIFYICONDATA szTip Remarks, learn.microsoft.com); the flag restores the
    // hover tooltip for both ADD and MODIFY. Without it the icon shows no tooltip.
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid_.uCallbackMessage = kTrayMsg;
    nid_.hIcon = LoadIconW(host_.hInst, MAKEINTRESOURCEW(IDI_APP_ICON));
    copy_wide(to_wide(tooltip_text(host_.status)), nid_.szTip,
              sizeof nid_.szTip / sizeof nid_.szTip[0]);
    if (!Shell_NotifyIconW(NIM_ADD, &nid_)) {
        host_.logger.log(LogLevel::Warn, "Shell_NotifyIcon add failed - running without a tray icon");
        return false;
    }
    // Version 4: the tray event carries coordinates and the long tooltip is allowed.
    NOTIFYICONDATAW v = {};
    v.cbSize = sizeof(NOTIFYICONDATAW);
    v.hWnd = hwnd_;
    v.uID = nid_.uID;
    v.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &v)) {
        // Review S2-6 (fixed 2026-11-08): the icon works without v4 (short tooltip, classic event
        // layout), so this is a warning, not a failure.
        host_.logger.log(LogLevel::Warn, "Shell_NotifyIcon NIM_SETVERSION(v4) failed - "
                                         "classic event layout and 63-char tooltip used");
    }
    icon_added_ = true;
    host_.logger.log(LogLevel::Info, "tray icon added, tooltip: " + tooltip_text(host_.status));
    return true;
}

void Tray::update_tooltip() {
    if (!icon_added_) return;
    nid_.uFlags = NIF_TIP | NIF_SHOWTIP;  // v4 needs the flag on every MODIFY (add_icon note)
    copy_wide(to_wide(tooltip_text(host_.status)), nid_.szTip,
              sizeof nid_.szTip / sizeof nid_.szTip[0]);
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
    host_.logger.log(LogLevel::Info, "tooltip: " + tooltip_text(host_.status));
}

void Tray::remove_icon() {
    if (!icon_added_) return;
    nid_.uFlags = 0;
    if (Shell_NotifyIconW(NIM_DELETE, &nid_)) {
        host_.logger.log(LogLevel::Info, "tray icon removed (NIM_DELETE)");
    } else {
        host_.logger.log(LogLevel::Warn, "Shell_NotifyIcon delete failed");
    }
    icon_added_ = false;
}

}  // namespace poe2
