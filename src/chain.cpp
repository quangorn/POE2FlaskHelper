#include "chain.h"

#include <algorithm>
#include <vector>

#include "signature.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace poe2 {

namespace {

constexpr uint64_t kLpWindow = 0x40;      // SPEC §4.6: LocalPlayers window +-0x40
constexpr uint64_t kLpStep = 8;           // patches move the field in multiples of 8
constexpr uint64_t kLpMaxCount = 64;      // poe_scan.py read_vector max_count for LocalPlayers
constexpr uint64_t kCluMaxCapacity = 50;  // SPEC §4.6: capacity <= 50
constexpr uint64_t kIngameIndex = 4;      // States[4] = InGameState
constexpr uint64_t kMaxVitalSpan = 4096;  // one block read for all 5 Vital' fields

int64_t vital_base(const Offsets& o, int i) {
    switch (i) {
        case kHealth: return o.health_base();
        case kMana: return o.mana_base();
        case kEnergyShield: return o.es_base();
        case kWard: return o.ward_base();
        default: return o.rage_base();
    }
}

// Offsets/addresses in the log are printed as hex (the offsets.json layout, like
// poe_scan.py trace lines).
std::string hx(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%llX", static_cast<unsigned long long>(v));
    return buf;
}

// u64/u32 from a byte block (little-endian, x64 target).
uint64_t block_u64(const uint8_t* p, size_t off) {
    uint64_t v;
    std::memcpy(&v, p + off, 8);
    return v;
}

uint32_t block_u32(const uint8_t* p, size_t off) {
    uint32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}

struct ItemBlock {
    uint64_t details = 0, comp_first = 0, comp_last = 0, comp_end = 0;
};

// poe_scan.py read_cstring (review S3-4, fixed 2026-11-08): read 16-byte chunks forward from addr
// until a NUL byte or a read that returns nothing, and assemble the bytes between. The
// python prototype therefore accepts a "Life" page that ends exactly at the committed
// region edge with no visible NUL: its first chunk read comes back short ("Life"), the
// next chunk read fails and the string is complete. A C-string that the region edge
// truncates BEFORE the full name (e.g. "Lif") is not a match, same as python.
std::string read_cstring(IMemoryReader& m, uint64_t addr, size_t maxlen = 64) {
    std::string out;
    uint64_t pos = addr;
    // The python loop runs maxlen / 16 + 1 times; a chunk read with no bytes stops it.
    for (size_t c = 0; c < maxlen / 16 + 1; ++c) {
        uint8_t chunk[16];
        const size_t got = m.read(pos, chunk, sizeof chunk);
        if (!got) break;
        size_t nul = 0;
        bool found = false;
        for (size_t i = 0; i < got; ++i) {
            if (chunk[i] == 0) { nul = i; found = true; break; }
        }
        out.append(reinterpret_cast<const char*>(chunk), found ? nul : got);
        if (found) break;
        pos += 16;
    }
    return out;
}

}  // namespace

const char* chain_step_name(ChainStep s) {
    switch (s) {
        case ChainStep::Signature: return "Signature";
        case ChainStep::GameState: return "GameState";
        case ChainStep::CurrentStatePtr: return "CurrentStatePtr";
        case ChainStep::States: return "States";
        case ChainStep::AreaInstance: return "AreaInstance";
        case ChainStep::LocalPlayers: return "LocalPlayers";
        case ChainStep::Player: return "Player";
        case ChainStep::EntityValid: return "EntityValid";
        case ChainStep::ItemStruct: return "ItemStruct";
        case ChainStep::CompVector: return "CompVector";
        case ChainStep::Lookup: return "ComponentLookUp";
        case ChainStep::Bucket: return "CLU bucket";
        case ChainStep::Capacity: return "Capacity";
        case ChainStep::LifeIndex: return "LifeIndex";
        case ChainStep::LifePtr: return "LifePtr";
        case ChainStep::VitalRead: return "VitalRead";
    }
    return "Chain";  // unreachable
}

bool read_vector(IMemoryReader& m, uint64_t addr, uint64_t max_count, VectorTriple& out) {
    uint8_t buf[24];
    if (m.read(addr, buf, sizeof buf) < sizeof buf) return false;   // short read at a region edge
    const uint64_t first = block_u64(buf, 0);
    const uint64_t last = block_u64(buf, 8);
    const uint64_t end = block_u64(buf, 16);
    if (!first || last <= first || end < last || (last - first) % 8) return false;
    const uint64_t count = (last - first) / 8;
    if (!count || count > max_count) return false;
    out = VectorTriple{first, last, end, count};
    return true;
}

