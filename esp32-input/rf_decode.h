// rf_decode.h (hub) - turns a burst of radio pulses (from the CC1101's demodulated data pin) into bits and bytes,
// so an unknown 433 MHz sensor (a TPMS wheel sensor) can be identified. No Arduino calls: host-tested
// (tests/host/test_rf_decode.cpp).
//
// A burst is a list of pulse widths in microseconds, alternating levels. Most TPMS sensors send Manchester-coded
// FSK at 9.6 to 20 kbit/s: every bit is one transition, so pulses are either one half-bit (T) or two (2T) long.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define RF_MAX_PULSES 600
#define RF_MAX_BITS 1200
#define RF_MAX_BYTES 64

// The shortest common pulse width: the half-bit time T. Uses the pulses between 20 and 2000 us; picks the
// smallest width that at least 1/8 of them are close to (within 30 %), so a few glitches do not set it.
static inline uint32_t rf_estimate_unit(const uint16_t *w, int n) {
  uint32_t best = 0;
  for (int i = 0; i < n; i++) {
    uint32_t c = w[i];
    if (c < 20 || c > 2000) continue;
    if (best && c >= best) continue;
    int near = 0, used = 0;
    for (int k = 0; k < n; k++) {
      if (w[k] < 20 || w[k] > 2000) continue;
      used++;
      if (w[k] * 10 >= c * 7 && w[k] * 10 <= c * 13) near++;
    }
    if (used && near * 8 >= used) best = c;
  }
  if (!best) return 0;
  // Refine twice: every pulse of about 1T or 2T votes with its width / k. (The smallest pulse alone is biased
  // short by jitter, and a short T rounds a long 2T pulse up to 3.)
  uint32_t T = best;
  for (int pass = 0; pass < 2; pass++) {
    uint32_t sum = 0; int cnt = 0;
    for (int k = 0; k < n; k++) {
      uint32_t q = (w[k] + T / 2) / T;
      if (q < 1 || q > 2) continue;
      sum += w[k] / q; cnt++;
    }
    if (cnt) T = sum / cnt;
  }
  return T;
}

// Pulses -> line levels at T resolution: a pulse of about k*T becomes k copies of its level (k = 1..8; longer
// pulses end the conversion, they are gaps). firstLevel is the level of pulse 0. Returns the number of levels.
static inline int rf_pulses_to_levels(const uint16_t *w, int n, uint8_t firstLevel, uint32_t T, uint8_t *lv, int maxLv) {
  if (!T) return 0;
  int m = 0; uint8_t level = firstLevel;
  for (int i = 0; i < n; i++, level ^= 1) {
    uint32_t k = (w[i] + T / 2) / T;
    if (k == 0) k = 1;
    if (k > 8) break;
    for (uint32_t j = 0; j < k && m < maxLv; j++) lv[m++] = level;
  }
  return m;
}

// Manchester decode from a start offset: each pair of half-bits must differ; (1,0) -> bit 1 (or 0 when inverted).
// Stops at the first pair that does not differ. Returns the number of bits.
static inline int rf_manchester(const uint8_t *lv, int m, int start, bool inverted, uint8_t *bits, int maxBits) {
  int nb = 0;
  for (int i = start; i + 1 < m && nb < maxBits; i += 2) {
    if (lv[i] == lv[i + 1]) break;
    bits[nb++] = (uint8_t)((lv[i] == 1) ^ inverted);
  }
  return nb;
}

// Finds the longest Manchester run (both start phases, both polarities). Writes up to maxBytes bytes and returns
// the number of whole bytes. bitsOut/bitCount give the full bit run for the page.
static inline int rf_best_manchester(const uint8_t *lv, int m, uint8_t *bytes, int maxBytes, uint8_t *bitsOut, int *bitCount, bool *invOut) {
  static uint8_t tmp[RF_MAX_BITS];
  int bestN = 0; bool bestInv = false;
  for (int phase = 0; phase < 2; phase++)
    for (int inv = 0; inv < 2; inv++) {
      for (int start = phase; start < m && start < phase + 400; start += 2) {
        int n = rf_manchester(lv, m, start, inv, tmp, RF_MAX_BITS);
        if (n > bestN) { bestN = n; bestInv = inv; memcpy(bitsOut, tmp, n); }
        if (n > 8) break;   // the first good run from this phase is the one
      }
    }
  *bitCount = bestN; *invOut = bestInv;
  int s = 0;   // bytes from the start of the run: the page also shows the bits, so a preamble/sync can be read off
  int nbytes = 0;
  for (int i = s; i + 8 <= bestN && nbytes < maxBytes; i += 8) {
    uint8_t b = 0;
    for (int k = 0; k < 8; k++) b = (uint8_t)((b << 1) | bitsOut[i + k]);
    bytes[nbytes++] = b;
  }
  return nbytes;
}

// ---- pair-based decoding (hub build 27) ----
// The CC1101 stretches one level and shortens the other by a similar amount (about 20 us at first, drifting to 7),
// so single pulses lie about the bit time. A pulse PAIR (one of each level) does not: it is 2, 3 or 4 half-bits.
// Real capture (tests/host/fixtures/u912_burst1.txt): pairs of 105 / 157 / 209 us -> half-bit 52 us, 9600 bit/s.

