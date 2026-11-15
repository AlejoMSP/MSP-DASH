// oled_pages.h (hub) - pages on the OLED: the warning lamps (oled_panel.h), one big number, up to four values, a G-meter
// and a clock. Changed with the "OLED next page" button action or on a timer; any lit lamp takes the screen over.
// No Arduino calls: host-tested (tests/host/test_oled_pages.cpp).
#pragma once
#include <stdio.h>
#include <math.h>
#include "oled_panel.h"

#define OLED_MAX_PAGES 6
#define OLED_LABEL_LEN 10   // including the 0
enum { OPG_LAMPS = 0, OPG_BIG = 1, OPG_VALUES = 2, OPG_GMETER = 3, OPG_CLOCK = 4, OPG_TYPES };

typedef struct __attribute__((packed)) {
  uint8_t type;                      // OPG_...
  uint8_t count;                     // OPG_VALUES: 1-4 readouts (OPG_BIG uses the first)
  uint8_t sig[4];                    // signals shown
  uint8_t dec[4];                    // decimals for each, 0-2
  char label[4][OLED_LABEL_LEN];     // label for each, e.g. "BOOST PSI"
} OledPage;

typedef struct __attribute__((packed)) {
  uint8_t count;                     // pages in use, 1..OLED_MAX_PAGES
  uint8_t cycle_s;                   // 0 = only the button changes page, else seconds per page
  uint8_t takeover;                  // 1 = a lit lamp switches to the lamps page until it clears
  uint8_t version;
  OledPage page[OLED_MAX_PAGES];
} OledPages;
#define OLED_PAGES_VERSION 1

// What a page needs to be drawn (gathered by the hub each pass).
typedef struct {
  float val[4]; bool has[4];         // the page's signals now
  float glat, glon, gpeak; bool ghas;// G-meter: lateral (+ left), longitudinal (+ forward), the biggest combined G so far
  const char *time, *date;           // clock page text ("" = not known)
} OledPageData;

static inline void oled_line_h(uint8_t *b, int x0, int x1, int y) { for (int x = x0; x <= x1; x++) oled_pixel(b, x, y); }
static inline void oled_line_v(uint8_t *b, int x, int y0, int y1) { for (int y = y0; y <= y1; y++) oled_pixel(b, x, y); }
static inline void oled_circle(uint8_t *b, int cx, int cy, int r) {   // midpoint circle, outline
  int x = r, y = 0, e = 1 - r;
  while (x >= y) {
    oled_pixel(b, cx + x, cy + y); oled_pixel(b, cx - x, cy + y); oled_pixel(b, cx + x, cy - y); oled_pixel(b, cx - x, cy - y);
    oled_pixel(b, cx + y, cy + x); oled_pixel(b, cx - y, cy + x); oled_pixel(b, cx + y, cy - x); oled_pixel(b, cx - y, cy - x);
    y++; if (e < 0) e += 2 * y + 1; else { x--; e += 2 * (y - x) + 1; }
  }
}
static inline void oled_disc(uint8_t *b, int cx, int cy, int r) { for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) if (dx * dx + dy * dy <= r * r) oled_pixel(b, cx + dx, cy + dy); }

static inline void oled_fmt(char *out, int len, float v, bool has, int dec) {
  if (!has || !isfinite(v)) { snprintf(out, len, "--"); return; }
  snprintf(out, len, "%.*f", dec < 0 ? 0 : dec > 2 ? 2 : dec, v);
}

