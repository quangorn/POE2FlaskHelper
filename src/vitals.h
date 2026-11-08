// Flask activation (SPEC §4.7): pure per-tick evaluation of the Vital' samples
// against the config thresholds, cooldowns and enabled flags. No Win32, no real
// time: the scanner thread injects now_ms (IClock) and owns VitalSendState; the
// actions are dispatched through IKeySender by the caller (src/scanner_session.cpp).

#include <array>
#include <climits>
#include <cstdint>
#include <string>
#include <vector>

#include "chain.h"    // VitalVals samples from tick()
#include "config.h"   // VitalCfg / VitalIndex / Config

namespace poe2 {

// Per-Vital cooldown state (SPEC §4.7: the cooldown runs from the LAST SEND). The
// sentinel kNoSend means "never sent": SPEC §4.7 has NO arming requirement, a Vital
// below the threshold fires on the very first tick after attach - an app started at
// 30% HP presses its key immediately.
constexpr int64_t kNoSend = INT64_MIN;

struct VitalSendState {
    std::array<int64_t, kVitalCount> last_send;
    // SPEC §4.7 (amended 2026-11-08): the "held by cooldown" info line fires ONCE per
    // cooldown window. The flag is armed by the SEND that ends the window (the next
    // cooldown firing), so a window logs at most one line and the window after the
    // next press logs one again. A send always resets it, and every held record lives
    // inside a window opened by a send: the flag can never stick on forever.
    std::array<bool, kVitalCount> held_logged;
    VitalSendState() { last_send.fill(kNoSend); held_logged.fill(false); }
};

// One key press produced by evaluate() (SPEC §4.7/§4.8). vk/extended feed
// IKeySender::press; key/threshold/percent build the SPEC §7 press line
// "Health 23% <= 50%, key=1" (the "<=" form of the 2026-11-08 amendment, plan-release.md
// item 3: the DISPLAYED percent is half-even rounded, so it can equal the threshold of a
// legal press - the strict core comparison current*100 < threshold*unreserved stays as is).
// percent is the current % of unreserved, printed with the same half-even (python round())
// rounding as the tick values line.
struct VitalAction {
    int vital = 0;             // VitalIndex (Health..Rage)
    std::string key;           // config key string, e.g. "1"
    uint16_t vk = 0;           // VK from the keymap (parse_key)
    bool extended = false;     // E0 prefix for the arrow and NumPad keys (SPEC §4.8)
    int percent = 0;
    int threshold = 0;
};

// A below-threshold Vital held back by the cooldown (SPEC §4.7, amended 2026-11-08).
// evaluate() reports all of them; the caller logs only the ones with first=true, so
// evaluate() stays pure and complete: no logging decision is made inside the core.
struct VitalWait {
    int vital = 0;             // VitalIndex
    std::string key;           // config key string (the same d.key as the action)
    int percent = 0;           // display percent, half-even like the press line
    int threshold = 0;
    int64_t remaining_ms = 0;  // cooldown left: cooldownMs - (now_ms - last_send)
    bool first = false;        // true = the one info line for this cooldown window
};

// One tick of flask logic, SPEC §4.7 exactly:
// - only enabled vitals, skipped when unreserved <= 0;
// - below threshold WITHOUT rounding: current*100 < threshold*unreserved
//   (exact equality is NOT below; overflow-safe for u32 values in int64 math);
// - fires when cooldownMs has fully elapsed since the LAST SEND (repeat every
//   cooldownMs while the value stays below; never a second press on one tick);
// - timers are independent per Vital; all vitals below threshold on one tick fire
//   together, one press each;
// - foreground=false -> no actions at all (SPEC §4.8: keys go only to the game
//   window; requireGameForeground is folded into this bool by the caller);
// - state is updated in place on every action: last_send = now_ms, held_logged reset
//   (the cooldown window closes and the one-line right is re-armed);
// - a Vital below the threshold inside its cooldown produces no action and one
//   VitalWait; waits is optional (nullptr = caller does not want the held report),
//   so every pre-amendment call site compiles unchanged.
// Invalid samples (chain broke) never reach evaluate: the caller skips the tick
// when tick() returned !ok, so there are no presses on broken values.
std::vector<VitalAction> evaluate(const std::array<VitalVals, kVitalCount>& samples,
                                  const Config& cfg, bool foreground,
                                  int64_t now_ms, VitalSendState& state,
                                  std::vector<VitalWait>* waits = nullptr);

}  // namespace poe2
