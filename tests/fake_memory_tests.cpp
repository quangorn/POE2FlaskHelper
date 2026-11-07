// FakeMemory contract tests (plan.md §2): regions by address, short reads at region
// edges, the read counter, failure injection on the N-th read. The fake is what every
// stage-3 test measures through, so its behaviour is pinned here.
#include "test.h"

#include <cstring>

#include "fakes/FakeMemory.h"

using poe2::testing::FakeMemory;

TEST(fake_memory_full_read) {
    FakeMemory m;
    m.add_region(0x1000, 16, 0);
    m.put_u64(0x1000, 0x1122334455667788ULL);
    uint64_t v = 0;
    CHECK_EQ(m.read(0x1000, &v, 8), 8u);
    CHECK_EQ(v, 0x1122334455667788ULL);
    CHECK_EQ(m.reads(), 1);
}

TEST(fake_memory_short_read_at_region_edge) {
    FakeMemory m;
    m.add_region(0x1000, 10, 0);
    uint8_t buf[16] = {};
    // Crossing the region end: only the bytes inside the region come back.
    CHECK_EQ(m.read(0x1006, buf, 16), 4u);
    CHECK_EQ(m.reads(), 1);
    CHECK_EQ(m.last_read_size(), 4);
    // Outside any region: the read fails with 0.
    CHECK_EQ(m.read(0x2000, buf, 8), 0u);
    // Starting inside, ending past the region: clipped.
    CHECK_EQ(m.read(0x1009, buf, 8), 1u);
}

TEST(fake_memory_failure_on_nth_read) {
    FakeMemory m;
    m.add_region(0x1000, 64, 0);
    m.fail_on_read(3);
    uint8_t buf[8];
    CHECK_EQ(m.read(0x1000, buf, 8), 8u);      // 1st ok
    CHECK_EQ(m.read(0x1008, buf, 8), 8u);      // 2nd ok
    CHECK_EQ(m.read(0x1010, buf, 8), 0u);      // 3rd injected failure
    CHECK_EQ(m.read(0x1018, buf, 8), 8u);      // 4th ok again
    CHECK_EQ(m.reads(), 4);
    m.fail_all_reads(true);
    CHECK_EQ(m.read(0x1000, buf, 8), 0u);
}

TEST(fake_memory_image_and_reset) {
    FakeMemory m;
    m.set_image(0x140000000, 0x800000);
    CHECK_EQ(m.image_base(), 0x140000000ULL);
    CHECK_EQ(m.image_size(), 0x800000u);
    m.add_region(0x140000000, 32, 0);
    uint8_t buf[8];
    m.read(0x140000000, buf, 8);
    CHECK_EQ(m.reads(), 1);
    m.reset_reads();
    CHECK_EQ(m.reads(), 0);
}

TEST(fake_memory_write_helpers) {
    FakeMemory m;
    m.add_region(0x2000, 32, 0xFF);
    m.put_u8(0x2000, 0xC);
    m.put_u32(0x2004, 0x12345678);
    m.put_u64(0x2008, 0x1122334455667788ULL);
    uint8_t buf[16] = {};
    REQUIRE(m.read(0x2000, buf, 16) == 16);
    CHECK_EQ(buf[0], 0xC);
    CHECK_EQ(buf[1], 0xFF);                                   // untouched filler
    CHECK_EQ(buf[4], 0x78);                                   // little-endian u32
    CHECK_EQ(buf[7], 0x12);
    CHECK_EQ(buf[8], 0x88);                                   // little-endian u64
    CHECK_EQ(buf[15], 0x11);
    // A write outside a region is a silent no-op (test bug guard).
    m.put_u64(0x9000, 1);
    CHECK_EQ(m.read(0x9000, buf, 8), 0u);
}
