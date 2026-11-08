// Command-line flag matching (SPEC §2.3, MINOR_ISSUES S0-6): --version / --batch must
// be exact arguments, never a substring of the command line. Pure logic in poe2_core;
// GetCommandLineW stays in main.cpp.
#include "test.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>  // CommandLineToArgvW (S1-10: the split is measured against it)

#include <string>
#include <vector>

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

// S1-11 coverage gaps: an unterminated quoted region, and a trailing 2n-backslash
// run before the closing quote at the very end of the command line.
TEST(command_line_split_edge_regions) {
    // Unterminated quoted region runs to the end of the line: the spaces inside it
    // are argument text, so "--version" is not an argument of its own.
    const std::wstring unclosed = L"app.exe \"unclosed --version";
    auto a = split_command_line(unclosed);
    CHECK_EQ(a.size(), 2u);
    CHECK_EQ(a[0], std::wstring(L"app.exe"));
    CHECK_EQ(a[1], std::wstring(L"unclosed --version"));
    CHECK(!match_flag(unclosed, L"--version"));

    // 2 trailing backslashes + quote at line end: 2n -> n backslashes, the quote
    // closes the quoted region (the argument is the path with one backslash).
    const std::wstring two = L"app.exe C:\\dir\\\\\"";  // raw line: app.exe C:\dir\\"
    auto b = split_command_line(two);
    CHECK_EQ(b.size(), 2u);
    CHECK_EQ(b[1], std::wstring(L"C:\\dir\\"));
    CHECK(!match_flag(two, L"--version"));

    // 4 trailing backslashes + quote: n = 2, the quote closes.
    auto c = split_command_line(L"app.exe C:\\dir\\\\\\\\\"");
    CHECK_EQ(c.size(), 2u);
    CHECK_EQ(c[1], std::wstring(L"C:\\dir\\\\"));

    // Odd trailing run (1 backslash) before the closing quote: literal quote, no region.
    auto d = split_command_line(L"app.exe C:\\dir\\\"");
    CHECK_EQ(d.size(), 2u);
    CHECK_EQ(d[1], std::wstring(L"C:\\dir\""));

    // A closed region at line end: the argument is the quoted text.
    auto e = split_command_line(L"app.exe \"--version\"");
    CHECK_EQ(e.size(), 2u);
    CHECK(match_flag(L"app.exe \"--version\"", L"--version"));
}

// S1-10: the split is measured against CommandLineToArgvW itself (shell32, linked for
// the tests only, like user32 for keymap_tests). The 15 command lines where the two
// agree are pinned as exact equality (kAgree below); 2 of the divergence families
// documented in args.h are pinned as measured divergences (leading whitespace, adjacent
// quoted regions; the third family - quote adjacent to unquoted text, and the empty /
// whitespace-only command line - is documented in args.h and covered by the flag
// probes). The flag decision (match_flag vs the argvw argument list) is checked on
// every case: 15 agree cases, 2 pinned divergences, 0 flag-decision divergences.
// (BR-2 2026-11-08: the comment count corrected from "16 equalities" to what the code
// measures.)
static std::vector<std::wstring> argvw(const std::wstring& cl) {
    int n = 0;
    LPWSTR* v = CommandLineToArgvW(cl.c_str(), &n);
    std::vector<std::wstring> out;
    for (int i = 0; i < n; ++i) out.push_back(std::wstring(v[i]));
    LocalFree(v);
    return out;
}

static bool argvw_has_flag(const std::wstring& cl, const wchar_t* flag) {
    for (const auto& a : argvw(cl)) {
        if (a == flag) return true;
    }
    return false;
}

TEST(flag_vs_commandlinetargvw) {
    // The 15 command lines where split_command_line and CommandLineToArgvW produce the
    // same arguments.
    static const std::wstring kAgree[] = {
        L"app.exe --version",
        L"app.exe foo--version",
        L"app.exe \"--version\"",
        L"app.exe \"C:\\My App\\data.json\" --version",
        L"app.exe \"foo\\\"--version\"",
        L"app.exe \"\" --version",
        L"app.exe \\--version",
        L"app.exe \"unclosed --version",       // S1-11: unterminated region
        L"app.exe C:\\dir\\\\\"",             // S1-11: 2 trailing backslashes + quote
        L"app.exe C:\\dir\\\\\\\\\"",         // ... 4 trailing backslashes + quote
        L"app.exe C:\\dir\\\"",              // odd trailing run: literal quote
        L"app.exe a\"b\"c --version",
        L"app.exe \"a b\" --batch",
        L"app.exe \"--version\"x",
        L"app.exe   --version\t\"\"  --batch",
    };
    for (const std::wstring& line : kAgree) {
        CHECK_EQ(split_command_line(line), argvw(line));
        CHECK(match_flag(line, L"--version") == argvw_has_flag(line, L"--version"));
        CHECK(match_flag(line, L"--batch") == argvw_has_flag(line, L"--batch"));
    }

    // Divergence 1 (documented in args.h): leading whitespace. The API yields an empty
    // argv[0]; split_command_line drops it. The flag decision is identical.
    const std::wstring lead = L"  app.exe --version";
    auto la = argvw(lead);
    CHECK_EQ(la.size(), 3u);
    CHECK_EQ(la[0], std::wstring(L""));
    auto ls = split_command_line(lead);
    CHECK_EQ(ls.size(), 2u);
    CHECK_EQ(ls[0], std::wstring(L"app.exe"));
    CHECK(match_flag(lead, L"--version") == argvw_has_flag(lead, L"--version"));

    // Divergence 2 (documented in args.h): adjacent quoted regions are glued here.
    const std::wstring glue = L"app.exe \"a\"\"b\"";
    auto ga = argvw(glue);
    CHECK_EQ(ga.size(), 2u);
    CHECK_EQ(ga[1], std::wstring(L"a\"b"));
    auto gs = split_command_line(glue);
    CHECK_EQ(gs.size(), 2u);
    CHECK_EQ(gs[1], std::wstring(L"ab"));
    CHECK(match_flag(glue, L"--version") == argvw_has_flag(glue, L"--version"));
}
