// Stage 2 pure state / UI mapping tests (SPEC §4.3-§4.5; plan.md §3 stage 2:
// "чистая функция состояния меню", "маппинг состояние -> WAV resource id").
// Deterministic, no sleeps, no Win32 UI - only poe2_core functions (src/state.cpp).
#include "test.h"

#include <string>

#include "res_ids.h"
#include "scanner_ctl.h"
#include "state.h"

using poe2::AppStatus;
using poe2::MenuItem;
using poe2::Phase;
using poe2::TrayCmd;

TEST(tray_menu_toggle_item_off) {
    // SPEC §4.3 (amended 2026-11-07): ONE row whose text and checkmark follow the
    // state. Scanner off: "Disabled", unchecked.
    const MenuItem tg = poe2::tray_toggle_item(false);
    CHECK_EQ(tg.label, std::string("Disabled"));
    CHECK_EQ(tg.checked, false);
    CHECK_EQ(tg.cmd, poe2::kCmdToggle);
}

TEST(tray_menu_toggle_item_on) {
    // Scanner on: "Enabled", checked.
    const MenuItem tg = poe2::tray_toggle_item(true);
    CHECK_EQ(tg.label, std::string("Enabled"));
    CHECK_EQ(tg.checked, true);
    CHECK_EQ(tg.cmd, poe2::kCmdToggle);
}

TEST(tray_menu_exit_row) {
    const MenuItem ex = poe2::tray_exit_item();
    CHECK_EQ(ex.label, std::string("Exit"));
    CHECK_EQ(ex.checked, false);  // Exit never has a checkmark
    CHECK_EQ(ex.cmd, poe2::kCmdExit);
    // The two rows have distinct command ids (the menu has exactly two rows).
    CHECK(poe2::tray_toggle_item(true).cmd != ex.cmd);
    CHECK(poe2::tray_toggle_item(false).cmd != ex.cmd);
}

TEST(menu_command_routing) {
    CHECK(poe2::tray_cmd_from_id(poe2::kCmdToggle) == TrayCmd::Toggle);
    CHECK(poe2::tray_cmd_from_id(poe2::kCmdExit) == TrayCmd::Exit);
    // Unknown ids (the tray menu never sends them) are ignored, not a crash path.
    CHECK(poe2::tray_cmd_from_id(0) == TrayCmd::None);
    CHECK(poe2::tray_cmd_from_id(9999) == TrayCmd::None);
}

TEST(tooltip_all_states) {
    AppStatus st;
    st.phase = Phase::ScannerOff;
    CHECK_EQ(poe2::tooltip_text(st), std::string("Scanner off"));

    st.phase = Phase::WaitingForGame;
    CHECK_EQ(poe2::tooltip_text(st), std::string("Waiting for game"));

    // SPEC §4.6 item 5 / §4.3 (added 2026-11-08): the game is open, not in a zone.
    st.phase = Phase::InMenu;
    st.pid = 0;   // apply_scanner_snapshot keeps pid 0 outside Attached (SPEC §4.3)
    CHECK_EQ(poe2::tooltip_text(st), std::string("In menu / loading"));

    st.phase = Phase::Attached;
    st.pid = 1234;
    CHECK_EQ(poe2::tooltip_text(st), std::string("Attached (PID 1234)"));
    st.pid = 4294967295u;  // largest PID on Windows
    CHECK_EQ(poe2::tooltip_text(st), std::string("Attached (PID 4294967295)"));
}

TEST(tooltip_menu_state_with_hotkey_busy) {
    // SPEC §4.3 + §4.4: the menu label is exact, fits szTip with the hotkey suffix, and
    // the merge keeps pid 0 in the menu (the label carries no PID).
    AppStatus st;
    st.scanner_enabled = true;
    CHECK(poe2::apply_scanner_snapshot(st, Phase::InMenu, 4242));
    CHECK(st.phase == Phase::InMenu);
    CHECK_EQ(st.pid, 0u);
    CHECK_EQ(poe2::tooltip_text(st), std::string("In menu / loading"));
    st.hotkey_busy = true;
    st.hotkey_text = "Ctrl+9";
    const std::string t = poe2::tooltip_text(st);
    CHECK(t.find("In menu / loading") == 0);
    CHECK(t.find("hotkey Ctrl+9 busy") != std::string::npos);
    CHECK(t.size() <= 63);
    st.scanner_enabled = false;
    CHECK(poe2::apply_scanner_snapshot(st, Phase::InMenu, 4242));
    CHECK(st.phase == Phase::ScannerOff);
}

TEST(tooltip_admin_required_status) {
    // SPEC §8: OpenProcess -> ERROR_ACCESS_DENIED is its own tooltip status. The
    // label is exact, and the status never sticks: the next merge from a successful
    // poll replaces it with the normal states (the tooltip recovery path).
    AppStatus st;
    st.scanner_enabled = true;
    st.phase = Phase::AdminRequired;
    CHECK_EQ(poe2::phase_label(st), std::string("Admin rights required"));
    CHECK_EQ(poe2::tooltip_text(st), std::string("Admin rights required"));
    // The admin string fits the szTip buffer with room for the hotkey status.
    CHECK(poe2::tooltip_text(st).size() <= 63);

    // Recovery: the snapshot of a successful poll overwrites it.
    CHECK(poe2::apply_scanner_snapshot(st, Phase::Attached, 1234));
    CHECK(st.phase == Phase::Attached);
    CHECK_EQ(poe2::tooltip_text(st), std::string("Attached (PID 1234)"));
    CHECK(poe2::apply_scanner_snapshot(st, Phase::WaitingForGame, 0));
    CHECK_EQ(poe2::tooltip_text(st), std::string("Waiting for game"));

    // Disabling the module clears the admin status with every other phase.
    st.phase = Phase::AdminRequired;
    st.scanner_enabled = false;
    CHECK(poe2::apply_scanner_snapshot(st, Phase::AdminRequired, 0));
    CHECK(st.phase == Phase::ScannerOff);
    CHECK_EQ(poe2::tooltip_text(st), std::string("Scanner off"));
}

