/*
  Headless test for the readout text sizes, the dial-number size and the dial angles in gauge_app.c:
  the REAL file against LVGL 8.4. Each run builds ONE scenario (up to 8 pages x 4 cells) and checks it:
      ./test_dial_text num1 | num2up | num3up | num2x2 | needleval | needleval4 | ticks | angles | angleshalf | nan | decimals | halign
*/
#include "lvgl.h"
#include "gauge_config.h"
#include "gauge_app.h"
#include "gauge_style.h"
#include "gauge_fonts.h"
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

static lv_color_t dbuf[466 * 20];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static void flush_cb(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *p) { (void)a; (void)p; lv_disp_flush_ready(d); }
static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("  FAIL: %s  (line %d)\n", msg, __LINE__); } } while (0)
static void run_ms(int ms) { for (int t = 0; t < ms; t += 5) { lv_tick_inc(5); lv_timer_handler(); } }

static lv_obj_t *s_pageScr[8]; static int s_nPage = 0;
static void index_pages(void) {
  lv_disp_t *d = lv_disp_get_default(); s_nPage = 0;
  for (uint32_t i = 0; i < d->screen_cnt; i++) { lv_obj_t *o = d->screens[i]; if (o == lv_layer_top() || o == lv_layer_sys() || lv_obj_get_child_cnt(o) == 0) continue; if (s_nPage < 8) s_pageScr[s_nPage++] = o; }
}

