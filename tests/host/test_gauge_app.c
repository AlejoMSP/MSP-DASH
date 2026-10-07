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
static bool g_logging = false, g_simActive = false;
bool sd_logging_is_active(void) { return g_logging; }
bool sim_is_active(void) { return g_simActive; }

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
  c->history_sec = 30;
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
  s_cfg[2].cells[0].history_sec = 10; s_cfg[2].cells[0].refresh_hz = 1;
  s_cfg[3].enabled = true; s_cfg[3].layout = LAYOUT_2X2;
  cell(&s_cfg[3].cells[0], SIG_FUELP, GAUGE_TYPE_HALFMOON, "FUEL", 0, 100, true, 40, false, false); /* warns, but NOT alert-enabled */
  cell(&s_cfg[3].cells[1], SIG_FLEX, GAUGE_TYPE_NEEDLE, "ETH", 0, 100, false, 0, false, false);
  cell(&s_cfg[3].cells[2], SIG_TPS, GAUGE_TYPE_NUMBER, "TPS", 0, 100, false, 0, false, false);
  cell(&s_cfg[3].cells[3], SIG_BATTV, GAUGE_TYPE_NEEDLE, "BATT", 0, 20, false, 5, false, true);    /* alert on, warning OFF, threshold 5 and value 0 would trip it IF the warning flag were ignored */

  /* pages 4-7: trend graphs in every layout (geometry, history limits, reference line, sample rate) */
  s_cfg[4].enabled = true; s_cfg[4].layout = LAYOUT_1CELL;
  cell(&s_cfg[4].cells[0], SIG_TPS, GAUGE_TYPE_HISTOGRAM, "TPS", 0, 100, false, 0, false, false);
  s_cfg[4].cells[0].history_sec = 2;                       /* 100 points x 20ms */
  s_cfg[5].enabled = true; s_cfg[5].layout = LAYOUT_2UP;
  cell(&s_cfg[5].cells[0], SIG_OILT, GAUGE_TYPE_HISTOGRAM, "OILT", 10, 20, false, 0, false, false);
  s_cfg[5].cells[0].history_sec = 20; s_cfg[5].cells[0].ref_enabled = true; s_cfg[5].cells[0].ref_value = 14.7f;
  cell(&s_cfg[5].cells[1], SIG_FLEX, GAUGE_TYPE_HISTOGRAM, "FLEX", 0, 100, false, 0, false, false);
  s_cfg[5].cells[1].history_sec = 600; s_cfg[5].cells[1].refresh_hz = 50;   /* 6s per sample no matter the refresh rate */
  s_cfg[6].enabled = true; s_cfg[6].layout = LAYOUT_3UP;
  cell(&s_cfg[6].cells[0], SIG_BATTV, GAUGE_TYPE_HISTOGRAM, "A", 0, 20, false, 0, false, false); s_cfg[6].cells[0].history_sec = 0;      /* out of range: clamps to 2 */
  cell(&s_cfg[6].cells[1], SIG_BATTV, GAUGE_TYPE_HISTOGRAM, "B", 0, 20, false, 0, false, false); s_cfg[6].cells[1].history_sec = 5000;   /* out of range: clamps to 600 */
  cell(&s_cfg[6].cells[2], SIG_BATTV, GAUGE_TYPE_HISTOGRAM, "C", 0, 20, false, 0, false, false); s_cfg[6].cells[2].history_sec = 90;
  s_cfg[7].enabled = true; s_cfg[7].layout = LAYOUT_2X2;
  cell(&s_cfg[7].cells[0], SIG_RPM,   GAUGE_TYPE_HISTOGRAM, "RPM", 0, 9000, false, 0, false, false);
  cell(&s_cfg[7].cells[1], SIG_FUELP, GAUGE_TYPE_HISTOGRAM, "FP",  0, 100, false, 0, false, false);
  cell(&s_cfg[7].cells[2], SIG_CLT,   GAUGE_TYPE_HISTOGRAM, "CLT", 65, 250, false, 0, false, false);
  cell(&s_cfg[7].cells[3], SIG_FLEX,  GAUGE_TYPE_HISTOGRAM, "ETH", 0, 100, false, 0, false, false);

  nodata();
  build_gauge_app();
  index_pages();
  run_ms(100);

  printf("T1 boot with no data\n");
  CHECK(s_nPage == 8, "found exactly our 8 page screens");
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
  g_page_nav_delta = -1; run_ms(100); CHECK(lv_scr_act() == screen(s_nPage - 1), "0 -> last page (wraps)");
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
  lv_coord_t snapA[128], snapB[128];
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


  printf("T15 trend graph: its own sample rate (100 samples across the history window), not the refresh rate\n");
  {
    lv_obj_t *ch = find_class(screen(4), &lv_chart_class);                 /* TPS, 2s window => 20ms per sample */
    lv_chart_series_t *sr = lv_chart_get_series_next(ch, NULL);
    s_has[SIG_TPS] = true; s_val[SIG_TPS] = 50; run_ms(1000);
    int filled = 0; lv_coord_t *y = lv_chart_get_y_array(ch, sr);
    for (int i = 0; i < 100; i++) if (y[i] != LV_CHART_POINT_NONE) filled++;
    CHECK(filled >= 47 && filled <= 53, "1s of data at 20ms/sample puts ~50 points on the graph");
    run_ms(1500);
    filled = 0; for (int i = 0; i < 100; i++) if (y[i] != LV_CHART_POINT_NONE) filled++;
    CHECK(filled == 100, "window completely full after 2.5s of a 2s window");

    lv_obj_t *slow = lv_obj_get_child(lv_obj_get_child(screen(5), 1), 0);  /* FLEX, 600s window, refresh_hz=50 */
    (void)slow;
    lv_obj_t *charts5[4]; int n5 = 0;
    for (uint32_t k = 0; k < lv_obj_get_child_cnt(screen(5)); k++) { lv_obj_t *c2 = find_class(lv_obj_get_child(screen(5), k), &lv_chart_class); if (c2) charts5[n5++] = c2; }
    CHECK(n5 == 2, "found both trend graphs on the 2-up page");
    lv_obj_t *flexch = charts5[1];
    lv_chart_series_t *fs = lv_chart_get_series_next(flexch, NULL);
    s_has[SIG_FLEX] = true; s_val[SIG_FLEX] = 40; run_ms(12500);
    int fp = 0; lv_coord_t *fy = lv_chart_get_y_array(flexch, fs);
    for (int i = 0; i < 100; i++) if (fy[i] != LV_CHART_POINT_NONE) fp++;
    CHECK(fp >= 2 && fp <= 4, "600s window samples every 6s even with refresh_hz=50 (about 3 points in 12.5s, not hundreds)");
  }

  printf("T16 trend graph resolution: small moves stay visible\n");
  {
    lv_obj_t *ch = NULL;
    for (uint32_t k = 0; k < lv_obj_get_child_cnt(screen(5)); k++) { lv_obj_t *c2 = find_class(lv_obj_get_child(screen(5), k), &lv_chart_class); if (c2) { ch = c2; break; } }
    /* LVGL lists the most recently added series first, and draws the list back to front: the
       data series (added second) is first in the list and is painted on top of the reference line. */
    lv_chart_series_t *data = lv_chart_get_series_next(ch, NULL);
    lv_chart_series_t *ref = lv_chart_get_series_next(ch, data);
    CHECK(ref != NULL && data != NULL, "graph has a reference series AND a data series");
    s_has[SIG_OILT] = true; s_val[SIG_OILT] = 14.2f; run_ms(1000);
    s_val[SIG_OILT] = 14.7f; run_ms(1000);
    lv_coord_t *y = lv_chart_get_y_array(ch, data);
    int has4200 = 0, has4700 = 0, other = 0;
    for (int i = 0; i < 100; i++) { if (y[i] == LV_CHART_POINT_NONE) continue; if (y[i] == 4200) has4200++; else if (y[i] == 4700) has4700++; else other++; }
    CHECK(has4200 > 0 && has4700 > 0, "14.2 and 14.7 land on different points (4200 and 4700 on a 10-20 range)");
    CHECK(other == 0, "no stray values: exactly the two levels that were fed in");
    lv_coord_t *ry = lv_chart_get_y_array(ch, ref);
    int refOk = 1; for (int i = 0; i < 100; i++) if (ry[i] != 4700) refOk = 0;
    CHECK(refOk, "reference line at 14.7 sits at 4700 across all 100 points");
  }

  printf("T17 trend graph captions state the time span, and out-of-range history is clamped\n");
  CHECK(find_label_text(screen(4), "TPS  2s") != NULL, "2s history");
  CHECK(find_label_text(screen(5), "OILT  20s") != NULL, "20s history");
  CHECK(find_label_text(screen(5), "FLEX  10 min") != NULL, "600s shown as minutes");
  CHECK(find_label_text(screen(6), "A  2s") != NULL, "history 0 clamps up to 2s");
  CHECK(find_label_text(screen(6), "B  10 min") != NULL, "history 5000 clamps down to 600s");
  CHECK(find_label_text(screen(6), "C  90s") != NULL, "90s history");

  printf("T18 every trend graph, in every layout, fits inside the round panel and its own cell\n");
  {
    const int S = 466, H = 233;
    struct { int page; int cells; } layouts[] = { {4, 1}, {5, 2}, {6, 3}, {7, 4} };
    int ok = 1, count = 0;
    for (int li = 0; li < 4; li++) {
      lv_obj_t *scr = screen(layouts[li].page);
      lv_obj_update_layout(scr);
      int idx = 0;
      for (uint32_t k = 0; k < lv_obj_get_child_cnt(scr); k++) {
        lv_obj_t *cont = lv_obj_get_child(scr, k);
        lv_obj_t *c2 = find_class(cont, &lv_chart_class);
        if (!c2) continue;
        lv_area_t a; lv_obj_get_coords(c2, &a);
        /* the cell this chart belongs to, per the layout definitions */
        int cx0, cy0, cw, chh;
        if (layouts[li].page == 4)      { cx0 = 0; cy0 = 0; cw = S; chh = S; }
        else if (layouts[li].page == 5) { cx0 = 0; cy0 = idx ? H : 0; cw = S; chh = H; }
        else if (layouts[li].page == 6) { if (idx == 0) { cx0 = 0; cy0 = 0; cw = S; chh = H; } else { cx0 = (idx == 1) ? 0 : H; cy0 = H; cw = H; chh = H; } }
        else                            { cx0 = (idx % 2) ? H : 0; cy0 = (idx / 2) ? H : 0; cw = H; chh = H; }
        int w = a.x2 - a.x1 + 1, h = a.y2 - a.y1 + 1;
        int corners[4][2] = { {a.x1, a.y1}, {a.x2, a.y1}, {a.x1, a.y2}, {a.x2, a.y2} };
        for (int q = 0; q < 4; q++) {
          double dx = corners[q][0] - 233.0, dy = corners[q][1] - 233.0;
          if (dx * dx + dy * dy > 233.0 * 233.0) { ok = 0; printf("  corner outside the circle: page %d chart %d corner %d (%d,%d)\n", layouts[li].page, idx, q, corners[q][0], corners[q][1]); }
        }
        if (a.x1 < cx0 || a.y1 < cy0 || a.x2 >= cx0 + cw || a.y2 >= cy0 + chh) { ok = 0; printf("  chart leaves its cell: page %d chart %d\n", layouts[li].page, idx); }
        if (w < 60 || h < 40) { ok = 0; printf("  chart too small: page %d chart %d is %dx%d\n", layouts[li].page, idx, w, h); }
        /* the caption (the container's other child) must also sit fully on the glass */
        lv_obj_t *cap = lv_obj_get_child(cont, 1);
        if (cap && lv_obj_check_type(cap, &lv_label_class)) {
          lv_obj_update_layout(cont);
          lv_area_t ca; lv_obj_get_coords(cap, &ca);
          int cc[4][2] = { {ca.x1, ca.y1}, {ca.x2, ca.y1}, {ca.x1, ca.y2}, {ca.x2, ca.y2} };
          for (int q = 0; q < 4; q++) { double dx = cc[q][0] - 233.0, dy = cc[q][1] - 233.0;
            if (dx * dx + dy * dy > 233.0 * 233.0) { ok = 0; printf("  CAPTION corner outside the circle: page %d chart %d corner (%d,%d) text '%s'\n", layouts[li].page, idx, cc[q][0], cc[q][1], lv_label_get_text(cap)); } }
          if (ca.y1 < a.y2) { ok = 0; printf("  caption overlaps its chart: page %d chart %d\n", layouts[li].page, idx); }
        } else { ok = 0; printf("  no caption label found for page %d chart %d\n", layouts[li].page, idx); }
        count++; idx++;
      }
    }
    CHECK(count == 10, "found all 10 trend graphs (1+2+3+4)");
    CHECK(ok, "every graph AND its caption fully inside the circle; graph inside its cell, at least 60x40, caption below it");
  }


  printf("T19 bench-simulator banner and the logging dot\n");
  {
    lv_obj_t *sys = lv_layer_sys();
    lv_obj_t *banner = NULL, *dot = NULL, *overlay = find_overlay();
    for (uint32_t k = 0; k < lv_obj_get_child_cnt(sys); k++) {
      lv_obj_t *ch = lv_obj_get_child(sys, k);
      if (lv_obj_check_type(ch, &lv_label_class) && strcmp(lv_label_get_text(ch), "SIMULATED") == 0) banner = ch;
      else if (lv_obj_get_width(ch) == 14 && lv_obj_get_height(ch) == 14) dot = ch;
    }
    CHECK(banner != NULL && dot != NULL, "both exist on the system layer");
    CHECK(lv_obj_has_flag(banner, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(dot, LV_OBJ_FLAG_HIDDEN), "both hidden at boot");
    g_simActive = true; run_ms(100);
    CHECK(!lv_obj_has_flag(banner, LV_OBJ_FLAG_HIDDEN), "banner shows while the simulator runs");
    g_simActive = false; run_ms(100);
    CHECK(lv_obj_has_flag(banner, LV_OBJ_FLAG_HIDDEN), "banner hides when it stops");
    lv_obj_update_layout(sys);
    lv_area_t ba, da; lv_obj_get_coords(banner, &ba); lv_obj_get_coords(dot, &da);
    int ok = 1;
    lv_area_t *areas[2] = { &ba, &da };
    for (int i = 0; i < 2; i++) {
      int cc[4][2] = { {areas[i]->x1, areas[i]->y1}, {areas[i]->x2, areas[i]->y1}, {areas[i]->x1, areas[i]->y2}, {areas[i]->x2, areas[i]->y2} };
      for (int q = 0; q < 4; q++) { double dx = cc[q][0] - 233.0, dy = cc[q][1] - 233.0; if (dx * dx + dy * dy > 233.0 * 233.0) { ok = 0; printf("  %s corner (%d,%d) is off the glass\n", i ? "logging dot" : "SIMULATED banner", cc[q][0], cc[q][1]); } }
    }
    CHECK(ok, "the banner AND the logging dot are fully inside the round panel (the dot used to sit in the off-glass corner)");
    CHECK(da.y2 < ba.y1 || da.y1 > ba.y2 || da.x2 < ba.x1 || da.x1 > ba.x2, "banner and dot do not overlap");
    CHECK(lv_obj_get_index(banner) > lv_obj_get_index(overlay) && lv_obj_get_index(dot) > lv_obj_get_index(overlay), "both are layered above the full-screen alert");
    g_logging = true; int seen = 0;
    for (int i = 0; i < 80; i++) { run_ms(25); if (!lv_obj_has_flag(dot, LV_OBJ_FLAG_HIDDEN)) seen++; }
    CHECK(seen > 10 && seen < 70, "while logging the dot blinks (visible part of the time)");
    g_logging = false; run_ms(100);
    CHECK(lv_obj_has_flag(dot, LV_OBJ_FLAG_HIDDEN), "dot off when not logging");
  }

  printf("T20 a trend graph with no data says NO DATA - promptly, whatever its history length\n");
  {
    int vis = 0, total = 0;
    s_has[SIG_BATTV] = false; run_ms(200);
    for (uint32_t k = 0; k < lv_obj_get_child_cnt(screen(6)); k++) {
      lv_obj_t *lab = find_label_text(lv_obj_get_child(screen(6), k), "NO DATA");
      if (lab) { total++; if (!lv_obj_has_flag(lab, LV_OBJ_FLAG_HIDDEN)) vis++; }
    }
    CHECK(total == 3 && vis == 3, "all three graphs on the 3-up page say NO DATA when their signal has none");
    s_has[SIG_BATTV] = true; s_val[SIG_BATTV] = 13.8f; run_ms(100);
    vis = 0; for (uint32_t k = 0; k < lv_obj_get_child_cnt(screen(6)); k++) { lv_obj_t *lab = find_label_text(lv_obj_get_child(screen(6), k), "NO DATA"); if (lab && !lv_obj_has_flag(lab, LV_OBJ_FLAG_HIDDEN)) vis++; }
    CHECK(vis == 0, "gone within 100ms of data arriving, even for the 10-minute graph (6s between samples)");
    s_has[SIG_BATTV] = false; run_ms(100);
    vis = 0; for (uint32_t k = 0; k < lv_obj_get_child_cnt(screen(6)); k++) { lv_obj_t *lab = find_label_text(lv_obj_get_child(screen(6), k), "NO DATA"); if (lab && !lv_obj_has_flag(lab, LV_OBJ_FLAG_HIDDEN)) vis++; }
    CHECK(vis == 3, "back when the signal is lost");
    CHECK(find_label_text(screen(0), "NO DATA") == NULL, "needle gauges have no such label");
    s_has[SIG_BATTV] = true;
  }


  printf("T21 starting or stopping the simulator wipes the trend graphs (fake and real data never share a graph)\n");
  {
    lv_obj_t *ch = find_class(screen(4), &lv_chart_class);
    lv_chart_series_t *sr = lv_chart_get_series_next(ch, NULL);
    s_has[SIG_TPS] = true; s_val[SIG_TPS] = 50; run_ms(500);
    lv_coord_t *y = lv_chart_get_y_array(ch, sr);
    int n = 0; for (int i = 0; i < 100; i++) if (y[i] != LV_CHART_POINT_NONE) n++;
    CHECK(n > 10, "the graph has a trace to begin with");
    g_simActive = true; run_ms(60);
    n = 0; for (int i = 0; i < 100; i++) if (y[i] != LV_CHART_POINT_NONE) n++;
    CHECK(n <= 3, "simulator START clears it (old real data does not carry into the simulation)");
    run_ms(500);
    n = 0; for (int i = 0; i < 100; i++) if (y[i] != LV_CHART_POINT_NONE) n++;
    CHECK(n > 10, "and the graph then fills with the simulated data");
    g_simActive = false; run_ms(60);
    n = 0; for (int i = 0; i < 100; i++) if (y[i] != LV_CHART_POINT_NONE) n++;
    CHECK(n <= 3, "simulator STOP clears it (fake data does not sit next to real data)");
  }

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
