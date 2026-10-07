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

// ---- tick colour zones ----
// Ticks whose value is in a zone are drawn yellow or red instead of the dial's tick colour. A zone is at the
// HIGH end of the scale (ticks at or above its value) or, with zonesLow, the LOW end (at or below it). Where
// yellow and red overlap, red wins.
#define GAUGE_ZONE_YELLOW 0xFFD600
#define GAUGE_ZONE_RED    0xFF2A2A

#endif
