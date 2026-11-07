// Stage 4 flask logic tests (plan.md "Этап 4"): evaluate() driven by FakeClock and
// RecordingKeySender — fire below threshold, no fire at exact equality, cooldown
// repeat exactly at cooldownMs, independent per-Vital timers, enabled:false,
// multiple vitals on one tick, foreground gate, no duplicate press without a new
// tick, reserved percent-of-unreserved path, Ward/Rage, first tick without arming,
// u32 overflow safety. Everything is pure core code, deterministic, no sleeps.
#include "test.h"

#include <array>
#include <string>

#include "config.h"
#include "vitals.h"
#include "fakes/FakeClock.h"
#include "fakes/RecordingKeySender.h"

using poe2::Config;
using poe2::evaluate;
using poe2::KeyResult;
using poe2::TickResult;
using poe2::VitalAction;
using poe2::VitalSendState;
using poe2::VitalVals;
using poe2::kEnergyShield;
using poe2::kHealth;
using poe2::kMana;
using poe2::kNoSend;
using poe2::kRage;
using poe2::kVitalCount;
using poe2::kWard;
using poe2::testing::FakeClock;
using poe2::testing::RecordingKeySender;

namespace {

// Synthetic-layout values that never fire on the default config (Health 80%,
// Mana >40%, ES/Ward/Rage disabled). Tests set one Vital below the threshold.
std::array<VitalVals, kVitalCount> quiet_samples() {
    std::array<VitalVals, kVitalCount> s{};
    s[kHealth] = VitalVals{5000, 4000, 100, 2023};   // unreserved 3888
    s[kMana] = VitalVals{3000, 2000, 0, 5000};       // unreserved 1500
    s[kEnergyShield] = VitalVals{1200, 800, 0, 0};
    s[kWard] = VitalVals{900, 300, 10, 1000};        // unreserved 800
    s[kRage] = VitalVals{700, 640, 0, 0};            // unreserved 700
    return s;
}

// The exact dispatch the scanner session performs per tick (press + state update),
// so a test sees presses with fake-clock timestamps. No logger: the press-line
// format is proven in vital_session_tests.cpp through the real session path.
void dispatch(const std::array<VitalVals, kVitalCount>& s, const Config& cfg,
              bool fg, int64_t now, VitalSendState& st, RecordingKeySender& keys) {
    for (const VitalAction& a : evaluate(s, cfg, fg, now, st)) {
        keys.press(a.vk, a.extended);
    }
}

// A sender stamped by the test clock, so press times are verifiable.
RecordingKeySender make_keys(FakeClock& c) {
    RecordingKeySender k;
    k.clock = &c;
    return k;
}

}  // namespace

TEST(vital_fire_below_threshold) {
    Config cfg = poe2::config_defaults();
    auto s = quiet_samples();
    s[kHealth].current = 1100;   // 1100*100=110000 < 50*3888=194400 -> below
    VitalSendState st;
    FakeClock clock;
    clock.set(100000);
    RecordingKeySender keys = make_keys(clock);
    dispatch(s, cfg, true, clock.now_ms(), st, keys);
    CHECK_EQ(keys.count(), 1);
    CHECK_EQ(keys.recs[0].vk, static_cast<uint16_t>('1'));   // Health key "1"
    CHECK_EQ(keys.recs[0].extended, false);
    CHECK_EQ(keys.recs[0].t, 100000);
    CHECK_EQ(st.last_send[kHealth], 100000);                 // timer set at the send
    CHECK_EQ(st.last_send[kMana], kNoSend);                  // untouched vitals
}

TEST(vital_no_fire_at_exact_threshold) {
    Config cfg = poe2::config_defaults();
    auto s = quiet_samples();
    VitalSendState st;
    RecordingKeySender keys;
    // current*100 == threshold*unreserved is NOT below (SPEC §4.7, no rounding).
    s[kHealth].current = 1944;   // 194400 == 50*3888
    CHECK(evaluate(s, cfg, true, 5000, st).empty());
    CHECK_EQ(keys.count(), 0);
    CHECK_EQ(st.last_send[kHealth], kNoSend);
    // One unit below fires.
    s[kHealth].current = 1943;   // 194300 < 194400
    CHECK_EQ(evaluate(s, cfg, true, 5000, st).size(), 1u);
    // Above: no fire.
    s[kHealth].current = 1945;
    CHECK(evaluate(s, cfg, true, 6000, st).empty());   // timer 5000, not expired anyway
}

