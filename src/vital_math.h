#pragma once
// Vital reserved/unreserved math (SPEC §4.7, poe_scan.py formula). Pure core code;
// Stage 4 `evaluate()` will consume it, the scanner tick already computes it.

#include <cstdint>

namespace poe2 {

// reserved = ceil(total * pct / 10000 + flat); pct is per 10000 (ReservedOff key).
// total == 0 -> 0 reserved (poe_scan.py "if total else 0").
// Integer-exact ceil: matches the python int(x + 0.999999) for every u32 input,
// without floating point (total * pct fits in int64: 4.29e9 * 1e4 < 1e14).
// Review S4-1 (2026-11-08, no-action): the int64 signature also accepts negative
// total/pct/flat, which poe_scan.py (u32 reads, int() truncation toward zero) has no
// equivalent for; the C++ clamps them to 0 defensively. A live tick never produces a
// negative field (chain.cpp reads u32), so the divergence is unreachable - pinned by
// reserved_negative_flat_is_clamped_defensive.
int64_t reserved_amount(int64_t total, int64_t flat, int64_t pct);

// unreserved = total - reserved; can be <= 0 (SPEC §4.7: such a vital is skipped).
int64_t unreserved_amount(int64_t total, int64_t flat, int64_t pct);

// SPEC §4.7 comparison without rounding: current * 100 < threshold * unreserved.
// Exactly at the threshold is NOT below; one unit below is. unreserved <= 0 -> false.
bool below_threshold(int64_t current, int threshold_pct, int64_t unreserved);

// Debug display percent of the unreserved pool: 100 * current / unreserved, rounded to
// the nearest integer with TIES TO EVEN (python round(), poe_scan.py prints the live
// line that way). Integer-exact half-even: q = (100*current) / unres, r = (100*current)
// % unres; 2*r == unres is the exact .5 tie, and there the answer is the even one of
// q / q+1. Non-ties round like python round() (nearest). unreserved <= 0 -> 0.
// No floating point anywhere (nearbyint on a double would reintroduce float rounding).
int percent_round(int64_t current, int64_t unreserved);

}  // namespace poe2
