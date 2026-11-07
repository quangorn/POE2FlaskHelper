#pragma once
// Vital reserved/unreserved math (SPEC §4.7, poe_scan.py formula). Pure core code;
// Stage 4 `evaluate()` will consume it, the scanner tick already computes it.

#include <cstdint>

namespace poe2 {

// reserved = ceil(total * pct / 10000 + flat); pct is per 10000 (ReservedOff key).
// total == 0 -> 0 reserved (poe_scan.py "if total else 0").
// Integer-exact ceil: matches the python int(x + 0.999999) for every u32 input,
// without floating point (total * pct fits in int64: 4.29e9 * 1e4 < 1e14).
int64_t reserved_amount(int64_t total, int64_t flat, int64_t pct);

// unreserved = total - reserved; can be <= 0 (SPEC §4.7: such a vital is skipped).
int64_t unreserved_amount(int64_t total, int64_t flat, int64_t pct);

// SPEC §4.7 comparison without rounding: current * 100 < threshold * unreserved.
// Exactly at the threshold is NOT below; one unit below is. unreserved <= 0 -> false.
bool below_threshold(int64_t current, int threshold_pct, int64_t unreserved);

}  // namespace poe2
