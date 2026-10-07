// Tests the REAL sd_logging.cpp: rows are written only for real data above 800 RPM, and NEVER while the simulator runs.
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

uint32_t g_fake_millis = 0;
uint32_t millis() { return g_fake_millis; }
void delay(uint32_t) {}
SerialClass Serial;
static bool g_simActive = false;
static float g_rpm = 0; static bool g_rpmHas = false;
extern "C" {
bool sim_is_active(void) { return g_simActive; }
bool sd_storage_is_mounted(void) { return true; }
bool gauge_config_is_log_enabled(SignalId s) { return s == SIG_RPM || s == SIG_CLT; }
bool gauge_config_signal_has_data(SignalId s) { return s == SIG_RPM ? g_rpmHas : s == SIG_CLT; }
float gauge_config_signal_value(SignalId s) { return s == SIG_RPM ? g_rpm : 190.0f; }
const char *gauge_config_signal_name(SignalId s) { return s == SIG_RPM ? "RPM" : s == SIG_CLT ? "CLT" : "x"; }
}

static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static int rows(const std::string &p) { std::ifstream f(p); std::string l; int n = 0; while (std::getline(f, l)) n++; return n; }
static void tick(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 50) { g_fake_millis += 50; sd_logging_loop(); } }

int main() {
  std::string log = std::string(SD_MOUNT_POINT) + "/log_1.csv";
  remove(log.c_str());
  sd_logging_init();
  CHECK(rows(log) == 1, "log file created with just a header row");

  printf("1. real data, engine running -> rows are written\n");
  g_rpmHas = true; g_rpm = 3000; tick(1100);
  int real = rows(log);
  CHECK(real >= 3, "rows written (every 500ms)");
  CHECK(sd_logging_is_active(), "logging indicator is active");

  printf("2. the simulator is ON -> nothing is written, indicator off\n");
  g_simActive = true; tick(3000);
  CHECK(rows(log) == real, "NO rows added while the simulator runs, even though RPM looks like a running engine");
  CHECK(!sd_logging_is_active(), "the blinking logging indicator is off");

  printf("3. simulator off again -> logging resumes\n");
  g_simActive = false; tick(1100);
  CHECK(rows(log) > real, "rows resume");
  CHECK(sd_logging_is_active(), "indicator back on");

  printf("4. existing rule: no rows at or below 800 RPM\n");
  int before = rows(log); g_rpm = 800; tick(2000);
  CHECK(rows(log) == before && !sd_logging_is_active(), "idle at 800: nothing written");
  g_rpm = 3000; g_rpmHas = false; tick(2000);
  CHECK(rows(log) == before, "RPM with no live data: nothing written");

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
