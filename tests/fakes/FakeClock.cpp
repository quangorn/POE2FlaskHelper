#include "fakes/FakeClock.h"

namespace poe2 {
namespace testing {

// TU anchor for FakeClock; behavior is fully inline in the header (deterministic,
// no OS time). The constructor is defined in the header.
void fake_clock_anchor() {}

}  // namespace testing
}  // namespace poe2
