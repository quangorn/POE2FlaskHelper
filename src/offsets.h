#pragma once
// offsets.json: the full rev-layout, keys and semantics compatible with
// `python scanner/poe_scan.py --offs-file` (SPEC §6). All keys are mandatory;
// missing / non-numeric / invalid -> ok=false with an error string, and the app
// logs + MessageBox + exits with code 2. Extra keys are ignored.

#include <cstdint>
#include <string>

namespace poe2 {

struct Offsets {
    int64_t CurrentStatePtr = 0;
    int64_t States = 0;
    int64_t TOTAL = 0;
    int64_t AreaInstanceData = 0;
    int64_t LocalPlayers = 0;
    int64_t EntityId = 0;
    int64_t EntityIsValid = 0;
    int64_t EntityDetailsName = 0;
    int64_t EntityDetailsLookup = 0;
    int64_t CLU_Bucket = 0;
    int64_t TotalOff = 0;
    int64_t CurrentOff = 0;
    int64_t ReservedOff = 0;
    int64_t ReservedFlat = 0;
    int64_t HealthOff = 0;
    int64_t ManaOff = 0;
    int64_t ESOff = 0;
    int64_t WardOff = 0;
    int64_t RageOff = 0;
    int64_t BackPtrOff = 0;
    int64_t Score = 0;

    // Derived semantics (SPEC §6): Vital base = Off - TotalOff (position of Total
    // inside Life -> base of the Vital struct).
    int64_t health_base() const { return HealthOff - TotalOff; }
    int64_t mana_base() const { return ManaOff - TotalOff; }
    int64_t es_base() const { return ESOff - TotalOff; }
    int64_t ward_base() const { return WardOff - TotalOff; }
    int64_t rage_base() const { return RageOff - TotalOff; }

    // ReservedOff is ReservedPercent (per 10000), ReservedFlat is the flat reserve.
    int64_t reserved_percent() const { return ReservedOff; }
    int64_t reserved_flat() const { return ReservedFlat; }
    // Current offset inside the Vital struct.
    int64_t current_off() const { return CurrentOff; }
};

struct OffsetsResult {
    bool ok = false;
    Offsets offsets;
    std::string error;  // aggregated problems; empty when ok
};

// text: raw offsets.json content. The parse is pure - exit-2 policy lives in main.cpp.
OffsetsResult offsets_parse(const std::string& text);

}  // namespace poe2
