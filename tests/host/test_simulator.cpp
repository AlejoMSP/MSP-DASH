// Tests the REAL simulator.cpp (unmodified) with a fake clock.
#include "Arduino.h"
#include "gauge_app.h"
#include "gauge_config.h"
#include "simulator.h"
#include <cstdio>
#include <cmath>

uint32_t g_fake_millis = 0;
uint32_t millis() { return g_fake_millis; }
void delay(uint32_t) {}
SerialClass Serial;
volatile float g_afr, g_clt_f, g_oilp_psi, g_oilt_f, g_fuelp_psi, g_flex_pct, g_boost_psi, g_rpm, g_tps, g_battv, g_egt_f, g_iat_f, g_ambt_f, g_hum_pct, g_fuel_pct, g_vss_mph;
volatile bool g_afr_has_data, g_clt_has_data, g_oilp_has_data, g_oilt_has_data, g_fuelp_has_data,
              g_flex_has_data, g_boost_has_data, g_rpm_has_data, g_tps_has_data, g_battv_has_data, g_egt_has_data, g_iat_has_data, g_ambt_has_data, g_hum_has_data, g_fuel_has_data, g_vss_has_data;
volatile int g_requested_page = -1, g_page_nav_delta = 0;
volatile uint16_t g_flag_bits = 0, g_flag_seen = 0;
static int marks[SIG_COUNT];
void gauge_config_mark_signal_active(SignalId s) { marks[s]++; }

static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static const int NSIG = SIG_COUNT - 1;      // every real signal: the ten analog ones plus the nine on/off flags
static bool allFlags(bool v) { return (v ? g_flag_seen == (uint16_t)((1u << SIG_FLAG_COUNT) - 1) : g_flag_seen == 0) && g_rpm_has_data == v && g_clt_has_data == v && g_boost_has_data == v && g_afr_has_data == v && g_oilp_has_data == v &&
                                      g_oilt_has_data == v && g_fuelp_has_data == v && g_flex_has_data == v && g_tps_has_data == v && g_battv_has_data == v && g_egt_has_data == v && g_iat_has_data == v; }
static int totalMarks() { int n = 0; for (int i = 0; i < SIG_COUNT; i++) n += marks[i]; return n; }
static void run(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 10) { g_fake_millis += 10; sim_loop(); } }

