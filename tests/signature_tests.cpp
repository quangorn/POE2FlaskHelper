// scan_signature tests (plan.md stage 3): exactly one match, correct global_addr,
// the ?? mask, a match spanning a chunk boundary, multiple matches, empty module.
#include "test.h"

#include <cstdint>
#include <vector>

#include "signature.h"
#include "fakes/FakeMemory.h"

using poe2::testing::FakeMemory;

namespace {

constexpr uint64_t kBase = 0x100000;

// Places the pattern at addr; disp points to `global` (global = anchor + disp + 4).
void place(FakeMemory& m, uint64_t addr, int32_t disp, uint8_t wild = 0x28) {
    uint8_t pat[16] = {0x48, 0x83, 0xEC, wild, 0x48, 0x8B, 0xF1, 0x33,
                       0xED, 0x48, 0x39, 0x2D};
    for (int i = 0; i < 4; ++i) pat[12 + i] = static_cast<uint8_t>((disp >> (8 * i)) & 0xFF);
    m.put_bytes(addr, pat, 16);
}

}  // namespace

TEST(signature_one_match) {
    FakeMemory m;
    m.set_image(kBase, 0x10000);
    m.add_region(kBase, 0x10000, 0);
    place(m, kBase + 0x200, 0x100);
    const auto hits = poe2::scan_signature(m, kBase, 0x10000);
    REQUIRE(hits.size() == 1);
    CHECK_EQ(hits[0].anchor_live, kBase + 0x200 + 12);       // anchor = pattern start + 12
    CHECK_EQ(hits[0].disp, 0x100);
    CHECK_EQ(hits[0].global_addr, kBase + 0x200 + 12 + 0x100 + 4);  // anchor + disp + 4
}

TEST(signature_negative_displacement) {
    FakeMemory m;
    m.set_image(kBase, 0x10000);
    m.add_region(kBase, 0x10000, 0);
    place(m, kBase + 0x500, -0x300);   // RIP-relative target below the anchor
    const auto hits = poe2::scan_signature(m, kBase, 0x10000);
    REQUIRE(hits.size() == 1);
    CHECK_EQ(hits[0].disp, -0x300);
    CHECK_EQ(hits[0].global_addr, kBase + 0x500 + 12 - 0x300 + 4);
}

TEST(signature_wildcard_mask) {
    // The "??; byte at pattern index 3 accepts any value: all four variants give
    // exactly one match with the same global_addr.
    const uint8_t wilds[] = {0x00, 0xFF, 0x28, 0x7F};
    for (uint8_t w : wilds) {
        FakeMemory m;
        m.set_image(kBase, 0x4000);
        m.add_region(kBase, 0x4000, 0);
        place(m, kBase + 0x100, 0x40, w);
        const auto hits = poe2::scan_signature(m, kBase, 0x4000);
        CHECK_EQ(hits.size(), 1u);
        if (hits.size() == 1) CHECK_EQ(hits[0].global_addr, kBase + 0x100 + 12 + 0x40 + 4);
    }
    // A byte that is not in the pattern set never matches.
    FakeMemory m;
    m.set_image(kBase, 0x4000);
    m.add_region(kBase, 0x4000, 0);
    m.put_u8(kBase + 0x100, 0x48);
    m.put_u8(kBase + 0x101, 0x84);   // 0x83 -> 0x84: fixed byte mismatch
    const auto hits = poe2::scan_signature(m, kBase, 0x4000);
    CHECK_EQ(hits.size(), 0u);
}

TEST(signature_chunk_boundary_match) {
    // The pattern straddles the chunk end (pattern start 8 in a 16-byte chunk): the
    // overlapping-chunk scan must find it exactly once.
    FakeMemory m;
    m.set_image(kBase, 32);
    m.add_region(kBase, 32, 0);
    place(m, kBase + 8, 0x10);
    const auto hits = poe2::scan_signature(m, kBase, 32, 16);
    REQUIRE(hits.size() == 1);
    CHECK_EQ(hits[0].anchor_live, kBase + 8 + 12);
    CHECK_EQ(hits[0].global_addr, kBase + 8 + 12 + 0x10 + 4);
}

TEST(signature_multiple_matches) {
    FakeMemory m;
    m.set_image(kBase, 0x1000);
    m.add_region(kBase, 0x1000, 0);
    place(m, kBase + 0x10, 0x10);
    place(m, kBase + 0x200, 0x200);
    const auto hits = poe2::scan_signature(m, kBase, 0x1000);
    CHECK_EQ(hits.size(), 2u);   // the chain treats >1 hit as a failure
    if (hits.size() == 2) {
        CHECK_EQ(hits[0].anchor_live, kBase + 0x10 + 12);
        CHECK_EQ(hits[1].anchor_live, kBase + 0x200 + 12);
    }
}

TEST(signature_empty_module) {
    FakeMemory m;
    m.set_image(kBase, 0);
    const auto hits = poe2::scan_signature(m, kBase, 0);
    CHECK_EQ(hits.size(), 0u);
    // A module with no readable bytes at all (regions registered outside the image):
    FakeMemory m2;
    m2.set_image(kBase, 0x1000);
    CHECK_EQ(poe2::scan_signature(m2, kBase, 0x1000).size(), 0u);
}

TEST(signature_short_chunk_read) {
    // Region edge inside a chunk: the short read is scanned as far as it goes and
    // the scan terminates (no infinite loop), finding the match in the readable part.
    FakeMemory m;
    m.set_image(kBase, 0x1000);
    m.add_region(kBase, 20, 0);   // only 20 bytes readable
    place(m, kBase + 2, 0x10);
    const auto hits = poe2::scan_signature(m, kBase, 0x1000, 1024);
    CHECK_EQ(hits.size(), 1u);
    CHECK(m.reads() > 0);   // the scan really issued reads
}

TEST(signature_chunk_smaller_than_pattern) {
    FakeMemory m;
    m.set_image(kBase, 0x100);
    m.add_region(kBase, 0x100, 0);
    place(m, kBase + 8, 0x10);
    // chunk < pattern length: nothing can match, the scan must not loop forever.
    CHECK_EQ(poe2::scan_signature(m, kBase, 0x100, 8).size(), 0u);
}
