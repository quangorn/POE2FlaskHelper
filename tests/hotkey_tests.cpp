// Hotkey parser + VK table tests (SPEC §4.4, §5; plan.md §3 Stage 1).
#include "test.h"

#include <string>

#include "keys.h"

using poe2::mod::kAlt;
using poe2::mod::kCtrl;
using poe2::mod::kShift;
using poe2::mod::kWin;
using poe2::parse_hotkey;
using poe2::parse_key;

TEST(hotkey_ctrl_9) {
    auto r = parse_hotkey("Ctrl+9");
    REQUIRE(r.ok);
    CHECK_EQ(r.hotkey.mods, kCtrl);
    CHECK_EQ(r.hotkey.vk, static_cast<uint16_t>('9'));
    CHECK_EQ(r.hotkey.extended, false);
}

TEST(hotkey_shift_alt_f5) {
    auto r = parse_hotkey("Shift+Alt+F5");
    REQUIRE(r.ok);
    CHECK_EQ(r.hotkey.mods, kShift | kAlt);
    CHECK_EQ(r.hotkey.vk, static_cast<uint16_t>(poe2::vk::kF1 + 4));  // F5
    CHECK(poe2::is_f_key(r.hotkey.vk));
}

TEST(hotkey_case_insensitive) {
    auto r = parse_hotkey("ctrl + SHIFT + f5");
    REQUIRE(r.ok);
    CHECK_EQ(r.hotkey.mods, kCtrl | kShift);
    CHECK_EQ(r.hotkey.vk, static_cast<uint16_t>(poe2::vk::kF1 + 4));
}

TEST(hotkey_multi_modifiers) {
    auto r = parse_hotkey("Ctrl+Alt+Shift+F12");
    REQUIRE(r.ok);
    CHECK_EQ(r.hotkey.mods, kCtrl | kAlt | kShift);
    CHECK_EQ(r.hotkey.vk, static_cast<uint16_t>(poe2::vk::kF1 + 11));  // F12
    auto w = parse_hotkey("Win+F1");
    REQUIRE(w.ok);
    CHECK_EQ(w.hotkey.mods, kWin);
}

TEST(hotkey_f_key_needs_no_modifier) {
    auto r = parse_hotkey("F5");
    REQUIRE(r.ok);
    CHECK_EQ(r.hotkey.mods, 0u);
    CHECK_EQ(r.hotkey.vk, static_cast<uint16_t>(poe2::vk::kF1 + 4));
}

TEST(hotkey_plain_key_needs_modifier) {
    auto r = parse_hotkey("9");
    CHECK(!r.ok);
    CHECK(r.error.find("modifier") != std::string::npos);
    CHECK(!parse_hotkey("A").ok);
    CHECK(!parse_hotkey("Numpad3").ok);  // plain key is not a valid toggle hotkey
}

TEST(hotkey_garbage_rejected) {
    CHECK(!parse_hotkey("").ok);
    CHECK(!parse_hotkey("Ctrl+").ok);   // modifier without key
    CHECK(!parse_hotkey("Ctrl+Zzz").ok);  // unknown key
    CHECK(!parse_hotkey("Meta+9").ok);     // unknown modifier
    CHECK(!parse_hotkey("Ctrl+Alt+Shift+F25").ok);  // beyond F24
    auto e = parse_hotkey("Ctrl+Zzz");
    CHECK(e.error.find("zzz") != std::string::npos);  // tokens are lowercased
}

TEST(hotkey_extended_flags) {
    auto up = parse_hotkey("Ctrl+Up");
    REQUIRE(up.ok);
    CHECK_EQ(up.hotkey.vk, poe2::vk::kUp);
    CHECK_EQ(up.hotkey.extended, true);
    auto np = parse_hotkey("Ctrl+Numpad5");
    REQUIRE(np.ok);
    CHECK_EQ(np.hotkey.vk, static_cast<uint16_t>(poe2::vk::kNumPad0 + 5));
    CHECK_EQ(np.hotkey.extended, true);
    auto dig = parse_hotkey("Ctrl+9");
    CHECK_EQ(dig.hotkey.extended, false);  // digits are not extended
}

TEST(vital_keys_plain_ok) {
    auto d = parse_key("1");
    REQUIRE(d.ok);
    CHECK_EQ(d.key.vk, static_cast<uint16_t>('1'));
    CHECK_EQ(d.key.extended, false);
    auto a = parse_key("a");
    REQUIRE(a.ok);
    CHECK_EQ(a.key.vk, static_cast<uint16_t>('A'));  // letters map to VK 'A'..'Z'
    auto f = parse_key("F5");
    REQUIRE(f.ok);
    CHECK(f.key.extended == false);
    auto np = parse_key("Numpad3");
    REQUIRE(np.ok);
    CHECK_EQ(np.key.vk, static_cast<uint16_t>(poe2::vk::kNumPad0 + 3));
    CHECK_EQ(np.key.extended, true);  // NumPad is extended (SPEC §4.8)
}

TEST(vital_key_rules) {
    auto mod = parse_key("Ctrl+1");
    CHECK(!mod.ok);  // vital keys are plain, modifiers rejected
    auto arrow = parse_key("Up");
    CHECK(!arrow.ok);  // SPEC §5: vital keys are letters/digits/F1-F24/NumPad
    CHECK(!parse_key("").ok);
    CHECK(!parse_key("Zzz").ok);
    auto r = parse_key("9");
    CHECK(r.ok);  // plain key is fine for a vital key (unlike a toggle hotkey)
}

TEST(vital_key_case) {
    auto f = parse_key("f5");
    REQUIRE(f.ok);
    CHECK_EQ(f.key.vk, static_cast<uint16_t>(poe2::vk::kF1 + 4));
    auto np = parse_key("NUMPAD0");
    REQUIRE(np.ok);
    CHECK_EQ(np.key.vk, poe2::vk::kNumPad0);
}
