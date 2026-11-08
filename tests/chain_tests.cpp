// Chain A-J + tick tests (plan.md stage 3): happy path (Life + index) and EVERY
// failure branch - GameState=0, short read, LocalPlayers shifted +-8 (window +-0x40),
// IsValid != 0xC, empty comp-vector, no "Life" in CLU, capacity > 50, life_index out
// of range; tick values, caches, rescan on invalid cache, short reads, closed process.
#include "test.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "chain.h"
#include "synthetic_layout.h"
#include "fakes/FakeMemory.h"

using poe2::ChainCache;
using poe2::ChainStep;
using poe2::FullChainResult;
using poe2::TickResult;
using poe2::testing::FakeMemory;
using poe2::testing::Synth;
using poe2::testing::rev34;

namespace {

// A ready FakeMemory with the full synthetic layout.
FakeMemory make_ready() {
    FakeMemory m;
    build_synthetic(m, rev34());
    return m;
}

}  // namespace

TEST(chain_happy_path) {
    FakeMemory m = make_ready();
    ChainCache c;
    const auto r = poe2::full_chain(m, rev34(), c);
    CHECK(r.ok);
    CHECK_EQ(r.fail.step, ChainStep::Signature);   // no failure recorded
    CHECK_EQ(c.global_addr, Synth::kGlobal);
    CHECK_EQ(c.off_lp, rev34().LocalPlayers);      // exact offset hits first
    CHECK_EQ(c.life_index, 0);                     // "Life" is bucket record 0
}

TEST(chain_gamestate_zero) {
    FakeMemory m = make_ready();
    m.put_u64(Synth::kGlobal, 0);   // GameStates* = 0 (menu / not logged in)
    ChainCache c;
    const auto r = poe2::full_chain(m, rev34(), c);
    CHECK(!r.ok);
    CHECK_EQ(r.fail.step, ChainStep::GameState);
    CHECK(!c.valid());   // a failed attach leaves no cache
}

TEST(chain_short_read_current_state_ptr) {
    FakeMemory m = make_ready();
    m.reset_reads();
    // full_chain reads: 9 signature chunks (1 MB with a 15-byte overlap over the
    // 8 MB image), then the GameState pointer (10th), then CurrentStatePtr (11th).
    // Killing the 11th read gives the CurrentStatePtr branch.
    m.fail_on_read(11);
    ChainCache c;
    const auto r = poe2::full_chain(m, rev34(), c);
    CHECK(!r.ok);
    CHECK_EQ(r.fail.step, ChainStep::CurrentStatePtr);
}

TEST(chain_local_players_shifted_plus8) {
    FakeMemory m = make_ready();
    // The patch moves the field +8: the exact offset is no longer a vector.
    const int64_t off = rev34().LocalPlayers;
    m.put_u64(Synth::kArea + static_cast<uint64_t>(off), 0);
    m.put_u64(Synth::kArea + static_cast<uint64_t>(off) + 8, Synth::kPlayersArr);
    m.put_u64(Synth::kArea + static_cast<uint64_t>(off) + 16, Synth::kPlayersArr + 8);
    m.put_u64(Synth::kArea + static_cast<uint64_t>(off) + 24, Synth::kPlayersArr + 8);
    ChainCache c;
    const auto r = poe2::full_chain(m, rev34(), c);
    CHECK(r.ok);
    CHECK_EQ(c.off_lp, off + 8);   // the window probe found the shifted field
}

TEST(chain_local_players_shifted_minus8) {
    FakeMemory m = make_ready();
    const int64_t off = rev34().LocalPlayers;
    // The patch moves the field -8: the triple now lives at AreaInstance+off-8
    // (First@-8, Last@0, End@+8), so the exact offset reads a dead pointer.
    m.put_u64(Synth::kArea + static_cast<uint64_t>(off), 0);
    m.put_u64(Synth::kArea + static_cast<uint64_t>(off) - 8, Synth::kPlayersArr);
    m.put_u64(Synth::kArea + static_cast<uint64_t>(off), Synth::kPlayersArr + 8);
    m.put_u64(Synth::kArea + static_cast<uint64_t>(off) + 8, Synth::kPlayersArr + 8);
    ChainCache c;
    const auto r = poe2::full_chain(m, rev34(), c);
    CHECK(r.ok);
    CHECK_EQ(c.off_lp, off - 8);   // the lower offset wins (sorted by |delta|)
}

TEST(chain_local_players_out_of_window) {
    // +-0x40 window exhausted: no valid vector anywhere.
    FakeMemory m = make_ready();
    const int64_t off = rev34().LocalPlayers;
    m.put_u64(Synth::kArea + static_cast<uint64_t>(off), 0);
    ChainCache c;
    const auto r = poe2::full_chain(m, rev34(), c);
    CHECK(!r.ok);
    CHECK_EQ(r.fail.step, ChainStep::LocalPlayers);
}

