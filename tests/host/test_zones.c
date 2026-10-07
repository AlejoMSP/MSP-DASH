/* Tests the pure-C warning-zone logic in gauge_style.h: which ranges of the scale are red / yellow, in what order. */
#include "gauge_style.h"
#include <stdio.h>
#include <string.h>

static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)

// thresholds are given as {low critical, low warning, high warning, high critical}, each as "value" or -9999 for "switched off"
#define OFF (-9999)
static int run(const int32_t v[4], int32_t min, int32_t max, GaugeZoneRange *out) {
  bool on[4]; int32_t at[4];
  for (int i = 0; i < 4; i++) { on[i] = v[i] != OFF; at[i] = on[i] ? v[i] : 0; }
  return gauge_zone_ranges(on, at, min, max, out);
}
static bool is(const GaugeZoneRange *r, int32_t a, int32_t b, int red) { return r->start == a && r->end == b && r->red == red; }

int main(void) {
  GaugeZoneRange r[4];
  printf("1. each threshold on its own (scale 0..100)\n");
  { int32_t v[4] = { OFF, OFF, OFF, OFF }; CHECK(run(v, 0, 100, r) == 0, "nothing switched on: no zones"); }
  { int32_t v[4] = { 20, OFF, OFF, OFF }; CHECK(run(v, 0, 100, r) == 1 && is(&r[0], 0, 20, 1), "low critical 20: RED from 0 up to 20"); }
  { int32_t v[4] = { OFF, 40, OFF, OFF }; CHECK(run(v, 0, 100, r) == 1 && is(&r[0], 0, 40, 0), "low warning 40: YELLOW from 0 up to 40"); }
  { int32_t v[4] = { OFF, OFF, 60, OFF }; CHECK(run(v, 0, 100, r) == 1 && is(&r[0], 60, 100, 0), "high warning 60: YELLOW from 60 up to 100"); }
  { int32_t v[4] = { OFF, OFF, OFF, 80 }; CHECK(run(v, 0, 100, r) == 1 && is(&r[0], 80, 100, 1), "high critical 80: RED from 80 up to 100"); }

  printf("2. all four, in DRAW order (yellow first, red last, so red covers yellow)\n");
  { int32_t v[4] = { 20, 40, 60, 80 }; int n = run(v, 0, 100, r);
    CHECK(n == 4 && is(&r[0], 0, 40, 0) && is(&r[1], 60, 100, 0) && is(&r[2], 0, 20, 1) && is(&r[3], 80, 100, 1), "yellow 0-40, yellow 60-100, then red 0-20 and red 80-100 drawn over them");
    CHECK(!r[0].red && !r[1].red && r[2].red && r[3].red, "both yellows come before both reds"); }
  { int32_t v[4] = { OFF, 40, 60, OFF }; CHECK(run(v, 0, 100, r) == 2 && is(&r[0], 0, 40, 0) && is(&r[1], 60, 100, 0), "only the two warnings on: two yellow bands, no red"); }
  { int32_t v[4] = { 20, OFF, OFF, 80 }; CHECK(run(v, 0, 100, r) == 2 && is(&r[0], 0, 20, 1) && is(&r[1], 80, 100, 1), "only the two criticals on: two red bands"); }
  { int32_t v[4] = { 90, 40, 10, 5 }; int n = run(v, 0, 100, r); CHECK(n == 4, "thresholds in a silly order are still all drawn (red wins, so it is the user's call)"); }

  printf("3. thresholds outside the scale are clamped, never drawn outside it\n");
  { int32_t v[4] = { OFF, 300, OFF, OFF }; CHECK(run(v, 0, 100, r) == 1 && is(&r[0], 0, 100, 0), "low warning 300 on a 0..100 scale: the whole scale"); }
  { int32_t v[4] = { OFF, -5, OFF, OFF }; CHECK(run(v, 0, 100, r) == 1 && is(&r[0], 0, 0, 0), "low warning -5: nothing of the scale (a zero-width range at the minimum)"); }
  { int32_t v[4] = { OFF, OFF, 150, OFF }; CHECK(run(v, 0, 100, r) == 1 && is(&r[0], 100, 100, 0), "high warning 150: a zero-width range at the maximum"); }
  { int32_t v[4] = { OFF, OFF, -50, OFF }; CHECK(run(v, 0, 100, r) == 1 && is(&r[0], 0, 100, 0), "high warning -50: the whole scale"); }
  { int32_t v[4] = { OFF, OFF, OFF, 100 }; CHECK(run(v, 0, 100, r) == 1 && is(&r[0], 100, 100, 0 + 1), "high critical exactly at the maximum: a zero-width range there"); }

  printf("4. scales that do not start at zero\n");
  { int32_t v[4] = { -30, -10, 150, 190 }; int n = run(v, -40, 250, r); CHECK(n == 4 && is(&r[0], -40, -10, 0) && is(&r[1], 150, 250, 0) && is(&r[2], -40, -30, 1) && is(&r[3], 190, 250, 1), "a -40..250 temperature scale"); }
  { int32_t v[4] = { 5, OFF, OFF, OFF }; CHECK(run(v, 5, 5, r) == 1 && is(&r[0], 5, 5, 1), "a scale with min = max: one zero-width range, no crash"); }

  printf("5. the style setting\n");
  CHECK(gauge_resolve_zone_style(1) == 1 && gauge_resolve_zone_style(2) == 2 && gauge_resolve_zone_style(3) == 3, "1 bands, 2 tick marks, 3 both are kept");
  CHECK(gauge_resolve_zone_style(0) == 1 && gauge_resolve_zone_style(4) == 1 && gauge_resolve_zone_style(-1) == 1 && gauge_resolve_zone_style(999) == 1, "anything else becomes 1 (bands)");
  CHECK((GAUGE_ZONE_BANDS | GAUGE_ZONE_TICKS) == 3 && (3 & GAUGE_ZONE_BANDS) && (3 & GAUGE_ZONE_TICKS) && !(2 & GAUGE_ZONE_BANDS) && !(1 & GAUGE_ZONE_TICKS), "style 3 means both, 2 ticks only, 1 bands only");

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