TEST(vital_cooldown_repeats_exactly_at_cooldown) {
    Config cfg = poe2::config_defaults();   // Health cooldownMs = 1000
    auto s = quiet_samples();
    s[kHealth].current = 1000;              // below 50% of unreserved
    VitalSendState st;
    FakeClock clock;
    clock.set(10000);
    RecordingKeySender keys = make_keys(clock);
    dispatch(s, cfg, true, clock.now_ms(), st, keys);        // fires on the first tick
    CHECK_EQ(keys.count(), 1);
    clock.advance(999);                     // 999 ms: not yet
    dispatch(s, cfg, true, clock.now_ms(), st, keys);
    CHECK_EQ(keys.count(), 1);
    clock.advance(1);                       // exactly cooldownMs: fires
    dispatch(s, cfg, true, clock.now_ms(), st, keys);
    CHECK_EQ(keys.count(), 2);
    CHECK_EQ(keys.recs[1].t, 11000);
    CHECK_EQ(st.last_send[kHealth], 11000);
}

TEST(vital_cooldown_runs_from_last_send) {
    // Independent per-Vital timers; the cooldown is measured from the LAST SEND,
    // not from the first below-threshold tick.
    Config cfg = poe2::config_defaults();
    cfg.vitals[kHealth].cooldownMs = 500;
    cfg.vitals[kMana].cooldownMs = 2000;
    auto s = quiet_samples();
    s[kHealth].current = 1000;   // below: 100000 < 194400
    s[kMana].current = 100;      // below: 10000 < 60000
    VitalSendState st;
    FakeClock clock;
    clock.set(0);
    RecordingKeySender keys = make_keys(clock);
    dispatch(s, cfg, true, clock.now_ms(), st, keys);        // t=0: both fire
    CHECK_EQ(keys.count(), 2);
    CHECK_EQ(keys.recs[0].vk, static_cast<uint16_t>('1'));
    CHECK_EQ(keys.recs[1].vk, static_cast<uint16_t>('2'));

    clock.set(500);
    dispatch(s, cfg, true, clock.now_ms(), st, keys);        // HP: 500 >= 500 fires
    CHECK_EQ(keys.count(), 3);
    CHECK_EQ(keys.recs[2].vk, static_cast<uint16_t>('1'));

    clock.set(1500);                                        // HP: 1000 >= 500 fires;
    dispatch(s, cfg, true, clock.now_ms(), st, keys);       // Mana: 1500 < 2000 no
    CHECK_EQ(keys.count(), 4);
    CHECK_EQ(keys.recs[3].vk, static_cast<uint16_t>('1'));

    clock.set(2000);                                        // Mana: 2000 from its last
    dispatch(s, cfg, true, clock.now_ms(), st, keys);       // send (0) fires; HP: 500
    CHECK_EQ(keys.count(), 6);                              // from 1500 fires; Health is
    CHECK_EQ(keys.recs[4].vk, static_cast<uint16_t>('1'));  // evaluated first, then Mana
    CHECK_EQ(keys.recs[5].vk, static_cast<uint16_t>('2'));
    CHECK_EQ(st.last_send[kMana], 2000);
}

TEST(vital_disabled_ignored) {
    Config cfg = poe2::config_defaults();   // EnergyShield disabled by default
    auto s = quiet_samples();
    s[kEnergyShield].current = 1;           // far below any threshold
    VitalSendState st;
    CHECK(evaluate(s, cfg, true, 5000, st).empty());
    CHECK_EQ(st.last_send[kEnergyShield], kNoSend);
    // Same values, enabled: fires with key "3".
    cfg.vitals[kEnergyShield].enabled = true;
    const auto acts = evaluate(s, cfg, true, 5000, st);
    REQUIRE(acts.size() == 1);
    CHECK_EQ(acts[0].vital, kEnergyShield);
    CHECK_EQ(acts[0].vk, static_cast<uint16_t>('3'));
    CHECK_EQ(st.last_send[kEnergyShield], 5000);
}

