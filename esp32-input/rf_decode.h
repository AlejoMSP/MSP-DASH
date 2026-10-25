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

// Hex string of bytes, "A1 B2 ...". out must hold 3*n+1 chars.
static inline void rf_hex(const uint8_t *b, int n, char *out) {
  int p = 0;
  for (int i = 0; i < n; i++) p += sprintf(out + p, "%02X%s", b[i], i + 1 < n ? " " : "");
  out[p] = 0;
}
