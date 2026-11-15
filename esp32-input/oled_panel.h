// oled_panel.h (hub) - the warning-light panel on the 0.96" SSD1306 OLED: up to 8 lamps in a 4 x 2 grid, each an icon
// (oled_icons.h, the same numbers as the screens' icons) lit by a signal. No Arduino calls: host-tested
// (tests/host/test_oled_panel.cpp). The SSD1306 driver is oled_ssd1306.h.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "oled_icons.h"
#include "hub_pins.h"

#define OLED_W 128
#define OLED_H 64
#define OLED_LAMPS 8

enum { OLED_ON = 0, OLED_ABOVE = 1, OLED_BELOW = 2, OLED_INPUT = 3 };   // when a lamp is lit (INPUT: a wire to the hub)
enum { LAMP_OFF = 0, LAMP_WARN = 1, LAMP_DANGER = 2 };                     // lamp state
// Inputs a lamp can watch (OledLamp.input): any hub GPIO with an input (code OLED_IN_GPIO_BASE + GPIO), or the 16 pins of
// up to two MCP23017 expanders on the I2C bus (16..31 board 1 PA0..PB7, 32..47 board 2). All read with pull-ups.
// Codes 1-3 are the older GPIO 2 / 12 / 0, read as their GPIO codes.
#define OLED_IN_GPIO_BASE 64
#define OLED_IN_GPIO2 (OLED_IN_GPIO_BASE + 2)
#define OLED_IN_GPIO12 (OLED_IN_GPIO_BASE + 12)
#define OLED_IN_GPIO0 (OLED_IN_GPIO_BASE + 0)
#define OLED_IN_MCP0 16   // board 1: 16..31
#define OLED_IN_MCP1 32   // board 2: 32..47
static inline int oled_input_upgrade(int in) { return in == 1 ? OLED_IN_GPIO2 : in == 2 ? OLED_IN_GPIO12 : in == 3 ? OLED_IN_GPIO0 : in; }
static inline bool oled_input_valid(int in) {
  return (in >= OLED_IN_MCP0 && in < OLED_IN_MCP1 + 16) || (in >= OLED_IN_GPIO_BASE && in < OLED_IN_GPIO_BASE + 40 && (gpio_caps(in - OLED_IN_GPIO_BASE) & CAP_IN));
}
enum { OLED_FIXED = 0, OLED_COMPACT = 1 };                                   // layout: each lamp in its own spot / lit ones packed

typedef struct __attribute__((packed)) {
  uint8_t icon;      // 0 = slot unused, else an icon number (1..OLED_ICON_COUNT)
  uint8_t sig;       // the signal it watches (not used for OLED_INPUT)
  uint8_t op;        // OLED_ON (a flag or any non-zero value), OLED_ABOVE / OLED_BELOW level, OLED_INPUT (a hub input)
  uint8_t blink;     // 1 = a WARNING flashes too (DANGER always flashes)
  float level;       // WARNING level (Above / Below)
  float danger;      // DANGER level (Above / Below), used when has_danger
  uint8_t has_danger;// Above / Below: a second, worse level; Is on / Input: lit means DANGER instead of WARNING
  uint8_t input;     // OLED_INPUT: which input (OLED_IN_...)
  uint8_t input_low; // OLED_INPUT: 1 = lit when the input reads LOW (a switch to ground), 0 = when HIGH
  uint8_t spare;
} OledLamp;

typedef struct __attribute__((packed)) {
  OledLamp lamp[OLED_LAMPS];
  uint8_t layout;          // OLED_FIXED / OLED_COMPACT
  uint8_t version;         // OLED_CFG_VERSION (an older saved layout is replaced by the defaults)
} OledConfig;
#define OLED_CFG_VERSION 2