TEST(tooltip_admin_required_with_hotkey_busy) {
    // SPEC §4.4 + §8: a taken combo appends to the admin status, the phase text stays
    // first, the string is clamped to szTip (64 wchar).
    AppStatus st;
    st.phase = Phase::AdminRequired;
    st.hotkey_busy = true;
    st.hotkey_text = "Ctrl+9";
    const std::string t = poe2::tooltip_text(st);
    CHECK(t.find("Admin rights required") == 0);
    CHECK(t.find("hotkey Ctrl+9 busy") != std::string::npos);
    CHECK(t.size() <= 63);

    // The clamp also holds with an overflowing hotkey string.
    st.hotkey_text = "Ctrl+Alt+Shift+Win+F24 plus some extra text to overflow 63 chars";
    const std::string c = poe2::tooltip_text(st);
    CHECK(c.size() <= 63);
    CHECK(c.find("Admin rights required") == 0);
}

TEST(tooltip_hotkey_busy_status) {
    // SPEC §4.4: a taken combo is a tooltip status, the phase text stays visible.
    AppStatus st;
    st.hotkey_busy = true;
    st.hotkey_text = "Ctrl+9";
    const std::string t = poe2::tooltip_text(st);
    CHECK(t.find("Scanner off") == 0);
    CHECK(t.find("hotkey Ctrl+9 busy") != std::string::npos);
    CHECK(t.size() <= 63);  // Shell_NotifyIcon szTip is WCHAR[64]

    AppStatus attached;
    attached.phase = Phase::Attached;
    attached.pid = 777;
    attached.hotkey_busy = true;
    attached.hotkey_text = "Ctrl+9";
    const std::string a = poe2::tooltip_text(attached);
    CHECK(a.find("Attached (PID 777)") == 0);
    CHECK(a.find("busy") != std::string::npos);
}

TEST(tooltip_clamped_to_sztip) {
    // Long hotkey strings must not overflow the tray tooltip buffer.
    AppStatus st;
    st.hotkey_busy = true;
    st.hotkey_text = "Ctrl+Alt+Shift+Win+F24 plus some extra text to overflow 63 chars";
    const std::string t = poe2::tooltip_text(st);
    CHECK(t.size() <= 63);
    CHECK(t.find("Scanner off") == 0);
}

TEST(state_to_wav_resource_id) {
    // SPEC §4.5 mapping, checked against the ids app.rc compiles (res_ids.h).
    CHECK_EQ(poe2::wav_res_id(true), static_cast<uint16_t>(ID_ENABLE_WAV));
    CHECK_EQ(poe2::wav_res_id(false), static_cast<uint16_t>(ID_DISABLE_WAV));
    CHECK(ID_ENABLE_WAV != ID_DISABLE_WAV);
}

TEST(scanner_toggle_state_transitions) {
    AppStatus st;
    uint16_t wav = poe2::set_scanner_enabled(st, true);
    CHECK(st.scanner_enabled);
    CHECK(st.phase == Phase::WaitingForGame);  // SPEC §4.6 item 1: waiting for process
    CHECK_EQ(wav, static_cast<uint16_t>(ID_ENABLE_WAV));

    // Simulate the stage 3 attach, then disable: PID must be dropped.
    st.phase = Phase::Attached;
    st.pid = 4242;
    wav = poe2::set_scanner_enabled(st, false);
    CHECK(!st.scanner_enabled);
    CHECK(st.phase == Phase::ScannerOff);
    CHECK_EQ(st.pid, 0u);
    CHECK_EQ(wav, static_cast<uint16_t>(ID_DISABLE_WAV));

    // Re-enable lands back in "waiting for game", not in Attached.
    wav = poe2::set_scanner_enabled(st, true);
    CHECK(st.phase == Phase::WaitingForGame);
    CHECK_EQ(wav, static_cast<uint16_t>(ID_ENABLE_WAV));
}

TEST(scanner_ctl_atomic_snapshot) {
    // The control block main.cpp and the scanner thread share (SPEC §4.2): plain
    // atomics; UI writes enable/stop, the thread writes phase/pid.
    poe2::ScannerCtl ctl;
    CHECK(!ctl.enabled.load());
    CHECK(!ctl.stop.load());
    CHECK_EQ(ctl.phase.load(), static_cast<int>(Phase::ScannerOff));
    CHECK_EQ(ctl.pid.load(), 0u);

    ctl.enabled.store(true);
    ctl.phase.store(static_cast<int>(Phase::Attached));
    ctl.pid.store(4242);
    CHECK(ctl.enabled.load());
    CHECK(static_cast<Phase>(ctl.phase.load()) == Phase::Attached);
    CHECK_EQ(ctl.pid.load(), 4242u);

    ctl.stop.store(true);   // shutdown request: the thread must leave its loop
    CHECK(ctl.stop.load());
}
