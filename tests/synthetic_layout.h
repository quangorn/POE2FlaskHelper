#pragma once
// Synthetic in-memory image for the scanner tests (plan.md §2, spirit of
// find_offsets.build_fake): signature at an RVA inside the module image, GameStates*,
// GameState with States[13], AreaInstance, LocalPlayers, Entity, ComponentLookUp with
// "Life", the Life component and all five Vital' values. No POE, no binary file:
// everything lives in FakeMemory regions (tests/fakes/FakeMemory.*).
//
// The layout is built from the rev34 offsets.json set (SPEC §6), so the tests run on
// exactly the offsets the shipping exe uses. Mutation helpers for the failure-branch
// tests are plain FakeMemory::put_* writes to the addresses below.

#include <cstdint>

#include "fakes/FakeMemory.h"
#include "offsets.h"

namespace poe2 {
namespace testing {

struct Synth {
    // module image (the PEB base/size the factory reports)
    static constexpr uint64_t kBase = 0x140000000;
    static constexpr uint32_t kImageSize = 0x800000;
    static constexpr uint64_t kSigRva = 0x1000;           // signature placement
    static constexpr uint64_t kGlobal = 0x140450000;      // GameStates* static, inside the image
    // heap chain
    static constexpr uint64_t kGsObj = 0x20000000;        // GameState object
    static constexpr uint64_t kStateArr = 0x20010000;     // CurrentStatePtr vector array
    static constexpr uint64_t kIngame = 0x20020000;       // States[4] = InGameState
    static constexpr uint64_t kArea = 0x20100000;         // AreaInstance
    static constexpr uint64_t kPlayersArr = 0x20200000;   // LocalPlayers array
    static constexpr uint64_t kEntity = 0x20400000;       // player Entity (ItemStruct)
    static constexpr uint64_t kCompArr = 0x20500000;      // ComponentList array (4 pointers)
    static constexpr uint64_t kDetails = 0x20600000;      // EntityDetails
    static constexpr uint64_t kLookup = 0x20700000;       // ComponentLookUp object
    static constexpr uint64_t kBucketArr = 0x20800000;    // CLU bucket array (2 records)
    static constexpr uint64_t kStrLife = 0x20900000;
    static constexpr uint64_t kStrPos = 0x20900100;
    static constexpr uint64_t kLife = 0x20A00000;         // Life component
    static constexpr uint64_t kCompPos = 0x20B00000;      // dummy components (not read)
    static constexpr uint64_t kCompOther = 0x20C00000;

    // Vital values in the synthetic Life (Health, Mana, ES, Ward, Rage order)
    static constexpr uint32_t hp_total = 5000, hp_current = 4000, hp_flat = 100, hp_pct = 2023;
    static constexpr uint32_t mp_total = 3000, mp_current = 2000, mp_flat = 0, mp_pct = 5000;
    static constexpr uint32_t es_total = 1200, es_current = 800, es_flat = 0, es_pct = 0;
    static constexpr uint32_t ward_total = 900, ward_current = 300, ward_flat = 10, ward_pct = 1000;
    static constexpr uint32_t rage_total = 700, rage_current = 640, rage_flat = 0, rage_pct = 0;
};

// Builds the whole layout in mem (registers regions, writes pointers and values) and
// sets image_base/image_size. `o` must be the rev34 offsets.json set.
void build_synthetic(FakeMemory& mem, const Offsets& o);

// The rev34 offsets.json set (SPEC §6) as an Offsets struct: the synthetic layout is
// built on exactly the keys the shipping exe loads.
const Offsets& rev34();

// The signature bytes with the displacement pointing at `global`: 16 bytes,
// 48 83 EC 28 48 8B F1 33 ED 48 39 2D disp(4). The "??"; byte is filled with 0x28.
void place_signature(FakeMemory& mem, uint64_t addr, uint64_t global);

// TU anchor for the synthetic layout module.
void synthetic_layout_anchor();

}  // namespace testing
}  // namespace poe2