TEST(vital_multiple_below_one_tick) {
    Config cfg = poe2::config_defaults();
    cfg.vitals[kEnergyShield].enabled = true;
    cfg.vitals[kWard].enabled = true;
    cfg.vitals[kRage].enabled = true;
    auto s = quiet_samples();
    s[kHealth].current = 1000;      // 26% < 50
    s[kMana].current = 100;         // 7% < 40
    s[kEnergyShield].current = 100; // 8% < 35
    s[kWard].current = 100;         // 12% < 30
    s[kRage].current = 100;         // 14% < 20
    VitalSendState st;
    FakeClock clock;
    clock.set(7000);
    RecordingKeySender keys = make_keys(clock);
    dispatch(s, cfg, true, clock.now_ms(), st, keys);
    CHECK_EQ(keys.count(), 5);      // one press per Vital, all on the same tick
    for (int i = 0; i < kVitalCount; ++i) {
        CHECK_EQ(keys.recs[i].vk, static_cast<uint16_t>('1' + i));   // keys 1..5
        CHECK_EQ(keys.recs[i].t, 7000);
        CHECK_EQ(st.last_send[i], 7000);
    }
}

TEST(vital_foreground_false_no_presses) {
    Config cfg = poe2::config_defaults();
    auto s = quiet_samples();
    s[kHealth].current = 1000;      // below threshold, everything ready to fire
    VitalSendState st;
    RecordingKeySender keys;
    dispatch(s, cfg, false, 5000, st, keys);     // SPEC §4.8/§9: game not foreground
    CHECK_EQ(keys.count(), 0);
    CHECK_EQ(st.last_send[kHealth], kNoSend);    // no send, so no cooldown state
    // Same tick value with the game back in foreground: fires immediately.
    dispatch(s, cfg, true, 5100, st, keys);
    CHECK_EQ(keys.count(), 1);
    CHECK_EQ(st.last_send[kHealth], 5100);
}

TEST(vital_no_duplicate_press_without_new_tick) {
    Config cfg = poe2::config_defaults();
    auto s = quiet_samples();
    s[kHealth].current = 1000;
    VitalSendState st;
    RecordingKeySender keys;
    // The same tick evaluated twice at the same time (no new tick, no time passing):
    // the second call must produce nothing.
    dispatch(s, cfg, true, 5000, st, keys);
    CHECK_EQ(keys.count(), 1);
    dispatch(s, cfg, true, 5000, st, keys);
    CHECK_EQ(keys.count(), 1);
    dispatch(s, cfg, true, 5000, st, keys);
    CHECK_EQ(keys.count(), 1);
    // Only cooldown expiry on a NEW tick allows the next press.
    dispatch(s, cfg, true, 6000, st, keys);
    CHECK_EQ(keys.count(), 2);
}

TEST(vital_mana_reserved_is_percent_of_unreserved) {
    // Mana: total 3000, pct 5000 (50% reserved), flat 0 -> unreserved 1500.
    // The threshold applies to the UNRESERVED pool: 40% * 1500 = 60000.
    Config cfg = poe2::config_defaults();   // Mana enabled, threshold 40, key "2"
    auto s = quiet_samples();
    VitalSendState st;
    RecordingKeySender keys;
    s[kMana].current = 800;   // 800*100=80000 >= 60000: 53% of unreserved, NOT below.
                              // Compared against total (40% of 3000 = 1200) it WOULD fire.
    dispatch(s, cfg, true, 5000, st, keys);
    CHECK_EQ(keys.count(), 0);
    s[kMana].current = 599;   // 59900 < 60000: below
    dispatch(s, cfg, true, 5000, st, keys);
    CHECK_EQ(keys.count(), 1);
    CHECK_EQ(keys.recs[0].vk, static_cast<uint16_t>('2'));
}

