#pragma once
// Stage 2 pure application state and UI mapping (SPEC §4.3 tray, §4.4 hotkey,
// §4.5 sound, §4.6 phases). No Win32 here: tray.cpp / hotkeys.cpp / sound.cpp only
// render these values, so every label, checkmark, tooltip string and WAV resource id
// is testable in poe2_tests (plan.md §1 "core knows no Win32 processes").

#include <cstdint>
#include <string>

#include "res_ids.h"

namespace poe2 {

// Resource ids as C++ constants (the same numbers app.rc compiles from res_ids.h):
// the state -> WAV mapping returns them and PlaySound consumes them.
constexpr uint16_t kIconResId = IDI_APP_ICON, kEnableWavResId = ID_ENABLE_WAV,
                   kDisableWavResId = ID_DISABLE_WAV;

// Menu command ids sent as WM_COMMAND(LOWORD(wParam)) by the tray popup menu: one
// state-dependent toggle item and Exit. The Exit id is the documented programmatic
// exit path for a reviewer (see the class-name note in src/tray.h).
constexpr uint32_t kCmdToggle = 1001, kCmdExit = 1002;

// Phases reachable before the scanner thread exists (SPEC §4.6 items 1-3). Stage 2
// wires enable/disable to state only; the reading phase arrives in stage 3.
// AdminRequired (SPEC §8, stage 5): OpenProcess answered ERROR_ACCESS_DENIED - the
// game is elevated and the helper is not. It is a phase (not a separate AppStatus
// flag) so it travels to the UI through the same ScannerCtl snapshot as the others,
// and the next successful attach replaces it (recovery is automatic).
// InMenu (SPEC §4.6 item 5, added 2026-11-08): the game process is found and
// openable, but it is not in a zone (main menu, character selection, zone loading:
// GameStates / States[4] / AreaInstance read zero). SPEC §8 lists this as a normal
// status, so the handle stays open and the session polls - it is not a chain break,
// and it is not "waiting for game" either: the process IS there. No PID is published
// to the UI (the label carries none), the scanner reads Vital' values in Attached only.
enum class Phase { ScannerOff, WaitingForGame, InMenu, Attached, AdminRequired };

// Commands the app reacts to: the menu toggle item, the hotkey, Exit.
enum class TrayCmd { None, Toggle, Exit };

struct AppStatus {
    bool scanner_enabled = false;
    Phase phase = Phase::ScannerOff;
    uint32_t pid = 0;          // attached process, meaningful in Phase::Attached (stage 3)
    bool hotkey_busy = false;  // SPEC §4.4: RegisterHotKey failed, the app keeps working
    std::string hotkey_text;   // registered hotkey string, e.g. "Ctrl+9"
};

// SPEC §4.3 tooltip strings: "Scanner off" / "Waiting for game" / "In menu / loading"
// / "Attached (PID n)" (SPEC §8 adds the "Admin rights required" status for a denied
// OpenProcess).
std::string phase_label(const AppStatus& st);
// Tooltip text for Shell_NotifyIcon, clamped to the classic szTip limit (63 chars;
// v4 allows 127, see the state.cpp note). hotkey_busy appends the busy status (SPEC §4.4).
std::string tooltip_text(const AppStatus& st);

struct MenuItem { std::string label; bool checked; uint32_t cmd; };

// SPEC §4.3 (amended 2026-11-07): the right-click menu has exactly two rows - one
// toggle item and Exit. The toggle item's text reflects the scanner state
// ("Enabled" / "Disabled") and its checkmark reflects it as well (checked when
// enabled). Clicking it is the same toggle as the hotkey.
MenuItem tray_toggle_item(bool scanner_enabled);
// The Exit row (never checked).
MenuItem tray_exit_item();

// WM_COMMAND id -> command; unknown id -> TrayCmd::None.
TrayCmd tray_cmd_from_id(uint32_t cmd_id);

// SPEC §4.5 mapping: enable -> enable.wav, disable -> disable.wav (resource ids from
// res_ids.h, the same numbers app.rc uses).
uint16_t wav_res_id(bool enabled);

// Set the scanner state (the single place that owns state transitions): updates
// phase/pid and returns the WAV resource id to play (SPEC §4.5). Pure.
uint16_t set_scanner_enabled(AppStatus& st, bool enabled);

// Merge the scanner thread's atomic snapshot (scanner_ctl.h) into AppStatus: the main
// thread only reads the snapshot, never memory (SPEC §4.2). A disabled scanner is
// always ScannerOff. Returns true when the displayed state changed, so main.cpp can
// refresh the tooltip exactly on changes (no per-tick tray updates).
bool apply_scanner_snapshot(AppStatus& st, Phase p, uint32_t pid);

}  // namespace poe2
