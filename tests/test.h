#pragma once
// Minimal test framework (plan.md §2): CHECK, CHECK_EQ, REQUIRE, registration,
// report, exit code. No third-party frameworks.
//
// Registration robustness (MINOR_ISSUES S0-7): every test TU is compiled directly
// into the poe2_tests exe, NEVER into a static library. The linker links each
// object file named on the command line, so the static registrars created by
// TEST() are never dropped. Registration order across TUs is unspecified and
// irrelevant - tests are independent. run_all() treats an empty registry as a
// failure, so a registration loss is reported loudly, not silently.

#include <cstdio>
#include <type_traits>
#include <vector>

namespace tf {

using Fn = void (*)();

inline std::vector<std::pair<const char*, Fn>>& tests() {
    static std::vector<std::pair<const char*, Fn>> t;
    return t;
}

inline int& fails() {
    static int f = 0;
    return f;
}

inline bool& quiet() {
    static bool q = false;
    return q;
}

inline bool report(const char* what, int line, bool ok) {
    if (ok) return true;
    ++fails();
    if (!quiet()) printf("  FAIL line %d: %s\n", line, what);
    return false;
}

// CHECK_EQ failure prints lhs/rhs values for arithmetic types (MINOR_ISSUES S0-8);
// non-arithmetic comparisons fall back to the generic message.
template <typename A, typename B>
void report_eq(const char* what, int line, const A& a, const B& b) {
    if (a == b) return;
    ++fails();
    if (quiet()) return;
    if constexpr (std::is_arithmetic_v<A> && std::is_arithmetic_v<B>) {
        printf("  FAIL line %d: %s  lhs=%g rhs=%g\n",
               line, what, static_cast<double>(a), static_cast<double>(b));
    } else {
        printf("  FAIL line %d: %s (lhs is not equal to rhs)\n", line, what);
    }
}

// Run fn in isolation: count its failures, print nothing, restore state.
// Used by framework self-checks.
inline int run_probe(Fn fn) {
    int saved_fails = fails();
    bool saved_quiet = quiet();
    fails() = 0;
    quiet() = true;
    fn();
    int f = fails();
    fails() = saved_fails;
    quiet() = saved_quiet;
    return f;
}

inline bool add_test(const char* name, Fn fn) {
    tests().emplace_back(name, fn);
    return true;
}

inline int run_all() {
    if (tests().empty()) {
        printf("no tests registered - test TUs must be linked into the exe\n");
        return 1;
    }
    int failed = 0;
    for (const auto& t : tests()) {
        fails() = 0;
        t.second();
        if (fails() > 0) {
            ++failed;
            printf("[FAIL] %s: %d failed checks\n", t.first, fails());
        } else {
            printf("[ OK ] %s\n", t.first);
        }
    }
    int total = static_cast<int>(tests().size());
    printf("%d/%d tests passed\n", total - failed, total);
    return failed == 0 ? 0 : 1;
}

}  // namespace tf

#define TEST(name)                                       \
    static void name();                                  \
    static const bool name##_registered = tf::add_test(#name, &name); \
    static void name()

#define CHECK(expr) tf::report(#expr, __LINE__, (expr))
#define CHECK_EQ(a, b) tf::report_eq(#a " == " #b, __LINE__, (a), (b))
#define REQUIRE(expr)                          \
    do {                                       \
        if (!tf::report("REQUIRE " #expr, __LINE__, (expr))) return; \
    } while (0)
