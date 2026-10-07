/*
  Headless test for the coloured tick zones and the needle trim in gauge_app.c: the REAL file against LVGL 8.4,
  checked in two ways - LVGL's own data (which tick-colour indicators exist, in what order) and the actual
  rendered pixels (is each tick really yellow/red/white; where does the needle really start and stop).
      ./test_zones_needle zones | zoneshalf | zonepix | needlepix
*/
#include "lvgl.h"
#include "gauge_config.h"
#include "gauge_app.h"
#include "gauge_style.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static PageConfig s_cfg[MAX_PAGES];
static float s_val[SIG_COUNT];
static bool s_has[SIG_COUNT];
PageConfig *gauge_config_get_pages(void) { return s_cfg; }
int gauge_config_cells_for_layout(LayoutType l) { switch (l) { case LAYOUT_1CELL: return 1; case LAYOUT_2UP: return 2; case LAYOUT_3UP: return 3; case LAYOUT_2X2: return 4; default: return 1; } }
float gauge_config_signal_value(SignalId s) { return s_val[s]; }
bool gauge_config_signal_has_data(SignalId s) { return s_has[s]; }
const char *gauge_config_signal_name(SignalId s) { (void)s; return "sig"; }
bool sd_logging_is_active(void) { return false; }
bool sim_is_active(void) { return false; }

static uint8_t fb[466 * 466 * 3];
static lv_color_t dbuf[466 * 466];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static void flush_cb(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *p) {
  for (int y = a->y1; y <= a->y2; y++) for (int x = a->x1; x <= a->x2; x++) { uint32_t c = lv_color_to32(*p++); uint8_t *o = &fb[(y * 466 + x) * 3]; o[0] = (c >> 16) & 255; o[1] = (c >> 8) & 255; o[2] = c & 255; }
  lv_disp_flush_ready(d);
}
static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("  FAIL: %s  (line %d)\n", msg, __LINE__); } } while (0)
static void run_ms(int ms) { for (int t = 0; t < ms; t += 5) { lv_tick_inc(5); lv_timer_handler(); } }

static void mk(CellConfig *c, GaugeType t, uint32_t needleColor) {
  memset(c, 0, sizeof(*c));
  c->enabled = true; c->signal = SIG_AFR; c->gauge_type = t; c->min_val = 0; c->max_val = 250; c->rest_val = 0;
  strcpy(c->caption, "AFR"); c->needle_color = needleColor; c->tick_color = 0xFFFFFF; c->bg_color = 0;
  c->value_divisor = 1.0f; c->tick_count = 26; c->major_nth = 5; c->refresh_hz = 50; c->value_font_size = 28; c->tick_font_size = 28;
  c->start_deg = t == GAUGE_TYPE_HALFMOON ? 270 : 225; c->end_deg = t == GAUGE_TYPE_HALFMOON ? 90 : 135; c->history_sec = 30;
}
static lv_obj_t *s_pageScr[8]; static int s_nPage = 0;
static void index_pages(void) {
  lv_disp_t *d = lv_disp_get_default(); s_nPage = 0;
  for (uint32_t i = 0; i < d->screen_cnt; i++) { lv_obj_t *o = d->screens[i]; if (o == lv_layer_top() || o == lv_layer_sys() || lv_obj_get_child_cnt(o) == 0) continue; if (s_nPage < 8) s_pageScr[s_nPage++] = o; }
}
static lv_obj_t *meter_of(lv_obj_t *page, int cell) { lv_obj_t *cont = lv_obj_get_child(page, cell); for (uint32_t i = 0; i < lv_obj_get_child_cnt(cont); i++) { lv_obj_t *c = lv_obj_get_child(cont, i); if (lv_obj_check_type(c, &lv_meter_class)) return c; } return NULL; }

/* ---------- pixels ---------- */
static void px(float x, float y, int *r, int *g, int *b) {
  int ix = (int)lroundf(x), iy = (int)lroundf(y);
  if (ix < 0 || iy < 0 || ix > 465 || iy > 465) { *r = *g = *b = 0; return; }
  uint8_t *o = &fb[(iy * 466 + ix) * 3]; *r = o[0]; *g = o[1]; *b = o[2];
}
/* A 2px tick at an odd angle is anti-aliased, so most of its pixels are a DARKER blend of its colour with the black
   background. Classify by the colour's proportions (yellow is r:g:b = 1 : 0.84 : 0, red 1 : 0.16 : 0.16, white 1 : 1 : 1)
   and ignore anything dim, rather than demanding the exact full-brightness RGB. */
