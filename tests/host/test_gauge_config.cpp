// Tests the REAL gauge_config.cpp (unmodified) with a fake clock, a flash-file folder, and an NVS fake that
// enforces ESP-IDF's real 4000-byte string limit.
//   ./test_cfg            normal scenarios
//   ./test_cfg mountfail  the flash partition will not mount
//   ./test_cfg baddir     the settings folder does not exist
#include "Arduino.h"
#include "Preferences.h"
#include "FFat.h"
#include "gauge_app.h"
#include "gauge_config.h"
#include "gauge_config_json.h"
#include "gauge_style.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sstream>

#ifndef GAUGE_CFG_DIR
#error "compile with -DGAUGE_CFG_DIR=\"<temp folder>\""
#endif
static const char *DIR = GAUGE_CFG_DIR;

uint32_t g_fake_millis = 0;
uint32_t millis() { return g_fake_millis; }
void delay(uint32_t) {}
SerialClass Serial;
bool g_ffat_mounts = true;
FFatClass FFat;
void *heap_caps_malloc(size_t n, int) { return malloc(n); }
volatile float g_afr, g_clt_f, g_oilp_kpa, g_oilt_f, g_fuelp_kpa, g_flex_pct, g_boost_kpa, g_rpm, g_tps, g_battv;
volatile bool g_afr_has_data, g_clt_has_data, g_oilp_has_data, g_oilt_has_data, g_fuelp_has_data,
              g_flex_has_data, g_boost_has_data, g_rpm_has_data, g_tps_has_data, g_battv_has_data;
volatile int g_requested_page = -1;
volatile int g_page_nav_delta = 0;

static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)

