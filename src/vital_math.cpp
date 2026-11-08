#include "vital_math.h"

namespace poe2 {

int64_t reserved_amount(int64_t total, int64_t flat, int64_t pct) {
    if (total <= 0) return 0;
    // MINOR_ISSUES S4-1 (parity note, entry kept as `leave`): poe_scan.py computes
    // int(pct / 10000 * total + flat + 0.999999), i.e. python's int() truncation toward
    // zero of a FLOAT. For the negative side that epsilon trick has no integer-exact
    // equivalent (the answer depends on double rounding), and every field is read as
    // u32 (chain.cpp block_u32, VitalVals is uint32_t), so total/pct/flat are never
    // negative in a live tick. The clamp below is therefore defensive only, never
    // reachable from memory: for the reachable u32 domain the ceil below is exactly
    // python's result (see reserved_rounding_boundaries).
    const int64_t t = total < 0 ? 0 : total;
    const int64_t p = pct < 0 ? 0 : pct;
    const int64_t f = flat < 0 ? 0 : flat;
    return (t * p + 9999) / 10000 + f;
}

int64_t unreserved_amount(int64_t total, int64_t flat, int64_t pct) {
    if (total <= 0) return 0;
    return total - reserved_amount(total, flat, pct);
}

bool below_threshold(int64_t current, int threshold_pct, int64_t unreserved) {
    if (unreserved <= 0) return false;   // SPEC §4.7: unreserved <= 0 -> skip
    return current * 100 < static_cast<int64_t>(threshold_pct) * unreserved;
}

int percent_round(int64_t current, int64_t unreserved) {
    if (unreserved <= 0) return 0;
    // 100 * current fits in int64 (u32 * 100); the division is integer-exact.
    const int64_t n = 100 * current;
    const int64_t q = n / unreserved;
    const int64_t r = n % unreserved;
    if (2 * r == unreserved) {
        // Exact .5 tie: python round() picks the even neighbour (banker's rounding).
        return static_cast<int>(q % 2 == 0 ? q : q + 1);
    }
    return static_cast<int>(2 * r > unreserved ? q + 1 : q);
}

}  // namespace poe2
