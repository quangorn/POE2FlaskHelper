#pragma once
// Command-line flag matching (SPEC §2.3 flags --version / --batch). The exe calls
// GetCommandLineW() in main.cpp (app layer); the splitting and matching here are pure
// so they are testable. Matching is per argument: a substring of an argument or of a
// path ("foo--version", "C:\dir\--version\app.exe") is NOT a flag
// (MINOR_ISSUES S0-6 - the old wcsstr over the whole command line made it one).

#include <string>
#include <vector>

namespace poe2 {

// Splits a Windows command line into arguments following the CommandLineToArgvW rules:
// 2n backslashes before a quote -> n backslashes, the quote opens/closes a quoted
// region; 2n+1 backslashes -> n backslashes + a literal quote; every other backslash
// is literal. Spaces inside a quoted region belong to the argument.
std::vector<std::wstring> split_command_line(const std::wstring& cmdline);

// True iff one of the arguments equals `flag` exactly (case sensitive).
bool match_flag(const std::wstring& cmdline, const wchar_t* flag);

}  // namespace poe2
