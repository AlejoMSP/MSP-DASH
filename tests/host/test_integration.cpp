// End-to-end: REAL simulator.cpp -> REAL gauge_config.cpp (defaults, staleness) -> REAL gauge_app.c -> LVGL, on a PC.
#include "Arduino.h"
#include "Preferences.h"
#include "FFat.h"
#include "lvgl.h"
#include "gauge_config.h"
#include "gauge_app.h"
#include "simulator.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// ---- platform stand-ins ----
uint32_t g_fake_millis = 0;
uint32_t millis() { return g_fake_millis; }
void delay(uint32_t) {}
SerialClass Serial;
bool g_ffat_mounts = true;
FFatClass FFat;
void *heap_caps_malloc(size_t n, int) { return malloc(n); }
extern "C" bool sd_logging_is_active(void) { return false; }

// ---- display capture ----
static uint8_t fb[466 * 466 * 3];
static lv_color_t dbuf[466 * 466];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static void flush_cb(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *p) {
  for (int y = a->y1; y <= a->y2; y++) for (int x = a->x1; x <= a->x2; x++) {
    uint32_t c = lv_color_to32(*p++); uint8_t *o = &fb[(y * 466 + x) * 3]; o[0] = (c >> 16) & 0xFF; o[1] = (c >> 8) & 0xFF; o[2] = c & 0xFF; }
  lv_disp_flush_ready(d);
}
static void save(const char *name) { FILE *f = fopen(name, "wb"); fwrite(fb, 1, sizeof(fb), f); fclose(f); }

// one simulated millisecond-step: the clock, LVGL, and the simulator loop all advance together
static void run_ms(int ms) { for (int t = 0; t < ms; t += 5) { g_fake_millis += 5; sim_loop(); lv_tick_inc(5); lv_timer_handler(); } }

static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)

static lv_obj_t *sys_label(const char *text) {
  lv_obj_t *sys = lv_layer_sys();
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(sys); i++) { lv_obj_t *c = lv_obj_get_child(sys, i); if (lv_obj_check_type(c, &lv_label_class) && strcmp(lv_label_get_text(c), text) == 0) return c; }
  return NULL;
}
static bool visible(lv_obj_t *o) { return o && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); }
static lv_obj_t *overlay() { lv_obj_t *sys = lv_layer_sys(); for (uint32_t i = 0; i < lv_obj_get_child_cnt(sys); i++) { lv_obj_t *c = lv_obj_get_child(sys, i); if (lv_obj_get_width(c) >= 400) return c; } return NULL; }
static lv_obj_t *find_class(lv_obj_t *o, const lv_obj_class_t *cls) { if (lv_obj_check_type(o, cls)) return o; for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) { lv_obj_t *r = find_class(lv_obj_get_child(o, i), cls); if (r) return r; } return NULL; }
static lv_obj_t *find_label_with(lv_obj_t *o, const char *needle) { if (lv_obj_check_type(o, &lv_label_class) && strstr(lv_label_get_text(o), needle)) return o; for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) { lv_obj_t *r = find_label_with(lv_obj_get_child(o, i), needle); if (r) return r; } return NULL; }

