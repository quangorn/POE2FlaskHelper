// Stage 4 keymap tests (plan.md "Этап 4"): Vital' key strings -> VK + scan code +
// extended flags. VK mapping is pure core code (keys.cpp parse_key); the scan codes
// are the real MapVirtualKey table the Win32KeySender sends (SPEC §4.8), so this TU
// runs in the app layer with user32. Layout-independent asserts: digits, F1-F24 and
// NumPad have fixed scan codes in every layout; letters are asserted as non-zero
// (their physical code depends on the keyboard layout).
#include "test.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

#include "keys.h"

using poe2::is_extended_key;
using poe2::parse_key;
using poe2::vk::kF1;
using poe2::vk::kNumPad0;

TEST(keymap_letters_digits_vk) {
    // Letters map to VK 'A'..'Z', digits to their ASCII codes, no modifiers, not
    // extended (SPEC §5: Vital' keys are plain keys).
    for (char c = 'a'; c <= 'z'; ++c) {
        auto r = parse_key(std::string(1, c));
        REQUIRE(r.ok);
        CHECK_EQ(r.key.vk, static_cast<uint16_t>('A' + (c - 'a')));
        CHECK_EQ(r.key.extended, false);
    }
    for (char c = '0'; c <= '9'; ++c) {
        auto r = parse_key(std::string(1, c));
        REQUIRE(r.ok);
        CHECK_EQ(r.key.vk, static_cast<uint16_t>(c));
        CHECK_EQ(r.key.extended, false);
    }
}

TEST(keymap_letters_have_scan_codes) {
    // MapVirtualKey resolves every Vital' letter to a physical key code (non-zero).
    for (char c = 'A'; c <= 'Z'; ++c) {
        CHECK(MapVirtualKeyW(c, MAPVK_VK_TO_VSC) != 0);
    }
    for (char c = '0'; c <= '9'; ++c) {
        CHECK(MapVirtualKeyW(c, MAPVK_VK_TO_VSC) != 0);
    }
}

TEST(keymap_digit_scan_codes) {
    // US top-row digits: '1'..'0' are physical codes 0x02..0x0B, identical in the
    // RU layout (the digit keys are shared). Both sides cast to int so the
    // comparison is signed/signed (no /W4 C4389 on UINT vs int).
    for (int n = 1; n <= 9; ++n) {
        CHECK_EQ(static_cast<int>(MapVirtualKeyW('0' + n, MAPVK_VK_TO_VSC)), 0x01 + n);
    }
    CHECK_EQ(static_cast<int>(MapVirtualKeyW('0', MAPVK_VK_TO_VSC)), 0x0B);
}

TEST(keymap_f1_to_f24) {
    // VK table: F1..F24 = 0x70..0x87. Scan codes are layout-fixed:
    // F1..F10 = 0x3B..0x44, F11=0x57, F12=0x58, F13..F24 = 0x64..0x6F.
    for (int n = 1; n <= 24; ++n) {
        auto r = parse_key("F" + std::to_string(n));
        REQUIRE(r.ok);
        CHECK_EQ(r.key.vk, static_cast<uint16_t>(kF1 + n - 1));
        CHECK_EQ(r.key.extended, false);   // F-keys are not extended
    }
    for (int n = 1; n <= 10; ++n) {
        CHECK_EQ(static_cast<int>(MapVirtualKeyW(kF1 + n - 1, MAPVK_VK_TO_VSC)), 0x3A + n);
    }
    CHECK_EQ(static_cast<int>(MapVirtualKeyW(kF1 + 10, MAPVK_VK_TO_VSC)), 0x57);   // F11
    CHECK_EQ(static_cast<int>(MapVirtualKeyW(kF1 + 11, MAPVK_VK_TO_VSC)), 0x58);   // F12
    // F13..F23 = 0x64..0x6E, F24 = 0x76 (values verified on the real MapVirtualKey).
    for (int n = 13; n <= 23; ++n) {
        CHECK_EQ(static_cast<int>(MapVirtualKeyW(kF1 + n - 1, MAPVK_VK_TO_VSC)), 0x57 + n);
    }
    CHECK_EQ(static_cast<int>(MapVirtualKeyW(kF1 + 23, MAPVK_VK_TO_VSC)), 0x76);   // F24
    CHECK(!parse_key("F25").ok);   // beyond F24 (SPEC §5)
}

TEST(keymap_numpad_extended_scan_codes) {
    // SPEC §4.8: NumPad keys are sent with KEYEVENTF_EXTENDEDKEY. VK 0x60..0x69.
    // Set-1 scan codes (layout-fixed, verified on the real MapVirtualKey):
    // KP0=0x52, KP1=0x4F, KP2=0x50, KP3=0x51, KP4=0x4B, KP5=0x4C, KP6=0x4D,
    // KP7=0x47, KP8=0x48, KP9=0x49.
    const uint16_t vk_digit[10] = {0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
                                   0x68, 0x69};
    const int scan[10] = {0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49};
    for (int n = 0; n <= 9; ++n) {
        auto r = parse_key("Numpad" + std::to_string(n));
        REQUIRE(r.ok);
        CHECK_EQ(r.key.vk, vk_digit[n]);
        CHECK_EQ(r.key.extended, true);
        CHECK_EQ(static_cast<int>(MapVirtualKeyW(vk_digit[n], MAPVK_VK_TO_VSC)), scan[n]);
    }
    auto d = parse_key("Numpad.");
    REQUIRE(d.ok);
    CHECK_EQ(d.key.vk, 0x6A);            // NumPad decimal point
    CHECK_EQ(d.key.extended, true);
}

TEST(keymap_extended_flags_table) {
    // SPEC §4.8: extended flags apply to arrows and NumPad; Vital' plain keys
    // (letters, digits, F-keys) are not extended.
    CHECK(is_extended_key(poe2::vk::kLeft));
    CHECK(is_extended_key(poe2::vk::kUp));
    CHECK(is_extended_key(poe2::vk::kRight));
    CHECK(is_extended_key(poe2::vk::kDown));
    for (uint16_t v = kNumPad0; v <= 0x6A; ++v) CHECK(is_extended_key(v));
    CHECK(!is_extended_key('A'));
    CHECK(!is_extended_key('7'));
    CHECK(!is_extended_key(kF1));
    CHECK(!is_extended_key(kF1 + 23));
}

TEST(keymap_modifier_rejected_on_vital_keys) {
    // SPEC §5: Vital' keys are single plain keys; modifiers must never reach the
    // keymap. Config validation rejects them (config_tests), parse_key too.
    CHECK(!parse_key("Ctrl+1").ok);
    CHECK(!parse_key("Shift+A").ok);
    CHECK(!parse_key("Alt+F5").ok);
    CHECK(!parse_key("Ctrl+Numpad5").ok);
    auto r = parse_key("Ctrl+1");
    CHECK(r.error.find("modifier") != std::string::npos);
    CHECK(!parse_key("Up").ok);          // arrows are not Vital' keys (SPEC §5)
}