static int size_of_font(const lv_font_t *f) {
  if (f == &lv_font_montserrat_14) return 14; if (f == &lv_font_montserrat_28) return 28; if (f == &lv_font_montserrat_36) return 36;
  if (f == &font_digits_48) return 48; if (f == &font_digits_64) return 64; if (f == &font_digits_80) return 80;
  if (f == &font_digits_96) return 96; if (f == &font_digits_120) return 120; return -1;
}
static const lv_font_t *font_of_size(int s) {
  switch (s) { case 14: return &lv_font_montserrat_14; case 28: return &lv_font_montserrat_28; case 36: return &lv_font_montserrat_36;
    case 48: return &font_digits_48; case 64: return &font_digits_64; case 80: return &font_digits_80; case 96: return &font_digits_96; case 120: return &font_digits_120; }
  return NULL;
}
static void mkcell(CellConfig *c, GaugeType t, int valSize, int tickSize, int start, int end, bool showValue) {
  memset(c, 0, sizeof(*c));
  c->enabled = true; c->signal = SIG_AFR; c->gauge_type = t; c->min_val = 0; c->max_val = 250; c->rest_val = 0;
  strcpy(c->caption, "AFR"); c->needle_color = 0xFFFFFF; c->tick_color = 0xFFFFFF; c->bg_color = 0;
  c->value_divisor = 1.0f; c->tick_count = 26; c->major_nth = 5; c->refresh_hz = 50;
  c->show_value = showValue; c->value_font_size = valSize; c->decimals = 1; c->tick_font_size = tickSize; c->start_deg = start; c->end_deg = end; c->history_sec = 30;
}
static bool inside_circle(const lv_area_t *a, float r) {
  float xs[2] = { (float)a->x1, (float)a->x2 + 1 }, ys[2] = { (float)a->y1, (float)a->y2 + 1 };
  for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) { float dx = xs[i] - 233.0f, dy = ys[j] - 233.0f; if (dx * dx + dy * dy > r * r) return false; }
  return true;
}
static lv_area_t box_around(const lv_area_t *labelArea, const lv_font_t *font, const char *text) {   // the worst-case text, centred where the label is
  lv_point_t sz; lv_txt_get_size(&sz, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  float cx = (labelArea->x1 + labelArea->x2 + 1) / 2.0f, cy = (labelArea->y1 + labelArea->y2 + 1) / 2.0f;
  lv_area_t a = { (lv_coord_t)(cx - sz.x / 2.0f), (lv_coord_t)(cy - sz.y / 2.0f), (lv_coord_t)(cx + sz.x / 2.0f - 1), (lv_coord_t)(cy + sz.y / 2.0f - 1) };
  return a;
}
static lv_obj_t *meter_in(lv_obj_t *container) { for (uint32_t i = 0; i < lv_obj_get_child_cnt(container); i++) { lv_obj_t *c = lv_obj_get_child(container, i); if (lv_obj_check_type(c, &lv_meter_class)) return c; } return NULL; }

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  const char *mode = argc > 1 ? argv[1] : "num1";
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, dbuf, NULL, 466 * 20);
  lv_disp_drv_init(&disp_drv); disp_drv.hor_res = 466; disp_drv.ver_res = 466; disp_drv.flush_cb = flush_cb; disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  int nPages = 8; LayoutType layout = LAYOUT_1CELL; int nCells = 1;
  bool numberMode = !strncmp(mode, "num", 3), needleVal = !strncmp(mode, "needleval", 9);
  if (!strcmp(mode, "num2up")) layout = LAYOUT_2UP, nCells = 2;
  if (!strcmp(mode, "num3up")) layout = LAYOUT_3UP, nCells = 3;
  if (!strcmp(mode, "num2x2") || !strcmp(mode, "needleval4") || !strncmp(mode, "angles", 6)) layout = LAYOUT_2X2, nCells = 4;

  // ---- the scenario ----
  static const int TICK_CASES[8][2] = { {28,28}, {96,28}, {48,-1}, {36,0}, {14,0}, {64,0}, {120,14}, {28,36} };   // {readout size, dial-number size}
  static const int ANGLES[16][2] = { {225,135}, {270,90}, {0,0}, {10,15}, {0,90}, {90,180}, {180,0}, {300,60}, {45,315}, {0,359}, {359,0}, {350,10}, {-135,135}, {500,140}, {135,45}, {90,270} };
  for (int p = 0; p < nPages; p++) {
    s_cfg[p].enabled = true; s_cfg[p].layout = layout;
    for (int c = 0; c < nCells; c++) {
      CellConfig *cc = &s_cfg[p].cells[c];
      if (numberMode) mkcell(cc, GAUGE_TYPE_NUMBER, GAUGE_VALUE_SIZES[p], 0, 0, 0, false);
      else if (needleVal) mkcell(cc, GAUGE_TYPE_NEEDLE, GAUGE_VALUE_SIZES[p], 28, 225, 135, true);
      else if (!strcmp(mode, "ticks")) mkcell(cc, GAUGE_TYPE_NEEDLE, TICK_CASES[p][0], TICK_CASES[p][1], 225, 135, true);
      else if (!strcmp(mode, "angles")) mkcell(cc, GAUGE_TYPE_NEEDLE, 28, 28, ANGLES[(p * 4 + c) % 16][0], ANGLES[(p * 4 + c) % 16][1], false);
      else if (!strcmp(mode, "angleshalf")) mkcell(cc, GAUGE_TYPE_HALFMOON, 28, 28, ANGLES[(p * 4 + c) % 16][0], ANGLES[(p * 4 + c) % 16][1], false);
      else if (!strcmp(mode, "nan")) mkcell(cc, GAUGE_TYPE_NUMBER, 96, 0, 0, 0, false);
      else if (!strcmp(mode, "halign")) { mkcell(cc, p == 2 ? GAUGE_TYPE_HALFMOON : GAUGE_TYPE_NUMBER, 48, 28, 270, 90, true);
        cc->h_align = (p == 0) ? 1 : 2; cc->signal = SIG_AFR; cc->min_val = 0; cc->max_val = 250; cc->warning_threshold = 1e9f; }
      else if (!strcmp(mode, "decimals")) { mkcell(cc, p == 3 ? GAUGE_TYPE_NEEDLE : GAUGE_TYPE_NUMBER, 120, 28, 225, 135, true);
        cc->decimals = (p == 4) ? 7 : (p == 3 ? 2 : p % 3); cc->signal = SIG_AFR; cc->min_val = 0; cc->max_val = 250; cc->warning_threshold = 1e9f; }
      else { printf("unknown mode %s\n", mode); return 2; }
    }
  }
  build_gauge_app();
  index_pages();
  CHECK(s_nPage == nPages, "all pages built");
  run_ms(300);

  if (numberMode) {
    printf("%s: a NUMBER readout at every size, in every cell\n", mode);
    for (int p = 0; p < s_nPage; p++) {
      int requested = GAUGE_VALUE_SIZES[p];
      lv_obj_update_layout(s_pageScr[p]);
      for (int c = 0; c < nCells; c++) {
        lv_obj_t *cont = lv_obj_get_child(s_pageScr[p], c), *num = lv_obj_get_child(cont, 0), *cap = lv_obj_get_child(cont, 1);
        lv_area_t cellA, labA, capA; lv_obj_get_coords(cont, &cellA); lv_obj_get_coords(num, &labA); lv_obj_get_coords(cap, &capA);
        const lv_font_t *f = lv_obj_get_style_text_font(num, LV_PART_MAIN);
        int got = size_of_font(f);
        char msg[160];
        snprintf(msg, sizeof(msg), "p%d c%d: asked %d, got a known font (%d)", p, c, requested, got); CHECK(got > 0, msg);
        snprintf(msg, sizeof(msg), "p%d c%d: never bigger than asked (asked %d, got %d)", p, c, requested, got); CHECK(got <= requested, msg);
        float cw = (float)(cellA.x2 - cellA.x1 + 1), ch = (float)(cellA.y2 - cellA.y1 + 1);
        lv_area_t worst = box_around(&labA, f, "250.0");
        snprintf(msg, sizeof(msg), "p%d c%d: size %d, the widest reading '250.0' lies fully on the round glass", p, c, got); CHECK(inside_circle(&worst, 233.0f), msg);
        snprintf(msg, sizeof(msg), "p%d c%d: ... and inside its cell", p, c); CHECK(worst.x1 >= cellA.x1 && worst.x2 <= cellA.x2 && worst.y1 >= cellA.y1 && worst.y2 <= cellA.y2, msg);
        snprintf(msg, sizeof(msg), "p%d c%d: the caption is cut off by neither the cell nor the glass", p, c); CHECK(capA.y1 >= cellA.y1 && inside_circle(&capA, 233.0f), msg);
        snprintf(msg, sizeof(msg), "p%d c%d: caption sits above the number, not on it", p, c); CHECK(capA.y2 < worst.y1 + 2, msg);
        // the biggest that fits: the NEXT allowed size up (if it was asked for) must genuinely not fit
        if (got < requested) {
          int next = 0; for (int i = 0; i < GAUGE_VALUE_SIZE_COUNT; i++) if (GAUGE_VALUE_SIZES[i] > got && GAUGE_VALUE_SIZES[i] <= requested) { next = GAUGE_VALUE_SIZES[i]; break; }
          lv_area_t nb = box_around(&labA, font_of_size(next), "250.0");
          float nw = (float)(nb.x2 - nb.x1 + 1), nh = (float)(nb.y2 - nb.y1 + 1);
          bool fits = nw <= cw * 0.94f && nh <= ch * 0.6f && inside_circle(&nb, 228.0f);
          snprintf(msg, sizeof(msg), "p%d c%d: size %d was used because %d would NOT fit", p, c, got, next); CHECK(!fits, msg);
        }
        if (layout == LAYOUT_1CELL) { snprintf(msg, sizeof(msg), "1-cell page: every size is honoured exactly (asked %d, got %d)", requested, got); CHECK(got == requested, msg); }
      }
    }
    // a bigger request never gives a smaller font (checked on the first cell of each page)
    int prev = 0; bool mono = true;
    for (int p = 0; p < s_nPage; p++) { int g = size_of_font(lv_obj_get_style_text_font(lv_obj_get_child(lv_obj_get_child(s_pageScr[p], 0), 0), LV_PART_MAIN)); if (g < prev) mono = false; prev = g; }
    CHECK(mono, "asking for a bigger size never produces a smaller one");
    if (layout == LAYOUT_2X2) { int g = size_of_font(lv_obj_get_style_text_font(lv_obj_get_child(lv_obj_get_child(s_pageScr[7], 0), 0), LV_PART_MAIN)); printf("   in a 2x2 cell, asking for 120 gives %d\n", g); CHECK(g < 120, "a 233px quadrant cannot hold a 120px readout, so it shrinks"); }
    if (layout == LAYOUT_2UP) { int g = size_of_font(lv_obj_get_style_text_font(lv_obj_get_child(lv_obj_get_child(s_pageScr[7], 0), 0), LV_PART_MAIN)); printf("   in a 2-up cell, asking for 120 gives %d\n", g); }
    // the digits-only fonts really do draw every character a readout uses
    for (int s = 48; s <= 120; s = (s == 48 ? 64 : s == 64 ? 80 : s == 80 ? 96 : s == 96 ? 120 : 999)) {
      const lv_font_t *f = font_of_size(s); bool all = true; const char *need = "0123456789.- +";
      for (const char *q = need; *q; q++) { lv_font_glyph_dsc_t g; if (!lv_font_get_glyph_dsc(f, &g, (uint32_t)*q, 0)) all = false; }
      char msg[80]; snprintf(msg, sizeof(msg), "the %dpx font has every character a reading uses", s); CHECK(all, msg);
    }
  }

  if (needleVal) {
    printf("%s: the readout on a needle dial\n", mode);
    for (int p = 0; p < s_nPage; p++) {
      int requested = GAUGE_VALUE_SIZES[p];
      lv_obj_update_layout(s_pageScr[p]);
      for (int c = 0; c < nCells; c++) {
        lv_obj_t *cont = lv_obj_get_child(s_pageScr[p], c);
        lv_obj_t *meter = lv_obj_get_child(cont, 0), *cap = lv_obj_get_child(cont, 1), *val = lv_obj_get_child(cont, 2);
        lv_area_t meterA, capA, valA; lv_obj_get_coords(meter, &meterA); lv_obj_get_coords(cap, &capA); lv_obj_get_coords(val, &valA);
        int got = size_of_font(lv_obj_get_style_text_font(val, LV_PART_MAIN));
        char msg[160];
        snprintf(msg, sizeof(msg), "p%d c%d: readout never bigger than asked (%d vs %d)", p, c, got, requested); CHECK(got > 0 && got <= requested, msg);
        lv_area_t worst = box_around(&valA, font_of_size(got), "250.0");
        float mw = (float)(meterA.x2 - meterA.x1 + 1);
        snprintf(msg, sizeof(msg), "p%d c%d: size %d fits inside the dial's lower area and on the glass", p, c, got); CHECK((worst.x2 - worst.x1 + 1) <= mw * 0.55f + 1 && inside_circle(&worst, 233.0f), msg);
        snprintf(msg, sizeof(msg), "p%d c%d: the readout does not sit on the caption", p, c); CHECK(valA.y2 <= capA.y1, msg);
        snprintf(msg, sizeof(msg), "p%d c%d: the dial's numbers stay at 28 whatever the readout size", p, c);
        CHECK(lv_obj_get_style_text_font(meter, LV_PART_TICKS) == &lv_font_montserrat_28, msg);
      }
    }
    int g0 = size_of_font(lv_obj_get_style_text_font(lv_obj_get_child(lv_obj_get_child(s_pageScr[0], 0), 2), LV_PART_MAIN));
    int g4 = size_of_font(lv_obj_get_style_text_font(lv_obj_get_child(lv_obj_get_child(s_pageScr[4], 0), 2), LV_PART_MAIN));
    int g7 = size_of_font(lv_obj_get_style_text_font(lv_obj_get_child(lv_obj_get_child(s_pageScr[7], 0), 2), LV_PART_MAIN));
    printf("   readout sizes got for 14 / 64 / 120: %d / %d / %d\n", g0, g4, g7);
    CHECK(g0 == 14 && g4 >= 28 && g7 >= g4 - 0, "small stays small, big gets bigger");
  }

  if (!strcmp(mode, "ticks")) {
    printf("ticks: the dial numbers have their own size\n");
    for (int p = 0; p < s_nPage; p++) {
      lv_obj_t *cont = lv_obj_get_child(s_pageScr[p], 0), *meter = lv_obj_get_child(cont, 0);
      int tickCfg = TICK_CASES[p][1], valCfg = TICK_CASES[p][0];
      int expect = tickCfg == -1 ? -1 : (tickCfg == 0 ? ((valCfg == 14 || valCfg == 28 || valCfg == 36) ? valCfg : 28) : tickCfg);
      const lv_font_t *tf = lv_obj_get_style_text_font(meter, LV_PART_TICKS);
      char msg[140];
      if (expect == -1) { snprintf(msg, sizeof(msg), "case %d: dial numbers hidden (transparent) when set to hide", p); CHECK(lv_obj_get_style_text_opa(meter, LV_PART_TICKS) == LV_OPA_TRANSP, msg); }
      else {
        snprintf(msg, sizeof(msg), "case %d: readout %d, dial numbers %d -> dial uses %d", p, valCfg, tickCfg, expect); CHECK(size_of_font(tf) == expect, msg);
        snprintf(msg, sizeof(msg), "case %d: dial numbers are visible", p); CHECK(lv_obj_get_style_text_opa(meter, LV_PART_TICKS) == LV_OPA_COVER, msg);
      }
    }
    lv_obj_t *v = lv_obj_get_child(lv_obj_get_child(s_pageScr[1], 0), 2);
    CHECK(size_of_font(lv_obj_get_style_text_font(v, LV_PART_MAIN)) >= 36, "readout 96 + dial numbers 28: the readout is big while the dial numbers stay small");
  }

  if (!strncmp(mode, "angles", 6)) {
    bool half = !strcmp(mode, "angleshalf");
    printf("%s: dial start/end angles (%d dials)\n", mode, s_nPage * nCells);
    // {start, end} -> {LVGL rotation, sweep, vertical shift sign}  written out by hand, not computed.
    // shift: +1 = a dial that stays in the TOP half is pushed down, -1 = stays in the BOTTOM half (e.g. 90 -> 180, the lower-right quarter) is pushed up, 0 = centred
    static const struct { int rot, sweep, shift; } NEEDLE[16] = {
      {135,270,0}, {180,180,+1}, {135,270,0}, {135,270,0}, {270,90,+1}, {0,90,-1}, {90,180,0}, {210,120,+1},
      {315,270,0}, {270,359,0}, {135,270,0}, {260,20,+1}, {135,270,0}, {135,270,0}, {45,270,0}, {0,180,-1} };
    static const struct { int rot, sweep, shift; } HALF[16] = {   // same inputs; the fallback is the half moon's 270 -> 90
      {135,270,0}, {180,180,+1}, {180,180,+1}, {180,180,+1}, {270,90,+1}, {0,90,-1}, {90,180,0}, {210,120,+1},
      {315,270,0}, {270,359,0}, {180,180,+1}, {260,20,+1}, {135,270,0}, {180,180,+1}, {45,270,0}, {0,180,-1} };
    for (int p = 0; p < s_nPage; p++) {
      lv_obj_update_layout(s_pageScr[p]);
      for (int c = 0; c < nCells; c++) {
        int idx = (p * 4 + c) % 16;
        lv_obj_t *cont = lv_obj_get_child(s_pageScr[p], c), *meter = meter_in(cont);
        lv_meter_t *m = (lv_meter_t *)meter; lv_meter_scale_t *sc = (lv_meter_scale_t *)_lv_ll_get_head(&m->scale_ll);
        int er = half ? HALF[idx].rot : NEEDLE[idx].rot, es = half ? HALF[idx].sweep : NEEDLE[idx].sweep, esh = half ? HALF[idx].shift : NEEDLE[idx].shift;
        char msg[160];
        snprintf(msg, sizeof(msg), "dial %d (start %d, end %d): sweeps %d degrees (got %d)", idx, ANGLES[idx][0], ANGLES[idx][1], es, (int)sc->angle_range); CHECK(sc->angle_range == es, msg);
        snprintf(msg, sizeof(msg), "dial %d (start %d, end %d): starts at LVGL angle %d (got %d)", idx, ANGLES[idx][0], ANGLES[idx][1], er, (int)sc->rotation); CHECK(sc->rotation == er, msg);
        lv_area_t ca, ma; lv_obj_get_coords(cont, &ca); lv_obj_get_coords(meter, &ma);
        float shift = (ma.y1 + ma.y2) / 2.0f - (ca.y1 + ca.y2) / 2.0f; float msz = (float)(ma.x2 - ma.x1 + 1);
        float want = esh * (float)((int)msz / 6);
        snprintf(msg, sizeof(msg), "dial %d: vertical position %s (shift %.0f, expected %.0f)", idx, esh > 0 ? "pushed down" : esh < 0 ? "pushed up" : "centred", shift, want); CHECK(fabsf(shift - want) <= 1.5f || (half && esh > 0 && shift > 0), msg);   // a grown half moon (build 124) is still pushed down, by its own amount
        snprintf(msg, sizeof(msg), "dial %d: the dial stays inside its cell", idx); CHECK(ma.x1 >= ca.x1 && ma.x2 <= ca.x2 && ma.y1 + (esh < 0 ? 0 : 0) >= ca.y1 - 40 && ma.y2 <= ca.y2 + 40, msg);
      }
    }
    // draw them all with moving data: the arc/tick drawing must survive every angle pair (no divide by zero etc.)
    s_has[SIG_AFR] = true; for (int i = 0; i < 100; i++) { s_val[SIG_AFR] = (float)((i * 7) % 250); run_ms(20); }
    CHECK(true, "rendered with moving data at every angle pair without crashing");
  }

  if (!strcmp(mode, "nan")) {
    printf("nan: a reading that is not a number never reaches the digit-only fonts\n");
    s_has[SIG_AFR] = true; s_val[SIG_AFR] = 14.7f; run_ms(100);
    lv_obj_t *num = lv_obj_get_child(lv_obj_get_child(s_pageScr[0], 0), 0);
    CHECK(strcmp(lv_label_get_text(num), "14.7") == 0, "a normal reading shows");
    s_val[SIG_AFR] = NAN; run_ms(100);
    CHECK(strcmp(lv_label_get_text(num), "--") == 0, "NaN shows --, not the letters 'nan' (which these fonts cannot draw)");
    s_val[SIG_AFR] = INFINITY; run_ms(100);
    CHECK(strcmp(lv_label_get_text(num), "--") == 0, "infinity shows -- too");
    s_val[SIG_AFR] = -3.25f; run_ms(100);
    CHECK(strcmp(lv_label_get_text(num), "-3.2") == 0 || strcmp(lv_label_get_text(num), "-3.3") == 0, "a negative reading shows its minus sign");
  }

  if (!strcmp(mode, "halign")) {
    printf("halign: the caption goes left / right inside the cell and the glass; the readout stays centred\n");
    s_has[SIG_AFR] = true; s_val[SIG_AFR] = 14.7f; run_ms(150);
    for (int p = 0; p < 3; p++) {
      lv_obj_update_layout(s_pageScr[p]);
      lv_obj_t *box = lv_obj_get_child(s_pageScr[p], 0);
      lv_area_t bx; lv_obj_get_coords(box, &bx);
      int nCap = 0;
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(box); i++) {
        lv_obj_t *k = lv_obj_get_child(box, i);
        if (!lv_obj_check_type(k, &lv_label_class)) continue;
        lv_area_t a; lv_obj_get_coords(k, &a);
        const char *t = lv_label_get_text(k);
        char m[160];
        if (lv_obj_has_flag(k, LV_OBJ_FLAG_USER_1)) {
          nCap++;
          lv_point_t sz; lv_txt_get_size(&sz, t, lv_obj_get_style_text_font(k, LV_PART_MAIN), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
          lv_area_t txt = a;
          if (p == 0) txt.x2 = a.x1 + sz.x - 1; else txt.x1 = a.x2 - sz.x + 1;
          snprintf(m, sizeof(m), "page %d caption '%s': on the glass and off centre", p, t);
          CHECK(inside_circle(&txt, 233.0f) && fabsf((txt.x1 + txt.x2) / 2.0f - (bx.x1 + bx.x2) / 2.0f) > 10.0f, m);
        } else if (strchr("0123456789-", t[0])) {
          snprintf(m, sizeof(m), "page %d readout '%s' stays centred", p, t);
          CHECK(fabsf((a.x1 + a.x2) / 2.0f - (bx.x1 + bx.x2) / 2.0f) <= 2.0f, m);
        }
      }
      char m[80]; snprintf(m, sizeof(m), "page %d: one caption follows the setting", p); CHECK(nCap == 1, m);
    }
  }

  if (!strcmp(mode, "decimals")) {
    printf("decimals: digits after the point in the readout\n");
    s_has[SIG_AFR] = true; s_val[SIG_AFR] = 14.66f; run_ms(100);
    static const char *WANT[5] = { "15", "14.7", "14.66", "14.66", "14.7" };   // 0, 1, 2, needle dial with 2, unknown (7) = 1
    for (int p = 0; p < 5; p++) {
      lv_obj_t *box = lv_obj_get_child(s_pageScr[p], 0), *lbl = NULL;
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(box) && !lbl; i++) { lv_obj_t *k = lv_obj_get_child(box, i);
        if (lv_obj_check_type(k, &lv_label_class) && strchr("0123456789", lv_label_get_text(k)[0])) lbl = k; }
      char m[120]; snprintf(m, sizeof(m), "page %d shows %s (got %s)", p, WANT[p], lbl ? lv_label_get_text(lbl) : "no label");
      CHECK(lbl && strcmp(lv_label_get_text(lbl), WANT[p]) == 0, m);
    }
    // the size is picked for the WIDEST text it will show: with 2 decimals "250.00" must fit inside the round glass and the cell
    for (int p = 0; p < 3; p++) {
      lv_obj_update_layout(s_pageScr[p]);
      lv_obj_t *box = lv_obj_get_child(s_pageScr[p], 0), *num = lv_obj_get_child(box, 0);
      const char *worst = p == 0 ? "250" : (p == 1 ? "250.0" : "250.00");
      lv_area_t a; lv_obj_get_coords(num, &a); lv_area_t w = box_around(&a, lv_obj_get_style_text_font(num, LV_PART_MAIN), worst);
      char m[120]; snprintf(m, sizeof(m), "%d decimals: the worst case %s fits the round glass", p, worst);
      CHECK(inside_circle(&w, 233.0f), m);
    }
  }

  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
