#pragma once
// Deterministic temp-dir helper for file-based tests (plan.md §2: temp dir, no FakeMemory yet).

#include <filesystem>
#include <fstream>
#include <iterator>

namespace poe2 {
namespace testing {

// Fresh directory per test: <temp>/poe2_stage1_tests/<name>, wiped on entry.
inline std::filesystem::path tmp_dir(const std::string& name) {
    namespace fs = std::filesystem;
    fs::path p = fs::temp_directory_path() / "poe2_stage1_tests" / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

inline std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

inline int count_lines(const std::string& text) {
    int n = 0;
    for (char c : text) {
        if (c == '\n') ++n;
    }
    return n;
}

}  // namespace testing
}  // namespace poe2