TEST(chain_entity_invalid) {
    FakeMemory m = make_ready();
    m.put_u8(Synth::kEntity + static_cast<uint64_t>(rev34().EntityIsValid), 0xB);
    ChainCache c;
    const auto r = poe2::full_chain(m, rev34(), c);
    CHECK(!r.ok);
    CHECK_EQ(r.fail.step, ChainStep::EntityValid);
}

TEST(chain_empty_comp_vector) {
    FakeMemory m = make_ready();
    m.put_u64(Synth::kEntity + 0x18, Synth::kCompArr);   // Last == First: empty vector
    ChainCache c;
    const auto r = poe2::full_chain(m, rev34(), c);
    CHECK(!r.ok);
    CHECK_EQ(r.fail.step, ChainStep::CompVector);
}

TEST(chain_no_life_in_clu) {
    FakeMemory m = make_ready();
    // Both bucket records resolve to "Position": no "Life" name anywhere.
    m.put_u64(Synth::kBucketArr, Synth::kStrPos);   // record 0 -> "Position"
    ChainCache c;
    const auto r = poe2::full_chain(m, rev34(), c);
    CHECK(!r.ok);
    CHECK_EQ(r.fail.step, ChainStep::LifeIndex);
    CHECK(r.fail.detail.find("Life") != std::string::npos);
}

TEST(chain_life_at_region_edge_without_nul_is_accepted) {
    // MINOR_ISSUES S3-4: "Life" written at the very end of a committed region with NO
    // NUL after it. poe_scan.py read_cstring reads 16-byte chunks: the first chunk read
    // comes back short ("Life"), the next chunk read fails, and the assembled string is
    // "Life" -> the entry is accepted. The old C++ check (got >= 5 with name[4] == NUL)
    // rejected it.
    FakeMemory m = make_ready();
    const uint64_t edge_str = 0x20900200;      // a fresh region, "Life" ending at its edge
    m.add_region(edge_str, 4, 0);
    m.put_bytes(edge_str, reinterpret_cast<const uint8_t*>("Life"), 4);
    m.put_u64(Synth::kBucketArr, edge_str);     // record 0 name -> the edge page
    ChainCache c;
    const auto r = poe2::full_chain(m, rev34(), c);
    CHECK(r.ok);
    CHECK_EQ(c.life_index, 0);                  // record 0 is "Life", index field 0
}

TEST(chain_life_truncated_at_region_edge_is_not_a_match) {
    // The region edge cuts the name BEFORE "Life" is complete ("Lif"): read_cstring
    // assembles "Lif", python's comparison to "Life" fails, and the chain reports the
    // missing Life entry. Pins that the parity is not a prefix match.
    FakeMemory m = make_ready();
    const uint64_t cut_str = 0x20900300;
    m.add_region(cut_str, 3, 0);
    m.put_bytes(cut_str, reinterpret_cast<const uint8_t*>("Lif"), 3);
    m.put_u64(Synth::kBucketArr, cut_str);      // record 0 -> "Lif", record 1 -> "Position"
    ChainCache c;
    const auto r = poe2::full_chain(m, rev34(), c);
    CHECK(!r.ok);
    CHECK_EQ(r.fail.step, ChainStep::LifeIndex);
}

TEST(chain_life_name_past_the_chunk_cap_is_not_a_match) {
    // read_cstring reads at most maxlen/16 + 1 = 5 chunks (poe_scan.py maxlen=64), then
    // stops even with no NUL. A page starting "Life" and running long is read as 80
    // bytes, not "Life" -> rejected, and the 5-chunk bound keeps the read count fixed
    // (5 chunk reads for this record, no unbounded scan).
    FakeMemory m = make_ready();
    const uint64_t long_str = 0x20900400;
    m.add_region(long_str, 128, 'A');           // no NUL anywhere
    m.put_bytes(long_str, reinterpret_cast<const uint8_t*>("Life"), 4);
    m.put_u64(Synth::kBucketArr, long_str);     // record 0 -> "LifeAAAA...", record 1 -> "Position"
    ChainCache c;
    const auto r = poe2::full_chain(m, rev34(), c);
    CHECK(!r.ok);
    CHECK_EQ(r.fail.step, ChainStep::LifeIndex);
    // The scan stops at the chunk cap: reset the counter and pin the read budget -
    // record 0 = 1 rec + 5 chunks, record 1 = 1 rec + 1 short name read.
    m.reset_reads();
    CHECK_EQ(poe2::find_life_index(m, Synth::kBucketArr, 2), -1);
    CHECK_EQ(m.reads(), 1 + 5 + 1 + 1);         // no unbounded scan past the cap
}

