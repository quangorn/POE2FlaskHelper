// Command-line flag matching (SPEC §2.3, MINOR_ISSUES S0-6): --version / --batch must
// be exact arguments, never a substring of the command line. Pure logic in poe2_core;
// GetCommandLineW stays in main.cpp.
#include "test.h"

#include <string>

#include "args.h"

using poe2::match_flag;
using poe2::split_command_line;

TEST(flag_exact_match) {
    // Plain flag: the mode fires.
    CHECK(match_flag(L"C:\\app\\POE2FlaskHelper.exe --version", L"--version"));
    CHECK(match_flag(L"app.exe --batch", L"--batch"));
    CHECK(match_flag(L"app.exe foo --version bar", L"--version"));  // flag in the middle

    // The regression: --version inside another argument must NOT trigger the mode.
    CHECK(!match_flag(L"app.exe foo--version", L"--version"));
    CHECK(!match_flag(L"app.exe --versionfoo", L"--version"));
    CHECK(!match_flag(L"app.exe C:\\dir\\--version\\app.exe", L"--version"));
    CHECK(!match_flag(L"app.exe --versions", L"--version"));
    CHECK(!match_flag(L"app.exe backup--batch.log", L"--batch"));
    // A flag-looking argument that is not the flag stays inert for the other flag.
    CHECK(!match_flag(L"app.exe --version", L"--batch"));
    CHECK(!match_flag(L"", L"--version"));  // empty command line
    CHECK(!match_flag(L"app.exe --VERSION", L"--version"));  // case sensitive
    // Prefixed flag (--dev--version) is not the flag.
    CHECK(!match_flag(L"app.exe --dev--version", L"--version"));
    // Escaped form: \--version is one argument "\--version", not the flag.
    CHECK(!match_flag(L"app.exe \\--version", L"--version"));
}

TEST(flag_quoted_arguments) {
    // Quoted flag: the quotes are delimiters, the argument is exactly --version.
    CHECK(match_flag(L"app.exe \"--version\"", L"--version"));
    // Quote closed and text glued on: argument is --versionx, not the flag.
    CHECK(!match_flag(L"app.exe \"--version\"x", L"--version"));
    // Quoted path with spaces, flag after it: exactly the second argument.
    CHECK(match_flag(L"C:\\Program Files\\POE2FlaskHelper.exe \"C:\\My App\\data.json\" --version",
                     L"--version"));
    // Quoted path CONTAINING --version as a substring: no flag.
    CHECK(!match_flag(L"app.exe \"C:\\tools\\foo--version\\x.exe\"", L"--version"));
    // Escaped quote inside a quoted argument: the argument is foo"--version.
    CHECK(!match_flag(L"app.exe \"foo\\\"--version\"", L"--version"));
    // Empty quoted argument then the flag.
    CHECK(match_flag(L"app.exe \"\" --version", L"--version"));
    // --batch as the only argument besides the exe path.
    CHECK(match_flag(L"C:\\Program Files\\app.exe \"--batch\"", L"--batch"));
}

TEST(command_line_split_quoting) {
    auto a = split_command_line(L"a b \"c d\"");
    CHECK_EQ(a.size(), 3u);
    CHECK_EQ(a[0], std::wstring(L"a"));
    CHECK_EQ(a[1], std::wstring(L"b"));
    CHECK_EQ(a[2], std::wstring(L"c d"));

    // Extra spaces, tabs, empty quoted argument.
    auto b = split_command_line(L"app.exe   --version\t\"\"  --batch");
    CHECK_EQ(b.size(), 4u);
    CHECK_EQ(b[0], std::wstring(L"app.exe"));
    CHECK_EQ(b[1], std::wstring(L"--version"));
    CHECK_EQ(b[2], std::wstring(L""));
    CHECK_EQ(b[3], std::wstring(L"--batch"));

    // Backslash rules: 2n before a quote -> n + quoting, 2n+1 -> n + literal quote.
    auto c = split_command_line(L"a \\b \\\\ --version");
    CHECK_EQ(c.size(), 4u);
    CHECK_EQ(c[1], std::wstring(L"\\b"));
    CHECK_EQ(c[2], std::wstring(L"\\\\"));
    CHECK_EQ(c[3], std::wstring(L"--version"));

    // \" inside a quoted region: literal quote, the region stays open.
    auto d = split_command_line(L"a \"b\\\"c\" d");
    CHECK_EQ(d.size(), 3u);
    CHECK_EQ(d[0], std::wstring(L"a"));
    CHECK_EQ(d[1], std::wstring(L"b\"c"));
    CHECK_EQ(d[2], std::wstring(L"d"));

    // Trailing backslashes belong to the last argument.
    auto e = split_command_line(L"app.exe C:\\dir\\");
    CHECK_EQ(e.size(), 2u);
    CHECK_EQ(e[1], std::wstring(L"C:\\dir\\"));

    // Empty command line yields no arguments.
    CHECK_EQ(split_command_line(L"").size(), 0u);
}
