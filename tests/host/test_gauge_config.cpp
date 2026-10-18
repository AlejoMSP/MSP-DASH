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
#include "gauge_indicators.h"
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
volatile float g_afr, g_clt_f, g_oilp_kpa, g_oilt_f, g_fuelp_kpa, g_flex_pct, g_boost_kpa, g_rpm, g_tps, g_battv, g_egt_f, g_iat_f;
volatile bool g_afr_has_data, g_clt_has_data, g_oilp_has_data, g_oilt_has_data, g_fuelp_has_data,
              g_flex_has_data, g_boost_has_data, g_rpm_has_data, g_tps_has_data, g_battv_has_data, g_egt_has_data, g_iat_has_data;
volatile int g_requested_page = -1;
volatile int g_page_nav_delta = 0;
volatile uint16_t g_flag_bits = 0, g_flag_seen = 0;

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
      c.low_crit_on = (j % 2) == 0; c.low_crit = 10 + i; c.low_warn_on = (i % 2) == 0; c.low_warn = 30 + j; c.high_warn_on = (i + j) % 2 == 1; c.high_warn = 150 + i; c.high_crit_on = (i + j) % 3 == 0; c.high_crit = 200 + j; c.zone_style = 1 + (i + j) % 3;
      c.needle_hub_pct = 5 + j; c.needle_tip_pct = 3 + i;
      c.needle_w = 2 + j; c.text_xy = (i + j) % 2; c.cap_x = -100 - j; c.cap_y = 120 + i; c.val_x = 33; c.val_y = -44;
      c.caption_font_size = GAUGE_CAPTION_SIZES[(i + j) % GAUGE_CAPTION_SIZE_COUNT]; c.caption_on_face = (i + j) % 2 == 0; snprintf(c.units, GAUGE_UNITS_MAXLEN, "u%d%d", i, j); c.v_align = (i + j) % 3;
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
  for (const char *key : { "enabled","signal","gauge_type","min","max","rest","caption","needle_color","tick_color","bg_color","value_divisor","tick_count","major_nth","refresh_hz","show_value","value_font_size","tick_font_size","start_deg","end_deg","low_crit_on","low_crit","low_warn_on","low_warn","high_warn_on","high_warn","high_crit_on","high_crit","zone_style","needle_hub_pct","needle_tip_pct","caption_font_size","caption_on_face","units","v_align","has_warning","warning_threshold","warning_above","alert_fullscreen","history_sec","ref_enabled","ref_value" })
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

  printf("S12 warning zones and needle trim\n");
  wipe(); gauge_config_init(); p = gauge_config_get_pages();
  { CellConfig &d = p[0].cells[0];
    CHECK(!d.low_crit_on && !d.low_warn_on && !d.high_warn_on && !d.high_crit_on && d.zone_style == GAUGE_ZONE_BANDS, "defaults: no warning zones, and coloured bands if you switch them on"); }
  CHECK(p[0].cells[0].needle_hub_pct == 0 && p[0].cells[0].needle_tip_pct == 0, "defaults: the normal needle (no trim)");
  { CellConfig &z0 = p[0].cells[0];
    z0.low_crit_on = true; z0.low_crit = 20; z0.low_warn_on = true; z0.low_warn = 40; z0.high_warn_on = true; z0.high_warn = 180; z0.high_crit_on = true; z0.high_crit = 220;
    z0.zone_style = 3; z0.needle_hub_pct = 12; z0.needle_tip_pct = 8; }
  CHECK(gauge_config_save(), "saves");
  gauge_config_init(); p = gauge_config_get_pages();
  CellConfig &z = p[0].cells[0];
  CHECK(z.low_crit_on && z.low_crit == 20 && z.low_warn_on && z.low_warn == 40 && z.high_warn_on && z.high_warn == 180 && z.high_crit_on && z.high_crit == 220 && z.zone_style == 3 && z.needle_hub_pct == 12 && z.needle_tip_pct == 8, "all four thresholds, the style and the needle trim survive a reboot");
  c10 = load1("{\"enabled\":true,\"gauge_type\":0}");
  CHECK(!c10.low_crit_on && !c10.high_crit_on && c10.zone_style == GAUGE_ZONE_BANDS && c10.needle_hub_pct == 0 && c10.needle_tip_pct == 0, "a config saved before these existed: no zones, normal needle");
  c10 = load1("{\"enabled\":true,\"zone_style\":1,\"low_warn_on\":true,\"low_warn\":-40,\"high_crit\":300}");
  CHECK(c10.low_warn_on && c10.low_warn == -40 && !c10.high_crit_on && c10.high_crit == 300, "a threshold's value is kept even while it is switched off; negative values are fine");
  c10 = load1("{\"enabled\":true,\"zone_style\":0}"); CHECK(c10.zone_style == 1, "style 0 -> bands");
  c10 = load1("{\"enabled\":true,\"zone_style\":9}"); CHECK(c10.zone_style == 1, "style 9 -> bands");
  c10 = load1("{\"enabled\":true,\"zone_style\":2}"); CHECK(c10.zone_style == 2, "style 2 (tick marks) is kept");

  printf("S12b zones saved by build 108 (one yellow and one red on one side, drawn as ticks) look the same afterwards\n");
  c10 = load1("{\"enabled\":true,\"yellow_enabled\":true,\"yellow_value\":200,\"red_enabled\":true,\"red_value\":230,\"zones_low\":false}");
  CHECK(c10.high_warn_on && c10.high_warn == 200 && c10.high_crit_on && c10.high_crit == 230 && !c10.low_warn_on && !c10.low_crit_on, "high end: yellow 200 becomes HIGH WARNING 200, red 230 becomes HIGH CRITICAL 230");
  CHECK(c10.zone_style == GAUGE_ZONE_TICKS, "... still drawn as coloured tick marks, as it was");
  c10 = load1("{\"enabled\":true,\"yellow_enabled\":true,\"yellow_value\":40,\"red_enabled\":true,\"red_value\":20,\"zones_low\":true}");
  CHECK(c10.low_warn_on && c10.low_warn == 40 && c10.low_crit_on && c10.low_crit == 20 && !c10.high_warn_on && !c10.high_crit_on, "low end: yellow 40 becomes LOW WARNING 40, red 20 becomes LOW CRITICAL 20");
  c10 = load1("{\"enabled\":true,\"yellow_enabled\":false,\"yellow_value\":200,\"red_enabled\":true,\"red_value\":230}");
  CHECK(!c10.high_warn_on && c10.high_crit_on && c10.high_crit == 230, "only red was on: only HIGH CRITICAL is on");
  c10 = load1("{\"enabled\":true,\"zone_style\":1,\"yellow_enabled\":true,\"yellow_value\":5,\"high_warn_on\":true,\"high_warn\":99}");
  CHECK(c10.high_warn_on && c10.high_warn == 99 && c10.zone_style == 1, "if the NEW keys are there the old ones are ignored (no double conversion)");
  { wipe(); gauge_config_init(); p = gauge_config_get_pages(); p[0].cells[0].high_crit_on = true; p[0].cells[0].high_crit = 222; gauge_config_save();
    JsonDocument d; gauge_config_pages_to_json(p, d); std::string js; serializeJson(d, js);
    CHECK(js.find("yellow_enabled") == std::string::npos && js.find("zones_low") == std::string::npos && js.find("\"high_crit\":222") != std::string::npos, "saving writes only the new keys; the old ones are gone from the file"); }
  c10 = load1("{\"enabled\":true,\"needle_hub_pct\":-5,\"needle_tip_pct\":999}");
  CHECK(c10.needle_hub_pct == 0 && c10.needle_tip_pct == 60, "trim is clamped to 0..60 each (-5 -> 0, 999 -> 60)");
  c10 = load1("{\"enabled\":true,\"needle_hub_pct\":60,\"needle_tip_pct\":60}");
  CHECK(c10.needle_hub_pct == 60 && c10.needle_tip_pct == 25, "hub 60 + tip 60 is too much: the hub gap wins, the tip gives way to 25 (total 85)");
  c10 = load1("{\"enabled\":true,\"needle_hub_pct\":40,\"needle_tip_pct\":45}");
  CHECK(c10.needle_hub_pct == 40 && c10.needle_tip_pct == 45, "40 + 45 = 85 is allowed exactly");
  c10 = load1("{\"enabled\":true,\"needle_hub_pct\":41,\"needle_tip_pct\":45}");
  CHECK(c10.needle_hub_pct == 41 && c10.needle_tip_pct == 44, "41 + 45 = 86: the tip gives back 1");

  printf("S13 the ECU on/off flags as signals\n");
  {
    static const char *NAMES[9] = { "Rev Limiter Active", "Main Relay", "Fuel Pump", "Check Engine Light", "O2 Heater", "Lambda Protection", "Fan 1", "Fan 2", "Brake Pedal" };
    bool namesOk = true; for (int i = 0; i < 9; i++) if (strcmp(gauge_config_signal_name((SignalId)(SIG_FIRST_FLAG + i)), NAMES[i]) != 0) namesOk = false;
    CHECK(namesOk && SIG_COUNT == 22 && SIG_FLAG_COUNT == 9, "nine flag signals with readable names (22 signals in all)");
    CHECK(!gauge_signal_is_flag(SIG_BATTV) && gauge_signal_is_flag(SIG_REVLIM) && gauge_signal_is_flag(SIG_BRAKE) && !gauge_signal_is_flag(SIG_COUNT) && !gauge_signal_is_flag(SIG_EGT) && !gauge_signal_is_flag(SIG_IAT) && !gauge_signal_is_flag(0), "which signals are flags");
    g_fake_millis = 50000; g_flag_bits = 0; g_flag_seen = 0;
    CHECK(!gauge_config_signal_has_data(SIG_FAN) && gauge_config_signal_value(SIG_FAN) == 0.0f, "before any frame: no data");
    g_flag_bits = (1u << (SIG_FAN - SIG_FIRST_FLAG)); g_flag_seen = 0x00FF; for (int i = 0; i < 8; i++) gauge_config_mark_signal_active((SignalId)(SIG_FIRST_FLAG + i));
    CHECK(gauge_config_signal_has_data(SIG_FAN) && gauge_config_signal_value(SIG_FAN) == 1.0f, "fan on: has data, value 1");
    CHECK(gauge_config_signal_has_data(SIG_CEL) && gauge_config_signal_value(SIG_CEL) == 0.0f, "check engine off: has data (it was reported), value 0 - 'off' is not 'no data'");
    CHECK(!gauge_config_signal_has_data(SIG_BRAKE), "the brake flag has not been reported yet: no data (it comes in a different frame)");
    g_fake_millis += 2001;
    CHECK(!gauge_config_signal_has_data(SIG_FAN), "2 seconds without a frame: the flag goes stale like any other signal");
    gauge_config_mark_signal_active(SIG_FAN);
    CHECK(gauge_config_signal_has_data(SIG_FAN), "and comes back with the next frame");
    g_flag_bits = 0xFFFF; CHECK(gauge_config_signal_value(SIG_BRAKE) == 1.0f && gauge_config_signal_value(SIG_REVLIM) == 1.0f, "every bit maps to its own signal");
    g_flag_bits = 0; g_flag_seen = 0;
  }
  printf("S14 log channels: flags are NOT logged unless you ask\n");
  {
    gauge_config_log_init();
    bool analogOn = true; for (int sg = 1; sg < SIG_FIRST_FLAG; sg++) if (!gauge_config_is_log_enabled((SignalId)sg)) analogOn = false;
    bool flagsOff = true; for (int sg = SIG_FIRST_FLAG; sg < SIG_FLAG_END; sg++) if (gauge_config_is_log_enabled((SignalId)sg)) flagsOff = false;
    CHECK(analogOn, "the ten analog signals are still logged by default (the CSV columns people use do not change)");
    CHECK(flagsOff, "the nine flags default to not logged");
    gauge_config_set_log_enabled(SIG_FAN, true); CHECK(gauge_config_is_log_enabled(SIG_FAN), "but a flag can be switched on");
    gauge_config_log_save(); gauge_config_set_log_enabled(SIG_FAN, false); gauge_config_log_init();
    CHECK(gauge_config_is_log_enabled(SIG_FAN), "and that choice is remembered");
  }

  printf("S15 indicator pages\n");
  {
    auto fillLamp = [](IndicatorConfig &l, int seed) {
      memset(&l, 0, sizeof(l));
      l.signal = (SignalId)(1 + seed % (SIG_COUNT - 1)); l.op = seed % 2; l.threshold = 10.5f + seed; l.hysteresis = 0.25f * (seed % 5);
      snprintf(l.on_text, IND_TEXT_MAXLEN, "ON %d", seed); snprintf(l.off_text, IND_TEXT_MAXLEN, "OFF %d", seed);
      l.on_color = 0x100000 + seed * 4097; l.off_color = 0x202020 + seed * 17; l.on_text_color = 0xF0F000 + seed; l.off_text_color = 0x707070 + seed; l.blink = (seed % 3) == 0;
    };
    wipe(); gauge_config_init(); p = gauge_config_get_pages();
    bool allGauge = true; for (int i = 0; i < MAX_PAGES; i++) if (p[i].kind != PAGE_GAUGES || p[i].ind_count != 0) allGauge = false;
    CHECK(allGauge, "the default pages are all gauge pages");

    // a page of 5 lamps, then a gauge page, then a page of 12 lamps
    p[0].enabled = true; p[0].kind = PAGE_INDICATORS; p[0].ind_cols = 3; p[0].ind_count = 5; for (int k = 0; k < 5; k++) fillLamp(p[0].indicators[k], k);
    PageConfig keepGauge = p[1];   // an ordinary default gauge page
    p[2].enabled = true; p[2].kind = PAGE_INDICATORS; p[2].ind_cols = 4; p[2].ind_count = 12; for (int k = 0; k < 12; k++) fillLamp(p[2].indicators[k], 20 + k);
    CHECK(gauge_config_save(), "saves");
    gauge_config_init(); p = gauge_config_get_pages();
    CHECK(p[0].kind == PAGE_INDICATORS && p[0].ind_cols == 3 && p[0].ind_count == 5, "page 1: an indicator page, 3 columns, 5 lamps");
    bool lampsOk = true;
    for (int k = 0; k < 5; k++) { IndicatorConfig e; fillLamp(e, k); if (memcmp(&e, &p[0].indicators[k], sizeof(e)) != 0) { if (e.signal != p[0].indicators[k].signal || e.op != p[0].indicators[k].op || e.threshold != p[0].indicators[k].threshold || e.hysteresis != p[0].indicators[k].hysteresis || strcmp(e.on_text, p[0].indicators[k].on_text) || strcmp(e.off_text, p[0].indicators[k].off_text) || e.on_color != p[0].indicators[k].on_color || e.off_color != p[0].indicators[k].off_color || e.on_text_color != p[0].indicators[k].on_text_color || e.off_text_color != p[0].indicators[k].off_text_color || e.blink != p[0].indicators[k].blink) lampsOk = false; } }
    CHECK(lampsOk, "every field of every lamp survives a reboot (signal, condition, threshold, hysteresis, texts, four colours, blink)");
    CHECK(p[1].kind == PAGE_GAUGES && p[1].layout == keepGauge.layout && strcmp(p[1].cells[0].caption, keepGauge.cells[0].caption) == 0, "page 2 is still an ordinary gauge page, untouched");
    CHECK(p[2].kind == PAGE_INDICATORS && p[2].ind_cols == 4 && p[2].ind_count == 12 && strcmp(p[2].indicators[11].on_text, "ON 31") == 0, "page 3: 12 lamps, the last one intact");

    // what the editors receive
    { JsonDocument d; gauge_config_pages_to_json(p, d); std::string js; serializeJson(d, js);
      JsonArray arr = d.as<JsonArray>();
      CHECK(arr[0]["kind"] == 1 && arr[0]["cols"] == 3 && arr[0]["indicators"].size() == 5, "an indicator page is sent with kind 1, its columns and its lamps");
      CHECK(arr[0]["cells"].isNull() && arr[0]["layout"].isNull(), "... and without a layout or cells");
      CHECK(arr[1]["kind"].isNull() && !arr[1]["cells"].isNull(), "a gauge page carries no 'kind' (so the JSON older tools know is unchanged)");
      CHECK(arr[0]["indicators"][0]["on_text"] == "ON 0" && arr[0]["indicators"][0]["threshold"] == 10.5, "lamp fields are named plainly"); }

    printf("S16 pages saved before indicator pages existed\n");
    JsonDocument d1; deserializeJson(d1, "[{\"enabled\":true,\"layout\":1,\"cells\":[{\"enabled\":true,\"signal\":3}]}]");
    gauge_config_pages_from_json(d1.as<JsonArray>(), scratch);
    CHECK(scratch[0].kind == PAGE_GAUGES && scratch[0].layout == LAYOUT_2UP && scratch[0].ind_count == 0, "no 'kind' means a gauge page");

    printf("S17 bad indicator data is repaired, not stored\n");
    auto lamp1 = [&](const std::string &lampJson, int cols = 2) -> IndicatorConfig {
      std::string j = "[{\"enabled\":true,\"kind\":1,\"cols\":" + std::to_string(cols) + ",\"indicators\":[" + lampJson + "]}]";
      JsonDocument d; deserializeJson(d, j); gauge_config_pages_from_json(d.as<JsonArray>(), scratch); return scratch[0].indicators[0]; };
    IndicatorConfig L = lamp1("{}");
    CHECK(scratch[0].ind_count == 1 && L.signal == SIG_NONE && L.op == GAUGE_IND_ABOVE && L.threshold == 0 && L.hysteresis == 0 && L.on_text[0] == 0 && !L.blink, "an empty lamp gets safe defaults");
    CHECK(L.on_color == 0xE53935 && L.off_color == 0x2A2A2A && L.on_text_color == 0xFFFFFF && L.off_text_color == 0x808080, "... and readable colours: red when on, dark grey when off");
    L = lamp1("{\"signal\":99,\"op\":5,\"hysteresis\":-3}"); CHECK(L.signal == SIG_NONE && L.op == GAUGE_IND_ABOVE && L.hysteresis == 0, "signal 99 -> none, op 5 -> above, negative hysteresis -> 0");
    L = lamp1("{\"signal\":-4}"); CHECK(L.signal == SIG_NONE, "a negative signal -> none");
    L = lamp1("{\"op\":1}"); CHECK(L.op == GAUGE_IND_BELOW, "op 1 stays 'below'");
    L = lamp1("{\"on_text\":\"ABCDEFGHIJKLMNOP\"}"); CHECK(strcmp(L.on_text, "ABCDEFGHIJK") == 0, "text is cut to 11 characters");
    L = lamp1("{\"on_text\":\"caf\\u00e9 \\u0007OK\\u2603\"}"); CHECK(strcmp(L.on_text, "caf OK") == 0, "accents, control characters and symbols the fonts cannot draw are dropped");
    L = lamp1("{\"on_color\":4294967295,\"off_color\":16777216,\"on_text_color\":-1}"); CHECK(L.on_color == 0xFFFFFF && L.off_color == 0 && L.on_text_color == 0xFFFFFF, "colours are masked to 24 bits");
    L = lamp1("{\"threshold\":null,\"hysteresis\":null}"); CHECK(L.threshold == 0 && L.hysteresis == 0, "null numbers become 0");
    lamp1("{}", 0); CHECK(scratch[0].ind_cols == 1, "columns 0 -> 1");
    lamp1("{}", 9); CHECK(scratch[0].ind_cols == 4, "columns 9 -> 4");
    { JsonDocument d; deserializeJson(d, "[{\"enabled\":true,\"kind\":1,\"indicators\":[{}]}]"); gauge_config_pages_from_json(d.as<JsonArray>(), scratch); CHECK(scratch[0].ind_cols == 2, "columns left out -> 2"); }
    { JsonDocument d; deserializeJson(d, "[{\"enabled\":true,\"kind\":7,\"layout\":3,\"cells\":[]}]"); gauge_config_pages_from_json(d.as<JsonArray>(), scratch); CHECK(scratch[0].kind == PAGE_GAUGES && scratch[0].layout == LAYOUT_2X2, "an unknown kind is a gauge page"); }
    { std::string j = "[{\"enabled\":true,\"kind\":1,\"cols\":3,\"indicators\":["; for (int k = 0; k < 20; k++) j += std::string(k ? "," : "") + "{\"on_text\":\"L" + std::to_string(k) + "\"}"; j += "]}]";
      JsonDocument d; deserializeJson(d, j); gauge_config_pages_from_json(d.as<JsonArray>(), scratch);
      CHECK(scratch[0].ind_count == 12 && strcmp(scratch[0].indicators[11].on_text, "L11") == 0, "20 lamps are cut to 12"); }
    { JsonDocument d; deserializeJson(d, "[{\"enabled\":true,\"kind\":1,\"cols\":2,\"indicators\":[{\"on_text\":\"X\"}]},{\"enabled\":true,\"layout\":0,\"cells\":[{\"enabled\":true}]}]");
      gauge_config_pages_from_json(d.as<JsonArray>(), scratch);
      CHECK(scratch[0].kind == PAGE_INDICATORS && scratch[1].kind == PAGE_GAUGES && scratch[1].ind_count == 0, "mixed pages come out in order, each its own kind"); }
    { JsonDocument d; deserializeJson(d, "[{\"enabled\":true,\"kind\":1,\"indicators\":[{\"on_text\":\"X\"},{\"on_text\":\"Y\"}]}]"); gauge_config_pages_from_json(d.as<JsonArray>(), scratch);
      JsonDocument d2; deserializeJson(d2, "[{\"enabled\":true,\"layout\":0,\"cells\":[{\"enabled\":true}]}]"); gauge_config_pages_from_json(d2.as<JsonArray>(), scratch);
      CHECK(scratch[0].kind == PAGE_GAUGES && scratch[0].ind_count == 0 && scratch[0].indicators[0].on_text[0] == 0, "replacing an indicator page with a gauge page leaves none of the old lamps behind"); }

    printf("S18 the biggest possible configuration\n");
    wipe(); gauge_config_init(); p = gauge_config_get_pages();
    for (int pg = 0; pg < MAX_PAGES; pg++) {
      p[pg].enabled = true; p[pg].kind = PAGE_INDICATORS; p[pg].ind_cols = 1 + pg % 4; p[pg].ind_count = 12;
      for (int k = 0; k < 12; k++) { fillLamp(p[pg].indicators[k], pg * 12 + k); snprintf(p[pg].indicators[k].on_text, IND_TEXT_MAXLEN, "ABCDEFGHIJK"); snprintf(p[pg].indicators[k].off_text, IND_TEXT_MAXLEN, "abcdefghijk"); p[pg].indicators[k].threshold = 12345.678f; }
    }
    std::string before = toJson(p);
    CHECK(gauge_config_save(), "8 pages of 12 lamps saves");
    size_t big = gauge_config_saved_bytes(0);
    printf("   8 indicator pages x 12 lamps = %zu bytes\n", big);
    CHECK(big < 32768, "it fits in the 32KB the web page may POST (and the 64KB file limit)");
    memset(p, 0, sizeof(PageConfig) * MAX_PAGES); gauge_config_init(); p = gauge_config_get_pages();
    CHECK(toJson(p) == before, "and reloads exactly as saved");
  }

  printf("S19 caption size, caption on the dial face, units, gauge position, default scales\n");
  wipe(); gauge_config_init(); p = gauge_config_get_pages();
  { CellConfig &d = p[0].cells[0];
    CHECK(d.caption_font_size == 0 && !d.caption_on_face && d.units[0] == 0 && d.v_align == 0, "defaults: caption in the display's default font, at the bottom, no units, centred"); }
  { CellConfig &z = p[0].cells[0]; z.caption_font_size = 36; z.caption_on_face = true; strcpy(z.units, "kPa"); z.v_align = 2; }
  CHECK(gauge_config_save(), "saves");
  gauge_config_init(); p = gauge_config_get_pages();
  { CellConfig &z = p[0].cells[0]; CHECK(z.caption_font_size == 36 && z.caption_on_face && strcmp(z.units, "kPa") == 0 && z.v_align == 2, "all four survive a reboot"); }
  c10 = load1("{\"enabled\":true,\"gauge_type\":0}");
  CHECK(c10.caption_font_size == 0 && !c10.caption_on_face && c10.units[0] == 0 && c10.v_align == 0, "a config saved before these existed looks exactly as before");
  c10 = load1("{\"enabled\":true,\"caption_font_size\":22}"); CHECK(c10.caption_font_size == 20, "caption size 22 snaps to 20");
  c10 = load1("{\"enabled\":true,\"caption_font_size\":999}"); CHECK(c10.caption_font_size == 48, "999 snaps to 48");
  c10 = load1("{\"enabled\":true,\"caption_font_size\":-5}"); CHECK(c10.caption_font_size == 0, "a negative size means 'default'");
  { wipe(); gauge_config_init(); p = gauge_config_get_pages();
    bool allOne = true; for (int pg = 0; pg < MAX_PAGES; pg++) for (int ci = 0; ci < MAX_CELLS_PER_PAGE; ci++) if (p[pg].enabled && p[pg].cells[ci].enabled && p[pg].cells[ci].decimals != 1) allOne = false;
    CHECK(allOne, "decimals: every default gauge shows 1 decimal, as before");
    for (int want : {0, 2}) { p[0].cells[0].decimals = want; CHECK(gauge_config_save(), "saves");
      gauge_config_init(); p = gauge_config_get_pages();
      CHECK(p[0].cells[0].decimals == want, want ? "decimals 2 survives a reboot" : "decimals 0 survives a reboot"); } }
  c10 = load1("{\"enabled\":true}"); CHECK(c10.decimals == 1, "a config saved before Decimals existed shows 1");
  c10 = load1("{\"enabled\":true,\"decimals\":5}"); CHECK(c10.decimals == 1, "an unknown decimals value is 1");
  c10 = load1("{\"enabled\":true,\"decimals\":-2}"); CHECK(c10.decimals == 1, "a negative decimals value is 1");
  c10 = load1("{\"enabled\":true,\"decimals\":0}"); CHECK(c10.decimals == 0, "0 is kept");
  c10 = load1("{\"enabled\":true}"); CHECK(c10.h_align == 0, "a config saved before text alignment existed is centred");
  c10 = load1("{\"enabled\":true,\"h_align\":2}"); CHECK(c10.h_align == 2, "right is kept");
  c10 = load1("{\"enabled\":true,\"h_align\":9}"); CHECK(c10.h_align == 0, "an unknown alignment is centred");
  c10 = load1("{\"enabled\":true}"); CHECK(c10.needle_w == 6 && !c10.text_xy && c10.cap_y == 0, "old configs: 6 px needle, automatic text");
  c10 = load1("{\"enabled\":true,\"needle_w\":12,\"text_xy\":true,\"cap_x\":-50,\"cap_y\":400,\"val_x\":7,\"val_y\":-9}");
  CHECK(c10.needle_w == 12 && c10.text_xy && c10.cap_x == -50 && c10.cap_y == 300 && c10.val_x == 7 && c10.val_y == -9, "needle width and coordinates load, kept within -300..300");
  c10 = load1("{\"enabled\":true,\"needle_w\":0}"); CHECK(c10.needle_w == 6, "a 0 px needle is 6");
  c10 = load1("{\"enabled\":true,\"v_align\":7}"); CHECK(c10.v_align == 0, "an unknown position is centre");
  c10 = load1("{\"enabled\":true,\"v_align\":1}"); CHECK(c10.v_align == 1, "top is kept");
  c10 = load1("{\"enabled\":true,\"units\":\"ABCDEFGHIJK\"}"); CHECK(strcmp(c10.units, "ABCDEFG") == 0, "units are cut to 7 characters");
  c10 = load1("{\"enabled\":true,\"units\":\"\\u00b0F \\u0007kPa\"}"); CHECK(strcmp(c10.units, "F kPa") == 0, "the degree sign and control characters (which the fonts cannot draw) are dropped");
  { wipe(); gauge_config_init(); p = gauge_config_get_pages();
    auto first = [&](SignalId sig) -> const CellConfig * { for (int i = 0; i < MAX_PAGES; i++) if (p[i].enabled && p[i].kind == PAGE_GAUGES && p[i].cells[0].signal == sig) return &p[i].cells[0]; return nullptr; };
    const CellConfig *clt = first(SIG_CLT), *oilt = first(SIG_OILT), *afr = first(SIG_AFR), *oilp = first(SIG_OILP), *fuelp = first(SIG_FUELP), *map = first(SIG_MAP);
    CHECK(clt && clt->min_val == 60 && clt->max_val == 250 && clt->rest_val == 60, "the default coolant gauge is 60 to 250 (it was 65)");
    CHECK(oilt && oilt->min_val == 60 && oilt->max_val == 250, "the default oil temperature gauge is 60 to 250");
    CHECK(afr && afr->min_val == 10 && afr->max_val == 20, "the default AFR gauge is 10 to 20");
    CHECK(oilp && oilp->min_val == 0 && oilp->max_val == 100 && fuelp && fuelp->min_val == 0 && fuelp->max_val == 100, "the default oil and fuel pressure gauges are 0 to 100");
    CHECK(map && map->min_val == 0 && map->max_val == 250 && map->rest_val == 100, "the default MAP/boost gauge keeps 0 to 250 (boost reads above 100)");
    PageConfig *w = gauge_config_get_web_pages(); gauge_config_web_init(); w = gauge_config_get_web_pages();
    bool webClt = false; for (int i = 0; i < 4; i++) if (w[0].cells[i].signal == SIG_CLT) webClt = w[0].cells[i].min_val == 60;
    CHECK(webClt, "the default web dashboard's coolant number uses 60 too"); }

  wipe();
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
