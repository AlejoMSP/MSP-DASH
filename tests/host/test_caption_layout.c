/*
  Headless test for caption size, units, the caption ON the dial face, and where the gauge stands (centre / top / bottom):
  the REAL gauge_app.c against LVGL 8.4.
      ./test_caption_layout capnum | capdial | caphist | units | face | valign | valignnum
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
static int cap_size_of(const lv_font_t *f) {
  if (f == &lv_font_montserrat_14) return 14; if (f == &font_text_20) return 20; if (f == &lv_font_montserrat_28) return 28;
  if (f == &lv_font_montserrat_36) return 36; if (f == &font_text_48) return 48; return -1;
}
static const lv_font_t *cap_font_of(int s) { switch (s) { case 14: return &lv_font_montserrat_14; case 20: return &font_text_20; case 28: return &lv_font_montserrat_28; case 36: return &lv_font_montserrat_36; case 48: return &font_text_48; } return NULL; }
static void mk(CellConfig *c, GaugeType t, const char *cap) {
  memset(c, 0, sizeof(*c));
  c->enabled = true; c->signal = SIG_AFR; c->gauge_type = t; c->min_val = 0; c->max_val = 250; c->rest_val = 0;
  strncpy(c->caption, cap, GAUGE_CAPTION_MAXLEN - 1); c->needle_color = 0xFFFFFF; c->tick_color = 0xFFFFFF; c->bg_color = 0;
  c->value_divisor = 1.0f; c->tick_count = 26; c->major_nth = 5; c->refresh_hz = 50; c->value_font_size = 28; c->decimals = 1; c->tick_font_size = 28; c->history_sec = 30;
  c->start_deg = t == GAUGE_TYPE_HALFMOON ? 270 : 225; c->end_deg = t == GAUGE_TYPE_HALFMOON ? 90 : 135;
}
static lv_area_t A(lv_obj_t *o) { lv_area_t a; lv_obj_get_coords(o, &a); return a; }
static float cxOf(const lv_area_t *a) { return (a->x1 + a->x2 + 1) / 2.0f; }
static float cyOf(const lv_area_t *a) { return (a->y1 + a->y2 + 1) / 2.0f; }
static bool on_glass(const lv_area_t *a, float r) {
  float xs[2] = { (float)a->x1, (float)a->x2 + 1 }, ys[2] = { (float)a->y1, (float)a->y2 + 1 };
  for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) { float dx = xs[i] - 233.0f, dy = ys[j] - 233.0f; if (dx * dx + dy * dy > r * r) return false; }
  return true;
}
static bool inside_cell(const lv_area_t *a, const lv_area_t *c) { return a->x1 >= c->x1 && a->x2 <= c->x2 && a->y1 >= c->y1 && a->y2 <= c->y2; }
static int ncells(int p) { return p >= 6 ? 4 : (p >= 4 ? 2 : 1); }   // capnum / capdial / caphist page layout: 1 cell, 2 cells, 4 cells
static float dialY(int page) { lv_area_t a = A(lv_obj_get_child(lv_obj_get_child(s_pageScr[page], 0), 0)); return cyOf(&a); }   // the y centre of the dial on a page
static lv_point_t text_size(const char *t, const lv_font_t *f) { lv_point_t p; lv_txt_get_size(&p, t, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE); return p; }

// A caption: the font is no bigger than asked and is the biggest that fits; the label lies inside its cell and on the glass.
static void check_caption(const char *ctx, lv_obj_t *cap, const lv_area_t *cell, int requested, float maxHFrac, const char *text) {
  const lv_font_t *f = lv_obj_get_style_text_font(cap, LV_PART_MAIN);
  int size = cap_size_of(f); lv_area_t a = A(cap); char m[220];
  if (requested == 0) { snprintf(m, sizeof(m), "%s: size never set -> the display's default font", ctx); CHECK(f == LV_FONT_DEFAULT, m); }
  else {
    snprintf(m, sizeof(m), "%s: asked %d, got %d (a known caption font, no bigger than asked)", ctx, requested, size); CHECK(size > 0 && size <= requested, m);
    if (size > 0 && size < requested) {
      int next = 0; static const int S[5] = { 14, 20, 28, 36, 48 };
      for (int i = 0; i < 5; i++) if (S[i] > size && S[i] <= requested) { next = S[i]; break; }
      lv_point_t t = text_size(text, cap_font_of(next)); float w = (float)t.x, h = (float)t.y, cx = cxOf(&a), bottom = (float)a.y2 + 1;
      lv_area_t b = { (lv_coord_t)(cx - w / 2), (lv_coord_t)(bottom - h), (lv_coord_t)(cx + w / 2 - 1), (lv_coord_t)(bottom - 1) };
      float cw = (float)(cell->x2 - cell->x1 + 1), ch = (float)(cell->y2 - cell->y1 + 1);
      bool fits = w <= cw * 0.94f && h <= ch * maxHFrac && on_glass(&b, 228.0f) && b.x1 >= cell->x1 && b.x2 <= cell->x2;   // (a caption centred under an off-centre graph must also stay inside the cell)
      snprintf(m, sizeof(m), "%s: %d was used because %d would NOT fit", ctx, size, next); CHECK(!fits, m);
    }
  }
  snprintf(m, sizeof(m), "%s: the caption lies inside its cell (caption x %d..%d y %d..%d, cell x %d..%d y %d..%d)", ctx, a.x1, a.x2, a.y1, a.y2, cell->x1, cell->x2, cell->y1, cell->y2); CHECK(inside_cell(&a, cell), m);
  snprintf(m, sizeof(m), "%s: ... and on the round glass (unless even the smallest size cannot fit, as for a long caption in a bottom-corner cell)", ctx); CHECK(on_glass(&a, 233.0f) || size == 14 || requested == 0, m);
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  const char *mode = argc > 1 ? argv[1] : "capnum";
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, dbuf, NULL, 466 * 20);
  lv_disp_drv_init(&disp_drv); disp_drv.hor_res = 466; disp_drv.ver_res = 466; disp_drv.flush_cb = flush_cb; disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  static const int SIZES[6] = { 0, 14, 20, 28, 36, 48 };

  if (!strcmp(mode, "capnum") || !strcmp(mode, "capdial") || !strcmp(mode, "caphist")) {
    GaugeType t = !strcmp(mode, "capnum") ? GAUGE_TYPE_NUMBER : (!strcmp(mode, "capdial") ? GAUGE_TYPE_NEEDLE : GAUGE_TYPE_HISTOGRAM);
    const char *text = "OIL PRESSURE";
    // pages 0-3: one cell, sizes 0/14/20/28 ; pages 4-5: two cells (466 x 233: short, so the caption band limits a graph) with 36 and 48 ; pages 6-7: four cells with 48 and 28
    for (int p = 0; p < 8; p++) {
      s_cfg[p].enabled = true; s_cfg[p].layout = p >= 6 ? LAYOUT_2X2 : (p >= 4 ? LAYOUT_2UP : LAYOUT_1CELL);
      for (int c = 0; c < ncells(p); c++) { mk(&s_cfg[p].cells[c], t, text); s_cfg[p].cells[c].caption_font_size = p >= 6 ? (p == 6 ? 48 : 28) : SIZES[p]; }
    }
    build_gauge_app(); index_pages(); run_ms(300);
    printf("%s: caption sizes (including a 12-letter caption in a small cell)\n", mode);
    for (int p = 0; p < s_nPage; p++) {
      lv_obj_update_layout(s_pageScr[p]);
      for (int c = 0; c < ncells(p); c++) {
        lv_obj_t *cont = lv_obj_get_child(s_pageScr[p], c); lv_area_t cell = A(cont);
        lv_obj_t *cap = t == GAUGE_TYPE_NUMBER ? lv_obj_get_child(cont, 1) : (t == GAUGE_TYPE_NEEDLE ? lv_obj_get_child(cont, 1) : NULL);
        if (t == GAUGE_TYPE_HISTOGRAM) { for (uint32_t k = 0; k < lv_obj_get_child_cnt(cont); k++) { lv_obj_t *o = lv_obj_get_child(cont, k); if (lv_obj_check_type(o, &lv_label_class)) cap = o; } }
        char ctx[80]; snprintf(ctx, sizeof(ctx), "page %d cell %d", p, c);
        char full[40]; snprintf(full, sizeof(full), "%s", lv_label_get_text(cap));
        check_caption(ctx, cap, &cell, p >= 6 ? (p == 6 ? 48 : 28) : SIZES[p], t == GAUGE_TYPE_NEEDLE ? 0.3f : 0.25f, full);
        if (t == GAUGE_TYPE_HISTOGRAM) {
          lv_obj_t *chart = NULL; for (uint32_t k = 0; k < lv_obj_get_child_cnt(cont); k++) { lv_obj_t *o = lv_obj_get_child(cont, k); if (lv_obj_check_type(o, &lv_chart_class)) chart = o; }
          lv_area_t ca = A(chart), la = A(cap); char m[160];
          snprintf(m, sizeof(m), "%s: the graph's four corners are on the glass and it ends above the caption", ctx); CHECK(on_glass(&ca, 233.0f) && ca.y2 < la.y1 + 3, m);
          snprintf(m, sizeof(m), "%s: the graph left room for the caption: it ends %d px above the cell's bottom (at least 12 expected)", ctx, cell.y2 + 1 - (la.y2 + 1)); CHECK(la.y2 + 1 <= cell.y2 + 1 - 12, m);
        }
      }
    }
    if (t == GAUGE_TYPE_NEEDLE) {   // the bigger the caption, the less room for the dial's readout stack: the dial itself never moves in centre mode
      lv_area_t m0 = A(lv_obj_get_child(lv_obj_get_child(s_pageScr[0], 0), 0)), m3 = A(lv_obj_get_child(lv_obj_get_child(s_pageScr[3], 0), 0));
      CHECK(m0.y1 == m3.y1 && m0.x1 == m3.x1 && m0.x2 == m3.x2, "a bigger caption (28 instead of the default) does not move or resize a centred dial");
    }
  }

  if (!strcmp(mode, "units")) {
    // 0: dial + show value + units (caption at the bottom) ; 1: dial, units set but Show value OFF ; 2: number + units
    // 3: dial + caption ON the face + units + value ; 4: dial + units + big caption 36 ; 5: number + units + caption 36 ; 6/7: 2x2 dials with units
    for (int p = 0; p < 8; p++) { s_cfg[p].enabled = true; s_cfg[p].layout = p >= 6 ? LAYOUT_2X2 : LAYOUT_1CELL; }
    for (int p = 0; p < 8; p++) for (int c = 0; c < (p >= 6 ? 4 : 1); c++) {
      CellConfig *cc = &s_cfg[p].cells[c]; mk(cc, (p == 2 || p == 5) ? GAUGE_TYPE_NUMBER : GAUGE_TYPE_NEEDLE, "CLT"); strcpy(cc->units, "kPa"); cc->show_value = true; cc->value_font_size = 36;
      if (p == 1) cc->show_value = false;
      if (p == 3) { cc->caption_on_face = true; cc->caption_font_size = 20; }
      if (p == 4 || p == 5) cc->caption_font_size = 36;
      if (p >= 6) cc->value_font_size = 28;
    }
    build_gauge_app(); index_pages(); run_ms(300);
    printf("units: printed under the readout\n");
    for (int p = 0; p < s_nPage; p++) {
      lv_obj_update_layout(s_pageScr[p]);
      lv_obj_t *cont = lv_obj_get_child(s_pageScr[p], 0); lv_area_t cell = A(cont); bool number = (p == 2 || p == 5); char m[200];
      snprintf(m, sizeof(m), "page %d: the cell has the labels it should (%d children)", p, number ? 3 : (p == 1 ? 2 : 4));
      CHECK((int)lv_obj_get_child_cnt(cont) == (number ? 3 : (p == 1 ? 2 : 4)), m);
      if ((int)lv_obj_get_child_cnt(cont) != (number ? 3 : (p == 1 ? 2 : 4))) continue;
      if (number) {
        lv_obj_t *num = lv_obj_get_child(cont, 0), *cap = lv_obj_get_child(cont, 1), *un = lv_obj_get_child(cont, 2);
        lv_area_t an = A(num), ac = A(cap), au = A(un);
        snprintf(m, sizeof(m), "page %d: number cell: caption above the number, units below it, none overlapping", p); CHECK(ac.y2 < an.y1 + 2 && au.y1 > an.y2 - 2, m);
        snprintf(m, sizeof(m), "page %d: the units text reads 'kPa'", p); CHECK(strcmp(lv_label_get_text(un), "kPa") == 0, m);
        snprintf(m, sizeof(m), "page %d: units are centred under the number and inside the cell and glass", p); CHECK(fabsf(cxOf(&au) - cxOf(&an)) <= 2.0f && inside_cell(&au, &cell) && on_glass(&au, 233.0f), m);
        snprintf(m, sizeof(m), "page %d: the units use the caption's size (%d)", p, p == 5 ? 36 : 0); { int us = cap_size_of(lv_obj_get_style_text_font(un, LV_PART_MAIN)); CHECK(p == 5 ? us > 0 && us <= 36 : lv_obj_get_style_text_font(un, LV_PART_MAIN) == LV_FONT_DEFAULT, m); }
      } else if (p == 1) {
        snprintf(m, sizeof(m), "page %d: units are NOT shown when the readout is off (children: meter + caption only)", p); CHECK(lv_obj_get_child_cnt(cont) == 2, m);
      } else {
        bool onFace = (p == 3);
        lv_obj_t *cap = lv_obj_get_child(cont, 1), *un = lv_obj_get_child(cont, 2), *val = lv_obj_get_child(cont, 3);
        lv_area_t ac = A(cap), au = A(un), av = A(val);
        snprintf(m, sizeof(m), "page %d: the dial has caption, units and readout labels", p); CHECK(lv_obj_get_child_cnt(cont) == 4, m);
        snprintf(m, sizeof(m), "page %d: the readout is above the units", p); CHECK(av.y2 <= au.y1 + 2, m);
        if (!onFace) { snprintf(m, sizeof(m), "page %d: ... and the units are above the caption at the bottom", p); CHECK(au.y2 <= ac.y1 + 2, m); }
        else { snprintf(m, sizeof(m), "page %d: caption on the face: the units sit at the very bottom, the title is not in the stack", p); CHECK(au.y2 > ac.y2 + 40, m); }
        snprintf(m, sizeof(m), "page %d: all three are centred and inside the cell and on the glass", p);
        CHECK(fabsf(cxOf(&au) - cxOf(&av)) <= 2.0f && inside_cell(&au, &cell) && inside_cell(&av, &cell) && on_glass(&au, 233.0f) && on_glass(&av, 233.0f) && on_glass(&ac, 233.0f), m);
        if (p >= 6) { for (int c = 1; c < 4; c++) { lv_obj_t *cc = lv_obj_get_child(s_pageScr[p], c); lv_area_t cellc = A(cc); lv_area_t uc = A(lv_obj_get_child(cc, 2)), vc = A(lv_obj_get_child(cc, 3));
          snprintf(m, sizeof(m), "page %d cell %d: units and readout inside the cell and on the glass", p, c); CHECK(inside_cell(&uc, &cellc) && inside_cell(&vc, &cellc) && on_glass(&uc, 233.0f) && on_glass(&vc, 233.0f), m); } }
      }
    }
  }

  if (!strcmp(mode, "face")) {
    // the caption printed ON the dial face, above the hub. pages 0-3: standard dial, caption sizes 0/20/36/48; 4: half moon; 5: custom top arc; 6: 2x2
    for (int p = 0; p < 8; p++) { s_cfg[p].enabled = true; s_cfg[p].layout = p == 6 ? LAYOUT_2X2 : LAYOUT_1CELL; }
    static const int FS[4] = { 0, 20, 36, 48 };
    for (int p = 0; p < 7; p++) for (int c = 0; c < (p == 6 ? 4 : 1); c++) {
      CellConfig *cc = &s_cfg[p].cells[c]; mk(cc, p == 4 ? GAUGE_TYPE_HALFMOON : GAUGE_TYPE_NEEDLE, "COOLANT"); cc->caption_on_face = true; cc->show_value = true; cc->value_font_size = 36; cc->caption_font_size = p < 4 ? FS[p] : 28;
      if (p == 5) { cc->start_deg = 300; cc->end_deg = 60; }
      if (p == 6) cc->value_font_size = 28;
    }
    build_gauge_app(); index_pages(); run_ms(300);
    printf("face: the caption is printed on the dial, above the hub\n");
    for (int p = 0; p < 7; p++) {
      lv_obj_update_layout(s_pageScr[p]);
      for (int c = 0; c < (p == 6 ? 4 : 1); c++) {
        lv_obj_t *cont = lv_obj_get_child(s_pageScr[p], c); lv_area_t cell = A(cont);
        lv_obj_t *meter = lv_obj_get_child(cont, 0), *cap = lv_obj_get_child(cont, 1), *val = lv_obj_get_child(cont, 2);
        lv_area_t am = A(meter), ac = A(cap), av = A(val);
        float R = (am.x2 - am.x1 + 1) / 2.0f - (float)lv_obj_get_style_pad_left(meter, LV_PART_MAIN), mcx = cxOf(&am), mcy = cyOf(&am); char m[200];
        snprintf(m, sizeof(m), "page %d cell %d: the title is centred over the hub (x off by %.1f)", p, c, cxOf(&ac) - mcx); CHECK(fabsf(cxOf(&ac) - mcx) <= 2.0f, m);
        snprintf(m, sizeof(m), "page %d cell %d: ... about 30%% of the radius above it (%.1f vs %.1f)", p, c, mcy - cyOf(&ac), 0.30f * R); CHECK(fabsf((mcy - cyOf(&ac)) - 0.30f * R) <= 3.0f, m);
        { bool inside = true; float xs[2] = { (float)ac.x1, (float)ac.x2 + 1 }, ys[2] = { (float)ac.y1, (float)ac.y2 + 1 };
          for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) { float dx = xs[i] - mcx, dy = ys[j] - mcy; if (dx * dx + dy * dy > R * R) inside = false; }
          snprintf(m, sizeof(m), "page %d cell %d: all four corners of the title are inside the dial's ticks (radius %.0f)", p, c, R); CHECK(inside, m); }
        snprintf(m, sizeof(m), "page %d cell %d: the title's text reads COOLANT and is on the glass and in its cell", p, c); CHECK(strcmp(lv_label_get_text(cap), "COOLANT") == 0 && on_glass(&ac, 233.0f) && inside_cell(&ac, &cell), m);
        snprintf(m, sizeof(m), "page %d cell %d: with no caption at the bottom the readout sits at the bottom of the cell", p, c); CHECK(av.y2 > cell.y2 - 40 && av.y1 > mcy, m);
        if (p < 4) { const lv_font_t *f = lv_obj_get_style_text_font(cap, LV_PART_MAIN); int sz = cap_size_of(f);
          snprintf(m, sizeof(m), "page %d: title size asked %d, got %d", p, FS[p], sz); CHECK(FS[p] == 0 ? f == LV_FONT_DEFAULT : (sz > 0 && sz <= FS[p]), m); }
      }
    }
    { lv_obj_t *t2 = lv_obj_get_child(lv_obj_get_child(s_pageScr[2], 0), 1), *t3 = lv_obj_get_child(lv_obj_get_child(s_pageScr[3], 0), 1);
      printf("   title sizes: 36 asked -> %d, 48 asked -> %d\n", cap_size_of(lv_obj_get_style_text_font(t2, LV_PART_MAIN)), cap_size_of(lv_obj_get_style_text_font(t3, LV_PART_MAIN)));
      CHECK(cap_size_of(lv_obj_get_style_text_font(t3, LV_PART_MAIN)) <= 48 && cap_size_of(lv_obj_get_style_text_font(t3, LV_PART_MAIN)) >= 20, "a 48 title shrinks to what fits inside the dial, never below 20 here"); }
  }

  if (!strcmp(mode, "valign")) {
    // dials. pages 0-2: standard 270 degree dial centre/top/bottom; 3-5: half moon; 6: custom top arc 300->60 at the top; 7: 2up, a standard dial and a 300->60 arc, both at the bottom
    for (int p = 0; p < 8; p++) { s_cfg[p].enabled = true; s_cfg[p].layout = p == 7 ? LAYOUT_2UP : LAYOUT_1CELL; }
    for (int p = 0; p < 8; p++) for (int c = 0; c < (p == 7 ? 2 : 1); c++) {
      CellConfig *cc = &s_cfg[p].cells[c]; mk(cc, (p >= 3 && p <= 5) ? GAUGE_TYPE_HALFMOON : GAUGE_TYPE_NEEDLE, "AFR"); cc->show_value = (p != 5); cc->v_align = p < 6 ? p % 3 : (p == 6 ? GAUGE_VALIGN_TOP : GAUGE_VALIGN_BOTTOM);
      if (p == 6 || (p == 7 && c == 1)) { cc->start_deg = 300; cc->end_deg = 60; }   // an arc that is entirely ABOVE its hub
      if (p == 7 && c == 1) cc->v_align = GAUGE_VALIGN_BOTTOM;
    }
    build_gauge_app(); index_pages(); run_ms(300);
    printf("valign: where the dial stands in its cell\n");
    float centreShift[2] = { 0, 0 };
    for (int p = 0; p < s_nPage; p++) {
      lv_obj_update_layout(s_pageScr[p]);
      for (int c = 0; c < (p == 7 ? 2 : 1); c++) {
        lv_obj_t *cont = lv_obj_get_child(s_pageScr[p], c); lv_area_t cell = A(cont); lv_obj_t *meter = lv_obj_get_child(cont, 0); lv_area_t am = A(meter);
        CellConfig *cc = &s_cfg[p].cells[c];
        float R = (am.x2 - am.x1 + 1) / 2.0f - (float)lv_obj_get_style_pad_left(meter, LV_PART_MAIN), mcy = cyOf(&am), cellCy = cyOf(&cell);
        int32_t st, en, sw; gauge_dial_resolve(cc->gauge_type == GAUGE_TYPE_HALFMOON, cc->start_deg, cc->end_deg, &st, &en, &sw);
        float hi = 0, lo = 0; for (int a = 0; a <= sw; a++) { float cs = cosf((float)(st + a) * 0.01745329f); if (cs > hi) hi = cs; if (cs < lo) lo = cs; }
        float extTop = mcy - hi * R, extBot = mcy - lo * R;
        // the top of the stack at the bottom of the cell: the highest of the caption / readout labels (they come after the meter)
        float stackTop = (float)cell.y2 + 1; for (uint32_t k = 1; k < lv_obj_get_child_cnt(cont); k++) { lv_area_t a2 = A(lv_obj_get_child(cont, k)); if (a2.y1 < stackTop) stackTop = (float)a2.y1; }
        char m[240]; int va = cc->v_align;
        if (va == GAUGE_VALIGN_TOP) { snprintf(m, sizeof(m), "page %d cell %d TOP: the dial's highest point is at the top of the cell (%.1f vs %d)", p, c, extTop, cell.y1 + 4); CHECK(fabsf(extTop - (cell.y1 + 4)) <= 2.5f, m); }
        if (va == GAUGE_VALIGN_BOTTOM) { snprintf(m, sizeof(m), "page %d cell %d BOTTOM: the dial's lowest point is just above the caption/readout (%.1f vs %.1f)", p, c, extBot, stackTop - 4); CHECK(fabsf(extBot - (stackTop - 4)) <= 2.5f, m); }
        if (va == GAUGE_VALIGN_CENTER) {
          float shift = mcy - cellCy; centreShift[p == 3 ? 1 : 0] = shift;
          float want = p >= 3 && p <= 5 ? (am.x2 - am.x1 + 1) / 6.0f : 0.0f;
          // a half moon that grew to fill its cell (build 124) is centred in the free area instead
          bool centredInArea = fabsf((extTop + extBot) / 2.0f - ((float)cell.y1 + 4.0f + stackTop - 4.0f) / 2.0f) <= 1.5f;
          snprintf(m, sizeof(m), "page %d CENTER: where it always stood, or (grown half moon) centred in the free area (shift %.1f, expected %.1f)", p, shift, want); CHECK(fabsf(shift - want) <= 1.5f || (p >= 3 && p <= 5 && centredInArea), m);
        }
        snprintf(m, sizeof(m), "page %d cell %d: the dial never reaches into the caption/readout (lowest point %.1f, stack starts %.1f)", p, c, extBot, stackTop); CHECK(va == GAUGE_VALIGN_CENTER || extBot <= stackTop, m);
        snprintf(m, sizeof(m), "page %d cell %d: the dial's highest point stays inside its cell", p, c); CHECK(va == GAUGE_VALIGN_CENTER || extTop >= cell.y1, m);
      }
    }
    // top < centre < bottom for the same dial
    { char m[200];
      snprintf(m, sizeof(m), "the 270 degree dial: centre y %.0f, top y %.0f, bottom y %.0f", dialY(0), dialY(1), dialY(2)); CHECK(dialY(1) < dialY(0) && dialY(0) < dialY(2), m);
      snprintf(m, sizeof(m), "the half moon: centre y %.0f, top y %.0f, bottom y %.0f", dialY(3), dialY(4), dialY(5)); CHECK(dialY(4) < dialY(3) && dialY(3) < dialY(5), m);
      printf("   the half moon moves %.0f px up and %.0f px down; the 270 degree dial moves %.0f up and %.0f down\n", dialY(3) - dialY(4), dialY(5) - dialY(3), dialY(0) - dialY(1), dialY(2) - dialY(0)); }
    (void)centreShift;
  }

  if (!strcmp(mode, "valignnum")) {
    // number cells. pages 0-2 1-cell centre/top/bottom; 3 2-up top; 4 2-up bottom; 5 2x2 top; 6 1-cell bottom with units + caption 28; 7 1-cell top with units + caption 28
    for (int p = 0; p < 8; p++) { s_cfg[p].enabled = true; s_cfg[p].layout = p == 3 || p == 4 ? LAYOUT_2UP : (p == 5 ? LAYOUT_2X2 : LAYOUT_1CELL); }
    for (int p = 0; p < 8; p++) for (int c = 0; c < (p == 3 || p == 4 ? 2 : (p == 5 ? 4 : 1)); c++) {
      CellConfig *cc = &s_cfg[p].cells[c]; mk(cc, GAUGE_TYPE_NUMBER, "OIL"); cc->value_font_size = p < 3 ? 96 : 64;
      cc->v_align = p < 3 ? p : (p == 3 || p == 5 || p == 7 ? GAUGE_VALIGN_TOP : GAUGE_VALIGN_BOTTOM);
      if (p >= 6) { cc->caption_font_size = 28; strcpy(cc->units, "kPa"); }
    }
    build_gauge_app(); index_pages(); run_ms(300);
    printf("valignnum: where a number cell's text block stands\n");
    for (int p = 0; p < s_nPage; p++) {
      lv_obj_update_layout(s_pageScr[p]);
      for (int c = 0; c < (p == 3 || p == 4 ? 2 : (p == 5 ? 4 : 1)); c++) {
        lv_obj_t *cont = lv_obj_get_child(s_pageScr[p], c); lv_area_t cell = A(cont);
        lv_obj_t *num = lv_obj_get_child(cont, 0), *cap = lv_obj_get_child(cont, 1); lv_area_t an = A(num), ac = A(cap);
        bool units = p >= 6; lv_area_t au = units ? A(lv_obj_get_child(cont, 2)) : (lv_area_t){ 0, 0, 0, 0 };
        CellConfig *cc = &s_cfg[p].cells[c]; int va = cc->v_align; char m[240];
        float capH = (float)lv_font_get_line_height(cc->caption_font_size ? cap_font_of(cc->caption_font_size) : LV_FONT_DEFAULT);
        if (va == GAUGE_VALIGN_TOP) { snprintf(m, sizeof(m), "page %d cell %d TOP: the number starts below the caption, which is 8px from the top (%d vs %.0f)", p, c, an.y1, cell.y1 + 8 + capH + 4); CHECK(fabsf((float)an.y1 - (cell.y1 + 8 + capH + 4)) <= 2.5f, m); }
        if (va == GAUGE_VALIGN_BOTTOM) { float want = (float)cell.y2 + 1 - 8 - (units ? capH + 2 : 0); snprintf(m, sizeof(m), "page %d cell %d BOTTOM: the number ends 8px (plus the units) above the bottom (%d vs %.0f)", p, c, an.y2 + 1, want); CHECK(fabsf((float)(an.y2 + 1) - want) <= 2.5f, m); }
        if (va == GAUGE_VALIGN_CENTER) { snprintf(m, sizeof(m), "page %d CENTER: the number is centred in the cell as always (%.1f vs %.1f)", p, cyOf(&an), cyOf(&cell)); CHECK(fabsf(cyOf(&an) - cyOf(&cell)) <= 2.0f, m); }
        snprintf(m, sizeof(m), "page %d cell %d: the caption is above the number%s", p, c, units ? " and the units below it" : ""); CHECK(ac.y2 < an.y1 + 2 && (!units || au.y1 > an.y2 - 2), m);
        snprintf(m, sizeof(m), "page %d cell %d: the whole block is inside the cell", p, c); CHECK(inside_cell(&an, &cell) && inside_cell(&ac, &cell) && (!units || inside_cell(&au, &cell)), m);
        // the widest reading ("250.0") on the glass, and the size is the biggest that fits there
        const lv_font_t *f = lv_obj_get_style_text_font(num, LV_PART_MAIN);
        lv_point_t sz = text_size("250.0", f);
        lv_area_t worst = { (lv_coord_t)(cxOf(&an) - sz.x / 2.0f), an.y1, (lv_coord_t)(cxOf(&an) + sz.x / 2.0f - 1), an.y2 };
        snprintf(m, sizeof(m), "page %d cell %d: the widest reading lies fully on the glass at this position (unless even the smallest size cannot fit there)", p, c); CHECK(on_glass(&worst, 233.0f) || f == &lv_font_montserrat_14, m);
      }
    }
    { lv_area_t y0 = A(lv_obj_get_child(lv_obj_get_child(s_pageScr[0], 0), 0)), y1 = A(lv_obj_get_child(lv_obj_get_child(s_pageScr[1], 0), 0)), y2 = A(lv_obj_get_child(lv_obj_get_child(s_pageScr[2], 0), 0));
      char m[160]; snprintf(m, sizeof(m), "the same number: centre y %.0f, top y %.0f, bottom y %.0f", cyOf(&y0), cyOf(&y1), cyOf(&y2)); CHECK(cyOf(&y1) < cyOf(&y0) && cyOf(&y0) < cyOf(&y2), m); }
  }

  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
