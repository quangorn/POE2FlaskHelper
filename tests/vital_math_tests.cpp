// Reserved/unreserved percent formula (SPEC §4.7, poe_scan.py) + the threshold
// comparison Stage 4 will consume: flat+percent+ceil, unreserved <= 0, rounding
// boundaries, "exactly at threshold" vs "one below". Pure core code (vital_math.cpp).
#include "test.h"

#include "vital_math.h"

using poe2::reserved_amount;
using poe2::unreserved_amount;
using poe2::below_threshold;

TEST(reserved_flat_plus_percent_ceil) {
    // Health: total 5000, flat 100, pct 2023 => ceil(5000*2023/10000) + 100
    // = ceil(1011.5) + 100 = 1112 (poe_scan.py prints 1112 for these inputs).
    CHECK_EQ(reserved_amount(5000, 100, 2023), 1112);
    CHECK_EQ(unreserved_amount(5000, 100, 2023), 5000 - 1112);

    // Ward: total 900, flat 10, pct 1000 => ceil(90) + 10 = 100
    CHECK_EQ(reserved_amount(900, 10, 1000), 100);
    // Mana: total 3000, flat 0, pct 5000 => 1500
    CHECK_EQ(reserved_amount(3000, 0, 5000), 1500);
    // ES: no reserve at all
    CHECK_EQ(reserved_amount(1200, 0, 0), 0);
    CHECK_EQ(unreserved_amount(1200, 0, 0), 1200);
}

TEST(reserved_total_zero_is_zero) {
    // poe_scan.py: "if total else 0" - flat is not applied when total == 0.
    CHECK_EQ(reserved_amount(0, 100, 5000), 0);
    CHECK_EQ(unreserved_amount(0, 100, 5000), 0);
}

TEST(reserved_rounding_boundaries) {
    // Exact division (residue 0) must NOT round up.
    CHECK_EQ(reserved_amount(10000, 0, 5000), 5000);   // 5000.0
    CHECK_EQ(reserved_amount(10000, 0, 1), 1);         // 1.0
    CHECK_EQ(reserved_amount(1, 0, 10000), 1);         // 1.0
    // Smallest positive residue rounds up by one.
    CHECK_EQ(reserved_amount(10000, 0, 5001), 5001);   // 5001.0 exact again
    CHECK_EQ(reserved_amount(10001, 0, 5000), 5001);   // 5000.5 -> 5001
    CHECK_EQ(reserved_amount(9999, 0, 1), 1);          // 0.9999 -> 1
    CHECK_EQ(reserved_amount(10000, 0, 1), 1);         // 1.0
    CHECK_EQ(reserved_amount(19999, 0, 1), 2);         // 1.9999 -> 2
    // Largest u32 total: no overflow in int64 math (2^32-1 * 10000 < 1e14).
    CHECK_EQ(reserved_amount(4294967295LL, 0, 10000), 4294967295LL);
    CHECK_EQ(reserved_amount(4294967295LL, 10000, 10000), 4294967295LL + 10000);
}

TEST(unreserved_can_be_zero_or_negative) {
    // Full reserve: unreserved == 0; flat over the total: negative.
    CHECK_EQ(unreserved_amount(100, 0, 10000), 0);       // 100% reserved, no flat
    CHECK_EQ(unreserved_amount(100, 10000, 10000), -10000);
    CHECK_EQ(unreserved_amount(100, 200, 0), -100);
    CHECK(!below_threshold(50, 50, 0));      // unreserved <= 0 -> skip (SPEC §4.7)
    CHECK(!below_threshold(0, 100, -100));
}

TEST(threshold_comparison_no_rounding) {
    // current * 100 < threshold * unreserved, exact integer comparison.
    const int64_t unres = 200;
    CHECK(!below_threshold(100, 50, unres));   // 10000 == 10000: exactly at the threshold
    CHECK(below_threshold(99, 50, unres));     // 9900 < 10000: one unit below
    CHECK(!below_threshold(101, 50, unres));   // above
    CHECK(below_threshold(0, 1, unres));       // 0 < 200
    CHECK(!below_threshold(200, 100, unres));  // 20000 == 20000
    CHECK(below_threshold(199, 100, unres));
    // Fractional-current boundary: current=7, threshold=35, unreserved=20 -> 700 < 700?
    CHECK(!below_threshold(7, 35, 20));
    CHECK(below_threshold(6, 35, 20));
}
