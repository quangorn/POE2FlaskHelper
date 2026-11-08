#pragma once
// Command-line flag matching (SPEC §2.3 flags --version / --batch). The exe calls
// GetCommandLineW() in main.cpp (app layer); the splitting and matching here are pure
// so they are testable. Matching is per argument: a substring of an argument or of a
// path ("foo--version", "C:\dir\--version\app.exe") is NOT a flag
// (review S0-6, fixed 2026-11-08 - the old wcsstr over the whole command line made it one).

#include <string>
#include <vector>

namespace poe2 {

// Splits a Windows command line into arguments following the CommandLineToArgvW rules:
// 2n backslashes before a quote -> n backslashes, the quote opens/closes a quoted
// region; 2n+1 backslashes -> n backslashes + a literal quote; every other backslash
// is literal. Spaces inside a quoted region belong to the argument.
//
// S1-10 - three simplification families vs CommandLineToArgvW, all measured against
// the API in tests/args_tests.cpp (flag_vs_commandlinetargvw):
// 1. leading whitespace: the API emits an empty argv[0] that split_command_line drops;
// 2. adjacent quoted regions are glued here ("a""b" -> "ab", the API gives "a\"b");
// 3. a quote adjacent to unquoted text is kept by the API and dropped here:
//    app.exe"x" -> argv[0] = app.exe"x" for the API, app.exex for the split (the quote
//    sits in an unquoted region, so the API treats it as literal text); an empty or
//    whitespace-only command line gives argvw n = 1 with one empty argument, split
//    n = 0 (tests/args_tests.cpp command_line_split_quoting pins the split side).
// Family 1 and family 2 are pinned as exact measured divergences in the test; family 3
// is documented here (flag-parity for it is pinned by the flag probes above).
// None of the three changes match_flag: 15 agree cases + 2 pinned divergences, the
// flag decision (match_flag vs argvw_has_flag) has 0 divergences on every case.
std::vector<std::wstring> split_command_line(const std::wstring& cmdline);

// True iff one of the arguments equals `flag` exactly (case sensitive).
bool match_flag(const std::wstring& cmdline, const wchar_t* flag);

}  // namespace poe2
