// Stage 0: trivial test + framework self-checks. ctest runs this as "unit".
#include "test.h"

#include <string>

#include "version.h"
#include "core_placeholder.h"

TEST(core_stub) {
    // poe2_core is linked into the test exe.
    CHECK_EQ(poe2::core_stub(), 42);
    CHECK_EQ(std::string(APP_VERSION_STR), std::string("1.0.0"));
    CHECK_EQ(APP_VERSION_MAJOR, 1);
    REQUIRE(poe2::core_stub() >= 0);
}

// Probes for the framework self-check.
static bool g_after_require = false;

static void probe_failing_check() { CHECK(1 == 2); }
static void probe_passing_check() { CHECK_EQ(2 + 2, 4); }
static void probe_require_stop() {
    REQUIRE(false);
    g_after_require = true;  // must be skipped after REQUIRE(false)
}

TEST(framework_selfcheck) {
    // A passing check is not counted as failure.
    CHECK_EQ(tf::run_probe(probe_passing_check), 0);

    // A failing assertion is detected and reported (probe counts failures).
    CHECK_EQ(tf::run_probe(probe_failing_check), 1);

    // REQUIRE stops the test on failure.
    g_after_require = false;
    tf::run_probe(probe_require_stop);
    CHECK(!g_after_require);

    // Global report state survives probes untouched.
    CHECK_EQ(tf::fails(), 0);

    // run_all() returns nonzero iff some test failed: verified on an
    // isolated one-test failure (probe mode keeps the global suite green).
    CHECK_EQ(tf::run_probe(probe_failing_check), 1);
}

int main() {
    return tf::run_all();
}