int64_t probe_local_players(IMemoryReader& m, uint64_t area, int64_t off, const Offsets& o) {
    // Candidates by increasing |delta|, exact offset first; within one delta the
    // lower offset comes first (the order poe_scan.py sorts to).
    for (uint64_t delta = 0; delta <= kLpWindow; delta += kLpStep) {
        const int n = delta == 0 ? 1 : 2;
        for (int k = 0; k < n; ++k) {
            const int64_t cur = off - static_cast<int64_t>(delta) + 2 * k * static_cast<int64_t>(delta);
            if (cur < 0) continue;
            VectorTriple v;
            if (!read_vector(m, static_cast<uint64_t>(area + cur), kLpMaxCount, v)) continue;
            const auto player = rd_u64(m, v.first);
            if (!player || !*player) continue;
            const auto valid = rd_u8(m, *player + static_cast<uint64_t>(o.EntityIsValid));
            if (valid && *valid == 0xC) return cur;
        }
    }
    return -1;
}

int64_t find_life_index(IMemoryReader& m, uint64_t b_first, uint64_t elems) {
    if (!b_first) return -1;
    for (uint64_t i = 0; i < elems; ++i) {
        uint8_t rec[16];
        if (m.read(b_first + i * 16, rec, sizeof rec) < 12) continue;
        const uint64_t name_ptr = block_u64(rec, 0);
        const uint32_t idx = block_u32(rec, 8);
        if (!name_ptr) continue;
        if (read_cstring(m, name_ptr) == "Life") return static_cast<int64_t>(idx);
    }
    return -1;
}

FullChainResult full_chain(IMemoryReader& m, const Offsets& o, ChainCache& cache) {
    FullChainResult r;
    cache.clear();

    // (A) signature -> GameStates* global address
    const auto hits = scan_signature(m, m.image_base(), m.image_size());
    if (hits.size() != 1) {
        r.fail = {ChainStep::Signature,
                  "signature matched " + std::to_string(hits.size()) +
                  " time(s), need exactly 1"};
        return r;
    }
    cache.global_addr = hits[0].global_addr;

    // (B) GameState object
    const auto gs = rd_u64(m, cache.global_addr);
    if (!gs) {
        r.fail = {ChainStep::GameState, "read at GameStates global " +
                                          hx(cache.global_addr) + " failed"};
        cache.clear();
        return r;
    }
    if (*gs == 0) {
        r.fail = {ChainStep::GameState, "GameStates pointer = 0 (menu / not in game)"};
        cache.clear();
        return r;
    }

    // (C) CurrentStatePtr: std::vector of 16-byte state entries, the current state is
    // the last element (poe_scan.py chain step C). Checked at attach; ticks read the
    // States array directly.
    // STRICTER than poe_scan.py on purpose (review S3-6, fixed 2026-11-08): python only checks the
    // 24-byte read length, then takes *(Last - 0x10) as the current state and continues
    // even when the triple is malformed. The C++ chain validates the triple as a real
    // std::vector (non-null First, Last > First, End >= Last, and the element stride
    // (Last - First) % 16 == 0) and requires the current-state pointer to read back
    // non-null and non-zero. The strictness is harmless: every extra check can only
    // reject a garbage/misaligned offset set, which SPEC §8 reports as "Offsets
    // mismatch" - the chain must never walk a pointer the vector bounds do not justify.
    uint8_t vec[24];
    if (m.read(*gs + static_cast<uint64_t>(o.CurrentStatePtr), vec, sizeof vec) < sizeof vec) {
        r.fail = {ChainStep::CurrentStatePtr, "short read of CurrentStatePtr at GameState+" +
                                              hx(static_cast<uint64_t>(o.CurrentStatePtr))};
        cache.clear();
        return r;
    }
    const uint64_t cs_first = block_u64(vec, 0), cs_last = block_u64(vec, 8),
                   cs_end = block_u64(vec, 16);
    if (!cs_first || cs_last <= cs_first || cs_end < cs_last || (cs_last - cs_first) % 16) {
        r.fail = {ChainStep::CurrentStatePtr, "CurrentStatePtr triple is not a valid vector"};
        cache.clear();
        return r;
    }
    const auto cur_state = rd_u64(m, cs_last - 16);
    if (!cur_state || !*cur_state) {
        r.fail = {ChainStep::CurrentStatePtr, "current state pointer = 0"};
        cache.clear();
        return r;
    }

    // (D)-(J): the dynamic walk is the same code path as tick(), with the caches
    // forced empty so LocalPlayers (window +-0x40) and life_index are resolved.
    cache.off_lp = -1;
    cache.life_index = -1;
    const TickResult t = tick(m, o, cache);
    if (!t.ok) {
        r.fail = t.fail;
        cache.clear();   // attach did not produce a usable cache
        return r;
    }
    r.ok = true;
    return r;
}