int main() {
  printf("1. boot state\n");
  CHECK(!sim_is_active(), "OFF at boot");
  CHECK(sim_last_stop_reason()[0] == '\0', "no stop reason yet");
  CHECK(sim_seconds_left() == 0, "no countdown while off");
  sim_loop(); CHECK(totalMarks() == 0 && allFlags(false), "sim_loop does nothing while off");
  sim_notify_real_frame();                                  // a real frame while OFF must not leave anything behind
  g_fake_millis = 5000;

  printf("2. the demo profile (10 simulated minutes, every 100 ms)\n");
  float v[SIG_COUNT]; float rpmMin = 1e9, rpmMax = -1e9; bool tripped = false, inRange = true;
  for (float t = 0; t < 600.0f; t += 0.1f) {
    sim_demo_values(t, v);
    rpmMin = fminf(rpmMin, v[SIG_RPM]); rpmMax = fmaxf(rpmMax, v[SIG_RPM]);
    if (v[SIG_CLT] >= 220 || v[SIG_AFR] >= 15 || v[SIG_OILP] <= 40 || v[SIG_FUELP] <= 40 || v[SIG_OILT] >= 220) tripped = true;
    for (int s = 1; s < SIG_COUNT; s++) { SimRange r = sim_range(s); if (v[s] < r.min || v[s] > r.max || !std::isfinite(v[s])) inRange = false; }
  }
  printf("   RPM spans %.0f to %.0f\n", rpmMin, rpmMax);
  CHECK(rpmMin > 800 && rpmMin < 900, "idles a little above 800 (so logging/alert gates see a running engine)");
  CHECK(rpmMax > 6100 && rpmMax <= 6200.1f, "peaks at 6200");
  CHECK(!tripped, "never crosses the DEFAULT warning thresholds (coolant 220, AFR 15, oil/fuel pressure 40, oil temp 220)");
  CHECK(inRange, "every value stays inside its slider range and finite");
  sim_demo_values(0, v);
  CHECK(v[SIG_CLT] < 130 && v[SIG_OILT] < 160, "starts cold ...");
  sim_demo_values(300, v);
  CHECK(v[SIG_CLT] > 195 && v[SIG_OILT] > 195, "... and warms up");

  printf("3. start (demo) and feed\n");
  sim_start(false);
  CHECK(sim_is_active() && !sim_is_manual(), "active, demo mode");
  CHECK(sim_seconds_left() >= 599 && sim_seconds_left() <= 600, "10-minute countdown");
  sim_loop();
  CHECK(allFlags(true), "every signal (ten analog + nine flags) has data");
  CHECK(g_rpm > 800 && g_rpm < 900, "RPM written");
  for (int s = 1; s < SIG_COUNT; s++) if (marks[s] != 1) { CHECK(false, "each signal marked active once on the first tick"); break; }
  int m0 = totalMarks();
  g_fake_millis += 20; sim_loop();
  CHECK(totalMarks() == m0, "no extra write within 50ms (about 20 updates a second)");
  g_fake_millis += 40; sim_loop();
  CHECK(totalMarks() == m0 + NSIG, "next tick after 50ms");
  run(1000);
  CHECK(totalMarks() >= m0 + NSIG * 18 && totalMarks() <= m0 + NSIG * 22, "about 20 ticks in one second");

  printf("4. a real CAN frame ends it\n");
  sim_notify_real_frame();
  sim_loop();
  CHECK(!sim_is_active(), "stopped");
  CHECK(strcmp(sim_last_stop_reason(), "real CAN traffic arrived") == 0, "reason recorded");
  CHECK(allFlags(false), "'has data' flags cleared: gauges go back to --");
  int m1 = totalMarks(); run(500);
  CHECK(totalMarks() == m1, "nothing written after it stopped");
  sim_notify_real_frame();                                  // while off: must not poison the next start
  sim_start(false); run(300);
  CHECK(sim_is_active(), "a real frame seen while OFF does not kill the next run");
  CHECK(sim_last_stop_reason()[0] == '\0', "starting clears the old reason");

  printf("5. the 10-minute timeout\n");
  sim_stop("test"); sim_start(false);
  g_fake_millis += SIM_TIMEOUT_MS - 100; sim_loop();
  CHECK(sim_is_active(), "still on just before 10 minutes");
  g_fake_millis += 200; sim_loop();
  CHECK(!sim_is_active() && strcmp(sim_last_stop_reason(), "10 minutes passed") == 0, "off after 10 minutes");

  printf("6. manual mode\n");
  CHECK(!sim_set_manual(SIG_RPM, 3000), "cannot set values while off");
  sim_start(false);
  CHECK(!sim_set_manual(SIG_RPM, 3000), "cannot set values in demo mode");
  sim_stop("t"); sim_start(true);
  CHECK(sim_is_manual(), "manual mode");
  run(100); CHECK(g_rpm > 800 && g_rpm < 900, "manual starts from the idle values");
  CHECK(sim_set_manual(SIG_RPM, 3000), "set RPM");
  run(100); CHECK(g_rpm == 3000, "the value reaches the gauges' globals");
  CHECK(sim_set_manual(SIG_RPM, 99999) && sim_value(SIG_RPM) == 9000, "clamped to the slider maximum");
  CHECK(sim_set_manual(SIG_OILP, -50) && sim_value(SIG_OILP) == 0, "clamped to the slider minimum");
  CHECK(!sim_set_manual(SIG_RPM, NAN), "NaN rejected");
  CHECK(!sim_set_manual(0, 1) && !sim_set_manual(SIG_COUNT, 1) && !sim_set_manual(-3, 1), "bad signal ids rejected");
  g_fake_millis += SIM_TIMEOUT_MS - 1000;
  CHECK(sim_set_manual(SIG_CLT, 225), "set CLT above the default warning");
  g_fake_millis += SIM_TIMEOUT_MS - 1000; sim_loop();
  CHECK(sim_is_active(), "touching a value restarts the 10-minute timer");
  run(100); CHECK(g_clt_f == 225, "CLT 225 (an alarm value) reaches the gauges");

  printf("7. stop\n");
  sim_stop("stopped from the web page");
  CHECK(!sim_is_active() && allFlags(false) && strcmp(sim_last_stop_reason(), "stopped from the web page") == 0, "stop works");
  sim_stop("again"); CHECK(strcmp(sim_last_stop_reason(), "stopped from the web page") == 0, "stopping twice keeps the first reason");
  CHECK(sim_seconds_left() == 0, "no countdown after stop");

  printf("8. slider ranges\n");
  bool ok = true; for (int s = 1; s < SIG_COUNT; s++) { SimRange r = sim_range(s); if (!(r.max > r.min && r.step > 0)) ok = false; }
  CHECK(ok, "every signal has min < max and a positive step");

  printf("9. the on/off flags\n");
  {
    bool everRev = false, everFan = false, everFan2 = false, everBrake = false, everEgo = false, wrong = false;
    for (float t = 0; t < 600.0f; t += 0.1f) {
      float d[SIG_COUNT]; sim_demo_values(t, d);
      for (int f = SIG_FIRST_FLAG; f < SIG_FLAG_END; f++) if (d[f] != 0.0f && d[f] != 1.0f) wrong = true;
      if (d[SIG_REVLIM] == 1.0f) { everRev = true; if (d[SIG_RPM] < 6150.0f) wrong = true; } else if (d[SIG_RPM] >= 6150.0f) wrong = true;
      if (d[SIG_CEL] != 0.0f || d[SIG_LAMBDAPROT] != 0.0f) wrong = true;                  // these never come on by themselves
      if (d[SIG_MAINRELAY] != 1.0f || d[SIG_FUELPUMP] != 1.0f) wrong = true;
      if ((d[SIG_FAN] == 1.0f) != (d[SIG_CLT] >= 195.0f)) wrong = true;
      if ((d[SIG_FAN2] == 1.0f) != (d[SIG_CLT] >= 200.0f)) wrong = true;
      if ((d[SIG_BRAKE] == 1.0f) != (fmodf(t, 10.0f) >= 8.5f)) wrong = true;
      if ((d[SIG_EGOHEAT] == 1.0f) != (t < 20.0f)) wrong = true;
      everFan |= d[SIG_FAN] == 1.0f; everFan2 |= d[SIG_FAN2] == 1.0f; everBrake |= d[SIG_BRAKE] == 1.0f; everEgo |= d[SIG_EGOHEAT] == 1.0f;
    }
    CHECK(!wrong, "demo flags: 0 or 1 only; rev limiter only at the top of the rev cycle; fans follow coolant; check engine / lambda protect never; relay and pump always");
    CHECK(everRev && everFan && everFan2 && everBrake && everEgo, "and the demo does switch rev limiter, both fans, brake and O2 heater on at some point");
    sim_stop("t"); sim_start(false); g_fake_millis += 60; sim_loop();
    CHECK((g_flag_bits & (1u << (SIG_MAINRELAY - SIG_FIRST_FLAG))) && (g_flag_bits & (1u << (SIG_FUELPUMP - SIG_FIRST_FLAG))) && !(g_flag_bits & (1u << (SIG_CEL - SIG_FIRST_FLAG))), "demo start: relay and pump on, check engine off, in g_flag_bits");
    sim_stop("t"); sim_start(true); run(100);
    CHECK(sim_set_manual(SIG_CEL, 1) && (run(100), (g_flag_bits >> (SIG_CEL - SIG_FIRST_FLAG)) & 1u), "manual: switch the check engine light on");
    CHECK(sim_set_manual(SIG_CEL, 0.2f) && (run(100), !((g_flag_bits >> (SIG_CEL - SIG_FIRST_FLAG)) & 1u)), "manual: 0.2 is off (a flag is on above 0.5)");
    CHECK(sim_set_manual(SIG_FAN, 7) && sim_value(SIG_FAN) == 1.0f, "manual: a flag value is clamped to 1");
    CHECK(sim_set_manual(SIG_BRAKE, -3) && sim_value(SIG_BRAKE) == 0.0f, "manual: ... and to 0");
    SimRange fr = sim_range(SIG_FAN2);
    CHECK(fr.min == 0 && fr.max == 1 && fr.step == 1, "a flag's slider runs 0..1 in steps of 1");
    sim_stop("t");
    CHECK(g_flag_seen == 0, "stopping clears the 'seen' bits so the lamps go back to no-data");
  }

  printf("R the hub's simulator, as a screen hears it (packets flagged LINK_MASK_SIMULATED)\n");
  sim_stop("test"); g_fake_millis += 1000;
  CHECK(!sim_is_active(), "off before any flagged packet");
  sim_note_remote();
  CHECK(sim_is_active(), "a flagged packet: the screen counts as simulating (SIMULATED on screen, no SD logging)");
  g_fake_millis += 999; CHECK(sim_is_active(), "still simulating 999 ms later");
  g_fake_millis += 2;   CHECK(!sim_is_active(), "one second without the flag: back to real data");
  sim_note_remote(); g_fake_millis = 0xFFFFFF00u; sim_note_remote(); g_fake_millis += 500;
  CHECK(sim_is_active(), "correct across the 32-bit millis wrap");
  g_fake_millis += 2000; CHECK(!sim_is_active(), "and ends after it");

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