static std::string path(const char *name) { return std::string(DIR) + "/" + name; }
static void writeFile(const std::string &p, const std::string &data) { std::ofstream f(p, std::ios::binary); f << data; }
static std::string readFile(const std::string &p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static bool exists(const std::string &p) { std::ifstream f(p); return f.good(); }
static void wipe() { remove(path("cfg_display.json").c_str()); remove(path("cfg_display.json.tmp").c_str()); remove(path("cfg_web.json").c_str()); remove(path("cfg_web.json.tmp").c_str()); }
static std::string toJson(const PageConfig *p) { JsonDocument d; gauge_config_pages_to_json(p, d); std::string s; serializeJson(d, s); return s; }
static void fillAll(PageConfig *p) {            // the worst case: every page, every cell, long captions, non-default everything
  for (int i = 0; i < MAX_PAGES; i++) {
    p[i].enabled = true; p[i].layout = LAYOUT_2X2;
    for (int j = 0; j < MAX_CELLS_PER_PAGE; j++) {
      CellConfig &c = p[i].cells[j];
      c.enabled = true; c.signal = (SignalId)(1 + (i + j) % 10); c.gauge_type = (GaugeType)((i + j) % 4);
      c.min_val = -50; c.max_val = 9000 + i * 10 + j; c.rest_val = 3;
      snprintf(c.caption, GAUGE_CAPTION_MAXLEN, "Caption %d-%d long text!!", i, j);
      c.needle_color = 0xFF8800 + j; c.tick_color = 0x123456; c.bg_color = 0x0A0B0C;
      c.value_divisor = 1000.0f; c.tick_count = 40 + j; c.major_nth = 4; c.refresh_hz = 25; c.show_value = true; c.value_font_size = GAUGE_VALUE_SIZES[(i + j) % GAUGE_VALUE_SIZE_COUNT];
      c.tick_font_size = (j % 2) ? 14 : -1; c.start_deg = 200 + 10 * j; c.end_deg = 100 + i;
      c.has_warning = true; c.warning_threshold = 123.5f; c.warning_above = (j & 1); c.alert_fullscreen = true;
      c.history_sec = 90 + i; c.ref_enabled = true; c.ref_value = 14.7f;
    }
  }
}

int main(int argc, char **argv) {
  std::string mode = argc > 1 ? argv[1] : "";

  if (mode == "mountfail") {
    g_ffat_mounts = false;
    printf("F1 the flash partition will not mount: saving must say so, not pretend\n");
    gauge_config_init();
    CHECK(!gauge_config_storage_ok(), "storage reported as not mounted");
    CHECK(gauge_config_get_pages()[0].enabled, "still boots with the default pages");
    CHECK(gauge_config_save() == false, "save returns false");
    CHECK(strstr(gauge_config_last_error(), "mount") != NULL, "the reason mentions the mount failure");
    CHECK(gauge_config_web_save() == false, "web save returns false too");
    printf("\n%d passed, %d failed\n", pass, fail); return fail ? 1 : 0;
  }
  if (mode == "baddir") {
    printf("F2 the settings folder cannot be written\n");
    gauge_config_init();
    CHECK(gauge_config_get_pages()[0].enabled, "boots with defaults when there is no file");
    CHECK(gauge_config_save() == false, "save returns false when the file cannot be created");
    CHECK(strlen(gauge_config_last_error()) > 0, "and says why");
    printf("\n%d passed, %d failed\n", pass, fail); return fail ? 1 : 0;
  }

  printf("S1 staleness timeout\n");
  g_fake_millis = 10000;
  CHECK(!gauge_config_signal_has_data(SIG_RPM), "no data before the first frame");
  g_rpm = 3000; g_rpm_has_data = true; gauge_config_mark_signal_active(SIG_RPM);
  CHECK(gauge_config_signal_has_data(SIG_RPM), "has data right after a frame");
  g_fake_millis += 1999; CHECK(gauge_config_signal_has_data(SIG_RPM), "still has data 1999ms later");
  g_fake_millis += 2;    CHECK(!gauge_config_signal_has_data(SIG_RPM), "LOST after 2001ms of silence");
  gauge_config_mark_signal_active(SIG_RPM); CHECK(gauge_config_signal_has_data(SIG_RPM), "back as soon as frames resume");
  g_fake_millis = 0xFFFFFF00u; gauge_config_mark_signal_active(SIG_RPM); g_fake_millis += 500;
  CHECK(gauge_config_signal_has_data(SIG_RPM), "correct across the 32-bit millis wrap");

  printf("S2 REGRESSION: settings survive a reboot (this failed on the board - every save was rejected by NVS)\n");
  wipe();
  gauge_config_init();
  CHECK(!exists(path("cfg_display.json")), "first boot: no file yet, defaults in use");
  PageConfig *p = gauge_config_get_pages();
  CHECK(p[0].cells[0].caption[0] == 'C', "default first caption is CLT");
  p[0].cells[0].caption[0] = 'X';
  CHECK(gauge_config_save() == true, "display save reports success");
  CHECK(exists(path("cfg_display.json")), "the file exists");
  size_t dispBytes = gauge_config_saved_bytes(0);
  printf("   default display config is %zu bytes as JSON\n", dispBytes);
  CHECK(dispBytes > 0 && dispBytes < 8000, "the default display config is a few KB now (it used to be 12KB: every empty page and cell was written)");
  gauge_config_init();                                       // "reboot"
  p = gauge_config_get_pages();
  CHECK(p[0].cells[0].caption[0] == 'X', "the edit is still there after a reboot");
  gauge_config_web_init();
  PageConfig *w = gauge_config_get_web_pages();
  w[0].cells[0].caption[0] = 'Y';
  CHECK(gauge_config_web_save() == true, "web save reports success");
  printf("   default web config is %zu bytes as JSON\n", gauge_config_saved_bytes(1));
  gauge_config_web_init();
  w = gauge_config_get_web_pages();
  CHECK(w[0].cells[0].caption[0] == 'Y', "the web edit survives a reboot");
  CHECK(strlen(gauge_config_last_error()) == 0, "no error recorded after good saves");

  printf("S3 worst case: all 8 pages x 4 cells, every field non-default\n");
  fillAll(p);
  std::string before = toJson(p);
  CHECK(gauge_config_save() == true, "saves");
  printf("   worst-case config is %zu bytes\n", gauge_config_saved_bytes(0));
  CHECK(gauge_config_saved_bytes(0) > 4000, "the worst case is far over NVS's 4000-byte limit - a config this size could never have been stored there");
  memset(p, 0, sizeof(PageConfig) * MAX_PAGES);
  gauge_config_init();
  p = gauge_config_get_pages();
  CHECK(toJson(p) == before, "reloads to exactly what was saved (all 32 cells, all fields)");
  CHECK(p[7].cells[3].enabled && p[7].cells[3].history_sec == 97 && p[7].cells[3].ref_value > 14.69f, "spot check: last cell of the last page");

  printf("S4 only enabled pages and the cells they use are written; deleted pages stay deleted\n");
  gauge_config_init(); p = gauge_config_get_pages();            // restore defaults
  wipe(); gauge_config_init(); p = gauge_config_get_pages();
  p[2].enabled = false;                                            // delete page 3 of the 7 defaults
  gauge_config_save();
  gauge_config_init(); p = gauge_config_get_pages();
  int enabledCount = 0; for (int i = 0; i < MAX_PAGES; i++) if (p[i].enabled) enabledCount++;
  CHECK(enabledCount == 6, "6 pages after deleting one of 7");
  CHECK(strncmp(p[2].cells[0].caption, "OIL PRESS", 9) == 0, "the page after the deleted one moved up");
  p[1].layout = LAYOUT_1CELL; p[1].cells[1].enabled = true;       // a stale second cell on a 1-cell page
  gauge_config_save();
  CHECK(readFile(path("cfg_display.json")).find("\"cells\":[{") != std::string::npos, "file looks like the editors' JSON");
  gauge_config_init(); p = gauge_config_get_pages();
  CHECK(p[1].cells[1].enabled == false, "a cell the layout doesn't use is not saved");

  printf("S5 the web-format JSON round-trips (what the editors send and receive)\n");
  fillAll(p);
  std::string web = toJson(p);
  for (const char *key : { "enabled","signal","gauge_type","min","max","rest","caption","needle_color","tick_color","bg_color","value_divisor","tick_count","major_nth","refresh_hz","show_value","value_font_size","tick_font_size","start_deg","end_deg","has_warning","warning_threshold","warning_above","alert_fullscreen","history_sec","ref_enabled","ref_value" })
    CHECK(web.find(std::string("\"") + key + "\":") != std::string::npos, (std::string("key present in the JSON: ") + key).c_str());
  PageConfig scratch[MAX_PAGES];
  { JsonDocument d; deserializeJson(d, web); gauge_config_pages_from_json(d.as<JsonArray>(), scratch); }
  CHECK(toJson(scratch) == web, "parsing then re-serializing gives the same JSON");
  { std::string one = "[{\"enabled\":true,\"layout\":0,\"cells\":[{\"enabled\":true,\"signal\":3,\"caption\":\"A\"}]}]";
    JsonDocument d; deserializeJson(d, one); gauge_config_pages_from_json(d.as<JsonArray>(), scratch); }
  CHECK(scratch[0].enabled && !scratch[1].enabled && !scratch[7].enabled, "pages not in the JSON are disabled");
  CHECK(!scratch[0].cells[1].enabled && !scratch[0].cells[3].enabled, "cells not in the JSON are cleared, not left over from before");

  printf("S6 hostile or broken values can never reach the display\n");
  std::string evil = "[{\"enabled\":true,\"layout\":99,\"cells\":[{\"enabled\":true,\"signal\":99,\"gauge_type\":77,\"min\":50,\"max\":10,"
                     "\"value_divisor\":0,\"tick_count\":0,\"major_nth\":0,\"refresh_hz\":0,\"history_sec\":99999,\"needle_color\":4294967295}]}]";
  { JsonDocument d; deserializeJson(d, evil); gauge_config_pages_from_json(d.as<JsonArray>(), scratch); }
  CellConfig &e = scratch[0].cells[0];
  CHECK(scratch[0].layout == LAYOUT_1CELL, "layout 99 becomes 1-cell");
  CHECK(e.signal == SIG_NONE && e.gauge_type == GAUGE_TYPE_NEEDLE, "signal 99 / gauge type 77 fall back to None / Needle");
  CHECK(e.max_val > e.min_val, "max below min is repaired");
  CHECK(e.value_divisor == 1.0f, "a zero divisor becomes 1 (no inf/NaN readings)");
  CHECK(e.tick_count >= 2 && e.major_nth >= 1 && e.refresh_hz >= 1, "zero tick count / major spacing / refresh rate are repaired (a zero major spacing divides by zero inside LVGL)");
  CHECK(e.history_sec == 600, "history 99999 is capped at 600");
  CHECK(e.needle_color <= 0xFFFFFF, "colors are masked to 24 bits");

  printf("S7 power loss and corruption\n");
  wipe(); gauge_config_init(); p = gauge_config_get_pages();
  p[0].cells[0].caption[0] = 'Q'; gauge_config_save();
  std::string good = readFile(path("cfg_display.json"));
  remove(path("cfg_display.json").c_str());                                   // crash after remove, before rename
  writeFile(path("cfg_display.json.tmp"), good);
  gauge_config_init(); p = gauge_config_get_pages();
  CHECK(p[0].cells[0].caption[0] == 'Q', "only the .tmp survived: it is recovered");
  writeFile(path("cfg_display.json"), "[{\"enabled\":tru");                  // main file cut off mid-write
  writeFile(path("cfg_display.json.tmp"), good);
  p[0].cells[0].caption[0] = '?'; gauge_config_init(); p = gauge_config_get_pages();
  CHECK(p[0].cells[0].caption[0] == 'Q', "corrupt main file: the valid .tmp is used");
  writeFile(path("cfg_display.json"), "not json at all"); writeFile(path("cfg_display.json.tmp"), "{{{");
  gauge_config_init(); p = gauge_config_get_pages();
  CHECK(p[0].enabled && p[0].cells[0].caption[0] == 'C', "both corrupt: boots on the defaults instead of crashing");
  writeFile(path("cfg_display.json"), "");
  remove(path("cfg_display.json.tmp").c_str());
  gauge_config_init();
  CHECK(gauge_config_get_pages()[0].enabled, "empty file: defaults");
  writeFile(path("cfg_display.json"), "[]");
  gauge_config_init();
  { int n = 0; for (int i = 0; i < MAX_PAGES; i++) if (gauge_config_get_pages()[i].enabled) n++;
    CHECK(n == 0, "a valid empty list means 'no pages' (the user deleted them all), not 'use defaults'"); }

  printf("S8 settings saved before the trend-graph and alert features load with sane defaults\n");
  writeFile(path("cfg_display.json"),
    "[{\"enabled\":true,\"layout\":0,\"cells\":[{\"enabled\":true,\"signal\":3,\"gauge_type\":3,\"min\":0,\"max\":250,\"rest\":0,\"caption\":\"MAP\"}]}]");
  gauge_config_init(); p = gauge_config_get_pages();
  CHECK(p[0].cells[0].gauge_type == GAUGE_TYPE_HISTOGRAM && p[0].cells[0].history_sec == 30, "trend graph, 30s default history");
  CHECK(!p[0].cells[0].ref_enabled && !p[0].cells[0].alert_fullscreen, "no reference line, no full-screen alert");

  printf("S9 the new text-size and dial-angle settings\n");
  wipe(); gauge_config_init(); p = gauge_config_get_pages();
  CHECK(p[0].cells[0].value_font_size == 28 && p[0].cells[0].tick_font_size == 28, "defaults: readout 28, dial numbers 28 (the look it always had)");
  CHECK(p[0].cells[0].start_deg == 225 && p[0].cells[0].end_deg == 135, "defaults: the standard 270 degree dial, 225 -> 135");
  p[0].cells[0].value_font_size = 96; p[0].cells[0].tick_font_size = -1; p[0].cells[0].start_deg = 200; p[0].cells[0].end_deg = 340;
  CHECK(gauge_config_save(), "saves");
  gauge_config_init(); p = gauge_config_get_pages();
  CHECK(p[0].cells[0].value_font_size == 96 && p[0].cells[0].tick_font_size == -1 && p[0].cells[0].start_deg == 200 && p[0].cells[0].end_deg == 340, "all of them survive a reboot");

  printf("S10 a config saved before these settings existed looks exactly as before\n");
  auto load1 = [&](const std::string &cellJson) -> CellConfig {
    std::string j = "[{\"enabled\":true,\"layout\":0,\"cells\":[" + cellJson + "]}]";
    JsonDocument d; deserializeJson(d, j); gauge_config_pages_from_json(d.as<JsonArray>(), scratch); return scratch[0].cells[0]; };
  CellConfig c10;
  c10 = load1("{\"enabled\":true,\"gauge_type\":0,\"value_font_size\":36}");
  CHECK(c10.tick_font_size == 36 && c10.value_font_size == 36, "dial numbers followed the old text size (36)");
  c10 = load1("{\"enabled\":true,\"gauge_type\":0,\"value_font_size\":14}");
  CHECK(c10.tick_font_size == 14, "... 14");
  c10 = load1("{\"enabled\":true,\"gauge_type\":0}");
  CHECK(c10.tick_font_size == 28 && c10.value_font_size == 28, "... and 28 when the size was never set");
  CHECK(c10.start_deg == 225 && c10.end_deg == 135, "a needle gauge without angles gets the 270 degree dial");
  c10 = load1("{\"enabled\":true,\"gauge_type\":2}");
  CHECK(c10.start_deg == 270 && c10.end_deg == 90, "a half moon without angles gets 270 -> 90 (flat side down)");

  printf("S11 bad text sizes and angles are repaired, not stored\n");
  c10 = load1("{\"enabled\":true,\"value_font_size\":50}");  CHECK(c10.value_font_size == 48, "50 snaps to 48");
  c10 = load1("{\"enabled\":true,\"value_font_size\":1000}"); CHECK(c10.value_font_size == 120, "1000 snaps to the largest, 120");
  c10 = load1("{\"enabled\":true,\"value_font_size\":-5}");   CHECK(c10.value_font_size == 14, "-5 snaps to the smallest, 14");
  c10 = load1("{\"enabled\":true,\"value_font_size\":64,\"tick_font_size\":99}"); CHECK(c10.tick_font_size == 28, "an invalid dial-number size becomes 28 (64 is too big for dial numbers)");
  c10 = load1("{\"enabled\":true,\"tick_font_size\":-1}");    CHECK(c10.tick_font_size == -1, "-1 (hide the dial numbers) is kept");
  c10 = load1("{\"enabled\":true,\"start_deg\":-135,\"end_deg\":135}"); CHECK(c10.start_deg == 225 && c10.end_deg == 135, "-135 is the same direction as 225");
  c10 = load1("{\"enabled\":true,\"start_deg\":400,\"end_deg\":-30}");  CHECK(c10.start_deg == 40 && c10.end_deg == 330, "400 and -30 wrap into 0..359");
  c10 = load1("{\"enabled\":true,\"start_deg\":90,\"end_deg\":90}");    CHECK(c10.start_deg == 225 && c10.end_deg == 135, "start = end is not a dial: the standard one is used");
  c10 = load1("{\"enabled\":true,\"start_deg\":100,\"end_deg\":105}");  CHECK(c10.start_deg == 225 && c10.end_deg == 135, "a 5 degree sliver is not a dial either");
  c10 = load1("{\"enabled\":true,\"gauge_type\":2,\"start_deg\":5,\"end_deg\":5}"); CHECK(c10.start_deg == 270 && c10.end_deg == 90, "... and for a half moon the half-moon default");
  c10 = load1("{\"enabled\":true,\"start_deg\":0,\"end_deg\":359}");    CHECK(c10.start_deg == 0 && c10.end_deg == 359, "a nearly full circle (359) is allowed");
  c10 = load1("{\"enabled\":true,\"start_deg\":0,\"end_deg\":10}");     CHECK(c10.start_deg == 0 && c10.end_deg == 10, "the narrowest allowed dial (10) is kept");
  CHECK(gauge_snap_value_size(36) == 36 && gauge_snap_value_size(42) == 36 && gauge_snap_value_size(43) == 48, "snapping goes to the NEAREST size");

  wipe();
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
