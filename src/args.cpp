#include "args.h"

namespace poe2 {

std::vector<std::wstring> split_command_line(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring cur;
    bool in_quote = false;  // inside a "..." region: quotes are delimiters, not text
    bool has_arg = false;   // an argument is being built (an empty quoted one counts)
    size_t i = 0;
    while (i < s.size()) {
        wchar_t c = s[i];
        if (c == L' ' || c == L'\t') {
            if (in_quote) {
                cur.push_back(c);  // a quoted space is argument text
                ++i;
                continue;
            }
            if (has_arg) {
                out.push_back(cur);
                cur.clear();
                has_arg = false;
            }
            ++i;
            continue;
        }
        size_t bs = 0;
        while (i < s.size() && s[i] == L'\\') { ++bs; ++i; }
        if (i < s.size() && s[i] == L'"') {
            cur.append(bs / 2, L'\\');
            if (bs % 2 == 1) {
                cur.push_back(L'"');  // odd backslashes: the quote is literal text
            } else {
                in_quote = !in_quote;  // even backslashes: the quote opens/closes
            }
            has_arg = true;
            ++i;
        } else {
            cur.append(bs, L'\\');  // every backslash here is literal text
            has_arg = true;         // trailing backslashes belong to the argument
            if (i < s.size() && s[i] != L' ' && s[i] != L'\t') {
                cur.push_back(s[i]);
                ++i;  // a following space stays for the flush at the top of the loop
            }
        }
    }
    if (has_arg) out.push_back(cur);
    return out;
}

bool match_flag(const std::wstring& cmdline, const wchar_t* flag) {
    const std::wstring target(flag);
    for (const auto& arg : split_command_line(cmdline)) {
        if (arg == target) return true;
    }
    return false;
}

}  // namespace poe2
