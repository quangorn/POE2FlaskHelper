// offsets.json validation tests (SPEC §6, plan.md §3 Stage 1) + compatibility with
// the real repo offsets.json used by `python scanner/poe_scan.py --offs-file`.
#include "test.h"

#include <string>

#include "app.h"
#include "offsets.h"

#ifndef P2_REPO_OFFSETS
#error "CMake must define P2_REPO_OFFSETS (path to the repo offsets.json)"
#endif

// Full SPEC §6 layout, rev34 values.
static const char* kFullOffsets = R"({
  "CurrentStatePtr": 16, "States": 80, "TOTAL": 13, "AreaInstanceData": 656,
  "LocalPlayers": 1472, "EntityId": 136, "EntityIsValid": 140,
  "EntityDetailsName": 8, "EntityDetailsLookup": 40, "CLU_Bucket": 40,
  "TotalOff": 52, "CurrentOff": 56, "ReservedOff": 28, "ReservedFlat": 24,
  "HealthOff": 476, "ManaOff": 564, "ESOff": 628, "WardOff": 788, "RageOff": 716,
  "BackPtrOff": 16, "Score": 9
})";

TEST(offsets_full_set_ok) {
    auto r = poe2::offsets_parse(kFullOffsets);
    REQUIRE(r.ok);
    const poe2::Offsets& o = r.offsets;
    CHECK_EQ(o.CurrentStatePtr, 16);
    CHECK_EQ(o.States, 80);
    CHECK_EQ(o.TOTAL, 13);
    CHECK_EQ(o.AreaInstanceData, 656);
    CHECK_EQ(o.LocalPlayers, 1472);
    CHECK_EQ(o.EntityId, 136);
    CHECK_EQ(o.EntityIsValid, 140);
    CHECK_EQ(o.EntityDetailsName, 8);
    CHECK_EQ(o.EntityDetailsLookup, 40);
    CHECK_EQ(o.CLU_Bucket, 40);
    CHECK_EQ(o.TotalOff, 52);
    CHECK_EQ(o.CurrentOff, 56);
    CHECK_EQ(o.ReservedOff, 28);
    CHECK_EQ(o.ReservedFlat, 24);
    CHECK_EQ(o.HealthOff, 476);
    CHECK_EQ(o.ManaOff, 564);
    CHECK_EQ(o.ESOff, 628);
    CHECK_EQ(o.WardOff, 788);
    CHECK_EQ(o.RageOff, 716);
    CHECK_EQ(o.BackPtrOff, 16);
    CHECK_EQ(o.Score, 9);
}

TEST(offsets_derived_bases) {
    // SPEC §6: vital base = Off - TotalOff; ReservedOff is ReservedPercent.
    auto r = poe2::offsets_parse(kFullOffsets);
    REQUIRE(r.ok);
    CHECK_EQ(r.offsets.health_base(), 424);  // 476 - 52 = 0x1A8
    CHECK_EQ(r.offsets.mana_base(), 512);    // 564 - 52 = 0x200
    CHECK_EQ(r.offsets.es_base(), 576);      // 628 - 52 = 0x240
    CHECK_EQ(r.offsets.ward_base(), 736);    // 788 - 52 = 0x2E0
    CHECK_EQ(r.offsets.rage_base(), 664);    // 716 - 52 = 0x298
    CHECK_EQ(r.offsets.reserved_percent(), 28);
    CHECK_EQ(r.offsets.reserved_flat(), 24);
    CHECK_EQ(r.offsets.current_off(), 56);
}

TEST(offsets_missing_key_is_error) {
    auto r = poe2::offsets_parse(R"({"CurrentStatePtr":16,"States":80,"TOTAL":13,
        "AreaInstanceData":656,"LocalPlayers":1472,"EntityId":136,"EntityIsValid":140,
        "EntityDetailsName":8,"EntityDetailsLookup":40,"CLU_Bucket":40,
        "CurrentOff":56,"ReservedOff":28,"ReservedFlat":24,
        "HealthOff":476,"ManaOff":564,"ESOff":628,"WardOff":788,"RageOff":716,
        "BackPtrOff":16,"Score":9})");  // TotalOff absent
    CHECK(!r.ok);
    CHECK(r.error.find("TotalOff") != std::string::npos);

    auto one = poe2::offsets_parse(R"({"CurrentStatePtr":16})");
    CHECK(!one.ok);
    CHECK(r.error.find("missing key") != std::string::npos);
}

TEST(offsets_non_numeric_is_error) {
    auto str = poe2::offsets_parse(R"({"HealthOff":"476","States":80,"TOTAL":13,
        "AreaInstanceData":656,"LocalPlayers":1472,"EntityId":136,"EntityIsValid":140,
        "EntityDetailsName":8,"EntityDetailsLookup":40,"CLU_Bucket":40,
        "TotalOff":52,"CurrentOff":56,"ReservedOff":28,"ReservedFlat":24,
        "ManaOff":564,"ESOff":628,"WardOff":788,"RageOff":716,
        "BackPtrOff":16,"Score":9})");
    CHECK(!str.ok);
    CHECK(str.error.find("HealthOff") != std::string::npos);
    CHECK(str.error.find("integer") != std::string::npos);

    auto nul = poe2::offsets_parse(R"({"TotalOff":null, "States":80,"TOTAL":13,
        "AreaInstanceData":656,"LocalPlayers":1472,"EntityId":136,"EntityIsValid":140,
        "EntityDetailsName":8,"EntityDetailsLookup":40,"CLU_Bucket":40,
        "CurrentOff":56,"ReservedOff":28,"ReservedFlat":24,
        "HealthOff":476,"ManaOff":564,"ESOff":628,"WardOff":788,"RageOff":716,
        "BackPtrOff":16,"Score":9, "CurrentStatePtr":16})");
    CHECK(!nul.ok);
}