TEST(chain_capacity_over_50) {
    FakeMemory m = make_ready();
    m.put_u32(Synth::kLookup + static_cast<uint64_t>(rev34().CLU_Bucket) + 32, 51);
    ChainCache c;
    const auto r = poe2::full_chain(m, rev34(), c);
    CHECK(!r.ok);
    CHECK_EQ(r.fail.step, ChainStep::Capacity);
}

TEST(chain_life_index_out_of_comp_range) {
    FakeMemory m = make_ready();
    // "Life" is in the bucket at index 10, the comp vector has only 4 entries.
    m.put_u32(Synth::kBucketArr + 8, 10);
    ChainCache c;
    const auto r = poe2::full_chain(m, rev34(), c);
    CHECK(!r.ok);
    CHECK_EQ(r.fail.step, ChainStep::LifeIndex);
    CHECK(r.fail.detail.find("out of") != std::string::npos);
}

TEST(tick_all_five_vitals) {
    FakeMemory m = make_ready();
    ChainCache c;
    REQUIRE(poe2::full_chain(m, rev34(), c).ok);
    const auto t = poe2::tick(m, rev34(), c);
    REQUIRE(t.ok);
    CHECK_EQ(t.vals[poe2::kHealth].total, Synth::hp_total);
    CHECK_EQ(t.vals[poe2::kHealth].current, Synth::hp_current);
    CHECK_EQ(t.vals[poe2::kHealth].flat, Synth::hp_flat);
    CHECK_EQ(t.vals[poe2::kHealth].pct, Synth::hp_pct);
    CHECK_EQ(t.vals[poe2::kMana].total, Synth::mp_total);
    CHECK_EQ(t.vals[poe2::kMana].current, Synth::mp_current);
    CHECK_EQ(t.vals[poe2::kMana].flat, Synth::mp_flat);
    CHECK_EQ(t.vals[poe2::kMana].pct, Synth::mp_pct);
    CHECK_EQ(t.vals[poe2::kEnergyShield].total, Synth::es_total);
    CHECK_EQ(t.vals[poe2::kEnergyShield].current, Synth::es_current);
    CHECK_EQ(t.vals[poe2::kWard].total, Synth::ward_total);
    CHECK_EQ(t.vals[poe2::kWard].current, Synth::ward_current);
    CHECK_EQ(t.vals[poe2::kWard].flat, Synth::ward_flat);
    CHECK_EQ(t.vals[poe2::kWard].pct, Synth::ward_pct);
    CHECK_EQ(t.vals[poe2::kRage].total, Synth::rage_total);
    CHECK_EQ(t.vals[poe2::kRage].current, Synth::rage_current);
    CHECK_EQ(t.vals[poe2::kRage].flat, Synth::rage_flat);
    CHECK_EQ(t.vals[poe2::kRage].pct, Synth::rage_pct);
}

TEST(tick_read_budget_and_cache_reuse) {
    FakeMemory m = make_ready();
    ChainCache c;
    REQUIRE(poe2::full_chain(m, rev34(), c).ok);
    m.reset_reads();
    const auto t = poe2::tick(m, rev34(), c);
    REQUIRE(t.ok);
    // ~10 reads per tick (SPEC §4.6): 10 dynamic reads + one vital block read.
    CHECK(t.vals[poe2::kHealth].current == Synth::hp_current);
    CHECK_EQ(m.reads(), 11);
    // The cached LocalPlayers offset is used: the exact-offset field is destroyed,
    // a valid vector is written at +8, and the tick must succeed with no probe reads.
    const int64_t off = rev34().LocalPlayers;
    m.put_u64(Synth::kArea + static_cast<uint64_t>(off), 0);
    m.put_u64(Synth::kArea + static_cast<uint64_t>(off) + 8, Synth::kPlayersArr);
    m.put_u64(Synth::kArea + static_cast<uint64_t>(off) + 16, Synth::kPlayersArr + 8);
    m.put_u64(Synth::kArea + static_cast<uint64_t>(off) + 24, Synth::kPlayersArr + 8);
    c.off_lp = off + 8;   // cached shift
    m.reset_reads();
    const auto t2 = poe2::tick(m, rev34(), c);
    CHECK(t2.ok);
    CHECK_EQ(m.reads(), 11);   // cache reuse: no window probe happened
    CHECK_EQ(c.off_lp, off + 8);
}

