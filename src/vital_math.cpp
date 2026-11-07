#include "vital_math.h"

namespace poe2 {

int64_t reserved_amount(int64_t total, int64_t flat, int64_t pct) {
    if (total <= 0) return 0;
    // ceil(total * pct / 10000) + flat, integer-exact. Inputs are u32 fields,
    // clamped to non-negative defensively (negative memory makes no sense here).
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

}  // namespace poe2
