// SendInput implementation of IKeySender (SPEC §4.8). App layer: compiled into the
// exe only, poe2_core never touches Win32 input. The scanner thread is the only
// caller (SPEC §4.2). PostMessage is NOT used: the game reads the keyboard through
// DirectInput, so the press must go through the real input queue (SPEC §4.8).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "keys.h"

namespace poe2 {

void Win32KeySender::press(uint16_t vk, bool extended) {
    // VK + scan code from MapVirtualKey (SPEC §4.8); KEYEVENTF_EXTENDEDKEY carries the
    // E0 prefix for arrows and NumPad keys. in[] is zero-initialized, so dwExtraInfo
    // and time are 0.
    const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[0].ki.wScan = static_cast<WORD>(scan);
    in[0].ki.dwFlags = 0;
    if (extended) in[0].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    in[1] = in[0];
    in[1].ki.dwFlags |= KEYEVENTF_KEYUP;   // keydown + keyup, one press
    SendInput(2, in, static_cast<int>(sizeof(INPUT)));
}

}  // namespace poe2