static bool is_yellow(int r, int g, int b) { return r > 70 && g * 100 >= r * 70 && g * 100 <= r * 98 && b * 100 < r * 22; }
static bool is_red(int r, int g, int b) { return r > 70 && g * 100 < r * 38 && b * 100 < r * 38 && abs(g - b) < 22; }
static bool is_white(int r, int g, int b) { return r > 70 && abs(r - g) < 22 && abs(g - b) < 22; }
static bool is_green(int r, int g, int b) { return g > 200 && r < 90 && b < 90; }

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  const char *mode = argc > 1 ? argv[1] : "zones";
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, dbuf, NULL, 466 * 466);
  lv_disp_drv_init(&disp_drv); disp_drv.hor_res = 466; disp_drv.ver_res = 466; disp_drv.flush_cb = flush_cb; disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  const lv_color_t Y = lv_color_hex(GAUGE_ZONE_YELLOW), R = lv_color_hex(GAUGE_ZONE_RED);

  if (!strcmp(mode, "zones") || !strcmp(mode, "zoneshalf")) {
    GaugeType t = !strcmp(mode, "zones") ? GAUGE_TYPE_NEEDLE : GAUGE_TYPE_HALFMOON;
    // case: {yellow on, yellow value, red on, red value, low}
    static const struct { bool yOn; int yV; bool rOn; int rV; bool low; } C[8] = {
      {0,0,0,0,0}, {1,200,0,0,0}, {0,0,1,230,0}, {1,200,1,230,0}, {1,50,1,20,1}, {0,0,1,10,1}, {1,300,0,0,0}, {1,240,1,200,0} };
    // what LVGL must hold, OLDEST first (the order it draws in: a later one wins): {isRed, start, end}
    static const struct { int n; struct { int red, start, end; } z[2]; } E[8] = {
      {0, {{0,0,0},{0,0,0}}}, {1, {{0,200,250},{0,0,0}}}, {1, {{1,230,250},{0,0,0}}}, {2, {{0,200,250},{1,230,250}}},
      {2, {{0,0,50},{1,0,20}}}, {1, {{1,0,10},{0,0,0}}}, {1, {{0,300,250},{0,0,0}}}, {2, {{0,240,250},{1,200,250}}} };
    for (int p = 0; p < 8; p++) {
      s_cfg[p].enabled = true; s_cfg[p].layout = LAYOUT_1CELL;
      CellConfig *c = &s_cfg[p].cells[0]; mk(c, t, 0xFFFFFF);
      c->yellow_enabled = C[p].yOn; c->yellow_value = C[p].yV; c->red_enabled = C[p].rOn; c->red_value = C[p].rV; c->zones_low = C[p].low;
    }
    build_gauge_app(); index_pages(); run_ms(200);
    printf("%s: the tick-colour indicators LVGL holds for each case\n", mode);
    for (int p = 0; p < 8; p++) {
      lv_meter_t *m = (lv_meter_t *)meter_of(s_pageScr[p], 0);
      int n = 0; struct { int red, start, end; } got[4];
      for (lv_meter_indicator_t *in = _lv_ll_get_tail(&m->indicator_ll); in; in = _lv_ll_get_prev(&m->indicator_ll, in)) {   // oldest first
        if (in->type != LV_METER_INDICATOR_TYPE_SCALE_LINES) continue;
        bool isY = in->type_data.scale_lines.color_start.full == Y.full, isR = in->type_data.scale_lines.color_start.full == R.full;
        if (n < 4) { got[n].red = isR ? 1 : (isY ? 0 : -1); got[n].start = in->start_value; got[n].end = in->end_value; }
        n++;
      }
      char msg[200];
      snprintf(msg, sizeof(msg), "case %d: %d colour zone(s) (got %d)", p, E[p].n, n); CHECK(n == E[p].n, msg);
      for (int i = 0; i < n && i < E[p].n; i++) {
        snprintf(msg, sizeof(msg), "case %d zone %d: %s from %d to %d (got %s %d..%d)", p, i, E[p].z[i].red ? "red" : "yellow", E[p].z[i].start, E[p].z[i].end,
                 got[i].red == 1 ? "red" : got[i].red == 0 ? "yellow" : "?", got[i].start, got[i].end);
        CHECK(got[i].red == E[p].z[i].red && got[i].start == E[p].z[i].start && got[i].end == E[p].z[i].end, msg);
      }
      if (E[p].n == 2) { snprintf(msg, sizeof(msg), "case %d: yellow is added BEFORE red, so red is drawn last and wins where they overlap", p); CHECK(got[0].red == 0 && got[1].red == 1, msg); }
      lv_meter_indicator_t *newest = _lv_ll_get_head(&m->indicator_ll);
      snprintf(msg, sizeof(msg), "case %d: the needle is the newest indicator (drawn on top of everything)", p); CHECK(newest->type == LV_METER_INDICATOR_TYPE_NEEDLE_LINE, msg);
    }
  }

  if (!strcmp(mode, "zonepix")) {
    // min 0, max 100, 11 ticks: tick i has value 10*i. Needle rests at 45, between two ticks, so it never covers a tick.
    static const struct { const char *name; bool yOn; int yV; bool rOn; int rV; bool low; } C[6] = {
      {"no zones", 0,0,0,0,0}, {"yellow >= 70, red >= 90", 1,70,1,90,0}, {"low end: red <= 20, yellow <= 40", 1,40,1,20,1},
      {"only red >= 100", 0,0,1,100,0}, {"yellow >= 55 (between two ticks)", 1,55,0,0,0}, {"red >= 90 with yellow >= 95 (yellow above red)", 1,95,1,90,0} };
    for (int p = 0; p < 6; p++) {
      s_cfg[p].enabled = true; s_cfg[p].layout = LAYOUT_1CELL;
      CellConfig *c = &s_cfg[p].cells[0]; mk(c, GAUGE_TYPE_NEEDLE, 0xFFFFFF);
      c->min_val = 0; c->max_val = 100; c->rest_val = 45; c->tick_count = 11; c->major_nth = 2;
      c->yellow_enabled = C[p].yOn; c->yellow_value = C[p].yV; c->red_enabled = C[p].rOn; c->red_value = C[p].rV; c->zones_low = C[p].low;
    }
    s_has[SIG_AFR] = true; s_val[SIG_AFR] = 45;
    build_gauge_app(); index_pages(); run_ms(300);
    printf("zonepix: the colour of every tick, measured in the rendered image\n");
    for (int p = 0; p < 6; p++) {
      for (int k = 0; k < 4; k++) run_ms(50);
      for (int i = 0; i <= 10; i++) {
        int value = 10 * i;
        bool y = C[p].yOn && (C[p].low ? value <= C[p].yV : value >= C[p].yV);
        bool r = C[p].rOn && (C[p].low ? value <= C[p].rV : value >= C[p].rV);
        const char *want = r ? "red" : (y ? "yellow" : "white");
        double theta = (135.0 + 27.0 * i) * M_PI / 180.0;
        int yh = 0, rh = 0, wh = 0;
        for (float rad = 160.0f; rad <= 215.0f; rad += 0.5f) {
          int pr, pg, pb; px(233.0f + rad * cosf((float)theta), 233.0f + rad * sinf((float)theta), &pr, &pg, &pb);
          if (is_yellow(pr, pg, pb)) yh++; else if (is_red(pr, pg, pb)) rh++; else if (is_white(pr, pg, pb)) wh++;
        }
        char msg[200];
        snprintf(msg, sizeof(msg), "[%s] tick %d: something is drawn along its ray (yellow %d, red %d, white %d)", C[p].name, value, yh, rh, wh);
        CHECK(yh + rh + wh >= 4, msg);
        snprintf(msg, sizeof(msg), "[%s] tick %d: should be %s (yellow pixels %d, red %d, white %d)", C[p].name, value, want, yh, rh, wh);
        if (r) CHECK(rh >= 4 && yh == 0, msg);
        else if (y) CHECK(yh >= 4 && rh == 0, msg);
        else CHECK(wh >= 4 && yh == 0 && rh == 0, msg);
      }
      g_page_nav_delta = 1; run_ms(300);
    }
  }

  if (!strcmp(mode, "needlepix")) {
    // The needle is bright green so nothing else on the dial can be mistaken for it. value 50 of 0..100 points straight up.
    static const struct { const char *name; LayoutType lay; int hub, tip; } C[8] = {
      {"1-cell, no trim", LAYOUT_1CELL, 0, 0}, {"1-cell, hub gap 20%", LAYOUT_1CELL, 20, 0}, {"1-cell, stops 10% short", LAYOUT_1CELL, 0, 10},
      {"1-cell, both (20% and 10%)", LAYOUT_1CELL, 20, 10}, {"1-cell, the most allowed (50% and 35%)", LAYOUT_1CELL, 50, 35},
      {"2x2, no trim", LAYOUT_2X2, 0, 0}, {"2x2, both (20% and 10%)", LAYOUT_2X2, 20, 10}, {"1-cell, over the limit (60% and 60%)", LAYOUT_1CELL, 60, 60} };
    for (int p = 0; p < 8; p++) {
      s_cfg[p].enabled = true; s_cfg[p].layout = C[p].lay;
      for (int c = 0; c < (C[p].lay == LAYOUT_2X2 ? 4 : 1); c++) {
        CellConfig *cc = &s_cfg[p].cells[c]; mk(cc, GAUGE_TYPE_NEEDLE, 0x00FF00);
        cc->min_val = 0; cc->max_val = 100; cc->needle_hub_pct = C[p].hub; cc->needle_tip_pct = C[p].tip; cc->tick_count = 11; cc->major_nth = 5;
      }
    }
    s_has[SIG_AFR] = true;
    build_gauge_app(); index_pages(); run_ms(300);
    printf("needlepix: where the needle really starts and stops, measured in the rendered image\n");
    double baseTip[2] = {0, 0};     // the untrimmed needle's tip radius, 1-cell and 2x2
    for (int p = 0; p < 8; p++) {
      // 1-cell centre is (233,233); the top-left 2x2 cell's centre is its middle. Measure both directions: straight up (50) and another (value 20)
      lv_obj_t *meter = meter_of(s_pageScr[p], 0); lv_area_t ma; lv_obj_get_coords(meter, &ma);
      float cx = (ma.x1 + ma.x2 + 1) / 2.0f, cy = (ma.y1 + ma.y2 + 1) / 2.0f;
      int idx = C[p].lay == LAYOUT_2X2 ? 1 : 0;
      for (int vi = 0; vi < 2; vi++) {
        float value = vi == 0 ? 50.0f : 20.0f;
        s_val[SIG_AFR] = value; for (int k = 0; k < 8; k++) run_ms(60);
        double theta = (135.0 + 270.0 * value / 100.0) * M_PI / 180.0;
        float rmin = 1e9f, rmax = -1e9f;
        for (float rad = 0.0f; rad <= 230.0f; rad += 0.5f) {
          bool hit = false;
          for (int lat = -1; lat <= 1; lat++) {
            int pr, pg, pb;
            px(cx + rad * cosf((float)theta) - lat * sinf((float)theta), cy + rad * sinf((float)theta) + lat * cosf((float)theta), &pr, &pg, &pb);
            if (is_green(pr, pg, pb)) hit = true;
          }
          if (hit) { if (rad < rmin) rmin = rad; if (rad > rmax) rmax = rad; }
        }
        char msg[220];
        if (C[p].hub == 0 && C[p].tip == 0 && vi == 0) baseTip[idx] = rmax;
        double Rr = baseTip[idx] + 12.0;                     // dial radius: the untrimmed tip is 12px inside it
        int hub = C[p].hub, tip = C[p].tip;
        if (hub + tip > 85) tip = 85 - hub;
        double wantMin = hub > 0 ? hub * 0.01 * Rr : 8.0;    // untrimmed, LVGL's round hub dot covers the first ~8px of the needle
        double wantMax = baseTip[idx] - tip * 0.01 * Rr;
        snprintf(msg, sizeof(msg), "[%s] value %.0f: needle starts %.1fpx from the centre (expected about %.1f)", C[p].name, value, rmin, wantMin);
        CHECK(fabs(rmin - wantMin) <= 3.5, msg);
        snprintf(msg, sizeof(msg), "[%s] value %.0f: needle ends %.1fpx from the centre (expected about %.1f)", C[p].name, value, rmax, wantMax);
        CHECK(fabs(rmax - wantMax) <= 3.0, msg);
        if (vi == 0) {
          int cr, cg, cb; px(cx, cy, &cr, &cg, &cb);
          bool dotShown = (cr + cg + cb) > 90;
          snprintf(msg, sizeof(msg), "[%s] the grey hub dot is %s", C[p].name, hub > 0 ? "hidden when there is a gap" : "shown as usual");
          CHECK(hub > 0 ? !dotShown : dotShown, msg);
        }
      }
      g_page_nav_delta = 1; run_ms(300);     // on to the next case's page
    }
  }

  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
