// Tests the REAL gauge_config.cpp (compiled unmodified) with a fake clock and fake flash store.
#include "Arduino.h"
#include "Preferences.h"
#include "gauge_app.h"
#include "gauge_config.h"
#include <cstdio>

uint32_t g_fake_millis = 0;

// gauge_app.c normally defines these globals; gauge_config.cpp reads them.
volatile float g_afr, g_clt_f, g_oilp_kpa, g_oilt_f, g_fuelp_kpa, g_flex_pct, g_boost_kpa, g_rpm, g_tps, g_battv;
volatile bool g_afr_has_data, g_clt_has_data, g_oilp_has_data, g_oilt_has_data, g_fuelp_has_data,
              g_flex_has_data, g_boost_has_data, g_rpm_has_data, g_tps_has_data, g_battv_has_data;
volatile int g_requested_page = -1;
volatile int g_page_nav_delta = 0;

static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)

int main() {
  printf("S1 staleness timeout\n");
  g_fake_millis = 10000;
  CHECK(!gauge_config_signal_has_data(SIG_RPM), "no data before the first frame");
  g_rpm = 3000; g_rpm_has_data = true; gauge_config_mark_signal_active(SIG_RPM);
  CHECK(gauge_config_signal_has_data(SIG_RPM), "has data right after a frame");
  g_fake_millis += 1999;
  CHECK(gauge_config_signal_has_data(SIG_RPM), "still has data 1999ms later");
  g_fake_millis += 2;
  CHECK(!gauge_config_signal_has_data(SIG_RPM), "LOST after 2001ms of silence (flag is still set, value still 3000)");
  CHECK(g_rpm_has_data, "the raw first-frame flag was not touched (only the answer changed)");
  gauge_config_mark_signal_active(SIG_RPM);
  CHECK(gauge_config_signal_has_data(SIG_RPM), "comes back as soon as frames resume");
  CHECK(!gauge_config_signal_has_data(SIG_CLT), "other signals unaffected: CLT never seen");
  CHECK(!gauge_config_signal_has_data(SIG_NONE), "SIG_NONE never has data");
  g_fake_millis = 0xFFFFFF00u; gauge_config_mark_signal_active(SIG_RPM);
  g_fake_millis += 500;   // wraps past 2^32
  CHECK(gauge_config_signal_has_data(SIG_RPM), "correct across the 32-bit millis wrap");

  printf("S2 alert_fullscreen persists through save + reboot\n");
  gauge_config_init();                               // empty store -> defaults
  PageConfig *p = gauge_config_get_pages();
  CHECK(p[0].cells[0].alert_fullscreen == false, "defaults to off");
  p[0].cells[0].alert_fullscreen = true;
  p[1].cells[0].alert_fullscreen = true;
  gauge_config_save();
  p[0].cells[0].alert_fullscreen = false;            // scribble over RAM, as if rebooted
  p[1].cells[0].alert_fullscreen = false;
  gauge_config_init();                               // "reboot": reload from the fake flash
  p = gauge_config_get_pages();
  CHECK(p[0].cells[0].alert_fullscreen == true, "page 0 cell 0 still on after reload");
  CHECK(p[1].cells[0].alert_fullscreen == true, "page 1 cell 0 still on after reload");
  CHECK(p[2].cells[0].alert_fullscreen == false, "an untouched cell is still off");
  CHECK(p[0].cells[0].has_warning == true && p[0].cells[0].warning_threshold == 220.0f, "other saved fields intact");

  printf("S3 a config saved by older firmware (no alert_fullscreen key) loads cleanly as off\n");
  Preferences::put_raw("gaugecfg2/pages",
    "[{\"enabled\":true,\"layout\":0,\"cells\":[{\"enabled\":true,\"signal\":2,\"gauge_type\":0,\"min\":65,\"max\":250,\"rest\":65,"
    "\"caption\":\"CLT\",\"has_warning\":true,\"warning_threshold\":220,\"warning_above\":true}]}]");
  gauge_config_init();
  p = gauge_config_get_pages();
  CHECK(p[0].cells[0].alert_fullscreen == false, "missing key reads as off");
  CHECK(p[0].cells[0].has_warning == true && p[0].cells[0].warning_above == true, "old fields still load");
  CHECK(strcmp(p[0].cells[0].caption, "CLT") == 0, "caption loads");

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
