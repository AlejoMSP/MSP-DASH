/*
  Headless test for gauge_app.c: the REAL file, compiled unmodified against
  LVGL 8.4 on this machine, with only the functions it expects from other
  files stubbed. Scripted scenarios drive fake signal values and check the
  full-screen alert, no-data display and page navigation.
*/
#include "lvgl.h"
#include "gauge_config.h"
#include "gauge_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- stubs for what gauge_app.c needs from other files ---------------- */
static PageConfig s_cfg[MAX_PAGES];
static float s_val[SIG_COUNT];
static bool s_has[SIG_COUNT];

PageConfig *gauge_config_get_pages(void) { return s_cfg; }
int gauge_config_cells_for_layout(LayoutType l) {
  switch (l) { case LAYOUT_1CELL: return 1; case LAYOUT_2UP: return 2; case LAYOUT_3UP: return 3; case LAYOUT_2X2: return 4; default: return 1; }
}
float gauge_config_signal_value(SignalId s) { return s_val[s]; }
bool gauge_config_signal_has_data(SignalId s) { return s_has[s]; }
const char *gauge_config_signal_name(SignalId s) {
  static const char *n[] = {"None","RPM","Coolant Temp","MAP/Boost","AFR","Oil Pressure","Oil Temp","Fuel Pressure","Ethanol %","Throttle Position","Battery Voltage"};
  return n[s];
}
bool sd_logging_is_active(void) { return false; }

/* ---------------- display ---------------- */
static lv_color_t dbuf[466 * 20];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static void flush_cb(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *p) { (void)a; (void)p; lv_disp_flush_ready(d); }

/* ---------------- test helpers ---------------- */
static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) { g_pass++; } else { g_fail++; printf("  FAIL: %s  (line %d)\n", msg, __LINE__); } } while (0)

static void run_ms(int ms) { for (int t = 0; t < ms; t += 5) { lv_tick_inc(5); lv_timer_handler(); } }

/* LVGL also keeps its blank default screen and its top/sys layers in the screens list, so find OUR
   pages by content: every screen that isn't a layer and actually has children, in creation order. */
static lv_obj_t *s_pageScr[8]; static int s_nPage = 0;
static void index_pages(void) {
  lv_disp_t *d = lv_disp_get_default();
  s_nPage = 0;
  for (uint32_t i = 0; i < d->screen_cnt; i++) {
    lv_obj_t *o = d->screens[i];
    if (o == lv_layer_top() || o == lv_layer_sys()) continue;
    if (lv_obj_get_child_cnt(o) == 0) continue;
    if (s_nPage < 8) s_pageScr[s_nPage++] = o;
  }
}
static lv_obj_t *screen(int i) { return s_pageScr[i]; }

static lv_obj_t *find_overlay(void) {
  lv_obj_t *sys = lv_layer_sys();
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(sys); i++) {
    lv_obj_t *ch = lv_obj_get_child(sys, i);
    if (lv_obj_get_width(ch) >= 400) return ch;
  }
  return NULL;
}
static bool overlay_visible(void) { lv_obj_t *o = find_overlay(); return o && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); }
static const char *ov_text(int child) { return lv_label_get_text(lv_obj_get_child(find_overlay(), child)); }

static lv_obj_t *find_label_text(lv_obj_t *o, const char *text) {
  if (lv_obj_check_type(o, &lv_label_class) && strcmp(lv_label_get_text(o), text) == 0) return o;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) {
    lv_obj_t *r = find_label_text(lv_obj_get_child(o, i), text);
    if (r) return r;
  }
  return NULL;
}
static lv_obj_t *find_class(lv_obj_t *o, const lv_obj_class_t *cls) {
  if (lv_obj_check_type(o, cls)) return o;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) {
    lv_obj_t *r = find_class(lv_obj_get_child(o, i), cls);
    if (r) return r;
  }
  return NULL;
}

