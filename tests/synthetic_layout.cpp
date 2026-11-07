// Synthetic layout TU (plan.md §2). See tests/synthetic_layout.h for the addresses.
#include "synthetic_layout.h"

#include <cstring>

namespace poe2 {
namespace testing {

void place_signature(FakeMemory& mem, uint64_t addr, uint64_t global) {
    // "48 83 EC ?? 48 8B F1 33 ED 48 39 2D ^ ?? ?? ?? ??" - the ??; byte is 0x28,
    // the displacement is a 4-byte signed value: global = addr + 12 + disp + 4.
    const uint8_t pat[16] = {0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0xF1, 0x33,
                             0xED, 0x48, 0x39, 0x2D};
    const int32_t disp = static_cast<int32_t>(global - addr - 12 - 4);
    uint8_t bytes[16];
    std::memcpy(bytes, pat, 12);
    for (int i = 0; i < 4; ++i) {
        bytes[12 + i] = static_cast<uint8_t>(disp >> (8 * i));
    }
    mem.put_bytes(addr, bytes, 16);
}

namespace {

void set64(FakeMemory& m, uint64_t addr, uint64_t v) { m.put_u64(addr, v); }
void set32(FakeMemory& m, uint64_t addr, uint32_t v) { m.put_u32(addr, v); }

// One Vital' struct inside the Life component: ReservedFlat / ReservedPercent /
// Total / Current. `base` is the Vital base (Off - TotalOff, SPEC §6) relative to
// the `life` component pointer, so every write is an absolute address.
void write_vital(FakeMemory& m, const Offsets& o, uint64_t life, int64_t base,
                 uint32_t total, uint32_t current, uint32_t flat, uint32_t pct) {
    m.put_u32(life + static_cast<uint64_t>(base + o.ReservedFlat), flat);
    m.put_u32(life + static_cast<uint64_t>(base + o.ReservedOff), pct);
    m.put_u32(life + static_cast<uint64_t>(base + o.TotalOff), total);
    m.put_u32(life + static_cast<uint64_t>(base + o.CurrentOff), current);
}

}  // namespace

void build_synthetic(FakeMemory& mem, const Offsets& o) {
    const uint64_t base = Synth::kBase;
    mem.set_image(base, Synth::kImageSize);
    mem.add_region(base, Synth::kImageSize, 0);
    place_signature(mem, base + Synth::kSigRva, Synth::kGlobal);

    // GameStates* static inside the image
    mem.add_region(Synth::kGlobal, 8, 0);
    set64(mem, Synth::kGlobal, Synth::kGsObj);

    // GameState object: CurrentStatePtr vector + States[TOTAL] of {ptr,size}
    mem.add_region(Synth::kGsObj, static_cast<size_t>(o.States) + 13 * 16 + 16, 0);
    set64(mem, Synth::kGsObj + static_cast<uint64_t>(o.CurrentStatePtr), Synth::kStateArr);
    set64(mem, Synth::kGsObj + static_cast<uint64_t>(o.CurrentStatePtr) + 8,
          Synth::kStateArr + 16);
    set64(mem, Synth::kGsObj + static_cast<uint64_t>(o.CurrentStatePtr) + 16,
          Synth::kStateArr + 16);
    set64(mem, Synth::kGsObj + static_cast<uint64_t>(o.States) + 4 * 16, Synth::kIngame);

    // CurrentStatePtr array: one 16-byte entry, current = *(Last - 0x10)
    mem.add_region(Synth::kStateArr, 16, 0);
    set64(mem, Synth::kStateArr, Synth::kIngame);

    // InGameState -> AreaInstance
    mem.add_region(Synth::kIngame, static_cast<size_t>(o.AreaInstanceData) + 8, 0);
    set64(mem, Synth::kIngame + static_cast<uint64_t>(o.AreaInstanceData), Synth::kArea);

    // AreaInstance: LocalPlayers std::vector {First,Last,End} -> [Entity]
    mem.add_region(Synth::kArea, static_cast<size_t>(o.LocalPlayers) + 24 + 8, 0);
    set64(mem, Synth::kArea + static_cast<uint64_t>(o.LocalPlayers), Synth::kPlayersArr);
    set64(mem, Synth::kArea + static_cast<uint64_t>(o.LocalPlayers) + 8,
          Synth::kPlayersArr + 8);
    set64(mem, Synth::kArea + static_cast<uint64_t>(o.LocalPlayers) + 16,
          Synth::kPlayersArr + 8);

    mem.add_region(Synth::kPlayersArr, 8, 0);
    set64(mem, Synth::kPlayersArr, Synth::kEntity);

    // Entity (ItemStruct): VTable@0, EntityDetailsPtr@8, ComponentList vector @+0x10
    mem.add_region(Synth::kEntity, static_cast<size_t>(o.EntityIsValid) + 8, 0);
    set64(mem, Synth::kEntity, base + 0x2000);            // vtable inside the module image
    set64(mem, Synth::kEntity + 8, Synth::kDetails);
    set64(mem, Synth::kEntity + 0x10, Synth::kCompArr);
    set64(mem, Synth::kEntity + 0x18, Synth::kCompArr + 32);
    set64(mem, Synth::kEntity + 0x20, Synth::kCompArr + 32);
    set32(mem, Synth::kEntity + static_cast<uint64_t>(o.EntityId), 123);
    mem.put_u8(Synth::kEntity + static_cast<uint64_t>(o.EntityIsValid), 0xC);

    // ComponentList array: Life is element 0
    mem.add_region(Synth::kCompArr, 32, 0);
    set64(mem, Synth::kCompArr, Synth::kLife);
    set64(mem, Synth::kCompArr + 8, Synth::kCompPos);
    set64(mem, Synth::kCompArr + 16, Synth::kCompOther);
    set64(mem, Synth::kCompArr + 24, 0);

    // EntityDetails: ComponentLookUpPtr at EntityDetailsLookup
    mem.add_region(Synth::kDetails, static_cast<size_t>(o.EntityDetailsLookup) + 8, 0);
    set64(mem, Synth::kDetails + static_cast<uint64_t>(o.EntityDetailsLookup), Synth::kLookup);

    // ComponentLookUp bucket: {First,Last,End,Unknown,Capacity=2}
    mem.add_region(Synth::kLookup, static_cast<size_t>(o.CLU_Bucket) + 36, 0);
    set64(mem, Synth::kLookup + static_cast<uint64_t>(o.CLU_Bucket), Synth::kBucketArr);
    set64(mem, Synth::kLookup + static_cast<uint64_t>(o.CLU_Bucket) + 8,
          Synth::kBucketArr + 32);
    set64(mem, Synth::kLookup + static_cast<uint64_t>(o.CLU_Bucket) + 16,
          Synth::kBucketArr + 32);
    set32(mem, Synth::kLookup + static_cast<uint64_t>(o.CLU_Bucket) + 32, 2);  // Capacity

    // Bucket records: "Life" index 0, "Position" index 1
    mem.add_region(Synth::kBucketArr, 32, 0);
    set64(mem, Synth::kBucketArr, Synth::kStrLife);
    set32(mem, Synth::kBucketArr + 8, 0);
    set64(mem, Synth::kBucketArr + 16, Synth::kStrPos);
    set32(mem, Synth::kBucketArr + 24, 1);

    mem.add_region(Synth::kStrLife, 5, 0);
    mem.put_bytes(Synth::kStrLife, reinterpret_cast<const uint8_t*>("Life"), 5);
    mem.add_region(Synth::kStrPos, 9, 0);
    mem.put_bytes(Synth::kStrPos, reinterpret_cast<const uint8_t*>("Position"), 9);

    // Life component: 5 Vital' structs at base = Off - TotalOff (SPEC §6)
    mem.add_region(Synth::kLife, static_cast<size_t>(o.WardOff) + 64, 0);
    write_vital(mem, o, Synth::kLife, o.health_base(), Synth::hp_total, Synth::hp_current,
                Synth::hp_flat, Synth::hp_pct);
    write_vital(mem, o, Synth::kLife, o.mana_base(), Synth::mp_total, Synth::mp_current,
                Synth::mp_flat, Synth::mp_pct);
    write_vital(mem, o, Synth::kLife, o.es_base(), Synth::es_total, Synth::es_current,
                Synth::es_flat, Synth::es_pct);
    write_vital(mem, o, Synth::kLife, o.ward_base(), Synth::ward_total, Synth::ward_current,
                Synth::ward_flat, Synth::ward_pct);
    write_vital(mem, o, Synth::kLife, o.rage_base(), Synth::rage_total, Synth::rage_current,
                Synth::rage_flat, Synth::rage_pct);
}

const Offsets& rev34() {
    // The literal is the repo offsets.json (rev34, SPEC §6): one source of numbers
    // for the layout builder and the tests.
    static const char* kRev34 = R"({
      "CurrentStatePtr": 16, "States": 80, "TOTAL": 13, "AreaInstanceData": 656,
      "LocalPlayers": 1472, "EntityId": 136, "EntityIsValid": 140,
      "EntityDetailsName": 8, "EntityDetailsLookup": 40, "CLU_Bucket": 40,
      "TotalOff": 52, "CurrentOff": 56, "ReservedOff": 28, "ReservedFlat": 24,
      "HealthOff": 476, "ManaOff": 564, "ESOff": 628, "WardOff": 788, "RageOff": 716,
      "BackPtrOff": 16, "Score": 9
    })";
    static const Offsets o = poe2::offsets_parse(kRev34).offsets;
    return o;
}

void synthetic_layout_anchor() {}

}  // namespace testing
}  // namespace poe2
