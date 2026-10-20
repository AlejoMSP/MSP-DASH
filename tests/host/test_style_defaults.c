/* Tests the pure-C helpers in gauge_style.h for default scales, caption sizes and the vertical position. */
#include "gauge_style.h"
#include <stdio.h>

static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static bool range(int sig, int32_t a, int32_t b) { int32_t mn = -1, mx = -1; return gauge_signal_default_range(sig, &mn, &mx) && mn == a && mx == b; }

int main(void) {
  printf("1. gauge templates\n");
  CHECK(range(SIG_AFR, 10, 20), "AFR is 10 to 20");
  CHECK(range(SIG_CLT, 40, 300) && range(SIG_OILT, 40, 300), "coolant and oil temperature are 40 to 300 F");
  CHECK(range(SIG_OILP, 0, 700) && range(SIG_FUELP, 0, 700), "oil and fuel pressure are 0 to 700 kPa (about 100 psi)");
  CHECK(range(SIG_MAP, 0, 250), "MAP/boost is 0 to 250 kPa absolute");
  CHECK(range(SIG_BATTV, 8, 16) && range(SIG_RPM, 0, 8000), "battery 8 to 16 V, RPM 0 to 8000");
  { int32_t a = 7, b = 7; bool none = true;
    for (int s = SIG_FIRST_FLAG; s < SIG_FLAG_END; s++) if (gauge_signal_default_range(s, &a, &b)) none = false;
    CHECK(none && a == 7 && b == 7, "the ECU on/off flags have no template and are left untouched"); }
  { float l = 0; bool up = false;
    CHECK(gauge_signal_default_warning(SIG_CLT, &l, &up) && l == 230 && up, "coolant warns above 230");
    CHECK(gauge_signal_default_warning(SIG_BATTV, &l, &up) && l == 12 && !up, "battery warns below 12");
    CHECK(gauge_signal_default_warning(SIG_OILP, &l, &up) && l == 70 && !up, "oil pressure warns below 70 kPa");
    CHECK(!gauge_signal_default_warning(SIG_AFR, &l, &up) && !gauge_signal_default_warning(SIG_CEL, &l, &up), "AFR and the flags have no template warning"); }
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
