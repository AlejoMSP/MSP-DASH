#ifndef GAUGE_STYLE_H
#define GAUGE_STYLE_H

/*
  gauge_style.h
  Pure-C helpers for HOW a cell is drawn, shared by the config code (which cleans up what
  the web editor sends), the display code, and the PC tests - so they cannot disagree.

    1. Readout text sizes. 14, 28 and 36 are LVGL's built-in Montserrat; 48 and up are
       digit-only fonts compiled into the sketch (font_digits_*.c, see tools/gen_fonts.sh),
       so lv_conf.h does not have to be edited. They contain only 0-9 + - . and space.
    2. Dial angles, in COMPASS degrees: 0 = straight up, increasing clockwise (90 = 3 o'clock,
       180 = straight down, 270 = 9 o'clock). A dial runs clockwise from `start` to `end`.
       The usual 270-degree car gauge starts at 225 (7:30) and ends at 135 (4:30).
*/

#include <stdint.h>
#include <stdbool.h>
#include "gauge_config.h"   // SignalId, for the default scales

// ---- default scales ----
// When a signal is chosen for a gauge, its scale starts at the usual range for that kind of reading:
// AFR 10-20, temperatures 60-250, pressures 0-100. MAP/boost is a pressure too, but it reads above 100 kPa under boost,
// so a 0-100 scale would peg it: it keeps its own 0-250. (The web editors keep the same table in JavaScript; a test
// reads THIS list and checks they agree.)
#define GAUGE_DEFAULT_RANGES(X) \
  X(SIG_AFR,   10, 20) \
  X(SIG_CLT,   60, 250) \
  X(SIG_OILT,  60, 250) \
  X(SIG_OILP,  0,  100) \
  X(SIG_FUELP, 0,  100)

static inline bool gauge_signal_default_range(int sig, int32_t *mn, int32_t *mx) {
#define GAUGE_RANGE_CASE(S, A, B) if (sig == (S)) { *mn = (A); *mx = (B); return true; }
  GAUGE_DEFAULT_RANGES(GAUGE_RANGE_CASE)
#undef GAUGE_RANGE_CASE
  return false;
}

// ---- caption text sizes ----
// 14, 28 and 36 are LVGL's own fonts; 20 and 48 are font_text_*.c (letters and digits). 0 = never set: the display's
// default font, exactly as captions always looked before this setting existed.
#define GAUGE_CAPTION_SIZE_COUNT 5
static const int32_t GAUGE_CAPTION_SIZES[GAUGE_CAPTION_SIZE_COUNT] = { 14, 20, 28, 36, 48 };

static inline int32_t gauge_snap_caption_size(int32_t v) {
  if (v <= 0) return 0;
  int32_t best = 14, bestDiff = 0x7FFFFFFF;
  for (int i = 0; i < GAUGE_CAPTION_SIZE_COUNT; i++) {
    int32_t d = v > GAUGE_CAPTION_SIZES[i] ? v - GAUGE_CAPTION_SIZES[i] : GAUGE_CAPTION_SIZES[i] - v;
    if (d < bestDiff) { bestDiff = d; best = GAUGE_CAPTION_SIZES[i]; }
  }
  return best;
}

// ---- where the gauge stands in its cell ----
#define GAUGE_VALIGN_CENTER 0
#define GAUGE_VALIGN_TOP    1
#define GAUGE_VALIGN_BOTTOM 2
static inline int32_t gauge_resolve_valign(int32_t v) { return (v == GAUGE_VALIGN_TOP || v == GAUGE_VALIGN_BOTTOM) ? v : GAUGE_VALIGN_CENTER; }

// ---- digits after the decimal point in a readout (the big number, the alert value) ----
#define GAUGE_DECIMALS_DEFAULT 1   // what every readout showed before this setting existed
static inline int32_t gauge_resolve_decimals(int32_t v) { return (v >= 0 && v <= 2) ? v : GAUGE_DECIMALS_DEFAULT; }

// ---- readout text sizes ----
#define GAUGE_VALUE_SIZE_COUNT 8
static const int32_t GAUGE_VALUE_SIZES[GAUGE_VALUE_SIZE_COUNT] = { 14, 28, 36, 48, 64, 80, 96, 120 };

// The allowed size nearest to v (a hand-edited or old config can hold anything).
static inline int32_t gauge_snap_value_size(int32_t v) {
  int32_t best = 28, bestDiff = 0x7FFFFFFF;
  for (int i = 0; i < GAUGE_VALUE_SIZE_COUNT; i++) {
    int32_t d = v > GAUGE_VALUE_SIZES[i] ? v - GAUGE_VALUE_SIZES[i] : GAUGE_VALUE_SIZES[i] - v;
    if (d < bestDiff) { bestDiff = d; best = GAUGE_VALUE_SIZES[i]; }
  }
  return best;
}

// The numbers around a needle dial: -1 = hidden, or 14 / 28 / 36. 0 (never set, e.g. a config
// saved before this setting existed) follows the readout size as it always did, so those
// dials look exactly as they did.
static inline int32_t gauge_resolve_tick_size(int32_t tick, int32_t valueSize) {
  if (tick == -1) return -1;
  if (tick == 14 || tick == 28 || tick == 36) return tick;
  return (valueSize == 14 || valueSize == 28 || valueSize == 36) ? valueSize : 28;
}

