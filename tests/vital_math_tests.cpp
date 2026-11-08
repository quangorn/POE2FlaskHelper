// Reserved/unreserved percent formula (SPEC §4.7, poe_scan.py) + the threshold
// comparison Stage 4 will consume: flat+percent+ceil, unreserved <= 0, rounding
// boundaries, "exactly at threshold" vs "one below". Pure core code (vital_math.cpp).
#include "test.h"

#include "vital_math.h"

using poe2::reserved_amount;
using poe2::unreserved_amount;
using poe2::below_threshold;
using poe2::percent_round;

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

TEST(percent_round_half_even) {
    // Review S3-5 (fixed 2026-11-08): the debug percent matches python round() - nearest integer,
    // ties to EVEN. Integer-exact, no float.
    // Exact .5 ties: the even neighbour wins (half-up would print one higher).
    CHECK_EQ(percent_round(1, 40), 2);      // 100*1/40 = 2.5  -> 2 (even), half-up: 3
    CHECK_EQ(percent_round(7, 8), 88);      // 700/8 = 87.5    -> 88 (even), half-up: 88
    CHECK_EQ(percent_round(3, 8), 38);      // 300/8 = 37.5    -> 38 (even), half-up: 38
    CHECK_EQ(percent_round(1, 200), 0);     // 100/200 = 0.5   -> 0 (even), half-up: 1
    CHECK_EQ(percent_round(3, 400), 1);     // 300/400 = 0.75  -> nearest: 1
    CHECK_EQ(percent_round(1, 8), 12);      // 100/8 = 12.5    -> 12 (even), half-up: 13
    // Non-ties round to the nearest like python round() (half-up agrees there).
    CHECK_EQ(percent_round(1500, 3888), 39);   // 150000/3888 = 38.6 -> 39
    CHECK_EQ(percent_round(1, 3), 33);         // 33.33 -> 33
    CHECK_EQ(percent_round(2, 3), 67);         // 66.67 -> 67
    // Exact division is not a tie.
    CHECK_EQ(percent_round(50, 100), 50);
    CHECK_EQ(percent_round(4000, 3888), 103);  // 102.88 -> 103
    // unreserved <= 0 -> 0, as the values line prints.
    CHECK_EQ(percent_round(10, 0), 0);
    CHECK_EQ(percent_round(10, -5), 0);
    // Largest u32 values: 100 * current fits in int64, no overflow.
    CHECK_EQ(percent_round(4294967295LL, 4294967295LL), 100);
    CHECK_EQ(percent_round(1, 4294967295LL), 0);
}

TEST(reserved_negative_flat_is_clamped_defensive) {
    // Review S4-1 (2026-11-08, no-action): a live tick reads every Vital' field as
    // u32, so flat/pct/total are never negative. The clamp below is defensive; python
    // int(x + 0.999999) truncates toward zero on the same input (-99 for flat -100),
    // which the integer-exact ceil cannot reproduce without float. Pinned so the choice
    // stays explicit: reserved >= 0, unreserved stays <= total.
    CHECK_EQ(reserved_amount(5000, -100, 0), 0);
    CHECK_EQ(unreserved_amount(5000, -100, 0), 5000);
    CHECK_EQ(reserved_amount(5000, -100, 2023), 1012);   // the pct ceil stands, flat drops
    CHECK_EQ(reserved_amount(5000, 100, -2023), 100);    // pct clamped, flat stands
    CHECK_EQ(reserved_amount(-5000, 100, 2023), 0);      // total <= 0 -> 0
    // The reachable u32 side of the same boundary is unchanged.
    CHECK_EQ(reserved_amount(5000, 0, 0), 0);
}