static void cell(CellConfig *c, SignalId sig, GaugeType t, const char *cap, int min, int max,
                 bool warn, float thr, bool above, bool alert) {
  memset(c, 0, sizeof(*c));
  c->enabled = true; c->signal = sig; c->gauge_type = t;
  c->min_val = min; c->max_val = max; c->rest_val = min;
  strncpy(c->caption, cap, GAUGE_CAPTION_MAXLEN - 1);
  c->needle_color = 0xFFFFFF; c->tick_color = 0xFFFFFF; c->bg_color = 0x000000;
  c->value_divisor = 1.0f; c->tick_count = 26; c->major_nth = 5; c->refresh_hz = 50; c->value_font_size = 28;
  c->has_warning = warn; c->warning_threshold = thr; c->warning_above = above; c->alert_fullscreen = alert;
}

static void nodata(void) { memset(s_has, 0, sizeof(s_has)); memset(s_val, 0, sizeof(s_val)); }

/* ---------------- scenarios ---------------- */
int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, dbuf, NULL, 466 * 20);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 466; disp_drv.ver_res = 466;
  disp_drv.flush_cb = flush_cb; disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  /* page 0: oil pressure, low alert.  page 1: CLT high alert + AFR number.
     page 2: MAP trend graph.  page 3: 2x2 with a non-alert warning and a misconfigured cell. */
  memset(s_cfg, 0, sizeof(s_cfg));
  s_cfg[0].enabled = true; s_cfg[0].layout = LAYOUT_1CELL;
  cell(&s_cfg[0].cells[0], SIG_OILP, GAUGE_TYPE_NEEDLE, "OIL PRESS", 0, 100, true, 40, false, true);
  s_cfg[0].cells[0].show_value = true;
  s_cfg[1].enabled = true; s_cfg[1].layout = LAYOUT_2UP;
  cell(&s_cfg[1].cells[0], SIG_CLT, GAUGE_TYPE_NEEDLE, "CLT", 65, 250, true, 220, true, true);
  cell(&s_cfg[1].cells[1], SIG_AFR, GAUGE_TYPE_NUMBER, "AFR", 10, 20, true, 15, true, false);
  s_cfg[2].enabled = true; s_cfg[2].layout = LAYOUT_1CELL;
  cell(&s_cfg[2].cells[0], SIG_MAP, GAUGE_TYPE_HISTOGRAM, "MAP", 0, 250, false, 0, false, false);
  s_cfg[3].enabled = true; s_cfg[3].layout = LAYOUT_2X2;
  cell(&s_cfg[3].cells[0], SIG_FUELP, GAUGE_TYPE_HALFMOON, "FUEL", 0, 100, true, 40, false, false); /* warns, but NOT alert-enabled */
  cell(&s_cfg[3].cells[1], SIG_FLEX, GAUGE_TYPE_NEEDLE, "ETH", 0, 100, false, 0, false, false);
  cell(&s_cfg[3].cells[2], SIG_TPS, GAUGE_TYPE_NUMBER, "TPS", 0, 100, false, 0, false, false);
  cell(&s_cfg[3].cells[3], SIG_BATTV, GAUGE_TYPE_NEEDLE, "BATT", 0, 20, false, 5, false, true);    /* alert on, warning OFF, threshold 5 and value 0 would trip it IF the warning flag were ignored */

  nodata();
  build_gauge_app();
  index_pages();
  run_ms(100);

  printf("T1 boot with no data\n");
  CHECK(s_nPage == 4, "found exactly our 4 page screens");
  CHECK(lv_scr_act() == screen(0), "page 0 is the active screen at boot");
  CHECK(find_overlay() != NULL, "alert overlay exists on the system layer");
  CHECK(!overlay_visible(), "overlay hidden at boot");
  CHECK(find_label_text(screen(1), "--") != NULL, "AFR number cell shows -- with no data (not 0.0)");
  CHECK(find_label_text(screen(0), "--") != NULL, "oil pressure show_value readout shows --");

  printf("T2 number readout follows data, and goes back to -- when data is lost\n");
  s_has[SIG_AFR] = true; s_val[SIG_AFR] = 14.7f; run_ms(200);
  CHECK(find_label_text(screen(1), "14.7") != NULL, "AFR shows 14.7");
  s_has[SIG_AFR] = false; run_ms(200);
  CHECK(find_label_text(screen(1), "--") != NULL, "AFR back to -- when signal lost");
  CHECK(find_label_text(screen(1), "14.7") == NULL, "no frozen 14.7 left behind");

  printf("T3 no alert while the engine is off (oil pressure reads low at key-on)\n");
  s_has[SIG_OILP] = true; s_val[SIG_OILP] = 20; run_ms(500);
  CHECK(!overlay_visible(), "hidden: no RPM data");
  s_has[SIG_RPM] = true; s_val[SIG_RPM] = 0; run_ms(500);
  CHECK(!overlay_visible(), "hidden: RPM 0");
  s_val[SIG_RPM] = 300; run_ms(500);
  CHECK(!overlay_visible(), "hidden: cranking RPM 300 (< 400)");

  printf("T4 alert appears when the engine runs with low oil pressure\n");
  s_val[SIG_RPM] = 800; run_ms(300);
  CHECK(overlay_visible(), "overlay visible");
  CHECK(strcmp(ov_text(0), "OIL PRESS LOW") == 0, "title is 'OIL PRESS LOW'");
  CHECK(strcmp(ov_text(1), "20.0") == 0, "value is 20.0");
  CHECK(strcmp(ov_text(2), "") == 0, "no '+N more' with a single alert");
  CHECK(lv_scr_act() == screen(0), "alert shows even though page 0 is not special - overlay is on the system layer");

  printf("T5 alert shows on a DIFFERENT page than the offending cell\n");
  s_val[SIG_OILP] = 60; run_ms(2500);           /* clear it fully first */
  CHECK(!overlay_visible(), "cleared");
  lv_disp_load_scr(screen(2));                   /* put the MAP graph page on screen */
  s_val[SIG_OILP] = 20; run_ms(300);
  CHECK(lv_scr_act() == screen(2), "still on the MAP page");
  CHECK(overlay_visible() && strcmp(ov_text(0), "OIL PRESS LOW") == 0, "oil alert visible over the MAP page");
  lv_disp_load_scr(screen(0));

  printf("T6 minimum show time: condition clears, alert holds ~2s, then goes\n");
  s_val[SIG_OILP] = 60; run_ms(1000);
  CHECK(overlay_visible(), "still up 1s after the condition cleared");
  run_ms(1500);
  CHECK(!overlay_visible(), "gone after the hold time");

  printf("T7 BOOT press dismisses the alert (no page change) and snoozes it\n");
  s_val[SIG_OILP] = 20; run_ms(300);
  CHECK(overlay_visible(), "alert up");
  lv_obj_t *before = lv_scr_act();
  g_page_nav_delta = 1; run_ms(100);
  CHECK(!overlay_visible(), "dismissed by the BOOT request");
  CHECK(lv_scr_act() == before, "BOOT did NOT change the page");
  run_ms(5000);
  CHECK(!overlay_visible(), "still snoozed 5s later even though the condition persists");
  run_ms(26000);
  CHECK(overlay_visible(), "comes back after the 30s snooze");

  printf("T8 BOOT with no alert showing changes pages, wrapping both ways\n");
  s_val[SIG_OILP] = 60; run_ms(2500);
  CHECK(!overlay_visible(), "no alert");
  lv_disp_load_scr(screen(0)); run_ms(50);
  g_page_nav_delta = 1; run_ms(100);  CHECK(lv_scr_act() == screen(1), "0 -> 1");
  g_page_nav_delta = -1; run_ms(100); CHECK(lv_scr_act() == screen(0), "1 -> 0");
  g_page_nav_delta = -1; run_ms(100); CHECK(lv_scr_act() == screen(3), "0 -> last page (wraps)");
  g_page_nav_delta = 1; run_ms(100);  CHECK(lv_scr_act() == screen(0), "last -> 0 (wraps)");

  printf("T9 two alerts: first in page order is shown, '+1 more' for the other\n");
  s_has[SIG_CLT] = true; s_val[SIG_CLT] = 230;
  s_val[SIG_OILP] = 20;
  s_has[SIG_FUELP] = true; s_val[SIG_FUELP] = 10;     /* warning but alert not enabled: must not count */
  s_has[SIG_BATTV] = true; s_val[SIG_BATTV] = 0;      /* alert enabled but warning off: must not count */
  run_ms(2200);                                        /* wait out any leftover snooze from T7 */
  CHECK(overlay_visible(), "alert visible");
  CHECK(strcmp(ov_text(0), "OIL PRESS LOW") == 0, "oil pressure (page 0) wins");
  CHECK(strcmp(ov_text(2), "+1 more") == 0, "exactly '+1 more' (fuel pressure and battery not counted)");

  printf("T10 a LOST signal is not an alarm\n");
  s_has[SIG_OILP] = false; s_has[SIG_CLT] = false; run_ms(2600);
  CHECK(!overlay_visible(), "alert clears when the signals disappear");

  printf("T11 'above' direction and formatting\n");
  s_has[SIG_OILP] = true; s_val[SIG_OILP] = 60;
  s_has[SIG_CLT] = true; s_val[SIG_CLT] = 230; run_ms(300);
  CHECK(overlay_visible() && strcmp(ov_text(0), "CLT HIGH") == 0, "title 'CLT HIGH'");
  CHECK(strcmp(ov_text(1), "230.0") == 0, "value 230.0");

  printf("T12 tapping the overlay dismisses it\n");
  lv_event_send(find_overlay(), LV_EVENT_CLICKED, NULL); run_ms(100);
  CHECK(!overlay_visible(), "dismissed by tap");

  printf("T13 trend graph only plots real data\n");
  lv_obj_t *chart = find_class(screen(2), &lv_chart_class);
  CHECK(chart != NULL, "found the chart on the MAP page");
  if (!chart) { printf("\n%d passed, %d failed\n", g_pass, g_fail); return 1; }
  lv_chart_series_t *ser = lv_chart_get_series_next(chart, NULL);
  uint32_t n = lv_chart_get_point_count(chart);
  lv_coord_t snapA[64], snapB[64];
  s_has[SIG_MAP] = true; s_val[SIG_MAP] = 100; run_ms(200);
  memcpy(snapA, lv_chart_get_y_array(chart, ser), sizeof(lv_coord_t) * n);
  s_val[SIG_MAP] = 200; run_ms(200);
  memcpy(snapB, lv_chart_get_y_array(chart, ser), sizeof(lv_coord_t) * n);
  CHECK(memcmp(snapA, snapB, sizeof(lv_coord_t) * n) != 0, "chart advances while data is flowing");
  s_has[SIG_MAP] = false; run_ms(400);
  memcpy(snapA, lv_chart_get_y_array(chart, ser), sizeof(lv_coord_t) * n);
  run_ms(400);
  memcpy(snapB, lv_chart_get_y_array(chart, ser), sizeof(lv_coord_t) * n);
  CHECK(memcmp(snapA, snapB, sizeof(lv_coord_t) * n) == 0, "chart holds still with no data (no fake sweep trace)");

  printf("T14 soak: 5 simulated minutes of random values, signal drops, button presses and taps\n");
  srand(12345);
  for (int step = 0; step < 3000; step++) {
    for (int s = 1; s < SIG_COUNT; s++) {
      if (rand() % 40 == 0) s_has[s] = !s_has[s];
      if (rand() % 3 == 0) s_val[s] = (float)(rand() % 3000) / 10.0f - 20.0f;
    }
    if (rand() % 25 == 0) g_page_nav_delta = (rand() & 1) ? 1 : -1;
    if (rand() % 90 == 0 && find_overlay()) lv_event_send(find_overlay(), LV_EVENT_CLICKED, NULL);
    run_ms(100);
  }
  CHECK(1, "soak finished without a crash or sanitizer error");

  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
