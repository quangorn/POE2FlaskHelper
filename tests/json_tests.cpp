// JSON parser/serializer tests (plan.md §3 Stage 1).
#include "test.h"

#include <cstring>
#include <limits>
#include <string>

#include "json.h"

using poe2::Json;

TEST(json_primitives) {
    auto r = Json::parse("42");
    CHECK(r.ok);
    CHECK(r.value.is_int());
    CHECK_EQ(r.value.as_int(), 42);

    CHECK_EQ(Json::parse("-7").value.as_int(), -7);
    CHECK_EQ(Json::parse("0").value.as_int(), 0);
    CHECK_EQ(Json::parse("9223372036854775807").value.as_int(), 9223372036854775807LL);

    auto f = Json::parse("1.5");
    CHECK(f.ok);
    CHECK(f.value.is_real());
    CHECK_EQ(f.value.as_real(), 1.5);
    auto e = Json::parse("1e3");
    CHECK(e.value.is_real());
    CHECK_EQ(e.value.as_real(), 1000.0);

    auto b = Json::parse("true");
    CHECK(b.ok && b.value.is_bool());
    CHECK(b.value.as_bool());
    CHECK(Json::parse("false").value.as_bool() == false);

    auto n = Json::parse("null");
    CHECK(n.ok && n.value.is_null());

    auto s = Json::parse("\"hi\"");
    CHECK(s.ok && s.value.is_string());
    CHECK_EQ(s.value.as_string(), std::string("hi"));

    // int64 overflow must be reported, not saturated; position = end of the digits.
    auto big = Json::parse("9223372036854775808");
    CHECK(!big.ok);
    CHECK_EQ(big.error.line, 1);
    CHECK_EQ(big.error.col, 20);
}

TEST(json_nesting) {
    auto r = Json::parse("{\"a\":[1,2,{\"b\":null}],\"c\":{}}");
    REQUIRE(r.ok);
    CHECK(r.value.is_object());
    const Json* a = r.value.find("a");
    REQUIRE(a && a->is_array());
    CHECK_EQ(a->array_items().size(), 3u);
    CHECK_EQ(a->array_items()[0].as_int(), 1);
    const Json* inner = a->array_items()[2].find("b");
    CHECK(inner && inner->is_null());
    const Json* c = r.value.find("c");
    CHECK(c && c->is_object() && c->members().empty());
    CHECK(r.value.find("missing") == nullptr);

    auto arr = Json::parse("[[]]");
    CHECK(arr.ok && arr.value.array_items().size() == 1);

    // Deep nesting must fail loudly, not recurse into stack overflow.
    std::string deep(70, '[');
    auto d = Json::parse(deep + std::string(70, ']'));
    CHECK(!d.ok);
}

TEST(json_strings_utf8_escapes) {
    auto e = Json::parse("\"\\u00e9\"");
    REQUIRE(e.ok);
    CHECK_EQ(e.value.as_string(), std::string("\xc3\xa9"));  // é in UTF-8

    auto em = Json::parse("\"\\ud83d\\ude00\"");  // surrogate pair -> U+1F600
    REQUIRE(em.ok);
    CHECK_EQ(em.value.as_string(), std::string("\xf0\x9f\x98\x80"));

    auto raw = Json::parse("\"\xc3\xa9\"");  // raw UTF-8 passes through
    REQUIRE(raw.ok);
    CHECK_EQ(raw.value.as_string(), std::string("\xc3\xa9"));

    auto esc = Json::parse("\"a\\nb\"");
    REQUIRE(esc.ok);
    CHECK_EQ(esc.value.as_string(), std::string("a\nb"));

    auto q = Json::parse("\"\\\"\\\\\\/\"");  // quote, backslash, slash
    REQUIRE(q.ok);
    CHECK_EQ(q.value.as_string(), std::string("\"\\/"));
    CHECK_EQ(q.value.as_string().size(), 3u);

    // Lone high surrogate is an error; invalid \u hex is an error.
    CHECK(!Json::parse("\"\\ud800\"").ok);
    auto bad = Json::parse("\"\\uZZ\"");
    CHECK(!bad.ok);
    CHECK_EQ(bad.error.line, 1);
    CHECK_EQ(bad.error.col, 4);  // position of the first bad hex digit
}

