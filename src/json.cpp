#include "json.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace poe2 {

namespace {

constexpr int kMaxDepth = 64;

// Position (1-based line, column) points at the next unconsumed character.
struct Parser {
    const std::string& t;
    size_t pos = 0;
    int line = 1;
    int col = 1;
    JsonParseResult res;

    explicit Parser(const std::string& text) : t(text) {}

    void fail(const char* msg) { res.ok = false; res.error = JsonError{msg, line, col}; }

    bool at_end() const { return pos >= t.size(); }
    char peek() const { return t[pos]; }

    void advance() {  // consume one char, track position
        if (t[pos] == '\n') { ++line; col = 1; } else { ++col; }
        ++pos;
    }

    void skip_ws() {
        while (pos < t.size()) {
            char c = t[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') advance();
            else break;
        }
    }

    bool expect(char c) {  // consume c or fail at its position
        skip_ws();
        if (at_end()) { fail("unexpected end of input"); return false; }
        if (peek() != c) { fail("unexpected character"); return false; }
        advance();
        return true;
    }

    // \uXXXX -> code unit; false on bad hex.
    bool hex4(uint16_t& out) {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            if (at_end()) { fail("unterminated string escape"); return false; }
            char c = peek();
            uint32_t d;
            if (c >= '0' && c <= '9') d = static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<uint32_t>(c - 'A' + 10);
            else { fail("invalid \\u escape"); return false; }
            v = v * 16 + d;
            advance();
        }
        out = static_cast<uint16_t>(v);
        return true;
    }

    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool parse_string(std::string& out) {
        if (!expect('"')) return false;
        out.clear();
        while (true) {
            if (at_end()) { fail("unterminated string"); return false; }
            char c = peek();
            if (c == '"') { advance(); return true; }
            unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 && u != 0x09) { fail("control character in string"); return false; }
            if (c != '\\') { out.push_back(c); advance(); continue; }
            advance();  // consume backslash
            if (at_end()) { fail("unterminated string escape"); return false; }
            char e = peek();
            advance();
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint16_t unit;
                    if (!hex4(unit)) return false;
                    uint32_t cp = unit;
                    if (unit >= 0xD800 && unit <= 0xDBFF) {  // high surrogate: need a low one
                        if (at_end() || peek() != '\\') { fail("lone surrogate escape"); return false; }
                        advance();
                        if (at_end() || peek() != 'u') { fail("lone surrogate escape"); return false; }
                        advance();
                        uint16_t lo;
                        if (!hex4(lo)) return false;
                        if (lo < 0xDC00 || lo > 0xDFFF) { fail("lone surrogate escape"); return false; }
                        cp = 0x10000 + ((static_cast<uint32_t>(unit) - 0xD800) << 10)
                             + (static_cast<uint32_t>(lo) - 0xDC00);
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: fail("invalid string escape"); return false;
            }
        }
    }

    bool parse_number(Json& out) {
        size_t start = pos;
        if (!at_end() && peek() == '-') advance();
        if (at_end() || peek() < '0' || peek() > '9') {
            pos = start; fail("invalid number"); return false;
        }
        if (peek() == '0') {  // JSON forbids leading zeros like 01, 012
            advance();
            if (!at_end() && peek() >= '0' && peek() <= '9') {
                pos = start; fail("leading zero in number"); return false;
            }
        }
        while (!at_end() && peek() >= '0' && peek() <= '9') advance();
        bool is_real = false;
        if (!at_end() && peek() == '.') {
            is_real = true;
            advance();
            if (at_end() || peek() < '0' || peek() > '9') {
                pos = start; fail("invalid number"); return false;
            }
            while (!at_end() && peek() >= '0' && peek() <= '9') advance();
        }
        if (!at_end() && (peek() == 'e' || peek() == 'E')) {
            is_real = true;
            advance();
            if (!at_end() && (peek() == '+' || peek() == '-')) advance();
            if (at_end() || peek() < '0' || peek() > '9') {
                pos = start; fail("invalid number"); return false;
            }
            while (!at_end() && peek() >= '0' && peek() <= '9') advance();
        }
        std::string num = t.substr(start, pos - start);
        if (is_real) {
            out = Json::real(std::strtod(num.c_str(), nullptr));
            return true;
        }
        int64_t v;
        auto r = std::from_chars(num.data(), num.data() + num.size(), v);
        if (r.ec != std::errc{}) { pos = start; fail("number out of range"); return false; }
        out = Json::integer(v);
        return true;
    }

    bool parse_value(Json& out, int depth) {
        if (depth > kMaxDepth) { fail("maximum nesting depth"); return false; }
        skip_ws();
        if (at_end()) { fail("unexpected end of input"); return false; }
        char c = peek();
        switch (c) {
            case '{': return parse_object(out, depth);
            case '[': return parse_array(out, depth);
            case '"': {
                std::string s;
                if (!parse_string(s)) return false;
                out = Json::string(std::move(s));
                return true;
            }
            case 't':  // true
                if (t.compare(pos, 4, "true") != 0) { fail("invalid literal"); return false; }
                pos += 4; col += 4;
                out = Json::boolean(true);
                return true;
            case 'f':  // false
                if (t.compare(pos, 5, "false") != 0) { fail("invalid literal"); return false; }
                pos += 5; col += 5;
                out = Json::boolean(false);
                return true;
            case 'n':  // null
                if (t.compare(pos, 4, "null") != 0) { fail("invalid literal"); return false; }
                pos += 4; col += 4;
                out = Json::null_value();
                return true;
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return parse_number(out);
                fail("unexpected character");
                return false;
        }
    }

