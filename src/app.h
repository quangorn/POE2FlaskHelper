#pragma once
// All paths are built from the exe directory (SPEC §4.1); the current working
// directory is ignored - critical when the exe is launched from cygwin.
// Pure string handling: GetModuleFileNameW lives in main.cpp (app layer).

#include <string>

namespace poe2 {

struct Paths {
    std::string dir;      // exe directory, normalized '/' separators, ends with '/'
    std::string exe_path; // full exe path, normalized

    std::string config_json() const;   // <dir>/config.json
    std::string offsets_json() const;  // <dir>/offsets.json

    // logging.file from config: relative -> rooted at the exe dir; a path with a
    // drive letter (X:...) or rooted at '/' is taken as-is.
    std::string log_file(const std::string& rel) const;

    // exe_full: full path from GetModuleFileNameW (backslashes are normalized).
    static Paths from_exe_path(const std::string& exe_full);
};

// Small text-file helpers (plain std streams, no Win32).
bool read_text_file(const std::string& path, std::string& out);
bool write_text_file(const std::string& path, const std::string& text);

std::string normalize_sep(std::string p);

}  // namespace poe2
