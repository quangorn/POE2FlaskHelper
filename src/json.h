#pragma once
// Minimal JSON parser + serializer (Stage 1, plan.md §3): objects, arrays, strings
// (UTF-8, escapes incl. \uXXXX with surrogate pairs), integers, reals, bool, null.
// Parse errors carry line/column. serialize() round-trips parse(). No external deps.
// Floats serialize in the shortest form that parses back to the exact same double,
// so parse(serialize(v)) == v for every double (1.23456789, 1e17, 1e-7, ...).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace poe2 {

struct JsonParseResult;  // full definition below; returned by Json::parse

class Json {
public:
    enum class Type { Null, Bool, Int, Real, String, Array, Object };

    Json() : type_(Type::Null) {}

    static Json null_value() { return Json(); }
    static Json boolean(bool b) { Json j; j.type_ = Type::Bool; j.b_ = b; return j; }
    static Json integer(int64_t i) { Json j; j.type_ = Type::Int; j.i_ = i; return j; }
    static Json real(double d) { Json j; j.type_ = Type::Real; j.d_ = d; return j; }
    static Json string(std::string s) { Json j; j.type_ = Type::String; j.s_ = std::move(s); return j; }
    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    void push_back(Json v) { arr_.push_back(std::move(v)); }
    void add(const std::string& key, Json v) { obj_.emplace_back(key, std::move(v)); }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_int() const { return type_ == Type::Int; }
    bool is_real() const { return type_ == Type::Real; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    // Accessors are safe on any node: wrong type returns a neutral value.
    bool as_bool() const { return b_; }
    int64_t as_int() const { return i_; }
    double as_real() const { return d_; }
    const std::string& as_string() const { return s_; }
    const std::vector<Json>& array_items() const { return arr_; }
    const std::vector<std::pair<std::string, Json>>& members() const { return obj_; }

    // Object member lookup; nullptr when absent. Members keep insertion order.
    const Json* find(const std::string& key) const {
        for (const auto& kv : obj_) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }

    static JsonParseResult parse(const std::string& text);

    // serialize(pretty): compact one-line, or indented with 2-space steps.
    // parse(serialize(v)) reproduces v; serialize(parse(t)) is canonical for t.
    std::string serialize(bool pretty) const;

private:
    Type type_ = Type::Null;
    bool b_ = false;
    int64_t i_ = 0;
    double d_ = 0.0;
    std::string s_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;
};

struct JsonError {
    std::string msg;
    int line = 1;
    int col = 1;
};

struct JsonParseResult {
    bool ok = false;
    Json value;
    JsonError error;
};

}  // namespace poe2