    bool parse_object(Json& out, int depth) {
        if (!expect('{')) return false;
        out = Json::object();
        skip_ws();
        if (!at_end() && peek() == '}') { advance(); return true; }
        while (true) {
            skip_ws();
            if (at_end()) { fail("unterminated object"); return false; }
            if (peek() != '"') { fail("object key must be a string"); return false; }
            std::string key;
            if (!parse_string(key)) return false;
            if (!expect(':')) return false;
            Json val;
            if (!parse_value(val, depth + 1)) return false;
            out.add(key, std::move(val));
            skip_ws();
            if (at_end()) { fail("unterminated object"); return false; }
            char c = peek();
            if (c == ',') { advance(); continue; }
            if (c == '}') { advance(); return true; }
            fail("expected ',' or '}'");
            return false;
        }
    }

    bool parse_array(Json& out, int depth) {
        if (!expect('[')) return false;
        out = Json::array();
        skip_ws();
        if (!at_end() && peek() == ']') { advance(); return true; }
        while (true) {
            Json val;
            if (!parse_value(val, depth + 1)) return false;
            out.push_back(std::move(val));
            skip_ws();
            if (at_end()) { fail("unterminated array"); return false; }
            char c = peek();
            if (c == ',') { advance(); continue; }
            if (c == ']') { advance(); return true; }
            fail("expected ',' or ']'");
            return false;
        }
    }
};

void escape_into(std::string& out, const std::string& s) {
    out.push_back('"');
    for (char c : s) {
        unsigned char u = static_cast<unsigned char>(c);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (u < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", u);
                    out += buf;
                } else {
                    out.push_back(c);  // UTF-8 bytes pass through
                }
        }
    }
    out.push_back('"');
}

// Shortest round-trip form of a double for JSON: try precision 17 (always enough to
// reproduce any double), then 16..1, and keep the shortest text that parses back to
// the exact same double. Trailing zeros are already trimmed by %g. An integral value
// keeps a ".0" so it re-parses as a Real, not as an Int ("5" -> "5.0"); exponent forms
// like 1e+17 already re-parse as Real and stay as they are.
std::string real_to_string(double d) {
    char buf[32];
    for (int p = 1; p <= 17; ++p) {
        std::snprintf(buf, sizeof buf, "%.*g", p, d);
        if (std::strtod(buf, nullptr) == d) break;  // first (shortest) round-trip wins
    }
    std::string s(buf);
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) {
        s += ".0";  // integral value stays a JSON Real after re-parse
    }
    return s;
}

void serialize_into(std::string& out, const Json& v, bool pretty, int depth) {
    auto indent = [&](int d) {
        if (pretty) { out.push_back('\n'); out.append(2 * d, ' '); }
    };
    switch (v.type()) {
        case Json::Type::Null: out += "null"; break;
        case Json::Type::Bool: out += v.as_bool() ? "true" : "false"; break;
        case Json::Type::Int: out += std::to_string(v.as_int()); break;
        case Json::Type::Real: {
            double d = v.as_real();
            if (std::isnan(d) || std::isinf(d)) { out += "null"; break; }  // no JSON form
            out += real_to_string(d);  // shortest text that re-parses to the same double
            break;
        }
        case Json::Type::String: escape_into(out, v.as_string()); break;
        case Json::Type::Array: {
            const auto& items = v.array_items();
            if (items.empty()) { out += "[]"; break; }
            out.push_back('[');
            for (size_t i = 0; i < items.size(); ++i) {
                if (i) out.push_back(',');
                indent(depth + 1);
                serialize_into(out, items[i], pretty, depth + 1);
            }
            indent(depth);
            out.push_back(']');
            break;
        }
        case Json::Type::Object: {
            const auto& mem = v.members();
            if (mem.empty()) { out += "{}"; break; }
            out.push_back('{');
            for (size_t i = 0; i < mem.size(); ++i) {
                if (i) out.push_back(',');
                indent(depth + 1);
                escape_into(out, mem[i].first);
                out += pretty ? ": " : ":";
                serialize_into(out, mem[i].second, pretty, depth + 1);
            }
            indent(depth);
            out.push_back('}');
            break;
        }
    }
}

}  // namespace

JsonParseResult Json::parse(const std::string& text) {
    Parser p(text);
    p.res.ok = true;
    Json v;
    if (!p.parse_value(v, 0)) { p.res.value = Json(); return p.res; }
    p.skip_ws();
    if (!p.at_end()) { p.fail("trailing characters"); return p.res; }
    p.res.value = std::move(v);
    return p.res;
}

std::string Json::serialize(bool pretty) const {
    std::string out;
    serialize_into(out, *this, pretty, 0);
    return out;
}

}  // namespace poe2