// Longest run of consecutive pulse pairs whose sums are 2, 3 or 4 x T (within 18 %). Both pairings are tried.
static inline int rf_pair_run(const uint16_t *w, int n, uint32_t T, int *start) {
  int best = 0;
  for (int par = 0; par < 2; par++) {
    int run = 0;
    for (int i = par; i + 1 < n; i += 2) {
      uint32_t S = (uint32_t)w[i] + w[i + 1], k = (S + T / 2) / T;
      int err = (int)S - (int)(k * T); if (err < 0) err = -err;
      if (k >= 2 && k <= 4 && err * 100 < (int)T * 18) {
        run++;
        if (run > best) { best = run; *start = i - 2 * (run - 1); }
      } else run = 0;
    }
  }
  return best;
}

// The half-bit time: the T (20..300 us) that gives the longest run of good pairs.
static inline uint32_t rf_pair_unit(const uint16_t *w, int n) {
  int bestRun = 0, st; uint32_t bestT = 0;
  for (uint32_t T = 20; T <= 300; T++) {
    int r = rf_pair_run(w, n, T, &st);
    if (r > bestRun) { bestRun = r; bestT = T; }
  }
  return bestRun >= 8 ? bestT : 0;   // 8 good pairs at least, or there is no steady signal
}

// Whole decode: unit, glitch merge, the good run, local stretch correction, Manchester. Writes '0'/'1' bits
// (up to maxBits) and returns how many; *unitOut is the refined half-bit time in us.
static inline int rf_decode_pairs(const uint16_t *win, int nin, uint8_t firstLevel, char *bitsOut, int maxBits, float *unitOut) {
  static uint16_t w[RF_MAX_PULSES]; static uint8_t lvl[RF_MAX_PULSES]; static uint8_t lv[RF_MAX_BITS * 2];
  *unitOut = 0;
  uint32_t T = rf_pair_unit(win, nin);
  if (!T) return 0;
  // merge glitches shorter than T/3 into the pulses around them (keeps the levels alternating)
  int n = 0;
  for (int i = 0; i < nin && n < RF_MAX_PULSES; i++) {
    uint8_t level = firstLevel ^ (i & 1);
    if (win[i] * 3 < T && n > 0 && i + 1 < nin) { uint32_t m = (uint32_t)w[n - 1] + win[i] + win[i + 1]; w[n - 1] = m > 65535 ? 65535 : m; i++; continue; }
    w[n] = win[i]; lvl[n] = level; n++;
  }
  int a = 0, np = rf_pair_run(w, n, T, &a);
  if (np < 8) return 0;
  uint32_t sum = 0, units = 0;
  for (int k = 0; k < np; k++) { uint32_t S = (uint32_t)w[a + 2 * k] + w[a + 2 * k + 1]; sum += S; units += (S + T / 2) / T; }
  float Tf = (float)sum / (float)units; *unitOut = Tf;
  int m = 0; uint8_t levA = lvl[a];
  for (int k = 0; k < np; k++) {
    float A = w[a + 2 * k], B = w[a + 2 * k + 1];
    int S = (int)((A + B) / Tf + 0.5f);
    // local stretch: median of (A - T) over nearby 1+1 pairs
    float near[17]; int nn = 0;
    for (int j = k - 8; j <= k + 8; j++) {
      if (j < 0 || j >= np) continue;
      float Aj = w[a + 2 * j], Bj = w[a + 2 * j + 1];
      if ((int)((Aj + Bj) / Tf + 0.5f) == 2) near[nn++] = Aj - Tf;
    }
    for (int x = 1; x < nn; x++) { float v = near[x]; int y = x - 1; while (y >= 0 && near[y] > v) { near[y + 1] = near[y]; y--; } near[y + 1] = v; }
    float bias = nn ? near[nn / 2] : 0;
    int kA = (int)((A - bias) / Tf + 0.5f); if (kA < 1) kA = 1; if (kA > S - 1) kA = S - 1;
    for (int q = 0; q < kA && m < (int)sizeof lv; q++) lv[m++] = levA;
    for (int q = 0; q < S - kA && m < (int)sizeof lv; q++) lv[m++] = levA ^ 1;
  }
  // Manchester from both half-bit phases; the longer one wins. (1,0) = 1.
  int best = 0;
  for (int st = 0; st < 2; st++) {
    int nb = 0;
    for (int i = st; i + 1 < m && nb < maxBits; i += 2) { if (lv[i] == lv[i + 1]) break; nb++; }
    if (nb > best) {
      best = nb; nb = 0;
      for (int i = st; i + 1 < m && nb < maxBits; i += 2) { if (lv[i] == lv[i + 1]) break; bitsOut[nb++] = lv[i] ? '1' : '0'; }
    }
  }
  bitsOut[best] = 0;
  return best;
}

// '0'/'1' text -> bytes (whole bytes only). Returns the number of bytes.
static inline int rf_bits_to_bytes(const char *bits, int nbits, uint8_t *out, int maxBytes) {
  int nb = 0;
  for (int i = 0; i + 8 <= nbits && nb < maxBytes; i += 8) {
    uint8_t b = 0;
    for (int k = 0; k < 8; k++) b = (uint8_t)((b << 1) | (bits[i + k] == '1'));
    out[nb++] = b;
  }
  return nb;
}

// Hex string of bytes, "A1 B2 ...". out must hold 3*n+1 chars.
static inline void rf_hex(const uint8_t *b, int n, char *out) {
  int p = 0;
  for (int i = 0; i < n; i++) p += sprintf(out + p, "%02X%s", b[i], i + 1 < n ? " " : "");
  out[p] = 0;
}
