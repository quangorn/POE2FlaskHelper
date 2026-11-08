#include "vitals.h"

#include "keys.h"
#include "vital_math.h"

namespace poe2 {

std::vector<VitalAction> evaluate(const std::array<VitalVals, kVitalCount>& samples,
                                  const Config& cfg, bool foreground,
                                  int64_t now_ms, VitalSendState& state) {
    std::vector<VitalAction> actions;
    if (!foreground) return actions;   // SPEC §4.8: keys only to the foreground game
    for (int i = 0; i < kVitalCount; ++i) {
        const VitalCfg& d = cfg.vitals[i];
        if (!d.enabled) continue;                        // enabled:false -> ignored
        const VitalVals& v = samples[i];
        const int64_t unres = unreserved_amount(v.total, v.flat, v.pct);
        if (unres <= 0) continue;                        // SPEC §4.7 skip
        // SPEC §4.7 comparison without rounding; u32 * 100 fits in int64.
        if (!below_threshold(v.current, d.threshold, unres)) continue;
        // Cooldown runs from the LAST SEND, not from the first below-threshold tick.
        if (state.last_send[i] != kNoSend &&
            now_ms - state.last_send[i] < d.cooldownMs) {
            continue;
        }
        const KeyResult kr = parse_key(d.key);   // config validation enforces a plain key
        if (!kr.ok) continue;                    // defensive: never with a validated config
        state.last_send[i] = now_ms;             // the timer restarts at this send
        VitalAction a;
        a.vital = i;
        a.key = d.key;
        a.vk = kr.key.vk;
        a.extended = kr.key.extended;
        // Display percent, the same half-even (python round()) as the tick values line.
        a.percent = percent_round(v.current, unres);
        a.threshold = d.threshold;
        actions.push_back(a);
    }
    return actions;
}

}  // namespace poe2