TickResult tick(IMemoryReader& m, const Offsets& o, ChainCache& cache) {
    TickResult r;
    if (!cache.valid()) {
        r.fail = {ChainStep::Signature, "no cached global_addr - full_chain must run first"};
        return r;
    }

    // (B) GameStates* -> GameState object
    const auto gs = rd_u64(m, cache.global_addr);
    if (!gs) {
        r.fail = {ChainStep::GameState, "read at GameStates global " +
                                          hx(cache.global_addr) + " failed (process closing?)"};
        return r;
    }
    if (*gs == 0) {
        r.fail = {ChainStep::GameState, "GameStates pointer = 0 (menu / loading)"};
        return r;
    }
    const uint64_t gst = *gs;

    // (C)-(D) States is TOTAL entries of 16 bytes {ptr, size}; InGameState = States[4].
    if (o.TOTAL <= static_cast<int64_t>(kIngameIndex)) {
        r.fail = {ChainStep::States, "TOTAL=" + std::to_string(o.TOTAL) +
                                     " has no InGameState slot"};
        return r;
    }
    const uint64_t states_len = static_cast<uint64_t>(o.TOTAL) * 16;
    std::vector<uint8_t> states(static_cast<size_t>(states_len));
    if (m.read(gst + static_cast<uint64_t>(o.States), states.data(), states.size())
        < states.size()) {
        r.fail = {ChainStep::States, "short read of States at GameState+" +
                                     hx(static_cast<uint64_t>(o.States))};
        return r;
    }
    const uint64_t ingame = block_u64(states.data(), kIngameIndex * 16);
    if (!ingame) {
        r.fail = {ChainStep::States, "InGameState (States[4]) = 0 (loading / menu)"};
        return r;
    }

    // (E) AreaInstance
    const auto area = rd_u64(m, ingame + static_cast<uint64_t>(o.AreaInstanceData));
    if (!area || !*area) {
        r.fail = {ChainStep::AreaInstance, "AreaInstance = 0 (zone loading)"};
        return r;
    }

    // (F) LocalPlayers: cached offset first; +-0x40 window probe on invalid cache.
    VectorTriple lp;
    int64_t off = cache.off_lp >= 0 ? cache.off_lp : static_cast<int64_t>(o.LocalPlayers);
    if (!read_vector(m, static_cast<uint64_t>(*area + off), kLpMaxCount, lp)) {
        const int64_t found = probe_local_players(m, *area, static_cast<int64_t>(o.LocalPlayers), o);
        if (found < 0) {
            r.fail = {ChainStep::LocalPlayers, "no valid vector in the +-0x40 window around AreaInstance+" +
                                               hx(static_cast<uint64_t>(o.LocalPlayers))};
            return r;
        }
        cache.off_lp = found;
        off = found;
        if (!read_vector(m, static_cast<uint64_t>(*area + off), kLpMaxCount, lp)) {
            r.fail = {ChainStep::LocalPlayers, "vector invalid after the window probe"};
            return r;
        }
    } else if (cache.off_lp < 0) {
        // The configured offset hit: pin it so the next tick starts from it.
        cache.off_lp = off;
    }

    const auto player = rd_u64(m, lp.first);
    if (!player || !*player) {
        r.fail = {ChainStep::Player, "LocalPlayers array is empty (player = 0)"};
        return r;
    }

    // (G/H) Entity validity
    const auto valid = rd_u8(m, *player + static_cast<uint64_t>(o.EntityIsValid));
    if (!valid) {
        r.fail = {ChainStep::EntityValid, "short read of IsValid"};
        return r;
    }
    if (*valid != 0xC) {
        r.fail = {ChainStep::EntityValid, "IsValid = 0x" + std::to_string(*valid) +
                                           ", need 0xC (offsets do not fit)"};
        return r;
    }

    // (I) ItemStruct: VTable@0, EntityDetailsPtr@8, ComponentList vector @+0x10
    uint8_t item[40];
    if (m.read(*player, item, sizeof item) < sizeof item) {
        r.fail = {ChainStep::ItemStruct, "short read of the entity struct"};
        return r;
    }
    const ItemBlock ib{block_u64(item, 8), block_u64(item, 16),
                       block_u64(item, 24), block_u64(item, 32)};
    if (!ib.comp_first || ib.comp_last <= ib.comp_first) {
        r.fail = {ChainStep::CompVector, "component vector is empty"};
        return r;
    }
    const uint64_t ncomp = (ib.comp_last - ib.comp_first) / 8;

    const auto lookup = rd_u64(m, ib.details + static_cast<uint64_t>(o.EntityDetailsLookup));
    if (!lookup || !*lookup) {
        r.fail = {ChainStep::Lookup, "ComponentLookUp = 0"};
        return r;
    }

    // (J) CLU bucket {Data{First,Last,End}, UnknownPtr, Capacity}
    uint8_t bucket[36];
    if (m.read(*lookup + static_cast<uint64_t>(o.CLU_Bucket), bucket, sizeof bucket)
        < sizeof bucket) {
        r.fail = {ChainStep::Bucket, "short read of the CLU bucket"};
        return r;
    }
    const uint64_t b_first = block_u64(bucket, 0);
    const uint64_t b_last = block_u64(bucket, 8);
    const uint32_t capacity = block_u32(bucket, 32);
    if (capacity > kCluMaxCapacity) {
        r.fail = {ChainStep::Capacity, "Capacity=" + std::to_string(capacity) +
                                       " > 50 (offsets do not fit)"};
        return r;
    }
    const uint64_t elems = (b_first && b_last > b_first) ? (b_last - b_first) / 16 : 0;

    int64_t idx = cache.life_index;
    if (idx < 0 || static_cast<uint64_t>(idx) >= elems) {
        idx = find_life_index(m, b_first, elems);
        cache.life_index = idx;
        if (idx < 0) {
            r.fail = {ChainStep::LifeIndex, "no \"Life\" entry in the CLU bucket"};
            return r;
        }
    }
    if (static_cast<uint64_t>(idx) >= ncomp) {
        r.fail = {ChainStep::LifeIndex, "Life index " + std::to_string(idx) +
                                         " is out of the comp vector (" +
                                         std::to_string(ncomp) + ")"};
        return r;
    }

    const auto life = rd_u64(m, ib.comp_first + static_cast<uint64_t>(idx) * 8);
    if (!life || !*life) {
        r.fail = {ChainStep::LifePtr, "Life component pointer = 0"};
        return r;
    }

    // Vital' fields: one block read covering all four fields of all 5 Vital'
    // structures inside Life (~10 reads on a tick in total). The span must cover
    // min/max over TotalOff, CurrentOff, ReservedOff AND ReservedFlat (review
    // S3-1, fixed 2026-11-08): an offsets.json with ReservedFlat > CurrentOff used to read past the
    // span end); every field index b+Off-lo is then inside [0, span).
    const int64_t fmin = std::min({o.TotalOff, o.CurrentOff, o.ReservedOff, o.ReservedFlat});
    const int64_t fmax = std::max({o.TotalOff, o.CurrentOff, o.ReservedOff, o.ReservedFlat});
    int64_t lo = vital_base(o, 0) + fmin;
    int64_t hi = lo + 4;
    for (int i = 0; i < kVitalCount; ++i) {
        const int64_t b = vital_base(o, i);
        lo = std::min(lo, b + fmin);
        hi = std::max(hi, b + fmax + 4);
    }
    const int64_t span = hi - lo;
    if (span <= 0 || static_cast<uint64_t>(span) > kMaxVitalSpan) {
        r.fail = {ChainStep::VitalRead, "vital block span " + std::to_string(span) +
                                         " bytes is out of bounds"};
        return r;
    }
    std::vector<uint8_t> block(static_cast<size_t>(span));
    if (m.read(*life + static_cast<uint64_t>(lo), block.data(), block.size()) < block.size()) {
        r.fail = {ChainStep::VitalRead, "short read of the vital block at Life+" +
                                        hx(static_cast<uint64_t>(lo))};
        return r;
    }
    for (int i = 0; i < kVitalCount; ++i) {
        const int64_t b = vital_base(o, i);
        VitalVals& v = r.vals[i];
        v.flat = block_u32(block.data(), static_cast<size_t>(b + o.ReservedFlat - lo));
        v.pct = block_u32(block.data(), static_cast<size_t>(b + o.ReservedOff - lo));
        v.total = block_u32(block.data(), static_cast<size_t>(b + o.TotalOff - lo));
        v.current = block_u32(block.data(), static_cast<size_t>(b + o.CurrentOff - lo));
    }
    r.ok = true;
    return r;
}

}  // namespace poe2