TEST(tick_rescan_life_index_on_invalid_cache) {
    FakeMemory m = make_ready();
    ChainCache c;
    REQUIRE(poe2::full_chain(m, rev34(), c).ok);
    c.life_index = -1;   // cache invalidated
    m.reset_reads();
    const auto t = poe2::tick(m, rev34(), c);
    REQUIRE(t.ok);
    CHECK_EQ(c.life_index, 0);            // the rescan restored it
    // The extra reads are exactly the bucket records: record + name per entry.
    CHECK_EQ(m.reads(), 11 + 2);
    // A cached index beyond the bucket size also forces a rescan.
    c.life_index = 7;
    m.reset_reads();
    CHECK(poe2::tick(m, rev34(), c).ok);
    CHECK_EQ(c.life_index, 0);
}

TEST(tick_short_read_vital_block) {
    FakeMemory m = make_ready();
    ChainCache c;
    REQUIRE(poe2::full_chain(m, rev34(), c).ok);
    m.reset_reads();
    m.fail_on_read(11);   // the 11th read is the vital block
    const auto t = poe2::tick(m, rev34(), c);
    CHECK(!t.ok);
    CHECK_EQ(t.fail.step, ChainStep::VitalRead);
}

TEST(tick_process_closed) {
    FakeMemory m = make_ready();
    ChainCache c;
    REQUIRE(poe2::full_chain(m, rev34(), c).ok);
    m.fail_all_reads(true);   // every read fails: the process handle is dead
    const auto t = poe2::tick(m, rev34(), c);
    CHECK(!t.ok);
    CHECK_EQ(t.fail.step, ChainStep::GameState);   // first read is the global pointer
}

TEST(tick_without_cache_is_signature_failure) {
    FakeMemory m = make_ready();
    ChainCache c;   // empty cache
    const auto t = poe2::tick(m, rev34(), c);
    CHECK(!t.ok);
    CHECK_EQ(t.fail.step, ChainStep::Signature);
}

TEST(tick_area_instance_zero) {
    FakeMemory m = make_ready();
    ChainCache c;
    REQUIRE(poe2::full_chain(m, rev34(), c).ok);
    m.put_u64(Synth::kIngame + static_cast<uint64_t>(rev34().AreaInstanceData), 0);
    const auto t = poe2::tick(m, rev34(), c);
    CHECK(!t.ok);
    CHECK_EQ(t.fail.step, ChainStep::AreaInstance);
}

TEST(tick_reserved_flat_above_current_off_no_oob) {
    // MINOR_ISSUES S3-1: an offsets.json where ReservedFlat > CurrentOff. The span
    // must cover flat/percent/total/current of every Vital', so all field indices
    // b+Off-lo are inside [0, span): the values read back are the ones written, and
    // nothing reads past the span end (FakeMemory would report a short read).
    poe2::Offsets o = rev34();
    o.ReservedFlat = 60;    // > CurrentOff (56), TotalOff (52), ReservedOff (28); stays
    // below the Vital' struct stride (~88 bytes) so the synthetic writes of one Vital'
    // never land on a neighbour's fields.
    FakeMemory m;
    build_synthetic(m, o);  // the layout builder writes fields at base+ReservedFlat
    ChainCache c;
    REQUIRE(poe2::full_chain(m, o, c).ok);
    m.reset_reads();
    const auto t = poe2::tick(m, o, c);
    REQUIRE(t.ok);           // a full-length block read, no short read at the span end
    CHECK_EQ(m.last_read_size(), 348);   // span lo=life+452 (min of all four offsets),
                                         // hi=life+800 (ward base + 60 + 4): 348 bytes
    for (int i = 0; i < poe2::kVitalCount; ++i) {
        const poe2::VitalVals& v = t.vals[i];
        uint32_t t_exp = 0, c_exp = 0, f_exp = 0, p_exp = 0;
        switch (i) {
            case poe2::kHealth: t_exp = Synth::hp_total; c_exp = Synth::hp_current;
                                f_exp = Synth::hp_flat; p_exp = Synth::hp_pct; break;
            case poe2::kMana: t_exp = Synth::mp_total; c_exp = Synth::mp_current;
                              f_exp = Synth::mp_flat; p_exp = Synth::mp_pct; break;
            case poe2::kEnergyShield: t_exp = Synth::es_total; c_exp = Synth::es_current;
                                      f_exp = Synth::es_flat; p_exp = Synth::es_pct; break;
            case poe2::kWard: t_exp = Synth::ward_total; c_exp = Synth::ward_current;
                              f_exp = Synth::ward_flat; p_exp = Synth::ward_pct; break;
            default: t_exp = Synth::rage_total; c_exp = Synth::rage_current;
                     f_exp = Synth::rage_flat; p_exp = Synth::rage_pct; break;
        }
        CHECK_EQ(v.total, t_exp);
        CHECK_EQ(v.current, c_exp);
        CHECK_EQ(v.flat, f_exp);
        CHECK_EQ(v.pct, p_exp);
    }
}
