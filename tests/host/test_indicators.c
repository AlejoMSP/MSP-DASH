/* Tests the pure-C lamp logic in gauge_indicators.h: the on/off rule (hysteresis, no data, flags) and the layout. */
#include "gauge_indicators.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static bool next(bool was, float v, int op, float thr, float hyst) { return gauge_indicator_next(was, true, v, op, thr, hyst); }

int main(void) {
  printf("1. above a threshold\n");
  CHECK(!next(false, 99.9f, GAUGE_IND_ABOVE, 100, 0), "below the threshold: off");
  CHECK(!next(false, 100.0f, GAUGE_IND_ABOVE, 100, 0), "exactly at it: off (the condition is 'above')");
  CHECK(next(false, 100.1f, GAUGE_IND_ABOVE, 100, 0), "just above: on");
  CHECK(next(true, 100.1f, GAUGE_IND_ABOVE, 100, 0) && !next(true, 100.0f, GAUGE_IND_ABOVE, 100, 0), "with no hysteresis an on lamp turns off the moment it is no longer above");

  printf("2. hysteresis (above 100, band 5)\n");
  CHECK(!next(false, 98.0f, GAUGE_IND_ABOVE, 100, 5), "an OFF lamp does not turn on inside the band");
  CHECK(next(false, 100.5f, GAUGE_IND_ABOVE, 100, 5), "it turns on above 100");
  CHECK(next(true, 98.0f, GAUGE_IND_ABOVE, 100, 5) && next(true, 95.1f, GAUGE_IND_ABOVE, 100, 5), "an ON lamp stays on while the value is inside the band (down to just above 95)");
  CHECK(!next(true, 95.0f, GAUGE_IND_ABOVE, 100, 5) && !next(true, 90.0f, GAUGE_IND_ABOVE, 100, 5), "and turns off at 95 or lower");
  { bool on = false; int flips = 0; float v[] = { 99, 101, 100.2f, 99.8f, 100.3f, 99.5f, 100.1f, 94, 99, 101 };
    for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); i++) { bool n = next(on, v[i], GAUGE_IND_ABOVE, 100, 5); if (n != on) flips++; on = n; }
    CHECK(flips == 3, "a noisy value hovering around the threshold flips the lamp only 3 times (on, off at 94, on again), not on every wobble"); }

  printf("3. below a threshold (oil pressure below 40, band 3)\n");
  CHECK(!next(false, 40.0f, GAUGE_IND_BELOW, 40, 3) && next(false, 39.9f, GAUGE_IND_BELOW, 40, 3), "on only below 40");
  CHECK(next(true, 42.9f, GAUGE_IND_BELOW, 40, 3) && !next(true, 43.0f, GAUGE_IND_BELOW, 40, 3), "once on it stays on until the value is back at 43");
  CHECK(!next(false, 41.0f, GAUGE_IND_BELOW, 40, 3), "an off lamp at 41 stays off");

  printf("4. no data and bad numbers: always off\n");
  CHECK(!gauge_indicator_next(true, false, 5.0f, GAUGE_IND_ABOVE, 1, 0) && !gauge_indicator_next(true, false, 5.0f, GAUGE_IND_BELOW, 100, 0), "no data: off, even for a lamp that was on and a value that would light it");
  CHECK(!gauge_indicator_next(true, true, NAN, GAUGE_IND_ABOVE, 1, 0) && !gauge_indicator_next(true, true, INFINITY, GAUGE_IND_ABOVE, 1, 0) && !gauge_indicator_next(true, true, -INFINITY, GAUGE_IND_BELOW, 1, 0), "NaN and infinity: off");
  CHECK(next(false, 101, GAUGE_IND_ABOVE, 100, -4) && !next(true, 100, GAUGE_IND_ABOVE, 100, -4), "a negative hysteresis acts like none");
  CHECK(next(false, 101, GAUGE_IND_ABOVE, 100, NAN) && !next(true, 100, GAUGE_IND_ABOVE, 100, NAN), "a NaN hysteresis acts like none");
  CHECK(!next(false, 5, GAUGE_IND_ABOVE, NAN, 0) && !next(true, 5, GAUGE_IND_ABOVE, NAN, 0), "a NaN threshold never lights the lamp");

  printf("5. ECU on/off flags (1 or 0), threshold 0.5\n");
  CHECK(next(false, 1, GAUGE_IND_ABOVE, 0.5f, 0) && !next(false, 0, GAUGE_IND_ABOVE, 0.5f, 0), "'flag is on': lit by 1, not by 0");
  CHECK(next(false, 0, GAUGE_IND_BELOW, 0.5f, 0) && !next(false, 1, GAUGE_IND_BELOW, 0.5f, 0), "'flag is off': lit by 0, not by 1");
  CHECK(!gauge_indicator_next(false, false, 1, GAUGE_IND_ABOVE, 0.5f, 0) && !gauge_indicator_next(false, false, 0, GAUGE_IND_BELOW, 0.5f, 0), "a flag the ECU never reported lights neither kind of lamp");

  printf("6. layout: every lamp count and column setting\n");
  int minW234 = 999;   // the smallest lamp for 2, 3 or 4 columns, any lamp count 1..12
  for (int cols = 1; cols <= 6; cols++) {
    for (int count = 1; count <= 12; count++) {
      GaugeLampRect r[12]; memset(r, 0, sizeof(r));
      int n = gauge_indicator_layout(count, cols, r);
      int effCols = cols > GAUGE_IND_MAX_COLS ? GAUGE_IND_MAX_COLS : (cols > count ? count : cols);
      char m[140];
      snprintf(m, sizeof(m), "%d lamps, %d cols: all placed", count, cols); CHECK(n == count, m);
      bool inside = true, overlap = false, sameSize = true, order = true;
      for (int i = 0; i < count; i++) {
        float xs[2] = { (float)r[i].x, (float)(r[i].x + r[i].w) }, ys[2] = { (float)r[i].y, (float)(r[i].y + r[i].h) };
        for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) { float dx = xs[a] - 233, dy = ys[b] - 233; if (dx * dx + dy * dy > 224.0f * 224.0f) inside = false; }   // 1px of rounding slack
        if (r[i].w != r[0].w || r[i].h != r[0].h) sameSize = false;
        for (int j = i + 1; j < count; j++) if (r[i].x < r[j].x + r[j].w && r[j].x < r[i].x + r[i].w && r[i].y < r[j].y + r[j].h && r[j].y < r[i].y + r[i].h) overlap = true;
        if (i > 0) { bool newRow = (i % effCols) == 0; if (newRow ? !(r[i].y > r[i - 1].y) : !(r[i].y == r[i - 1].y && r[i].x > r[i - 1].x)) order = false; }
      }
      snprintf(m, sizeof(m), "%d lamps, %d cols: every corner of every lamp is on the round glass", count, cols); CHECK(inside, m);
      snprintf(m, sizeof(m), "%d lamps, %d cols: no two lamps overlap", count, cols); CHECK(!overlap, m);
      snprintf(m, sizeof(m), "%d lamps, %d cols: all the same size, and not tiny (%dx%d)", count, cols, r[0].w, r[0].h); CHECK(sameSize && r[0].w >= 36 && r[0].w <= 220, m);
      snprintf(m, sizeof(m), "%d lamps, %d cols: left to right, then down", count, cols); CHECK(order, m);
      // each row is centred, and the whole block is centred vertically
      int rows = (count + effCols - 1) / effCols; bool centred = true;
      for (int row = 0; row < rows; row++) {
        int first = row * effCols, last = (row == rows - 1 ? count : first + effCols) - 1;
        float mid = ((float)r[first].x + (float)(r[last].x + r[last].w)) / 2.0f;
        if (fabsf(mid - 233.0f) > 1.5f) centred = false;
      }
      float vmid = ((float)r[0].y + (float)(r[count - 1].y + r[count - 1].h)) / 2.0f;
      snprintf(m, sizeof(m), "%d lamps, %d cols: rows (including a part-filled last row) and the block are centred", count, cols); CHECK(centred && fabsf(vmid - 233.0f) <= 1.5f, m);
      if (cols >= 2 && cols <= 4 && r[0].w < minW234) minW234 = r[0].w;
    }
  }
  CHECK(minW234 >= 90, "with 2-4 columns even 12 lamps are at least 90px wide - big enough to read");
  { GaugeLampRect a[12], b[12];
    gauge_indicator_layout(1, 1, b); CHECK(b[0].w == 220 && b[0].h == 136, "one lamp: the biggest, 220 x 136");
    gauge_indicator_layout(6, 2, a); CHECK(a[0].w == 156 && a[0].h == 97, "6 lamps in 2 columns: 156 x 97 (hand-checked: at 158 wide a corner would be 224.2px from the centre, past the 223 limit)");
    gauge_indicator_layout(12, 3, a); CHECK(a[0].w == 106 && a[0].h == 66, "12 lamps in 3 columns: 106 x 66"); }
  { GaugeLampRect r[12]; CHECK(gauge_indicator_layout(0, 2, r) == 0, "no lamps: nothing to place"); }
  { GaugeLampRect r[12]; gauge_indicator_layout(2, 4, r); CHECK(r[0].y == r[1].y && r[1].x > r[0].x, "2 lamps with 4 columns: the columns shrink to 2, one row"); }
  { GaugeLampRect r3[12], r4[12]; gauge_indicator_layout(3, 3, r3); gauge_indicator_layout(4, 3, r4); CHECK(r4[0].w <= r3[0].w, "adding a lamp never makes the others bigger"); }

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