int main() {
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, dbuf, NULL, 466 * 466);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 466; disp_drv.ver_res = 466; disp_drv.flush_cb = flush_cb; disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // REAL defaults (CLT, MAP, AFR, OIL PRESS, OIL TEMP, FUEL PRESS, ETH%), plus one extra page and the oil-pressure alert switched on
  g_fake_millis = 100000;
  gauge_config_init();
  PageConfig *p = gauge_config_get_pages();
  p[3].cells[0].alert_fullscreen = true;                                      // OIL PRESS: warn below 40, now also full-screen
  p[7].enabled = true; p[7].layout = LAYOUT_2UP;                              // an 8th page: RPM trend + coolant number
  p[7].cells[0] = p[0].cells[0]; p[7].cells[0].signal = SIG_RPM; p[7].cells[0].gauge_type = GAUGE_TYPE_HISTOGRAM;
  strcpy(p[7].cells[0].caption, "RPM"); p[7].cells[0].min_val = 0; p[7].cells[0].max_val = 7000; p[7].cells[0].history_sec = 30; p[7].cells[0].has_warning = false;
  p[7].cells[1] = p[0].cells[0]; p[7].cells[1].signal = SIG_CLT; p[7].cells[1].gauge_type = GAUGE_TYPE_NUMBER; strcpy(p[7].cells[1].caption, "CLT"); p[7].cells[1].has_warning = false;
  build_gauge_app();
  int pages = gauge_app_get_num_pages();
  run_ms(200);

  printf("E1 boot: no ECU, simulator off\n");
  CHECK(!sim_is_active(), "simulator is OFF after boot");
  CHECK(!gauge_config_signal_has_data(SIG_RPM), "no signal has data");
  CHECK(!visible(sys_label("SIMULATED")) && !visible(overlay()), "no banner, no alert");

  printf("E2 start the demo: data flows into the real gauge pipeline\n");
  sim_start(false); run_ms(1000);
  CHECK(sim_is_active() && visible(sys_label("SIMULATED")), "simulator on, SIMULATED banner visible on the round screen");
  bool all = true; for (int s = 1; s < SIG_COUNT; s++) if (!gauge_config_signal_has_data((SignalId)s)) all = false;
  CHECK(all, "all ten signals have live data through the real staleness check");
  g_page_nav_delta = pages - 1; run_ms(200);                                   // jump to the extra page
  float rpmMin = 1e9, rpmMax = -1e9; bool alertEver = false;
  for (int i = 0; i < 1200; i++) {                                              // 60 simulated seconds, sampled every 50ms
    run_ms(50);
    float r = gauge_config_signal_value(SIG_RPM); if (r < rpmMin) rpmMin = r; if (r > rpmMax) rpmMax = r;
    if (visible(overlay())) alertEver = true;
  }
  printf("   RPM seen by the gauges: %.0f .. %.0f\n", rpmMin, rpmMax);
  CHECK(rpmMin < 900 && rpmMax > 6000, "the gauges saw a full rev cycle (idle to ~6200)");
  CHECK(!alertEver, "the demo NEVER raised an alert, even with the oil-pressure alert switched on");
  CHECK(sim_is_active(), "still running after a minute (timeout is 10)");
  lv_obj_t *scr = lv_scr_act();
  lv_obj_t *chart = find_class(scr, &lv_chart_class);
  CHECK(chart != NULL, "the trend graph page is showing");
  lv_chart_series_t *ser = lv_chart_get_series_next(chart, NULL);
  lv_coord_t *y = lv_chart_get_y_array(chart, ser); int filled = 0; lv_coord_t lo = 32767, hi = -1;
  for (int i = 0; i < 100; i++) if (y[i] != LV_CHART_POINT_NONE) { filled++; if (y[i] < lo) lo = y[i]; if (y[i] > hi) hi = y[i]; }
  printf("   trend graph: %d of 100 points, chart values %d .. %d (of 7000*scale)\n", filled, lo, hi);
  CHECK(filled == 100, "the 30s trend graph is full after 60s of demo");
  CHECK(hi - lo > 3000, "and shows the rev cycle's swing, not a flat line");
  CHECK(find_label_with(scr, "NO DATA") && !visible(find_label_with(scr, "NO DATA")), "NO DATA label hidden while data flows");
  lv_obj_t *cltLabel = NULL; { lv_obj_t *l = find_label_with(scr, "."); if (l && strcmp(lv_label_get_text(l), "NO DATA") != 0) cltLabel = l; }
  CHECK(cltLabel != NULL, "the coolant number cell shows a live value, not --");
  save("e2_demo_graph.rgb");

  printf("E3 manual mode: set a bad oil pressure -> the full-screen alert fires over this page\n");
  sim_stop("test"); sim_start(true); run_ms(500);
  sim_set_manual(SIG_RPM, 3000); sim_set_manual(SIG_OILP, 90); run_ms(500);
  CHECK(!visible(overlay()), "oil pressure 90: no alert");
  sim_set_manual(SIG_OILP, 20); run_ms(600);
  CHECK(visible(overlay()), "oil pressure 20 with the engine running: ALERT");
  CHECK(strcmp(lv_label_get_text(lv_obj_get_child(overlay(), 0)), "OIL PRESS LOW") == 0, "it says OIL PRESS LOW");
  CHECK(visible(sys_label("SIMULATED")), "and the SIMULATED banner is still visible above it");
  save("e3_alert_simulated.rgb");
  sim_set_manual(SIG_RPM, 300); run_ms(300);
  CHECK(sim_value(SIG_RPM) == 300, "manual RPM follows the slider");
  run_ms(2600);
  CHECK(!visible(overlay()), "engine below 400 RPM: alert cleared (cranking/key-on never alarms)");

  printf("E4 a REAL CAN frame arrives: everything returns to 'no data'\n");
  sim_set_manual(SIG_RPM, 3000); run_ms(300);
  sim_notify_real_frame(); run_ms(100);
  CHECK(!sim_is_active() && strcmp(sim_last_stop_reason(), "real CAN traffic arrived") == 0, "the simulator switched itself off");
  CHECK(!visible(sys_label("SIMULATED")), "banner gone");
  run_ms(2300);
  bool none = true; for (int s = 1; s < SIG_COUNT; s++) if (gauge_config_signal_has_data((SignalId)s)) none = false;
  CHECK(none, "within 2.3s no signal has data any more - nothing fake lingers");
  CHECK(!visible(overlay()), "no alert");
  CHECK(visible(find_label_with(lv_scr_act(), "NO DATA")), "the trend graph says NO DATA again");
  save("e4_after_real_frame.rgb");

  printf("E5 refuses to leave stale fake values: a restart of the demo begins clean\n");
  sim_start(false); run_ms(200);
  CHECK(sim_is_active() && sim_value(SIG_RPM) > 800 && sim_value(SIG_RPM) < 900, "demo restarts at idle");

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
