// RecordingKeySender (plan.md §2): the fake IKeySender for Stage 4 tests. Records
// every press as (vk, extended, time); the time is stamped from an injected FakeClock,
// so cooldown timing is verified exactly with no sleeps and no real time.
#include <cstdint>
#include <vector>

#include "clock.h"
#include "keys.h"

namespace poe2 {
namespace testing {

struct RecordingKeySender : IKeySender {
    struct Rec { uint16_t vk; bool extended; int64_t t; };

    std::vector<Rec> recs;
    IClock* clock = nullptr;   // press-time stamp source; null -> t = 0

    void press(uint16_t vk, bool extended) override {
        recs.push_back(Rec{vk, extended, clock ? clock->now_ms() : 0});
    }

    int count() const { return static_cast<int>(recs.size()); }
    void clear() { recs.clear(); }
};

// TU anchor for the RecordingKeySender module (the FakeClock/FakeMemory pattern).
void recording_key_sender_anchor();

}  // namespace testing
}  // namespace poe2
