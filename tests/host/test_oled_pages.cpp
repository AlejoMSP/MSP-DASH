// OLED pages (esp32-input/oled_pages.h): each page type draws, the lamp takeover, the defaults.
// Build: g++ -std=c++17 -I../../esp32-input test_oled_pages.cpp   (writes /tmp/opage_*.pbm previews)
#include "oled_pages.h"
#include <string>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static bool px(const uint8_t *b, int x, int y) { return b[(y / 8) * 128 + x] >> (y % 8) & 1; }
static int count(const uint8_t *b, int x0, int y0, int w, int h) { int n = 0; for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) n += px(b, x, y); return n; }
static void pbm(const uint8_t *b, const char *fn) { FILE *f = fopen(fn, "w"); fprintf(f, "P1\n128 64\n"); for (int y = 0; y < 64; y++) { for (int x = 0; x < 128; x++) fprintf(f, "%d ", px(b, x, y)); fprintf(f, "\n"); } fclose(f); }
int main() {
  OledPages c; oled_pages_defaults(&c); uint8_t buf[1024]; OledPageData d = {};
  printf("1. defaults\n");
  CHECK(c.count == 5 && c.page[0].type == OPG_LAMPS && c.page[1].type == OPG_BIG && c.page[2].type == OPG_VALUES && c.page[3].type == OPG_GMETER && c.page[4].type == OPG_CLOCK, "lamps, big number, values, G-meter, clock");
  CHECK(c.page[1].sig[0] == 3 && !strcmp(c.page[1].label[0], "BOOST PSI") && c.page[2].count == 4 && c.page[2].sig[3] == 10, "boost big; oil psi, oil temp, water, volts");
  CHECK(sizeof(OledPage) == 50 && sizeof(OledPages) == 4 + 6 * 50, "saved size 304 bytes");
  printf("2. drawing\n");
  d.val[0] = 12.4f; d.has[0] = true;
  CHECK(oled_render_page(buf, &c.page[1], &d) && count(buf, 0, 12, 128, 52) > 300 && count(buf, 0, 0, 128, 8) > 30, "big number: big digits under its label");
  pbm(buf, "/tmp/opage_big.pbm");
  d.has[0] = false; oled_render_page(buf, &c.page[1], &d);
  CHECK(count(buf, 0, 12, 128, 52) > 50 && count(buf, 0, 12, 128, 52) < 400, "no data: -- (not 0)");
  float v[4] = { 42, 215, 190, 13.8f }; for (int i = 0; i < 4; i++) { d.val[i] = v[i]; d.has[i] = true; }
  oled_render_page(buf, &c.page[2], &d);
  CHECK(count(buf, 0, 0, 60, 64) > 80 && count(buf, 80, 0, 48, 64) > 80, "four values: labels left, numbers right");
  pbm(buf, "/tmp/opage_values.pbm");
  OledPage two = c.page[2]; two.count = 2; oled_render_page(buf, &two, &d);
  CHECK(count(buf, 64, 12, 64, 16) > 60 && count(buf, 0, 40, 128, 24) > 60, "two values: bigger rows");
  d.glat = 0.9f; d.glon = 0; d.gpeak = 1.02f; d.ghas = true;
  oled_render_page(buf, &c.page[3], &d);
  CHECK(count(buf, 3, 26, 14, 12) > count(buf, 46, 26, 14, 12) + 10, "G-meter: a left turn puts the dot left of centre");
  pbm(buf, "/tmp/opage_g.pbm");
  d.glat = 0; d.glon = -1.0f; oled_render_page(buf, &c.page[3], &d);
  CHECK(count(buf, 25, 44, 14, 10) > count(buf, 25, 10, 14, 10) + 10, "braking puts it below centre");
  d.glat = 5; d.glon = 5; oled_render_page(buf, &c.page[3], &d);   // must stay inside the circle, no crash
  CHECK(count(buf, 64, 0, 4, 64) == 0, "a huge reading stays inside the circle");
  d.time = "2:32 PM"; d.date = "2026-11-14"; oled_render_page(buf, &c.page[4], &d);
  CHECK(count(buf, 0, 12, 128, 24) > 200 && count(buf, 0, 50, 128, 8) > 30, "clock: time big, date under it");
  pbm(buf, "/tmp/opage_clock.pbm");
  OledPage lamps = c.page[0]; CHECK(!oled_render_page(buf, &lamps, &d), "the lamps page is drawn by oled_render, not here");
  printf("3. which page shows\n");
  CHECK(oled_page_to_show(&c, 1, false) == 1 && oled_page_to_show(&c, 1, true) == 0, "a lit lamp takes over the big-number page");
  CHECK(oled_page_to_show(&c, 9, false) == 0, "a page number past the end: the first page");
  c.takeover = 0; CHECK(oled_page_to_show(&c, 3, true) == 3, "takeover off: stays");
  c.takeover = 1; c.page[0].type = OPG_CLOCK; CHECK(oled_page_to_show(&c, 2, true) == 2, "no lamps page in the list: stays");
  c.count = 0; CHECK(oled_page_to_show(&c, 0, false) == -1, "no pages: none");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
