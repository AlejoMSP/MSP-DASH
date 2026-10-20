// Tests the REAL sd_logging.cpp: the start conditions (key on / running / moving / a sensor level), the stop delay,
// a new file per session, NO rows while the simulator runs, and the settings checks and storage.
#include "Arduino.h"
#include "Preferences.h"
#include "sd_logging.h"
#include "sd_storage.h"
#include "gauge_config.h"
#include "simulator.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>

uint32_t g_fake_millis = 0;
uint32_t millis() { return g_fake_millis; }
void delay(uint32_t) {}
SerialClass Serial;
static bool g_simActive = false, g_mounted = true;
static float g_val[SIG_COUNT]; static bool g_has[SIG_COUNT];
extern "C" {
bool sim_is_active(void) { return g_simActive; }
bool sd_storage_is_mounted(void) { return g_mounted; }
bool gauge_config_is_log_enabled(SignalId s) { return s == SIG_RPM || s == SIG_CLT; }
bool gauge_config_signal_has_data(SignalId s) { return s > 0 && s < SIG_COUNT && g_has[s]; }
float gauge_config_signal_value(SignalId s) { return (s > 0 && s < SIG_COUNT) ? g_val[s] : 0; }
const char *gauge_config_signal_name(SignalId s) { return s == SIG_RPM ? "RPM" : s == SIG_CLT ? "CLT" : "x"; }
}

static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static std::string P(int n) { return std::string(SD_MOUNT_POINT) + "/log_" + std::to_string(n) + ".csv"; }
static bool exists(const std::string &p) { std::ifstream f(p); return f.good(); }
static int rows(const std::string &p) { std::ifstream f(p); std::string l; int n = 0; while (std::getline(f, l)) n++; return n; }
static void tick(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 50) { g_fake_millis += 50; sd_logging_loop(); } }
static void set(SignalId s, float v) { g_val[s] = v; g_has[s] = true; }
static void lose(SignalId s) { g_has[s] = false; }
static void trig(uint8_t mode, uint16_t stop, uint8_t sig = SIG_TPS, bool above = true, float level = 90) {
  SdLogTrigger t = { mode, sig, above, level, stop }; CHECK(sd_logging_set_trigger(&t), "trigger accepted");
}
static void clean() { for (int i = 1; i < 30; i++) remove(P(i).c_str()); }

