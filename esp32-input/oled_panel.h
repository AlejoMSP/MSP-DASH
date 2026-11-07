// oled_panel.h (hub) - the warning-light panel on the 0.96" SSD1306 OLED: up to 8 lamps in a 4 x 2 grid, each an icon
// (oled_icons.h, the same numbers as the screens' icons) lit by a signal. No Arduino calls: host-tested
// (tests/host/test_oled_panel.cpp). The SSD1306 driver is oled_ssd1306.h.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "oled_icons.h"

#define OLED_W 128
#define OLED_H 64
#define OLED_LAMPS 8

enum { OLED_ON = 0, OLED_ABOVE = 1, OLED_BELOW = 2, OLED_SEATBELT = 3 };   // when a lamp is lit
enum { OLED_FIXED = 0, OLED_COMPACT = 1 };                                   // layout: each lamp in its own spot / lit ones packed

typedef struct __attribute__((packed)) {
  uint8_t icon;      // 0 = slot unused, else an icon number (1..OLED_ICON_COUNT)
  uint8_t sig;       // the signal it watches (not used for OLED_SEATBELT)
  uint8_t op;        // OLED_ON (a flag or any non-zero value), OLED_ABOVE / OLED_BELOW level, OLED_SEATBELT (hub input)
  uint8_t blink;     // 1 = flashes while lit
  float level;
} OledLamp;

typedef struct __attribute__((packed)) {
  OledLamp lamp[OLED_LAMPS];
  uint8_t layout;          // OLED_FIXED / OLED_COMPACT
  uint8_t belt_lit_low;    // seatbelt input: 1 = lit when the pin reads LOW (switch to ground when unbuckled)
} OledConfig;

// Is the lamp lit? A signal with no live data never lights it (a missing ECU must not look like a warning... or OK:
// the panel shows "NO DATA" itself when the hub hears nothing).
static inline bool oled_lamp_lit(const OledLamp *l, float value, bool hasData, bool beltPinLow, bool beltLitLow) {
  if (!l->icon) return false;
  if (l->op == OLED_SEATBELT) return beltLitLow ? beltPinLow : !beltPinLow;
  if (!hasData) return false;
  if (l->op == OLED_ABOVE) return value > l->level;
  if (l->op == OLED_BELOW) return value < l->level;
  return value != 0.0f;
}

// ---- drawing into a 1024-byte SSD1306 buffer (8 pages of 128 columns, bit 0 = top) ----
static inline void oled_clear(uint8_t *buf) { memset(buf, 0, OLED_W * OLED_H / 8); }
static inline void oled_pixel(uint8_t *buf, int x, int y) { if (x >= 0 && x < OLED_W && y >= 0 && y < OLED_H) buf[(y / 8) * OLED_W + x] |= (uint8_t)(1 << (y % 8)); }
// A 32x32 icon with its top-left at (x, y); y must be a multiple of 8 (always is in the grid).
static inline void oled_icon(uint8_t *buf, int n, int x, int y) {
  if (n < 1 || n > OLED_ICON_COUNT) return;
  for (int p = 0; p < 4; p++) for (int c = 0; c < 32; c++) {
    int X = x + c, P = y / 8 + p;
    if (X >= 0 && X < OLED_W && P >= 0 && P < 8) buf[P * OLED_W + X] |= OLED_ICONS[n][p * 32 + c];
  }
}
// Text in the 8x8 font, scaled 1 or 2 times, starting at (x, y).
static inline void oled_text(uint8_t *buf, const char *s, int x, int y, int scale) {
  for (; *s; s++, x += 8 * scale) {
    int c = (unsigned char)*s; if (c < 32 || c > 126) c = '?';
    for (int col = 0; col < 8; col++) for (int row = 0; row < 8; row++)
      if (OLED_FONT8[c - 32][col] >> row & 1)
        for (int a = 0; a < scale; a++) for (int b = 0; b < scale; b++) oled_pixel(buf, x + col * scale + a, y + row * scale + b);
  }
}
static inline int oled_text_w(const char *s, int scale) { return (int)strlen(s) * 8 * scale; }

// The whole panel. lit[i]: lamp i is lit now; blinkOn: the blink phase. Lit lamps are drawn in a 4 x 2 grid of 32 x 32
// cells (fixed: lamp i in cell i; compact: lit ones packed from the top left). With nothing lit: `idle` (the clock, or
// "ALL OK") in big text and `sub` under it.
static inline int oled_render(uint8_t *buf, const OledConfig *cfg, const bool lit[OLED_LAMPS], bool blinkOn, const char *idle, const char *sub) {
  oled_clear(buf);
  int shown = 0, k = 0;
  for (int i = 0; i < OLED_LAMPS; i++) {
    const OledLamp *l = &cfg->lamp[i];
    if (!l->icon) continue;
    int cell = cfg->layout == OLED_COMPACT ? k : i;
    if (lit[i]) { k++; if (!l->blink || blinkOn) { oled_icon(buf, l->icon, (cell % 4) * 32, (cell / 4) * 32); shown++; } }
  }
  if (k == 0) {
    oled_text(buf, idle, (OLED_W - oled_text_w(idle, 2)) / 2, 16, 2);
    if (sub && *sub) oled_text(buf, sub, (OLED_W - oled_text_w(sub, 1)) / 2, 44, 1);
  }
  return k;   // how many are lit (blinking ones count while in their off phase too)
}

// Defaults: check engine, battery under 12.0 V, oil pressure under 10, coolant over 230, seatbelt, fuel under 15 %,
// rev limiter, fan. Icon numbers are the lines of tools/icons/icons.txt.
static inline void oled_defaults(OledConfig *c) {
  memset(c, 0, sizeof *c);
  const OledLamp d[OLED_LAMPS] = {
    { 1, 14, OLED_ON, 1, 0 },        // engine          / SIG_CEL
    { 9, 10, OLED_BELOW, 0, 12.0f }, // car-battery     / SIG_BATTV
    { 6, 5, OLED_BELOW, 1, 10.0f },  // oil             / SIG_OILP
    { 4, 2, OLED_ABOVE, 1, 230.0f }, // coolant temp    / SIG_CLT
    { 56, 0, OLED_SEATBELT, 0, 0 },  // seatbelt        / hub input GPIO 2
    { 13, 24, OLED_BELOW, 0, 15.0f },// gas-station     / SIG_FUEL
    { 30, 11, OLED_ON, 0, 0 },       // speedometer     / SIG_REVLIM
    { 26, 17, OLED_ON, 0, 0 },       // fan             / SIG_FAN
  };
  memcpy(c->lamp, d, sizeof d);
  c->layout = OLED_FIXED; c->belt_lit_low = 1;
}
