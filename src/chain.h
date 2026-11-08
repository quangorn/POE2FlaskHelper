#pragma once
// The A-J indirect chain on IMemoryReader + parsed Offsets (SPEC §4.6, structure of
// scanner/poe_scan.py chain()/tick()):
//   signature -> GameStates* -> GameState -> States[4] (InGameState) -> AreaInstance
//   -> LocalPlayers (window +-0x40) -> Entity -> ComponentList -> ComponentLookUp
//   -> "Life" index -> Life component -> Vital values.
// Caches: global_addr, the actual LocalPlayers offset, life_index (SPEC §4.6).
// Every failure returns the typed step that broke, so the log can say exactly which
// offset does not fit ("Offsets mismatch", SPEC §7/§8).

#include <array>
#include <cstdint>
#include <string>

#include "config.h"      // VitalIndex / kVitalCount
#include "mem/reader.h"
#include "offsets.h"

namespace poe2 {

struct ChainCache {
    uint64_t global_addr = 0;   // GameStates* static address, from the signature scan
    int64_t off_lp = -1;        // actual LocalPlayers offset inside AreaInstance
    int64_t life_index = -1;    // index of "Life" in the CLU bucket
    void clear() { *this = ChainCache{}; }
    bool valid() const { return global_addr != 0; }
};

// Which step of the chain broke (SPEC §8: "Смещения не подходят" -> status text).
enum class ChainStep {
    Signature, GameState, CurrentStatePtr, States, AreaInstance, LocalPlayers,
    Player, EntityValid, ItemStruct, CompVector, Lookup, Bucket, Capacity,
    LifeIndex, LifePtr, VitalRead
};

const char* chain_step_name(ChainStep s);

struct ChainFail {
    ChainStep step = ChainStep::Signature;
    std::string detail;   // human-readable, English (SPEC §7: log language)
};

struct VitalVals {
    uint32_t total = 0, current = 0, flat = 0, pct = 0;
};

// The chain walks pointer by pointer. Three of the pointers are legitimately ZERO
// while the game is not in a zone: the GameStates object (main menu), the InGameState
// entry States[4] (login / loading), and AreaInstance (zone loading). SPEC §8 calls
// this a normal status, so it is reported as `not_in_zone` and must NOT be treated as
// a chain break (SPEC §4.6 item 5, amended 2026-11-08): the session keeps the handle
// open and does not detach. Every other failure means the offsets do not fit.
struct FullChainResult {
    bool ok = false;
    ChainFail fail;
    uint64_t life_addr = 0;
    bool not_in_zone = false;
};

struct TickResult {
    bool ok = false;
    ChainFail fail;
    std::array<VitalVals, kVitalCount> vals;   // Health, Mana, EnergyShield, Ward, Rage
    bool not_in_zone = false;
};

// std::vector {First,Last,End} triple at addr (SPEC §4.6 vector bounds checks).
struct VectorTriple {
    uint64_t first = 0, last = 0, end = 0, count = 0;
};

bool read_vector(IMemoryReader& m, uint64_t addr, uint64_t max_count, VectorTriple& out);

// LocalPlayers in the window +-0x40 around `off` (SPEC §4.6): 8-aligned candidates
// ordered by |delta| (exact offset first), a candidate is accepted when the triple
// is a valid std::vector<IntPtr> and its first element is an Entity with
// IsValid == 0xC. Returns the accepted offset, -1 when the window has none.
int64_t probe_local_players(IMemoryReader& m, uint64_t area, int64_t off, const Offsets& o);

// Index of the "Life" entry in a CLU bucket array (16-byte records
// {name ptr, index}); -1 when "Life" is not in the bucket.
int64_t find_life_index(IMemoryReader& m, uint64_t b_first, uint64_t elems);

// Full chain A-J: fills cache.global_addr / off_lp / life_index. Runs at attach.
FullChainResult full_chain(IMemoryReader& m, const Offsets& o, ChainCache& cache);

// Live tick: dynamic pointers only, cached global_addr/off_lp/life_index, one block
// read for all 5 Vital' fields, ~10 reads/tick. Invalid cache entries are re-probed
// (off_lp window, life_index rescan) inside the tick.
TickResult tick(IMemoryReader& m, const Offsets& o, ChainCache& cache);

}  // namespace poe2
