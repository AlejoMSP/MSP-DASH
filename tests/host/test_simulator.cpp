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
volatile float g_afr, g_clt_f, g_oilp_kpa, g_oilt_f, g_fuelp_kpa, g_flex_pct, g_boost_kpa, g_rpm, g_tps, g_battv;
volatile bool g_afr_has_data, g_clt_has_data, g_oilp_has_data, g_oilt_has_data, g_fuelp_has_data,
              g_flex_has_data, g_boost_has_data, g_rpm_has_data, g_tps_has_data, g_battv_has_data;
volatile int g_requested_page = -1, g_page_nav_delta = 0;
static int marks[SIG_COUNT];
void gauge_config_mark_signal_active(SignalId s) { marks[s]++; }

static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static bool allFlags(bool v) { return g_rpm_has_data == v && g_clt_has_data == v && g_boost_has_data == v && g_afr_has_data == v && g_oilp_has_data == v &&
                                      g_oilt_has_data == v && g_fuelp_has_data == v && g_flex_has_data == v && g_tps_has_data == v && g_battv_has_data == v; }
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
  CHECK(allFlags(true), "all ten 'has data' flags set");
  CHECK(g_rpm > 800 && g_rpm < 900, "RPM written");
  for (int s = 1; s < SIG_COUNT; s++) if (marks[s] != 1) { CHECK(false, "each signal marked active once on the first tick"); break; }
  int m0 = totalMarks();
  g_fake_millis += 20; sim_loop();
  CHECK(totalMarks() == m0, "no extra write within 50ms (about 20 updates a second)");
  g_fake_millis += 40; sim_loop();
  CHECK(totalMarks() == m0 + 10, "next tick after 50ms");
  run(1000);
  CHECK(totalMarks() >= m0 + 10 * 18 && totalMarks() <= m0 + 10 * 22, "about 20 ticks in one second");

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

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