TEST(vital_ward_rage_when_enabled) {
    Config cfg = poe2::config_defaults();
    cfg.vitals[kWard].enabled = true;   // key "4", threshold 30, cooldown 1000
    cfg.vitals[kRage].enabled = true;   // key "5", threshold 20, cooldown 2000
    auto s = quiet_samples();
    VitalSendState st;
    RecordingKeySender keys;
    // Ward: unreserved 800 (pct 1000 + flat 10). 240 is exactly at the threshold
    // (24000 == 30*800) and does NOT fire; 239 fires with key "4".
    s[kWard].current = 240;
    CHECK(evaluate(s, cfg, true, 1000, st).empty());
    s[kWard].current = 239;
    const auto w = evaluate(s, cfg, true, 1000, st);
    REQUIRE(w.size() == 1);
    CHECK_EQ(w[0].key, "4");
    CHECK_EQ(w[0].vk, static_cast<uint16_t>('4'));
    CHECK_EQ(w[0].extended, false);
    CHECK_EQ(st.last_send[kWard], 1000);
    // Rage: unreserved 700, threshold 20 -> 14000. 139 fires with key "5".
    s[kRage].current = 139;
    const auto r = evaluate(s, cfg, true, 1000, st);
    REQUIRE(r.size() == 1);
    CHECK_EQ(r[0].key, "5");
    CHECK_EQ(r[0].vk, static_cast<uint16_t>('5'));
    CHECK_EQ(st.last_send[kRage], 1000);
    // Independent timers: at t=2000 Ward fires (1000 ms >= 1000), Rage must NOT
    // (only 1000 ms from its last send, needs 2000).
    const auto r2 = evaluate(s, cfg, true, 2000, st);
    REQUIRE(r2.size() == 1);
    CHECK_EQ(r2[0].vital, kWard);
    CHECK_EQ(st.last_send[kWard], 2000);
    CHECK_EQ(st.last_send[kRage], 1000);
    // At t=3000 both fire: Ward 1000 ms from 2000, Rage 2000 ms from 1000.
    const auto r3 = evaluate(s, cfg, true, 3000, st);
    CHECK_EQ(r3.size(), 2u);
    CHECK_EQ(st.last_send[kRage], 3000);
    // evaluate is pure: nothing was sent through the sender (the session dispatches).
    CHECK_EQ(keys.count(), 0);
}

TEST(vital_first_tick_no_arming) {
    // SPEC §4.7: the app started at 30% HP fires on the first tick — no arming.
    Config cfg = poe2::config_defaults();
    std::array<VitalVals, kVitalCount> s{};
    s[kHealth] = VitalVals{1000, 300, 0, 0};   // 30% of unreserved
    VitalSendState st;                          // fresh state: never sent
    RecordingKeySender keys;
    const auto acts = evaluate(s, cfg, true, 12345, st);
    REQUIRE(acts.size() == 1);
    CHECK_EQ(acts[0].vital, kHealth);
    CHECK_EQ(acts[0].percent, 30);
    CHECK_EQ(acts[0].threshold, 50);
    CHECK_EQ(acts[0].key, "1");
    keys.press(acts[0].vk, acts[0].extended);
    CHECK_EQ(keys.count(), 1);
    CHECK_EQ(keys.recs[0].vk, static_cast<uint16_t>('1'));
}

TEST(vital_unreserved_le_zero_skipped) {
    Config cfg = poe2::config_defaults();
    auto s = quiet_samples();
    s[kHealth].total = 100; s[kHealth].current = 1;   // unreserved <= 0
    s[kHealth].flat = 200; s[kHealth].pct = 0;        // reserved 200 > total
    VitalSendState st;
    CHECK(evaluate(s, cfg, true, 5000, st).empty());  // SPEC §4.7: skip, no press
    CHECK_EQ(st.last_send[kHealth], kNoSend);
    s[kHealth].pct = 10000; s[kHealth].flat = 0;      // 100% reserved, unreserved = 0
    CHECK(evaluate(s, cfg, true, 5000, st).empty());
}

TEST(vital_large_u32_values_overflow_safe) {
    Config cfg = poe2::config_defaults();
    std::array<VitalVals, kVitalCount> s{};
    // Largest u32 total: current*100 = 4.29e11 and threshold*unreserved fit in int64.
    s[kHealth] = VitalVals{4294967295u, 4294967295u, 0, 0};
    VitalSendState st;
    CHECK(evaluate(s, cfg, true, 5000, st).empty());   // exactly 100%: not below
    s[kHealth].current = 1u;                            // 100 < 1*4294967295: fires
    const auto acts = evaluate(s, cfg, true, 5000, st);
    REQUIRE(acts.size() == 1);
    CHECK_EQ(acts[0].percent, 0);   // integer display percent rounds to 0
}
