#include "state.h"

namespace poe2 {

namespace {

// Shell_NotifyIconW szTip is WCHAR[64]; clamping keeps the tray call valid.
constexpr size_t kTipMax = 63;

}  // namespace

std::string phase_label(const AppStatus& st) {
    switch (st.phase) {
        case Phase::ScannerOff:     return "Scanner off";
        case Phase::WaitingForGame: return "Waiting for game";
        case Phase::InMenu:         return "In menu / loading";  // SPEC §4.6 item 5
        case Phase::Attached:       return "Attached (PID " + std::to_string(st.pid) + ")";
        case Phase::AdminRequired:  return "Admin rights required";  // SPEC §8
    }
    return "Scanner off";  // unreachable, keeps /W4 happy
}

std::string tooltip_text(const AppStatus& st) {
    std::string t = phase_label(st);
    if (st.hotkey_busy) {
        // SPEC §4.4: a taken combo is a warning status shown in the tooltip, not
        // a fatal error - the app continues working.
        t += " - hotkey " + st.hotkey_text + " busy";
    }
    if (t.size() > kTipMax) t.resize(kTipMax);
    return t;
}

MenuItem tray_toggle_item(bool scanner_enabled) {
    // SPEC §4.3 (amended 2026-11-07): one row, text and checkmark follow the state.
    return {scanner_enabled ? "Enabled" : "Disabled", scanner_enabled, kCmdToggle};
}

MenuItem tray_exit_item() { return {"Exit", false, kCmdExit}; }

TrayCmd tray_cmd_from_id(uint32_t cmd_id) {
    switch (cmd_id) {
        case kCmdToggle: return TrayCmd::Toggle;
        case kCmdExit:   return TrayCmd::Exit;
        default:         return TrayCmd::None;
    }
}

uint16_t wav_res_id(bool enabled) {
    return enabled ? kEnableWavResId : kDisableWavResId;
}

uint16_t set_scanner_enabled(AppStatus& st, bool enabled) {
    st.scanner_enabled = enabled;
    st.pid = 0;  // the attached PID arrives with the scanner thread (stage 3)
    // SPEC §4.6: enabling starts the "waiting for process" phase; disabling drops
    // back to off. The scanner thread then drives the real phase.
    st.phase = enabled ? Phase::WaitingForGame : Phase::ScannerOff;
    return wav_res_id(enabled);
}

bool apply_scanner_snapshot(AppStatus& st, Phase p, uint32_t pid) {
    const Phase want = st.scanner_enabled ? p : Phase::ScannerOff;
    const uint32_t want_pid = (want == Phase::Attached) ? pid : 0;
    if (st.phase == want && st.pid == want_pid) return false;
    st.phase = want;
    st.pid = want_pid;
    return true;
}

}  // namespace poe2
