#pragma once
// Hotkey string parsing and VK table (SPEC §4.4, §5): "Ctrl+9", "Shift+Alt+F5".
// Pure functions - RegisterHotKey and SendInput come in stages 2/4.
// VK constants are fixed Win32 numbers; no windows.h here (plan.md §1: core is
// OS-agnostic beyond interfaces).

#include <cstdint>
#include <string>

namespace poe2 {

namespace vk {
// Win32 virtual key codes used by the parsers.
constexpr uint16_t kBack = 0x08, kTab = 0x09, kClear = 0x0C, kReturn = 0x0D, kEscape = 0x1B;
constexpr uint16_t kSpace = 0x20;
constexpr uint16_t kPageUp = 0x21, kPageDown = 0x22;
constexpr uint16_t kEnd = 0x23, kHome = 0x24, kLeft = 0x25, kUp = 0x26, kRight = 0x27, kDown = 0x28;
constexpr uint16_t kInsert = 0x2D, kDelete = 0x2E;
// Letters A-Z and digits 0-9 map to VK == their ASCII codes.
constexpr uint16_t kF1 = 0x70;  // F1..F24 = 0x70..0x87
constexpr uint16_t kNumPad0 = 0x60;  // Numpad 0..9 = 0x60..0x69, decimal point 0x6A
}  // namespace vk

namespace mod {  // RegisterHotKey modifier values (SPEC §4.4)
constexpr uint32_t kAlt = 0x0001;
constexpr uint32_t kCtrl = 0x0002;
constexpr uint32_t kShift = 0x0004;
constexpr uint32_t kWin = 0x0008;
}  // namespace mod

struct Hotkey { uint32_t mods; uint16_t vk; bool extended; };
struct KeyDef { uint16_t vk; bool extended; };

struct HotkeyResult { bool ok; Hotkey hotkey; std::string error; };
struct KeyResult { bool ok; KeyDef key; std::string error; };

bool is_f_key(uint16_t key);
// SPEC §4.8: extended flags apply to arrows and NumPad keys.
bool is_extended_key(uint16_t key);

// Toggle hotkey: "Ctrl+9", "Shift+Alt+F5", "F5". Must have at least one modifier
// (Ctrl/Shift/Alt/Win) or be an F-key (SPEC §4.4). Case-insensitive, spaces ignored.
HotkeyResult parse_hotkey(const std::string& s);

// Vital key: a plain key without modifiers - letters, digits, F1-F24, NumPad (SPEC §5).
KeyResult parse_key(const std::string& s);

// Key press sender interface (plan.md §1). Win32-free; the scanner thread is the only
// caller (SPEC §4.8). Tests inject RecordingKeySender.
struct IKeySender {
    virtual ~IKeySender() = default;
    // One full key press: keydown + keyup, as if the player pressed the key.
    // extended = the E0 prefix for arrows/NumPad (SPEC §4.8).
    virtual void press(uint16_t vk, bool extended) = 0;
};

// Real sender (SPEC §4.8): SendInput with VK + MapVirtualKey scan code, keydown+keyup.
// Declared in this Win32-free header (the Win32ProcessProvider pattern); the
// implementation compiles into the exe (src/win_keys.cpp), never into poe2_core.
struct Win32KeySender : IKeySender {
    void press(uint16_t vk, bool extended) override;
};

}  // namespace poe2