// S1-1: a lone LOW surrogate (\udc00) must be rejected exactly like a lone high one.
// Before the fix it was accepted and emitted CESU-8 bytes ed b0 80 - invalid UTF-8.
TEST(json_lone_surrogates_symmetric) {
    // Lone low surrogate at the end of a string.
    auto lo = Json::parse("\"\\udc00\"");
    CHECK(!lo.ok);
    CHECK_EQ(lo.error.line, 1);
    CHECK_EQ(lo.error.col, 8);  // the character right after the \uXXXX escape
    CHECK_EQ(lo.error.msg, std::string("lone surrogate escape"));

    // Lone low surrogate in the middle of a string: no partial text is produced.
    auto mid = Json::parse("\"x\\udc00y\"");
    CHECK(!mid.ok);
    CHECK_EQ(mid.error.msg, std::string("lone surrogate escape"));

    // The whole low-surrogate half of the range is rejected.
    CHECK(!Json::parse("\"\\udcff\"").ok);
    CHECK(!Json::parse("\"\\ud800\"").ok);  // high half (regression: stays rejected)

    // Low then high is not a pair (order matters): rejected at the first escape.
    auto rev = Json::parse("\"\\udc00\\ud83d\"");
    CHECK(!rev.ok);
    CHECK_EQ(rev.error.msg, std::string("lone surrogate escape"));

    // A high surrogate followed by a non-surrogate escape is rejected (unchanged).
    CHECK(!Json::parse("\"\\ud83d\\u0041\"").ok);

    // A valid pair in the middle of text is accepted and decodes to the code point.
    auto pair = Json::parse("\"x\\ud83d\\ude00y\"");
    REQUIRE(pair.ok);
    CHECK_EQ(pair.value.as_string(), std::string("x\xf0\x9f\x98\x80y"));

    // A lone low surrogate inside an object key is rejected too.
    auto key = Json::parse("{\"\\udc00\":1}");
    CHECK(!key.ok);
    CHECK_EQ(key.error.msg, std::string("lone surrogate escape"));
}

TEST(json_broken_positions) {
    auto a = Json::parse("{\"a\": }");  // '}' at line 1, column 7
    CHECK(!a.ok);
    CHECK_EQ(a.error.line, 1);
    CHECK_EQ(a.error.col, 7);

    auto b = Json::parse("{\n \"a\" 1}");  // missing ':' at line 2, column 6
    CHECK(!b.ok);
    CHECK_EQ(b.error.line, 2);
    CHECK_EQ(b.error.col, 6);

    auto c = Json::parse("{\"a\":1,}");  // trailing comma: key expected at col 8
    CHECK(!c.ok);
    CHECK_EQ(c.error.line, 1);
    CHECK_EQ(c.error.col, 8);

    auto d = Json::parse("[1,]");  // value expected at col 4
    CHECK(!d.ok);
    CHECK_EQ(d.error.col, 4);

    auto t = Json::parse("1 2");  // trailing characters at col 3
    CHECK(!t.ok);
    CHECK_EQ(t.error.line, 1);
    CHECK_EQ(t.error.col, 3);

    auto empty = Json::parse("");
    CHECK(!empty.ok);
    CHECK_EQ(empty.error.line, 1);
    CHECK_EQ(empty.error.col, 1);

    auto str = Json::parse("\"abc");  // unterminated string
    CHECK(!str.ok);
    CHECK_EQ(str.error.line, 1);
    CHECK_EQ(str.error.col, 5);

    auto obj = Json::parse("{");  // unterminated object
    CHECK(!obj.ok);
    CHECK_EQ(obj.error.col, 2);

    auto lz = Json::parse("01");  // JSON forbids leading zero; position = the offending digit
    CHECK(!lz.ok);
    CHECK_EQ(lz.error.col, 2);

    auto key = Json::parse("{1:2}");  // key must be a string
    CHECK(!key.ok);
    CHECK_EQ(key.error.col, 2);
}

TEST(json_round_trip) {
    Json root = Json::object();
    root.add("i", Json::integer(52));
    root.add("r", Json::real(1.5));
    root.add("s", Json::string("é\"\n"));  // UTF-8 + quote + newline
    root.add("t", Json::boolean(true));
    root.add("n", Json::null_value());
    Json arr = Json::array();
    arr.push_back(Json::integer(1));
    Json inner = Json::array();
    inner.push_back(Json::integer(2));
    arr.push_back(std::move(inner));
    root.add("arr", std::move(arr));
    root.add("obj", Json::object());

    // Pretty round-trip: serialize -> parse -> serialize must be identical.
    std::string pretty = root.serialize(true);
    auto p1 = Json::parse(pretty);
    CHECK(p1.ok);
    CHECK_EQ(p1.value.serialize(true), pretty);

    // Compact round-trip.
    std::string compact = root.serialize(false);
    auto p2 = Json::parse(compact);
    CHECK(p2.ok);
    CHECK_EQ(p2.value.serialize(false), compact);

    // Spot values survive the trip.
    CHECK_EQ(p1.value.find("i")->as_int(), 52);
    CHECK_EQ(p1.value.find("r")->as_real(), 1.5);
    CHECK_EQ(p1.value.find("s")->as_string(), std::string("é\"\n"));
    CHECK(p1.value.find("n")->is_null());
    CHECK_EQ(compact.find("\"i\":52"), 1u);  // first member after the opening '{'
}

