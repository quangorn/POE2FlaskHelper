#include "app.h"

#include <fstream>
#include <iterator>

namespace poe2 {

std::string normalize_sep(std::string p) {
    for (char& c : p) {
        if (c == '\\') c = '/';
    }
    return p;
}

Paths Paths::from_exe_path(const std::string& exe_full) {
    Paths r;
    r.exe_path = normalize_sep(exe_full);
    size_t pos = r.exe_path.find_last_of('/');
    r.dir = (pos == std::string::npos) ? "./" : r.exe_path.substr(0, pos + 1);
    return r;
}

std::string Paths::config_json() const { return dir + "config.json"; }

std::string Paths::offsets_json() const { return dir + "offsets.json"; }

std::string Paths::log_file(const std::string& rel) const {
    std::string r = normalize_sep(rel);
    if (r.size() >= 2 && r[1] == ':') return r;  // absolute with drive letter
    if (!r.empty() && r[0] == '/') return r;     // rooted
    return dir + r;
}

bool read_text_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool write_text_file(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    f.flush();
    return f.good();
}

}  // namespace poe2
