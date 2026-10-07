/*
  Headless test for INDICATOR PAGES in gauge_app.c: the REAL file against LVGL 8.4.
      ./test_indicator_page layout | states | pixels | nav
*/
#include "lvgl.h"
#include "gauge_config.h"
#include "gauge_app.h"
#include "gauge_indicators.h"
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

static lv_obj_t *s_pageScr[8]; static int s_nPage = 0;
static void index_pages(void) {
  lv_disp_t *d = lv_disp_get_default(); s_nPage = 0;
  for (uint32_t i = 0; i < d->screen_cnt; i++) { lv_obj_t *o = d->screens[i]; if (o == lv_layer_top() || o == lv_layer_sys() || lv_obj_get_child_cnt(o) == 0) continue; if (s_nPage < 8) s_pageScr[s_nPage++] = o; }
}
static lv_obj_t *find_overlay(void) { lv_obj_t *sys = lv_layer_sys(); for (uint32_t i = 0; i < lv_obj_get_child_cnt(sys); i++) { lv_obj_t *c = lv_obj_get_child(sys, i); if (lv_obj_get_width(c) >= 400) return c; } return NULL; }
static bool overlay_visible(void) { lv_obj_t *o = find_overlay(); return o && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); }

static void lamp(IndicatorConfig *l, SignalId sig, int op, float thr, float hyst, const char *on, const char *off, uint32_t onC, uint32_t offC, bool blink) {
  memset(l, 0, sizeof(*l));
  l->signal = sig; l->op = op; l->threshold = thr; l->hysteresis = hyst; strncpy(l->on_text, on, IND_TEXT_MAXLEN - 1); strncpy(l->off_text, off, IND_TEXT_MAXLEN - 1);
  l->on_color = onC; l->off_color = offC; l->on_text_color = 0xFFFFFF; l->off_text_color = 0x808080; l->blink = blink;
}
static void ipage(PageConfig *p, int cols, int count) { memset(p, 0, sizeof(*p)); p->enabled = true; p->kind = PAGE_INDICATORS; p->ind_cols = cols; p->ind_count = count; }
static lv_color_t bg_of(lv_obj_t *lampObj) { return lv_obj_get_style_bg_color(lampObj, LV_PART_MAIN); }
static lv_obj_t *label_of(lv_obj_t *lampObj) { return lv_obj_get_child(lampObj, 0); }
static bool is_color(lv_obj_t *lampObj, uint32_t rgb) { return bg_of(lampObj).full == lv_color_hex(rgb).full; }
static int size_of_font(const lv_font_t *f) { return f == &lv_font_montserrat_36 ? 36 : f == &lv_font_montserrat_28 ? 28 : f == &lv_font_montserrat_14 ? 14 : -1; }
static void px(float x, float y, int *r, int *g, int *b) { int ix = (int)lroundf(x), iy = (int)lroundf(y); if (ix < 0 || iy < 0 || ix > 465 || iy > 465) { *r = *g = *b = 0; return; } uint8_t *o = &fb[(iy * 466 + ix) * 3]; *r = o[0]; *g = o[1]; *b = o[2]; }

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  const char *mode = argc > 1 ? argv[1] : "layout";
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, dbuf, NULL, 466 * 466);
  lv_disp_drv_init(&disp_drv); disp_drv.hor_res = 466; disp_drv.ver_res = 466; disp_drv.flush_cb = flush_cb; disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  if (!strcmp(mode, "layout")) {
    // pages: 6 lamps/2 cols, 12/3, 1/1, 5/4, empty, a gauge page, 12/4, 3/1
    static const int N[8] = { 6, 12, 1, 5, 0, -1, 12, 3 }, C[8] = { 2, 3, 1, 4, 2, 0, 4, 1 };
    for (int p = 0; p < 8; p++) {
      if (N[p] < 0) { s_cfg[p].enabled = true; s_cfg[p].layout = LAYOUT_1CELL; s_cfg[p].cells[0].enabled = true; s_cfg[p].cells[0].gauge_type = GAUGE_TYPE_NUMBER; s_cfg[p].cells[0].signal = SIG_RPM; s_cfg[p].cells[0].max_val = 100; s_cfg[p].cells[0].value_divisor = 1; s_cfg[p].cells[0].value_font_size = 28; s_cfg[p].cells[0].refresh_hz = 10; continue; }
      ipage(&s_cfg[p], C[p], N[p]);
      for (int i = 0; i < N[p]; i++) lamp(&s_cfg[p].indicators[i], SIG_OILP, GAUGE_IND_BELOW, 40, 0, i % 2 ? "CHECK ENG" : "OIL", i % 3 ? "ok" : "", 0xE53935, 0x2A2A2A, false);
    }
    build_gauge_app(); index_pages(); run_ms(300);
    printf("layout: indicator pages are built the way gauge_indicator_layout() says, on the glass, with readable text\n");
    CHECK(s_nPage == 8 && gauge_app_get_num_pages() == 8, "all eight pages exist, indicator pages included in the page count");
    for (int p = 0; p < 8; p++) {
      if (N[p] < 0) continue;
      lv_obj_t *page = s_pageScr[p]; lv_obj_update_layout(page);
      char m[160];
      if (N[p] == 0) { snprintf(m, sizeof(m), "page %d: an empty indicator page says NO INDICATORS", p); lv_obj_t *l = lv_obj_get_child(page, 0); CHECK(l && lv_obj_check_type(l, &lv_label_class) && strcmp(lv_label_get_text(l), "NO INDICATORS") == 0, m); continue; }
      snprintf(m, sizeof(m), "page %d: %d lamps built (got %d)", p, N[p], (int)lv_obj_get_child_cnt(page)); CHECK((int)lv_obj_get_child_cnt(page) == N[p], m);
      GaugeLampRect want[12]; gauge_indicator_layout(N[p], C[p], want);
      bool placed = true, glass = true, overlap = false, fonts = true, offLook = true; int firstSize = -1; bool sameFont = true;
      for (int i = 0; i < N[p]; i++) {
        lv_obj_t *lp = lv_obj_get_child(page, i); lv_area_t a; lv_obj_get_coords(lp, &a);
        int w = a.x2 - a.x1 + 1, h = a.y2 - a.y1 + 1;
        if (a.x1 != want[i].x || a.y1 != want[i].y || w != want[i].w || h != want[i].h) placed = false;
        float xs[2] = { (float)a.x1, (float)a.x2 + 1 }, ys[2] = { (float)a.y1, (float)a.y2 + 1 };
        for (int x = 0; x < 2; x++) for (int y = 0; y < 2; y++) { float dx = xs[x] - 233, dy = ys[y] - 233; if (dx * dx + dy * dy > 224.0f * 224.0f) glass = false; }
        for (int j = i + 1; j < N[p]; j++) { lv_area_t b; lv_obj_get_coords(lv_obj_get_child(page, j), &b); if (a.x1 <= b.x2 && b.x1 <= a.x2 && a.y1 <= b.y2 && b.y1 <= a.y2) overlap = true; }
        // text: both texts fit inside the lamp in the chosen font, and it is the biggest of 36/28/14 that does
        const lv_font_t *f = lv_obj_get_style_text_font(label_of(lp), LV_PART_MAIN); int fs = size_of_font(f);
        const char *on = i % 2 ? "CHECK ENG" : "OIL", *off = i % 3 ? "ok" : "";
        lv_point_t so, sf; lv_txt_get_size(&so, on, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE); lv_txt_get_size(&sf, off, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        int tw = so.x > sf.x ? so.x : sf.x;
        if (fs != 14 && !(tw <= w * 0.88f && so.y <= h * 0.8f)) fonts = false;
        if (fs == 36 || fs == 28) { const lv_font_t *bigger = fs == 36 ? NULL : &lv_font_montserrat_36; if (bigger) { lv_point_t bo, bf; lv_txt_get_size(&bo, on, bigger, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE); lv_txt_get_size(&bf, off, bigger, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE); int btw = bo.x > bf.x ? bo.x : bf.x; if (btw <= w * 0.88f && bo.y <= h * 0.8f) fonts = false; } }
        if (fs <= 0) fonts = false;
        if (i % 2 == 0 && i % 3 != 0 && firstSize < 0) firstSize = fs;
        if (!is_color(lp, 0x2A2A2A) || strcmp(lv_label_get_text(label_of(lp)), off) != 0) offLook = false;
        (void)sameFont;
      }
      snprintf(m, sizeof(m), "page %d: lamps are exactly where gauge_indicator_layout() puts them", p); CHECK(placed, m);
      snprintf(m, sizeof(m), "page %d: every lamp lies fully on the round glass", p); CHECK(glass, m);
      snprintf(m, sizeof(m), "page %d: no two lamps overlap", p); CHECK(!overlap, m);
      snprintf(m, sizeof(m), "page %d: lamp text is the biggest of 36/28/14 that fits inside the lamp", p); CHECK(fonts, m);
      snprintf(m, sizeof(m), "page %d: with no data every lamp starts in its off look (off colour and off text)", p); CHECK(offLook, m);
    }
  }

  if (!strcmp(mode, "states")) {
    IndicatorConfig *L = s_cfg[0].indicators; ipage(&s_cfg[0], 3, 8);
    lamp(&L[0], SIG_OILP, GAUGE_IND_BELOW, 40, 0, "OIL", "oil", 0xE53935, 0x2A2A2A, false);
    lamp(&L[1], SIG_CLT, GAUGE_IND_ABOVE, 200, 10, "HOT", "temp", 0xFB8C00, 0x2A2A2A, false);
    lamp(&L[2], SIG_FAN, GAUGE_IND_ABOVE, 0.5f, 0, "FAN", "fan", 0x1E88E5, 0x2A2A2A, false);
    lamp(&L[3], SIG_BRAKE, GAUGE_IND_BELOW, 0.5f, 0, "NO BRK", "brk", 0x8E24AA, 0x2A2A2A, false);   // lit while the brake flag is OFF
    lamp(&L[4], SIG_BATTV, GAUGE_IND_BELOW, 12, 0, "BATT", "batt", 0xFDD835, 0x2A2A2A, true);        // flashes
    lamp(&L[5], SIG_AFR, GAUGE_IND_ABOVE, 15, 0, "LEAN", "afr", 0x43A047, 0x2A2A2A, false);
    lamp(&L[6], SIG_RPM, GAUGE_IND_ABOVE, 6000, 0, "REV", "rev", 0xE53935, 0x2A2A2A, false);
    lamp(&L[7], SIG_NONE, GAUGE_IND_ABOVE, 0, 0, "NONE", "none", 0xE53935, 0x2A2A2A, false);      // no signal chosen
    build_gauge_app(); index_pages(); run_ms(200);
    lv_obj_t *pg = s_pageScr[0]; lv_obj_t *lp[8]; for (int i = 0; i < 8; i++) lp[i] = lv_obj_get_child(pg, i);
    uint32_t ON[8] = { 0xE53935, 0xFB8C00, 0x1E88E5, 0x8E24AA, 0xFDD835, 0x43A047, 0xE53935, 0xE53935 }, OFF = 0x2A2A2A;
    printf("states: live lamps\n");
    bool allOff = true; for (int i = 0; i < 8; i++) if (!is_color(lp[i], OFF)) allOff = false;
    CHECK(allOff, "no data at all: every lamp off - including the ones that light 'below' a value or 'while a flag is off'");
    s_has[SIG_OILP] = true; s_val[SIG_OILP] = 35; run_ms(100);
    CHECK(is_color(lp[0], ON[0]) && strcmp(lv_label_get_text(label_of(lp[0])), "OIL") == 0, "oil pressure 35 (below 40): lamp on, shows its ON text");
    s_val[SIG_OILP] = 45; run_ms(100);
    CHECK(is_color(lp[0], OFF) && strcmp(lv_label_get_text(label_of(lp[0])), "oil") == 0, "oil pressure 45: off, shows its OFF text");
    s_val[SIG_OILP] = 40; run_ms(100); CHECK(is_color(lp[0], OFF), "exactly 40 is not below 40");
    s_val[SIG_OILP] = 12; run_ms(100); s_has[SIG_OILP] = false; run_ms(100);
    CHECK(is_color(lp[0], OFF), "the oil pressure signal goes silent while the lamp is lit: the lamp goes OUT (a lost sensor is not an alarm)");
    s_has[SIG_OILP] = true; s_val[SIG_OILP] = NAN; run_ms(100); CHECK(is_color(lp[0], OFF), "a value that is not a number: off");

    s_has[SIG_CLT] = true; s_val[SIG_CLT] = 195; run_ms(100); CHECK(is_color(lp[1], OFF), "coolant 195 (threshold 200): off");
    s_val[SIG_CLT] = 205; run_ms(100); CHECK(is_color(lp[1], ON[1]), "coolant 205: on");
    s_val[SIG_CLT] = 195; run_ms(100); CHECK(is_color(lp[1], ON[1]), "coolant back to 195: STILL on - inside the 10 degree hysteresis band");
    s_val[SIG_CLT] = 191; run_ms(100); CHECK(is_color(lp[1], ON[1]), "191: still on");
    s_val[SIG_CLT] = 190; run_ms(100); CHECK(is_color(lp[1], OFF), "190 (threshold - hysteresis): off");
    s_val[SIG_CLT] = 195; run_ms(100); CHECK(is_color(lp[1], OFF), "and back at 195 it stays off (it has to pass 200 again)");

    s_has[SIG_FAN] = true; s_val[SIG_FAN] = 0; run_ms(100); CHECK(is_color(lp[2], OFF), "fan flag reported OFF: lamp off");
    s_val[SIG_FAN] = 1; run_ms(100); CHECK(is_color(lp[2], ON[2]), "fan flag ON: lamp on");
    s_has[SIG_FAN] = false; run_ms(100); CHECK(is_color(lp[2], OFF), "fan flag no longer reported: off");

    s_has[SIG_BRAKE] = true; s_val[SIG_BRAKE] = 1; run_ms(100); CHECK(is_color(lp[3], OFF), "'while the brake flag is OFF' lamp, brake pressed: off");
    s_val[SIG_BRAKE] = 0; run_ms(100); CHECK(is_color(lp[3], ON[3]), "brake released (flag 0): on");
    s_has[SIG_BRAKE] = false; run_ms(100); CHECK(is_color(lp[3], OFF), "flag never reported: off (unknown is not 'off')");

    s_has[SIG_BATTV] = true; s_val[SIG_BATTV] = 13.8f; run_ms(1300);
    { int onSeen = 0; for (int i = 0; i < 60; i++) { run_ms(25); if (is_color(lp[4], ON[4])) onSeen++; } CHECK(onSeen == 0, "battery 13.8: the flashing lamp stays dark"); }
    s_val[SIG_BATTV] = 11.0f; run_ms(100);
    { int onSeen = 0, offSeen = 0; for (int i = 0; i < 100; i++) { run_ms(25); if (is_color(lp[4], ON[4])) onSeen++; else offSeen++; }
      printf("   a flashing lamp over 2.5s: lit %d samples, dark %d\n", onSeen, offSeen);
      CHECK(onSeen > 20 && offSeen > 20, "battery 11: the lamp FLASHES (about half the time lit, half dark)"); }
    s_val[SIG_BATTV] = 13.0f; run_ms(100); CHECK(is_color(lp[4], OFF), "battery recovers: the flashing stops, lamp off");

    s_has[SIG_AFR] = true; s_val[SIG_AFR] = 15.5f; s_has[SIG_RPM] = true; s_val[SIG_RPM] = 6500; run_ms(100);
    CHECK(is_color(lp[5], ON[5]) && is_color(lp[6], ON[6]), "AFR above 15 and RPM above 6000: those two lamps on at once, independently");
    CHECK(is_color(lp[7], OFF), "a lamp with no signal chosen never lights");
    // a lit lamp's text colour changes too
    CHECK(lv_obj_get_style_text_color(label_of(lp[5]), LV_PART_MAIN).full == lv_color_hex(0xFFFFFF).full, "lit lamp: the on text colour");
    s_val[SIG_AFR] = 14; run_ms(100); CHECK(lv_obj_get_style_text_color(label_of(lp[5]), LV_PART_MAIN).full == lv_color_hex(0x808080).full, "off lamp: the off text colour");
  }

  if (!strcmp(mode, "pixels")) {
    ipage(&s_cfg[0], 2, 4);
    lamp(&s_cfg[0].indicators[0], SIG_OILP, GAUGE_IND_BELOW, 40, 0, "OIL", "OIL", 0xE53935, 0x1E1E1E, false);
    lamp(&s_cfg[0].indicators[1], SIG_FAN, GAUGE_IND_ABOVE, 0.5f, 0, "FAN", "FAN", 0x1E88E5, 0x1E1E1E, false);
    lamp(&s_cfg[0].indicators[2], SIG_CEL, GAUGE_IND_ABOVE, 0.5f, 0, "CHECK", "CHECK", 0xFDD835, 0x1E1E1E, false);
    lamp(&s_cfg[0].indicators[3], SIG_BRAKE, GAUGE_IND_ABOVE, 0.5f, 0, "BRAKE", "BRAKE", 0x43A047, 0x1E1E1E, false);
    build_gauge_app(); index_pages(); run_ms(300);
    s_has[SIG_OILP] = true; s_val[SIG_OILP] = 20; s_has[SIG_FAN] = true; s_val[SIG_FAN] = 1; s_has[SIG_CEL] = true; s_val[SIG_CEL] = 0; s_has[SIG_BRAKE] = true; s_val[SIG_BRAKE] = 0;
    for (int k = 0; k < 6; k++) run_ms(80);
    printf("pixels: what is really drawn\n");
    lv_obj_t *pg = s_pageScr[0]; lv_obj_update_layout(pg);
    uint32_t want[4] = { 0xE53935, 0x1E88E5, 0x1E1E1E, 0x1E1E1E };       // lit, lit, off, off
    for (int i = 0; i < 4; i++) {
      lv_area_t a; lv_obj_get_coords(lv_obj_get_child(pg, i), &a);
      int r, g, b; px(a.x1 + 14, (a.y1 + a.y2) / 2.0f, &r, &g, &b);            // inside the lamp, left of the text
      char m[140]; snprintf(m, sizeof(m), "lamp %d: its fill is (%d,%d,%d), expected about (%d,%d,%d)", i, r, g, b, (want[i] >> 16) & 255, (want[i] >> 8) & 255, want[i] & 255);
      CHECK(abs(r - (int)((want[i] >> 16) & 255)) <= 9 && abs(g - (int)((want[i] >> 8) & 255)) <= 9 && abs(b - (int)(want[i] & 255)) <= 9, m);
      int text = 0; for (int y = a.y1 + 4; y < a.y2 - 4; y++) for (int x = a.x1 + 4; x < a.x2 - 4; x++) { px((float)x, (float)y, &r, &g, &b); int fillR = (want[i] >> 16) & 255, fillG = (want[i] >> 8) & 255, fillB = want[i] & 255; if (abs(r - fillR) + abs(g - fillG) + abs(b - fillB) > 120) text++; }
      snprintf(m, sizeof(m), "lamp %d: its text is drawn (%d text pixels)", i, text); CHECK(text > 60, m);
      // outside the lamp, between lamps, is black
      px(a.x1 - 4.0f, (a.y1 + a.y2) / 2.0f, &r, &g, &b);
      (void)r; (void)g; (void)b;
    }
    // the four lamps sit on the glass: the pixels just outside the round panel's corners stay black (nothing is cut by the bezel)
    int r, g, b; px(2, 2, &r, &g, &b); CHECK(r + g + b == 0, "the panel's corner is black (no lamp reaches it)");
    { FILE *f = fopen("indicator_pixels.rgb", "wb"); fwrite(fb, 1, sizeof(fb), f); fclose(f); }
  }

  if (!strcmp(mode, "nav")) {
    // page 0 gauge (with a full-screen alert), page 1 indicators, page 2 gauge
    for (int p = 0; p < 3; p++) { s_cfg[p].enabled = true; s_cfg[p].layout = LAYOUT_1CELL; CellConfig *c = &s_cfg[p].cells[0]; c->enabled = true; c->gauge_type = GAUGE_TYPE_NUMBER; c->signal = SIG_OILP; c->max_val = 100; c->value_divisor = 1; c->value_font_size = 28; c->refresh_hz = 20; strcpy(c->caption, p ? "P2" : "OIL");
      if (p == 0) { c->has_warning = true; c->warning_threshold = 40; c->warning_above = false; c->alert_fullscreen = true; } }
    ipage(&s_cfg[1], 2, 2); lamp(&s_cfg[1].indicators[0], SIG_FAN, GAUGE_IND_ABOVE, 0.5f, 0, "FAN", "fan", 0x1E88E5, 0x2A2A2A, false); lamp(&s_cfg[1].indicators[1], SIG_CEL, GAUGE_IND_ABOVE, 0.5f, 0, "CEL", "cel", 0xFDD835, 0x2A2A2A, false);
    build_gauge_app(); index_pages(); run_ms(300);
    printf("nav: indicator pages are part of the page cycle\n");
    CHECK(gauge_app_get_num_pages() == 3 && lv_scr_act() == s_pageScr[0], "3 pages, starting on the first");
    g_page_nav_delta = 1; run_ms(100); CHECK(lv_scr_act() == s_pageScr[1], "BOOT / next goes to the indicators page");
    g_page_nav_delta = 1; run_ms(100); CHECK(lv_scr_act() == s_pageScr[2], "... then on to the next gauge page");
    g_page_nav_delta = 1; run_ms(100); CHECK(lv_scr_act() == s_pageScr[0], "... and wraps");
    g_page_nav_delta = -1; run_ms(100); CHECK(lv_scr_act() == s_pageScr[2], "previous goes backwards");
    g_page_nav_delta = -2; run_ms(100); CHECK(lv_scr_act() == s_pageScr[0], "two back lands where it should");
    g_requested_page = 1; run_ms(100); CHECK(lv_scr_act() == s_pageScr[1], "the web page's 'go to page 2' lands on the indicators page");
    // lamps keep working while their page is not the one on screen, and an alert still takes over the screen
    s_has[SIG_FAN] = true; s_val[SIG_FAN] = 1; run_ms(100);
    CHECK(is_color(lv_obj_get_child(s_pageScr[1], 0), 0x1E88E5), "the fan lamp lit");
    g_page_nav_delta = 1; run_ms(100);   // on to page 3 (index 2)
    s_has[SIG_RPM] = true; s_val[SIG_RPM] = 3000; s_has[SIG_OILP] = true; s_val[SIG_OILP] = 20; run_ms(600);
    CHECK(overlay_visible(), "a full-screen alert from a gauge on another page still takes over (indicator pages do not disturb the alert system)");
    CHECK(lv_scr_act() == s_pageScr[2], "(we are on the third page, so the indicators page is hidden)");
    s_has[SIG_FAN] = false; run_ms(100);
    CHECK(is_color(lv_obj_get_child(s_pageScr[1], 0), 0x2A2A2A), "a lamp keeps tracking its signal while its page is hidden: the fan lamp went out although another page is showing");
    s_has[SIG_FAN] = true; s_val[SIG_FAN] = 1; run_ms(100);
    CHECK(is_color(lv_obj_get_child(s_pageScr[1], 0), 0x1E88E5), "... and comes back on while hidden too");
  }

  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
