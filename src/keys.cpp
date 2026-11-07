#include "keys.h"

#include <string>
#include <vector>

namespace poe2 {

namespace {

std::string lower_trim(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> split_plus(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find('+', start);
        out.push_back(lower_trim(s.substr(start, p == std::string::npos ? std::string::npos
                                                                       : p - start)));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return out;
}

// Modifier name -> RegisterHotKey bit; empty on unknown.
uint32_t modifier_bit(const std::string& tok) {
    if (tok == "ctrl" || tok == "control") return mod::kCtrl;
    if (tok == "shift") return mod::kShift;
    if (tok == "alt") return mod::kAlt;
    if (tok == "win" || tok == "windows" || tok == "super") return mod::kWin;
    return 0;
}

// Key token -> VK; 0 on unknown. full=true allows arrows/space (hotkey keys).
uint16_t key_vk(const std::string& tok, bool full) {
    if (tok.size() == 1) {
        char c = tok[0];
        if (c >= 'a' && c <= 'z') return static_cast<uint16_t>(c - 'a' + 'A');
        if (c >= '0' && c <= '9') return static_cast<uint16_t>(c);
    }
    if (tok.size() > 1 && tok.size() <= 3 && tok[0] == 'f') {  // F-keys: f1..f24
        bool digits = true;
        for (size_t i = 1; i < tok.size(); ++i) {
            if (tok[i] < '0' || tok[i] > '9') { digits = false; break; }
        }
        if (digits) {
            int n = std::stoi(tok.substr(1));
            if (n >= 1 && n <= 24) return static_cast<uint16_t>(vk::kF1 + n - 1);
        }
    }
    if (tok.size() > 6 && tok.compare(0, 6, "numpad") == 0) {
        std::string rest = tok.substr(6);
        if (rest.size() == 1 && rest[0] >= '0' && rest[0] <= '9') {
            return static_cast<uint16_t>(vk::kNumPad0 + (rest[0] - '0'));
        }
        if (rest == "." || rest == "dec") return 0x6A;
    }
    if (!full) return 0;
    if (tok == "up") return vk::kUp;
    if (tok == "down") return vk::kDown;
    if (tok == "left") return vk::kLeft;
    if (tok == "right") return vk::kRight;
    if (tok == "space") return vk::kSpace;
    return 0;
}

}  // namespace

bool is_f_key(uint16_t key) { return key >= vk::kF1 && key <= vk::kF1 + 23; }

bool is_extended_key(uint16_t key) {
    if (key == vk::kLeft || key == vk::kUp || key == vk::kRight || key == vk::kDown) return true;
    if (key >= vk::kNumPad0 && key <= 0x6A) return true;  // Numpad 0..9 + decimal point
    return false;
}

HotkeyResult parse_hotkey(const std::string& s) {
    HotkeyResult r{false, {0, 0, false}, {}};
    std::vector<std::string> tok = split_plus(s);
    if (tok.empty() || (tok.size() == 1 && tok[0].empty())) {
        r.error = "empty hotkey";
        return r;
    }
    uint32_t mods = 0;
    for (size_t i = 0; i + 1 < tok.size(); ++i) {
        uint32_t bit = modifier_bit(tok[i]);
        if (!bit) {
            r.error = "unknown modifier '" + tok[i] + "'";
            return r;
        }
        mods |= bit;
    }
    if (tok.back().empty()) {
        r.error = "missing key";
        return r;
    }
    uint16_t key = key_vk(tok.back(), true);
    if (!key) {
        r.error = "unknown key '" + tok.back() + "'";
        return r;
    }
    if (mods == 0 && !is_f_key(key)) {
        r.error = "hotkey requires a modifier or an F-key";
        return r;
    }
    r.ok = true;
    r.hotkey = {mods, key, is_extended_key(key)};
    return r;
}

KeyResult parse_key(const std::string& s) {
    KeyResult r{false, {0, false}, {}};
    std::vector<std::string> tok = split_plus(s);
    if (tok.size() != 1 || tok[0].empty()) {
        r.error = "key must be a single key without modifiers";
        return r;
    }
    uint16_t key = key_vk(tok[0], false);  // strict vital set: letters, digits, F1-F24, NumPad
    if (!key) {
        r.error = "unknown key '" + tok[0] + "'";
        return r;
    }
    r.ok = true;
    r.key = {key, is_extended_key(key)};
    return r;
}

}  // namespace poe2
