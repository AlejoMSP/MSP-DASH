// Tests esp32-input/rf_decode.h: pulses from the CC1101 -> half-bit time -> Manchester bits -> bytes.
// Build: g++ -std=c++17 -I../../esp32-input test_rf_decode.cpp
#include "rf_decode.h"
#include <stdlib.h>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)

// Builds the pulse train a Manchester transmitter makes for these bytes: bit 1 = high then low (T each).
static int encode(const uint8_t *bytes, int n, int T, int jitter, bool inverted, uint16_t *w, uint8_t *first) {
  uint8_t lv[4000]; int m = 0;
  for (int i = 0; i < n; i++) for (int k = 7; k >= 0; k--) {
    int bit = ((bytes[i] >> k) & 1) ^ inverted;
    lv[m++] = bit; lv[m++] = !bit;
  }
  int np = 0; *first = lv[0];
  for (int i = 0; i < m;) {
    int j = i; while (j < m && lv[j] == lv[i]) j++;
    int d = (j - i) * T + (jitter ? (rand() % (2 * jitter + 1)) - jitter : 0);
    w[np++] = (uint16_t)d; i = j;
  }
  return np;
}
static int decode(const uint16_t *w, int n, uint8_t first, uint8_t *bytes, uint32_t *T, bool *inv) {
  static uint8_t lv[RF_MAX_BITS * 2], bits[RF_MAX_BITS]; int nb;
  *T = rf_estimate_unit(w, n);
  int m = rf_pulses_to_levels(w, n, first, *T, lv, sizeof lv);
  return rf_best_manchester(lv, m, bytes, 32, bits, &nb, inv);
}

int main() {
  srand(1);
  const uint8_t msg[] = { 0xA5, 0x3C, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 };
  uint16_t w[1000]; uint8_t first, out[64]; uint32_t T; bool inv; char hex[200];

  printf("1. clean 9.6 kbit/s Manchester (half-bit 52 us)\n");
  int n = encode(msg, sizeof msg, 52, 0, false, w, &first);
  int nb = decode(w, n, first, out, &T, &inv);
  CHECK(T >= 50 && T <= 54, "half-bit time found");
  CHECK(nb >= 10 && !memcmp(out, msg, 10), "the 10 bytes come back exactly");
  rf_hex(out, 3, hex); CHECK(!strcmp(hex, "A5 3C 12"), "hex text");

  printf("2. timing jitter of +-12 us (the receiver is not a perfect clock)\n");
  n = encode(msg, sizeof msg, 52, 12, false, w, &first);
  nb = decode(w, n, first, out, &T, &inv);
  CHECK(nb >= 10 && !memcmp(out, msg, 10), "still exact");

  printf("3. 19.2 kbit/s and opposite polarity\n");
  n = encode(msg, sizeof msg, 26, 4, true, w, &first);
  nb = decode(w, n, first, out, &T, &inv);
  uint8_t flipped[10]; for (int i = 0; i < 10; i++) flipped[i] = (uint8_t)~msg[i];
  bool same = nb >= 10 && (!memcmp(out, msg, 10) || !memcmp(out, flipped, 10));
  CHECK(same, "bytes come back, as sent or bit-flipped (polarity cannot be known from the air)");

  printf("4. junk does not crash or invent a message\n");
  for (int i = 0; i < 300; i++) w[i] = (uint16_t)(5 + rand() % 3000);
  nb = decode(w, 300, 0, out, &T, &inv);
  CHECK(nb < 4, "random pulses give little or nothing");
  CHECK(rf_estimate_unit(w, 0) == 0 && rf_pulses_to_levels(w, 10, 0, 0, (uint8_t *)hex, 10) == 0, "empty input / no unit: nothing, no crash");
  uint16_t big[3] = { 60000, 60000, 60000 };
  CHECK(rf_estimate_unit(big, 3) == 0, "only long gaps: no unit");

  printf("5. a real capture: U912 tire sensor (fixtures/u912_burst1.txt)\n");
  { FILE *f = fopen("fixtures/u912_burst1.txt", "r"); uint16_t cap[700]; int nc = 0; char line[512];
    while (f && fgets(line, sizeof line, f)) { if (line[0] == '#') continue; char *p = line, *e; for (;;) { long v = strtol(p, &e, 10); if (e == p) break; if (nc < 700) cap[nc++] = (uint16_t)v; p = e; } }
    if (f) fclose(f);
    CHECK(nc == 293, "the fixture has the 293 pulses");
    char bits[RF_MAX_BITS + 1]; float unit = 0;
    int nb = rf_decode_pairs(cap, nc, 0, bits, RF_MAX_BITS, &unit);
    CHECK(unit > 51.5f && unit < 53.0f, "half-bit 52 us (9600 bit/s), from pulse pairs");
    CHECK(nb >= 120, "120+ Manchester bits in a row (the old single-pulse method found 0)");
    uint8_t by[32]; int nby = rf_bits_to_bytes(bits, nb, by, 32);
    const uint8_t want[] = { 0x44, 0x1D, 0xC5, 0xA2, 0xA2, 0xB5, 0x3B, 0x51, 0x0E, 0xCD, 0x44, 0x1D, 0xC5, 0xA2 };
    CHECK(nby >= 14 && !memcmp(by, want, sizeof want), "bytes 44 1D C5 A2 A2 B5 3B 51 0E CD, then 44 1D C5 A2 again (sent twice)");
    char hx[200]; rf_hex(by, nby, hx); printf("    decoded: %s\n", hx); }
  { uint16_t junk[300]; for (int i = 0; i < 300; i++) junk[i] = (uint16_t)(5 + rand() % 3000);
    char bits[RF_MAX_BITS + 1]; float unit; CHECK(rf_decode_pairs(junk, 300, 0, bits, RF_MAX_BITS, &unit) < 16, "random pulses: no long decode"); }

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