TEST(offsets_out_of_range_is_error) {
    // Vital base must be positive: Off must be > TotalOff (SPEC §6).
    auto eq = poe2::offsets_parse(R"({"CurrentStatePtr":16,"States":80,"TOTAL":13,
        "AreaInstanceData":656,"LocalPlayers":1472,"EntityId":136,"EntityIsValid":140,
        "EntityDetailsName":8,"EntityDetailsLookup":40,"CLU_Bucket":40,
        "TotalOff":52,"CurrentOff":56,"ReservedOff":28,"ReservedFlat":24,
        "HealthOff":52,"ManaOff":564,"ESOff":628,"WardOff":788,"RageOff":716,
        "BackPtrOff":16,"Score":9})");
    CHECK(!eq.ok);
    CHECK(eq.error.find("HealthOff") != std::string::npos);

    auto neg = poe2::offsets_parse(R"({"CurrentStatePtr":-16,"States":80,"TOTAL":13,
        "AreaInstanceData":656,"LocalPlayers":1472,"EntityId":136,"EntityIsValid":140,
        "EntityDetailsName":8,"EntityDetailsLookup":40,"CLU_Bucket":40,
        "TotalOff":52,"CurrentOff":56,"ReservedOff":28,"ReservedFlat":24,
        "HealthOff":476,"ManaOff":564,"ESOff":628,"WardOff":788,"RageOff":716,
        "BackPtrOff":16,"Score":9})");
    CHECK(!neg.ok);

    auto z = poe2::offsets_parse(R"({"TOTAL":0, "CurrentStatePtr":16,"States":80,
        "AreaInstanceData":656,"LocalPlayers":1472,"EntityId":136,"EntityIsValid":140,
        "EntityDetailsName":8,"EntityDetailsLookup":40,"CLU_Bucket":40,
        "TotalOff":52,"CurrentOff":56,"ReservedOff":28,"ReservedFlat":24,
        "HealthOff":476,"ManaOff":564,"ESOff":628,"WardOff":788,"RageOff":716,
        "BackPtrOff":16,"Score":9})");
    CHECK(!z.ok);
}

TEST(offsets_extra_keys_ignored) {
    auto r = poe2::offsets_parse(R"({"CurrentStatePtr":16,"States":80,"TOTAL":13,
        "AreaInstanceData":656,"LocalPlayers":1472,"EntityId":136,"EntityIsValid":140,
        "EntityDetailsName":8,"EntityDetailsLookup":40,"CLU_Bucket":40,
        "TotalOff":52,"CurrentOff":56,"ReservedOff":28,"ReservedFlat":24,
        "HealthOff":476,"ManaOff":564,"ESOff":628,"WardOff":788,"RageOff":716,
        "BackPtrOff":16,"Score":9,"FooBar":123,"Ward":1})");  // extra keys
    REQUIRE(r.ok);
    CHECK_EQ(r.offsets.health_base(), 424);
    CHECK_EQ(r.offsets.LocalPlayers, 1472);
}

TEST(offsets_broken_json_is_error) {
    auto r = poe2::offsets_parse("{");
    CHECK(!r.ok);
    CHECK(!r.error.empty());
    auto e = poe2::offsets_parse("");
    CHECK(!e.ok);
    auto nonobj = poe2::offsets_parse("16");
    CHECK(!nonobj.ok);
}

TEST(offsets_real_file_compatible_with_scanner) {
    // The repo offsets.json must load through the app parser and carry rev34 values,
    // identical to what `poe_scan.py --offs-file` reads (key names/types).
    std::string text;
    REQUIRE(poe2::read_text_file(P2_REPO_OFFSETS, text));
    auto r = poe2::offsets_parse(text);
    REQUIRE(r.ok);
    CHECK_EQ(r.offsets.LocalPlayers, 1472);   // rev34: 0x5C0 (moved +8 from rev33)
    CHECK_EQ(r.offsets.health_base(), 424);
    CHECK_EQ(r.offsets.mana_base(), 512);
    CHECK_EQ(r.offsets.es_base(), 576);
    CHECK_EQ(r.offsets.ward_base(), 736);
    CHECK_EQ(r.offsets.rage_base(), 664);
    CHECK_EQ(r.offsets.reserved_percent(), 28);
    CHECK_EQ(r.offsets.reserved_flat(), 24);
    CHECK_EQ(r.offsets.current_off(), 56);
    CHECK_EQ(r.offsets.TotalOff, 52);
    CHECK_EQ(r.offsets.TOTAL, 13);
}
