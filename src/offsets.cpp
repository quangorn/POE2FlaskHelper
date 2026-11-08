#include "offsets.h"

#include <cassert>
#include <cstdio>
#include <iterator>
#include <vector>

#include "json.h"

namespace poe2 {

namespace {

// Sanity bounds: chain/vital offsets live in object structures, < 16 MB is plenty.
constexpr int64_t kMaxOffset = 0x1000000;

struct KeySpec { const char* name; int64_t lo; int64_t hi; };

// Full key set from SPEC §6 - every key mandatory, order fixed for stable errors.
const KeySpec kKeys[] = {
    {"CurrentStatePtr", 0, kMaxOffset},
    {"States", 0, kMaxOffset},
    {"TOTAL", 1, 64},
    {"AreaInstanceData", 0, kMaxOffset},
    {"LocalPlayers", 0, kMaxOffset},
    {"EntityId", 0, kMaxOffset},
    {"EntityIsValid", 0, kMaxOffset},
    {"EntityDetailsName", 0, kMaxOffset},
    {"EntityDetailsLookup", 0, kMaxOffset},
    {"CLU_Bucket", 0, kMaxOffset},
    {"TotalOff", 1, kMaxOffset},
    {"CurrentOff", 1, kMaxOffset},
    {"ReservedOff", 0, kMaxOffset},
    {"ReservedFlat", 0, kMaxOffset},
    {"HealthOff", 0, kMaxOffset},
    {"ManaOff", 0, kMaxOffset},
    {"ESOff", 0, kMaxOffset},
    {"WardOff", 0, kMaxOffset},
    {"RageOff", 0, kMaxOffset},
    {"BackPtrOff", 1, kMaxOffset},
    {"Score", 0, 1000},
};

void add_error(std::string& agg, const std::string& msg) {
    if (!agg.empty()) agg += "; ";
    agg += msg;
}

int64_t* field(Offsets& o, const char* name) {
    const std::string s(name);
    if (s == "CurrentStatePtr") return &o.CurrentStatePtr;
    if (s == "States") return &o.States;
    if (s == "TOTAL") return &o.TOTAL;
    if (s == "AreaInstanceData") return &o.AreaInstanceData;
    if (s == "LocalPlayers") return &o.LocalPlayers;
    if (s == "EntityId") return &o.EntityId;
    if (s == "EntityIsValid") return &o.EntityIsValid;
    if (s == "EntityDetailsName") return &o.EntityDetailsName;
    if (s == "EntityDetailsLookup") return &o.EntityDetailsLookup;
    if (s == "CLU_Bucket") return &o.CLU_Bucket;
    if (s == "TotalOff") return &o.TotalOff;
    if (s == "CurrentOff") return &o.CurrentOff;
    if (s == "ReservedOff") return &o.ReservedOff;
    if (s == "ReservedFlat") return &o.ReservedFlat;
    if (s == "HealthOff") return &o.HealthOff;
    if (s == "ManaOff") return &o.ManaOff;
    if (s == "ESOff") return &o.ESOff;
    if (s == "WardOff") return &o.WardOff;
    if (s == "RageOff") return &o.RageOff;
    if (s == "BackPtrOff") return &o.BackPtrOff;
    if (s == "Score") return &o.Score;  // Score is a key like any other, not a fallback
    // MINOR_ISSUES S1-7: the old code returned &o.Score for ANY unknown name.
    // field() is called only with kKeys names (the loop below), so this default is
    // unreachable; assert documents it and nullptr means "no field" for the caller.
    assert(false && "field() called with a name outside kKeys");
    return nullptr;
}

}  // namespace

OffsetsResult offsets_parse(const std::string& text) {
    OffsetsResult r;
    auto pr = Json::parse(text);
    if (!pr.ok) {
        char buf[256];
        std::snprintf(buf, sizeof buf, "line %d, column %d: %s",
                      pr.error.line, pr.error.col, pr.error.msg.c_str());
        r.error = buf;
        return r;
    }
    if (!pr.value.is_object()) {
        r.error = "root must be a JSON object";
        return r;
    }
    const Json& root = pr.value;
    std::vector<int64_t*> out(std::size(kKeys));
    for (size_t i = 0; i < std::size(kKeys); ++i) {
        const KeySpec& k = kKeys[i];
        out[i] = field(r.offsets, k.name);
        if (!out[i]) {  // defensive: a kKeys name with no field (unreachable, S1-7)
            add_error(r.error, std::string("internal: no field for key \"") + k.name + "\"");
            continue;
        }
        const Json* n = root.find(k.name);
        if (!n) {
            add_error(r.error, std::string("missing key \"") + k.name + "\"");
            continue;
        }
        if (!n->is_int()) {
            add_error(r.error, std::string("key \"") + k.name + "\" is not an integer");
            continue;
        }
        int64_t v = n->as_int();
        if (v < k.lo || v > k.hi) {
            char buf[256];
            std::snprintf(buf, sizeof buf, "key \"%s\" out of range (%lld..%lld)",
                          k.name, k.lo, k.hi);
            add_error(r.error, buf);
            continue;
        }
        *out[i] = v;
    }
    if (!r.error.empty()) return r;

    // Cross-checks (SPEC §6): vital positions are Total-inside-Life, base = Off - TotalOff
    // must be strictly positive.
    const int64_t t = r.offsets.TotalOff;
    const std::pair<const char*, int64_t> vital_offs[] = {
        {"HealthOff", r.offsets.HealthOff}, {"ManaOff", r.offsets.ManaOff},
        {"ESOff", r.offsets.ESOff}, {"WardOff", r.offsets.WardOff},
        {"RageOff", r.offsets.RageOff},
    };
    for (const auto& kv : vital_offs) {
        if (kv.second <= t) {
            add_error(r.error, std::string("\"") + kv.first
                      + "\" must be > TotalOff (vital base must be positive)");
        }
    }
    r.ok = r.error.empty();
    return r;
}

}  // namespace poe2
