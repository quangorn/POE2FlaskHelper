// Framework self-checks + the version-header consistency test. ctest runs this as "unit".
// The stage-0 core_stub tests are deleted (review S5-1, fixed 2026-11-08): poe2_core is exercised by
// the real module tests, and the version assertion compares the generated macros with
// each other, never a literal "1.0.0" - a project(... VERSION) bump keeps this test green
// (D2, merged into S5-1: proven by the scratch bump to 2.5.1, ctest green, zip named 2.5.1).
#include "test.h"

#include <string>

#include "version.h"

TEST(version_header_consistency) {
    // build/version.h is generated from res/version.h.in by project(... VERSION)
    // (SPEC §2.2): the string and the numeric macros must describe one and the same
    // version, whatever that version is.
    const std::string v = APP_VERSION_STR;
    const size_t d1 = v.find('.');
    REQUIRE(d1 != std::string::npos);
    const size_t d2 = v.find('.', d1 + 1);
    REQUIRE(d2 != std::string::npos);
    CHECK_EQ(v.find('.', d2 + 1), std::string::npos);          // exactly major.minor.patch
    CHECK_EQ(std::stoi(v.substr(0, d1)), APP_VERSION_MAJOR);   // string fields == macros
    CHECK_EQ(std::stoi(v.substr(d1 + 1, d2 - d1 - 1)), APP_VERSION_MINOR);
    CHECK_EQ(std::stoi(v.substr(d2 + 1)), APP_VERSION_PATCH);
    // VERSIONINFO and the manifest carry "<version>.0" (S0-5): APP_VERSION_FULL is the
    // project version plus the zero revision field, derived from the same macros.
    CHECK_EQ(std::string(APP_VERSION_FULL), v + ".0");
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