// The lamp's state: off, WARNING or DANGER. A signal with no live data never lights it (0 V from a silent ECU is not a
// warning; the panel says NO ECU DATA itself). inputLow: the lamp's input reads LOW (only for OLED_INPUT).
static inline int oled_lamp_state(const OledLamp *l, float value, bool hasData, bool inputLow) {
  if (!l->icon) return LAMP_OFF;
  if (l->op == OLED_INPUT) { bool on = l->input_low ? inputLow : !inputLow; return on ? (l->has_danger ? LAMP_DANGER : LAMP_WARN) : LAMP_OFF; }
  if (!hasData) return LAMP_OFF;
  if (l->op == OLED_ON) return value != 0.0f ? (l->has_danger ? LAMP_DANGER : LAMP_WARN) : LAMP_OFF;
  bool above = l->op == OLED_ABOVE;
  if (l->has_danger && (above ? value > l->danger : value < l->danger)) return LAMP_DANGER;
  if (above ? value > l->level : value < l->level) return LAMP_WARN;
  return LAMP_OFF;
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

// A cell drawn as DANGER: filled white with the icon cut out of it (black on white).
static inline void oled_icon_inverted(uint8_t *buf, int n, int x, int y) {
  if (n < 1 || n > OLED_ICON_COUNT) return;
  for (int p = 0; p < 4; p++) for (int c = 0; c < 32; c++) {
    int X = x + c, P = y / 8 + p;
    if (X >= 0 && X < OLED_W && P >= 0 && P < 8) buf[P * OLED_W + X] = (uint8_t)~OLED_ICONS[n][p * 32 + c];
  }
}

// The whole panel. state[i]: lamp i's LAMP_ state; blinkOn: the blink phase. The display has one colour, so the levels are
// told apart by style: WARNING = the icon (flashing if its blink box is ticked), DANGER = a white box with the icon cut
// out, flashing between that and the plain icon (never dark, so it cannot be missed). Lit lamps go in a 4 x 2 grid of
// 32 x 32 cells (fixed: lamp i in cell i; compact: lit ones packed from the top left). With nothing lit: `idle` (the
// clock, or "ALL OK") in big text and `sub` under it.
static inline int oled_render(uint8_t *buf, const OledConfig *cfg, const uint8_t state[OLED_LAMPS], bool blinkOn, const char *idle, const char *sub) {
  oled_clear(buf);
  int k = 0;
  for (int i = 0; i < OLED_LAMPS; i++) {
    const OledLamp *l = &cfg->lamp[i];
    if (!l->icon || state[i] == LAMP_OFF) continue;
    int cell = cfg->layout == OLED_COMPACT ? k : i, x = (cell % 4) * 32, y = (cell / 4) * 32;
    k++;
    if (state[i] == LAMP_DANGER) { if (blinkOn) oled_icon_inverted(buf, l->icon, x, y); else oled_icon(buf, l->icon, x, y); }
    else if (!l->blink || blinkOn) oled_icon(buf, l->icon, x, y);
  }
  if (k == 0) {
    oled_text(buf, idle, (OLED_W - oled_text_w(idle, 2)) / 2, 16, 2);
    if (sub && *sub) oled_text(buf, sub, (OLED_W - oled_text_w(sub, 1)) / 2, 44, 1);
  }
  return k;   // how many are lit (blinking ones count while in their off phase too)
}

// Defaults (warning / danger): check engine (warning), battery under 12.4 / 11.8 V, oil pressure under 15 / 8 psi,
// coolant over 220 / 235 F, seatbelt (GPIO 2 LOW), fuel under 15 / 7 %, rev limiter (danger), fan (warning).
// Icon numbers are the lines of tools/icons/icons.txt.
static inline void oled_defaults(OledConfig *c) {
  memset(c, 0, sizeof *c);
  const OledLamp d[OLED_LAMPS] = {
    //icon sig op          blink warn    danger  has  input          low
    {  1, 14, OLED_ON,    0, 0,      0,      0,   0,             0, 0 },   // engine        / SIG_CEL
    {  9, 10, OLED_BELOW, 0, 12.4f,  11.8f,  1,   0,             0, 0 },   // car-battery   / SIG_BATTV
    {  6,  5, OLED_BELOW, 0, 15.0f,  8.0f,   1,   0,             0, 0 },   // oil           / SIG_OILP
    {  4,  2, OLED_ABOVE, 0, 220.0f, 235.0f, 1,   0,             0, 0 },   // coolant temp  / SIG_CLT
    { 56,  0, OLED_INPUT, 0, 0,      0,      0,   OLED_IN_GPIO2, 1, 0 },   // seatbelt      / GPIO 2 LOW
    { 13, 24, OLED_BELOW, 0, 15.0f,  7.0f,   1,   0,             0, 0 },   // gas-station   / SIG_FUEL
    { 30, 11, OLED_ON,    0, 0,      0,      1,   0,             0, 0 },   // speedometer   / SIG_REVLIM (danger)
    { 26, 17, OLED_ON,    0, 0,      0,      0,   0,             0, 0 },   // fan           / SIG_FAN
  };
  memcpy(c->lamp, d, sizeof d);
  c->layout = OLED_FIXED; c->version = OLED_CFG_VERSION;
}