int main() {
  clean(); Preferences::wipeAll();
  sd_logging_init();
  { SdLogTrigger t; sd_logging_get_trigger(&t);
    CHECK(t.mode == LOG_START_RUNNING && t.stop_delay_s == 10, "default: start when the engine runs, keep 10 s after"); }

  printf("1. engine running: no file at all until it starts\n");
  set(SIG_CLT, 190); set(SIG_RPM, 0); tick(3000);
  CHECK(!exists(P(1)) && !sd_logging_is_active(), "key on, engine off: no file is made (no more empty logs)");
  set(SIG_RPM, 3000); tick(1100);
  CHECK(exists(P(1)) && rows(P(1)) >= 3 && sd_logging_is_active(), "RPM above 400: log_1.csv with a header and rows every 500 ms");
  CHECK(std::string(sd_logging_status_text()).find("log_1.csv") != std::string::npos, "status names the file");

  printf("2. the stop delay keeps one file across a short stop\n");
  set(SIG_RPM, 0); tick(5000);
  CHECK(sd_logging_is_active() && !exists(P(2)), "5 s after the engine stops it is still logging into log_1");
  CHECK(std::string(sd_logging_status_text()).find("stops in") != std::string::npos, "the status counts down to the stop");
  set(SIG_RPM, 2500); tick(1000);
  CHECK(!exists(P(2)), "the engine restarts within 10 s: same file");
  set(SIG_RPM, 0); tick(10100);
  CHECK(!sd_logging_is_active(), "10 s after it stops: logging ends");
  int r1 = rows(P(1)); tick(2000);
  CHECK(rows(P(1)) == r1, "no rows after it ended");
  set(SIG_RPM, 3000); tick(600);
  CHECK(exists(P(2)), "the next start opens a NEW file: log_2.csv");

  printf("3. the simulator: never logs, closes the file at once\n");
  int r2 = rows(P(2));
  g_simActive = true; tick(3000);
  CHECK(rows(P(2)) == r2 && !sd_logging_is_active(), "no rows while the simulator runs, even with RPM high");
  CHECK(std::string(sd_logging_status_text()).find("simulator") != std::string::npos, "status says why");
  g_simActive = false; tick(600);
  CHECK(exists(P(3)) && rows(P(2)) == r2, "after the simulator: real data goes in a NEW file (log_3), never next to fake rows");

  printf("4. key on\n");
  trig(LOG_START_KEY_ON, 0); clean(); lose(SIG_RPM); tick(1000);
  CHECK(!sd_logging_is_active(), "no ECU data: not logging");
  set(SIG_RPM, 0); tick(600);
  CHECK(sd_logging_is_active() && exists(P(4)), "ECU sends data (RPM 0, engine off): logging");
  lose(SIG_RPM); tick(100);
  CHECK(!sd_logging_is_active(), "stop delay 0: ends as soon as the ECU goes quiet");

  printf("5. moving\n");
  trig(LOG_START_MOVING, 0); set(SIG_RPM, 3000);
  set(SIG_VSS, 0); tick(1000);
  CHECK(!sd_logging_is_active(), "engine running but standing still: not logging");
  set(SIG_VSS, 1.5f); tick(500); CHECK(!sd_logging_is_active(), "1.5 mph (creeping): not yet");
  set(SIG_VSS, 15); tick(600); CHECK(sd_logging_is_active(), "15 mph: logging");
  lose(SIG_VSS); tick(100); CHECK(!sd_logging_is_active(), "no speed data: not logging");

  printf("6. a sensor level, like throttle above 90 %%\n");
  trig(LOG_START_SIGNAL, 0, SIG_TPS, true, 90);
  set(SIG_TPS, 50); tick(1000); CHECK(!sd_logging_is_active(), "throttle 50: not logging");
  set(SIG_TPS, 95); tick(600); CHECK(sd_logging_is_active(), "throttle 95: logging");
  set(SIG_TPS, 90); tick(100); CHECK(!sd_logging_is_active(), "exactly 90 is not above 90");
  trig(LOG_START_SIGNAL, 0, SIG_OILP, false, 10);
  set(SIG_OILP, 40); tick(600); CHECK(!sd_logging_is_active(), "oil pressure 40 with 'below 10': not logging");
  set(SIG_OILP, 6); tick(600); CHECK(sd_logging_is_active(), "oil pressure 6: logging (below works too)");

  printf("7. bad settings are refused and change nothing\n");
  { SdLogTrigger before; sd_logging_get_trigger(&before);
    SdLogTrigger b = before; b.mode = 9; CHECK(!sd_logging_set_trigger(&b), "unknown mode refused");
    b = before; b.stop_delay_s = 601; CHECK(!sd_logging_set_trigger(&b), "stop delay over 600 s refused");
    b = before; b.mode = LOG_START_SIGNAL; b.sig = 0; CHECK(!sd_logging_set_trigger(&b), "sensor mode with no sensor refused");
    b = before; b.mode = LOG_START_SIGNAL; b.sig = SIG_COUNT; CHECK(!sd_logging_set_trigger(&b), "sensor number out of range refused");
    b = before; b.mode = LOG_START_SIGNAL; b.level = NAN; CHECK(!sd_logging_set_trigger(&b), "no level refused");
    SdLogTrigger after; sd_logging_get_trigger(&after);
    CHECK(after.mode == before.mode && after.sig == before.sig && after.stop_delay_s == before.stop_delay_s, "and the saved setting is unchanged"); }

  printf("8. the setting survives a reboot\n");
  trig(LOG_START_SIGNAL, 30, SIG_TPS, true, 85.5f);
  sd_logging_init();
  { SdLogTrigger t; sd_logging_get_trigger(&t);
    CHECK(t.mode == LOG_START_SIGNAL && t.sig == SIG_TPS && t.above && fabsf(t.level - 85.5f) < 0.01f && t.stop_delay_s == 30, "all five fields come back from NVS"); }

  printf("9. no SD card\n");
  g_mounted = false; sd_logging_init(); clean();
  set(SIG_TPS, 99); tick(1000);
  CHECK(!sd_logging_is_active() && !exists(P(1)), "nothing written and the logging dot stays off");
  CHECK(std::string(sd_logging_status_text()).find("No SD card") != std::string::npos, "status says there is no card");

  clean();
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
