/* Tests the pure-C helpers in gauge_style.h for default scales, caption sizes and the vertical position. */
#include "gauge_style.h"
#include <stdio.h>

static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static bool range(int sig, int32_t a, int32_t b) { int32_t mn = -1, mx = -1; return gauge_signal_default_range(sig, &mn, &mx) && mn == a && mx == b; }

int main(void) {
  printf("1. default scales\n");
  CHECK(range(SIG_AFR, 10, 20), "AFR is 10 to 20");
  CHECK(range(SIG_CLT, 60, 250) && range(SIG_OILT, 60, 250), "coolant and oil temperature are 60 to 250");
  CHECK(range(SIG_OILP, 0, 100) && range(SIG_FUELP, 0, 100), "oil and fuel pressure are 0 to 100");
  { int32_t a = 7, b = 7;
    bool none = true;
    for (int s = 0; s < SIG_COUNT; s++) {
      if (s == SIG_AFR || s == SIG_CLT || s == SIG_OILT || s == SIG_OILP || s == SIG_FUELP || s == SIG_EGT || s == SIG_IAT || s == SIG_AMBT || s == SIG_HUM) continue;
      if (gauge_signal_default_range(s, &a, &b)) none = false;
    }
    CHECK(none, "every other signal has NO default scale (RPM, MAP/boost, ethanol, throttle, battery and the ECU flags keep whatever the gauge has)");
    CHECK(a == 7 && b == 7, "and asking about them leaves the numbers untouched"); }
  CHECK(!gauge_signal_default_range(SIG_MAP, &(int32_t){0}, &(int32_t){0}), "MAP/boost is not 0-100: under boost it reads above 100 kPa, so that scale would peg it");
  CHECK(!gauge_signal_default_range(-1, &(int32_t){0}, &(int32_t){0}) && !gauge_signal_default_range(SIG_COUNT, &(int32_t){0}, &(int32_t){0}), "out-of-range signal numbers are safe");

  printf("2. caption sizes\n");
  CHECK(gauge_snap_caption_size(0) == 0 && gauge_snap_caption_size(-3) == 0, "0 (never set) and negatives stay 0: the display's default font, as captions always looked");
  CHECK(gauge_snap_caption_size(14) == 14 && gauge_snap_caption_size(20) == 20 && gauge_snap_caption_size(28) == 28 && gauge_snap_caption_size(36) == 36 && gauge_snap_caption_size(48) == 48, "the five sizes are kept");
  CHECK(gauge_snap_caption_size(1) == 14 && gauge_snap_caption_size(16) == 14 && gauge_snap_caption_size(18) == 20 && gauge_snap_caption_size(25) == 28 && gauge_snap_caption_size(100) == 48, "anything else snaps to the NEAREST size");
  CHECK(gauge_snap_caption_size(17) == 14 && gauge_snap_caption_size(24) == 20, "exactly halfway between two sizes goes to the smaller one");

  printf("3. vertical position\n");
  CHECK(gauge_resolve_valign(0) == 0 && gauge_resolve_valign(1) == 1 && gauge_resolve_valign(2) == 2, "centre, top, bottom are kept");
  CHECK(gauge_resolve_valign(3) == 0 && gauge_resolve_valign(-1) == 0 && gauge_resolve_valign(99) == 0, "anything else is centre");

  printf("4. decimals\n");
  CHECK(gauge_resolve_decimals(0) == 0 && gauge_resolve_decimals(1) == 1 && gauge_resolve_decimals(2) == 2, "0, 1, 2 are kept");
  CHECK(gauge_resolve_decimals(3) == 1 && gauge_resolve_decimals(-1) == 1 && gauge_resolve_decimals(99) == 1, "anything else is 1, what readouts always showed");

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