// ---- dial angles ----
#define GAUGE_DIAL_MIN_SWEEP 10   // a dial narrower than this is not a dial - the standard one is used instead

static inline int32_t gauge_dial_norm(int32_t deg) {   // any number of degrees -> 0..359
  int32_t d = deg % 360;
  return d < 0 ? d + 360 : d;
}

// The standard dials: 270 degrees for a needle gauge, 180 (flat side down) for a half moon.
static inline void gauge_dial_defaults(int isHalfMoon, int32_t *start, int32_t *end) {
  *start = isHalfMoon ? 270 : 225;
  *end = isHalfMoon ? 90 : 135;
}

// What to actually draw. start/end come out as 0..359 and sweep as 10..359. Equal angles (which
// is also what an unset, all-zero cell holds) or a sliver narrower than GAUGE_DIAL_MIN_SWEEP
// fall back to the standard dial for that gauge type.
static inline void gauge_dial_resolve(int isHalfMoon, int32_t startDeg, int32_t endDeg,
                                      int32_t *start, int32_t *end, int32_t *sweep) {
  int32_t s = gauge_dial_norm(startDeg), e = gauge_dial_norm(endDeg);
  int32_t sw = gauge_dial_norm(e - s);
  if (sw < GAUGE_DIAL_MIN_SWEEP) {
    gauge_dial_defaults(isHalfMoon, &s, &e);
    sw = gauge_dial_norm(e - s);
  }
  *start = s; *end = e; *sweep = sw;
}


// ---- needle trim ----
// The needle can be pulled back from BOTH ends, as a percentage of the dial's radius: a gap at the centre
// (hub) and a stop-short at the tip. 0 and 0 is the normal needle. Each end up to 60%, together at most 85%:
// the stock needle is about 94% of the radius long, so some needle is always left.
#define GAUGE_NEEDLE_TRIM_MAX 60
#define GAUGE_NEEDLE_TRIM_TOTAL 85

static inline int32_t gauge_clamp_trim(int32_t v) {
  return v < 0 ? 0 : (v > GAUGE_NEEDLE_TRIM_MAX ? GAUGE_NEEDLE_TRIM_MAX : v);
}

// Cleans up what the editor sent. If the two together are too much, the hub gap wins and the tip pull-back gives way.
static inline void gauge_resolve_needle_trim(int32_t hubPct, int32_t tipPct, int32_t *hub, int32_t *tip) {
  int32_t h = gauge_clamp_trim(hubPct), t = gauge_clamp_trim(tipPct);
  if (h + t > GAUGE_NEEDLE_TRIM_TOTAL) t = GAUGE_NEEDLE_TRIM_TOTAL - h;
  *hub = h; *tip = t;
}

// ---- warning zones (TunerStudio style) ----
// A dial has four thresholds, each switched on or off on its own:
//     LOW CRITICAL  (red)     the scale from its minimum up to the value
//     LOW WARNING   (yellow)  the scale from its minimum up to the value
//     HIGH WARNING  (yellow)  the scale from the value up to its maximum
//     HIGH CRITICAL (red)     the scale from the value up to its maximum
// They are drawn in a fixed order - yellow ones first, then red - and what is drawn later wins, so red covers
// yellow where they overlap: with low critical 20 and low warning 40 the dial is red up to 20 and yellow from 20 to 40.
// They can be drawn as coloured BANDS behind the tick marks, as coloured TICK MARKS, or both.
#define GAUGE_ZONE_YELLOW 0xFFD600
#define GAUGE_ZONE_RED    0xFF2A2A
#define GAUGE_ZONE_BANDS  1     // zone_style bit: a coloured arc behind the ticks
#define GAUGE_ZONE_TICKS  2     // zone_style bit: the tick marks themselves are coloured
#define GAUGE_ZONE_BAND_WIDTH 12   // pixels

static inline int32_t gauge_resolve_zone_style(int32_t v) { return (v >= 1 && v <= 3) ? v : GAUGE_ZONE_BANDS; }

typedef struct { int32_t start, end; int red; } GaugeZoneRange;

// Threshold index: 0 = low critical, 1 = low warning, 2 = high warning, 3 = high critical.
// Fills `out` with the ranges to draw, IN DRAW ORDER (yellow low, yellow high, red low, red high), for the thresholds
// that are on, clamped to [min, max]. A range that would be empty (start > end) is left out. Returns how many.
static inline int gauge_zone_ranges(const bool on[4], const int32_t at[4], int32_t min, int32_t max, GaugeZoneRange out[4]) {
  static const int order[4] = { 1, 2, 0, 3 };
  int n = 0;
  for (int k = 0; k < 4; k++) {
    int i = order[k];
    if (!on[i]) continue;
    bool low = (i < 2);
    int32_t a = low ? min : at[i], b = low ? at[i] : max;
    if (a < min) a = min;
    if (a > max) a = max;
    if (b < min) b = min;
    if (b > max) b = max;
    if (a > b) continue;
    out[n].start = a; out[n].end = b; out[n].red = (i == 0 || i == 3); n++;
  }
  return n;
}

#endif