TEST(json_extra_fields) {
    auto r = Json::parse("{\"known\":1,\"extra\":\"x\",\"nested\":{\"deep\":true}}");
    REQUIRE(r.ok);
    const Json* extra = r.value.find("extra");
    REQUIRE(extra);
    CHECK_EQ(extra->as_string(), std::string("x"));
    CHECK_EQ(r.value.members().size(), 3u);
    // Order preserved, unknown fields are data the caller can inspect.
    CHECK_EQ(r.value.members()[0].first, std::string("known"));
    CHECK_EQ(r.value.members()[1].first, std::string("extra"));
    const Json* nested = r.value.find("nested");
    REQUIRE(nested && nested->is_object());
    CHECK(nested->find("deep")->as_bool());
    CHECK(r.value.find("absent") == nullptr);
}

// Round-trip contract: parse(serialize(v)) reproduces v exactly. The old serializer
// printed 6 digits (1.23456789 -> 1.23457), which broke it for any double needing
// more; the shortest round-trip form fixes that.
TEST(json_real_shortest_form) {
    CHECK_EQ(Json::real(1.23456789).serialize(false), std::string("1.23456789"));
    CHECK_EQ(Json::real(0.1).serialize(false), std::string("0.1"));
    CHECK_EQ(Json::real(1.5).serialize(false), std::string("1.5"));
    CHECK_EQ(Json::real(5.0).serialize(false), std::string("5.0"));  // integral keeps .0
    CHECK_EQ(Json::real(-0.0).serialize(false), std::string("-0.0"));
    // No JSON number form for inf/nan: they serialize as null (as before).
    const double inf = std::numeric_limits<double>::infinity();
    CHECK_EQ(Json::real(inf).serialize(false), std::string("null"));
    CHECK_EQ(Json::real(-inf).serialize(false), std::string("null"));
    CHECK_EQ(Json::real(std::numeric_limits<double>::quiet_NaN()).serialize(false),
             std::string("null"));
    // "1e999" in a file parses to inf, and serializes back as null.
    auto r = Json::parse("1e999");
    CHECK(r.ok);
    CHECK_EQ(r.value.serialize(false), std::string("null"));
}

TEST(json_real_round_trip_fuzz) {
    static const double kVals[] = {
        0.0, -0.0, 0.5, 0.1, 0.2, 1.0 / 3.0, 1.5, -1.5, 5.0, -5.0,
        1.23456789, 3.141592653589793, 1e17, 1e-7, 1e300, 1e-300, 2.5e-8,
        12345678901234567.0, 9007199254740993.0, 1e15, 1e-15,
        std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
        std::numeric_limits<double>::denorm_min(),
        -std::numeric_limits<double>::denorm_min(),
    };
    for (double v : kVals) {
        std::string text = Json::real(v).serialize(false);
        auto r = Json::parse(text);
        CHECK(r.ok);
        CHECK(r.value.is_real());
        double got = r.value.as_real();
        // Bit-exact, not just ==: -0.0 must survive as -0.0, big doubles stay exact.
        CHECK(std::memcmp(&got, &v, sizeof(double)) == 0);
        // Canonical text is stable in both forms: for a top-level number the compact
        // and the pretty serialization are the same text.
        CHECK_EQ(r.value.serialize(false), text);
        CHECK_EQ(r.value.serialize(true), text);
    }
}

TEST(json_int64_round_trip) {
    const int64_t v = 9223372036854775807LL;  // int64 max
    std::string text = Json::integer(v).serialize(false);
    CHECK_EQ(text, std::string("9223372036854775807"));
    auto r = Json::parse(text);
    CHECK(r.ok && r.value.is_int());
    CHECK_EQ(r.value.as_int(), v);
    CHECK_EQ(r.value.serialize(false), text);
    std::string mint = Json::integer(-9223372036854775807LL - 1).serialize(false);
    CHECK_EQ(mint, std::string("-9223372036854775808"));
    auto r2 = Json::parse(mint);
    CHECK(r2.ok && r2.value.is_int());
    CHECK_EQ(r2.value.as_int(), -9223372036854775807LL - 1);
}