// Draws one page (not the lamps page: oled_render does that). Returns false for a type it does not draw.
static inline bool oled_render_page(uint8_t *buf, const OledPage *p, const OledPageData *d) {
  oled_clear(buf);
  char t[24];
  if (p->type == OPG_BIG) {   // the label on top, the number as big as it fits (32 px for 4 characters, 24 for 5)
    oled_text(buf, p->label[0], (OLED_W - oled_text_w(p->label[0], 1)) / 2, 0, 1);
    oled_fmt(t, sizeof t, d->val[0], d->has[0], p->dec[0]);
    int sc = oled_text_w(t, 4) <= OLED_W ? 4 : oled_text_w(t, 3) <= OLED_W ? 3 : 2;
    oled_text(buf, t, (OLED_W - oled_text_w(t, sc)) / 2, 12 + (52 - 8 * sc) / 2, sc);
    return true;
  }
  if (p->type == OPG_VALUES) {   // 1-2 readouts: big rows; 3-4: four small rows. Label left, value right.
    int n = p->count < 1 ? 1 : p->count > 4 ? 4 : p->count, sc = n <= 2 ? 2 : 1, rowH = n <= 2 ? 32 : 16;
    for (int i = 0; i < n; i++) {
      int y = i * rowH + (rowH - 8 * sc) / 2;
      oled_text(buf, p->label[i], 0, n <= 2 ? i * rowH : y, 1);
      oled_fmt(t, sizeof t, d->val[i], d->has[i], p->dec[i]);
      oled_text(buf, t, OLED_W - oled_text_w(t, sc), n <= 2 ? i * rowH + 12 : y, sc);
    }
    return true;
  }
  if (p->type == OPG_GMETER) {   // a circle of 1.5 G on the left (rings at 0.5 and 1.0 G), the numbers on the right
    const int cx = 31, cy = 31, R = 30;
    oled_circle(buf, cx, cy, R); oled_circle(buf, cx, cy, R * 2 / 3); oled_circle(buf, cx, cy, R / 3);
    oled_line_h(buf, cx - R, cx + R, cy); oled_line_v(buf, cx, cy - R, cy + R);
    if (d->ghas) {   // the dot: left turn to the left, speeding up toward the top, braking toward the bottom
      float x = -d->glat, y = -d->glon, m = sqrtf(x * x + y * y), lim = 1.5f;
      if (m > lim) { x *= lim / m; y *= lim / m; }
      oled_disc(buf, cx + (int)lroundf(x / lim * R), cy + (int)lroundf(y / lim * R), 3);
    }
    oled_fmt(t, sizeof t, d->glat, d->ghas, 2); oled_text(buf, "LAT", 68, 4, 1); oled_text(buf, t, OLED_W - oled_text_w(t, 1), 4, 1);
    oled_fmt(t, sizeof t, d->glon, d->ghas, 2); oled_text(buf, "LON", 68, 20, 1); oled_text(buf, t, OLED_W - oled_text_w(t, 1), 20, 1);
    oled_fmt(t, sizeof t, d->gpeak, d->ghas, 2); oled_text(buf, "PEAK", 68, 44, 1); oled_text(buf, t, OLED_W - oled_text_w(t, 1), 54, 1);
    return true;
  }
  if (p->type == OPG_CLOCK) {
    const char *tm = d->time && d->time[0] ? d->time : "--:--";
    int sc = oled_text_w(tm, 3) <= OLED_W ? 3 : 2;
    oled_text(buf, tm, (OLED_W - oled_text_w(tm, sc)) / 2, 12, sc);
    const char *dt = d->date && d->date[0] ? d->date : "NO GPS TIME";
    oled_text(buf, dt, (OLED_W - oled_text_w(dt, 1)) / 2, 50, 1);
    return true;
  }
  return false;
}

static inline void oled_page_set(OledPage *p, uint8_t type, int count) { memset(p, 0, sizeof *p); p->type = type; p->count = (uint8_t)count; }
static inline void oled_page_item(OledPage *p, int i, uint8_t sig, uint8_t dec, const char *label) {
  p->sig[i] = sig; p->dec[i] = dec; strncpy(p->label[i], label, OLED_LABEL_LEN - 1); p->label[i][OLED_LABEL_LEN - 1] = 0;
}
// Defaults: lamps, boost, four engine values, G-meter, clock. Button changes page, no timer, lamps take over.
static inline void oled_pages_defaults(OledPages *c) {
  memset(c, 0, sizeof *c);
  c->count = 5; c->cycle_s = 0; c->takeover = 1; c->version = OLED_PAGES_VERSION;
  oled_page_set(&c->page[0], OPG_LAMPS, 0);
  oled_page_set(&c->page[1], OPG_BIG, 1); oled_page_item(&c->page[1], 0, 3, 1, "BOOST PSI");          // SIG_MAP
  oled_page_set(&c->page[2], OPG_VALUES, 4);
  oled_page_item(&c->page[2], 0, 5, 0, "OIL PSI"); oled_page_item(&c->page[2], 1, 6, 0, "OIL F");      // SIG_OILP, SIG_OILT
  oled_page_item(&c->page[2], 2, 2, 0, "WATER F"); oled_page_item(&c->page[2], 3, 10, 1, "VOLTS");     // SIG_CLT, SIG_BATTV
  oled_page_set(&c->page[3], OPG_GMETER, 0);
  oled_page_set(&c->page[4], OPG_CLOCK, 0);
}
// Which page to show: the chosen one, or the lamps page while a lamp is lit (takeover on). -1 = none in use.
static inline int oled_page_to_show(const OledPages *c, int chosen, bool anyLit) {
  if (c->count < 1) return -1;
  if (chosen < 0 || chosen >= c->count) chosen = 0;
  if (anyLit && c->takeover && c->page[chosen].type != OPG_LAMPS)
    for (int i = 0; i < c->count; i++) if (c->page[i].type == OPG_LAMPS) return i;
  return chosen;
}
