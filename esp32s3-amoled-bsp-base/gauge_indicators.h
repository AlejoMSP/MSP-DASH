#ifndef GAUGE_INDICATORS_H
#define GAUGE_INDICATORS_H

/*
  gauge_indicators.h
  Pure-C logic for an INDICATORS page (a grid of on/off lamps, like TunerStudio's indicator panel), shared by the
  display code and the PC tests so they cannot disagree:
    - when is a lamp on (a threshold on any signal, or an ECU on/off flag), with an optional hysteresis band
    - where the lamps go on the round 466x466 panel, as big as they can be while staying fully on the glass
  The web dashboard repeats the on/off rule in JavaScript (tests/host/test_front_page.js checks the two agree).
*/

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define GAUGE_IND_ABOVE 0   // the lamp is ON while the value is above the threshold
#define GAUGE_IND_BELOW 1   // ... while it is below it

// Is the lamp on now? `wasOn` is its previous state, which only matters inside the hysteresis band.
//   above: turns on when value > threshold, turns off again when value <= threshold - hysteresis
//   below: turns on when value < threshold, turns off again when value >= threshold + hysteresis
// No data (never seen, or the bus went quiet) or a value that is not a number: OFF. A lamp that lights because
// a sensor went silent would be a false alarm. An ECU on/off flag is 1 or 0, so "above 0.5" means "the flag is on".
static inline bool gauge_indicator_next(bool wasOn, bool hasData, float value, int op, float threshold, float hysteresis) {
  if (!hasData || !isfinite(value) || !isfinite(threshold)) return false;   // a lamp with no usable threshold never lights
  if (!(hysteresis > 0.0f)) hysteresis = 0.0f;
  if (op == GAUGE_IND_BELOW) return wasOn ? !(value >= threshold + hysteresis) : (value < threshold);
  return wasOn ? !(value <= threshold - hysteresis) : (value > threshold);
}

// ---- layout ----
#define GAUGE_IND_MAX_COLS 4
#define GAUGE_IND_PANEL_R 223.0f       // lamp corners must stay inside this radius (the glass is round, radius 233)

typedef struct { int16_t x, y, w, h; } GaugeLampRect;

// Places `count` lamps in a grid `cols` wide (rows fill left to right, a partly filled last row is centred),
// as large as possible with all four corners of every lamp inside the round panel. Lamps are 1.6 : 1 with an 8px gap.
// Returns how many rects were written (0 for no lamps).
static inline int gauge_indicator_layout(int count, int cols, GaugeLampRect *out) {
  if (count < 1) return 0;
  if (cols < 1) cols = 1;
  if (cols > GAUGE_IND_MAX_COLS) cols = GAUGE_IND_MAX_COLS;
  if (cols > count) cols = count;
  const int rows = (count + cols - 1) / cols;
  const float centre = 233.0f, gap = 8.0f, aspect = 0.62f;
  for (int w = 220; w >= 36; w -= 2) {
    float h = (float)w * aspect;
    float top = centre - (rows * h + (rows - 1) * gap) / 2.0f;
    bool fits = true;
    for (int i = 0; i < count && fits; i++) {
      int row = i / cols, col = i % cols;
      int inRow = (row == rows - 1) ? count - row * cols : cols;
      float left = centre - (inRow * (float)w + (inRow - 1) * gap) / 2.0f + col * ((float)w + gap);
      float y = top + row * (h + gap);
      for (int cx = 0; cx < 2 && fits; cx++)
        for (int cy = 0; cy < 2; cy++) {
          float dx = left + cx * (float)w - centre, dy = y + cy * h - centre;
          if (dx * dx + dy * dy > GAUGE_IND_PANEL_R * GAUGE_IND_PANEL_R) { fits = false; break; }
        }
    }
    if (!fits && w > 36) continue;
    for (int i = 0; i < count; i++) {
      int row = i / cols, col = i % cols;
      int inRow = (row == rows - 1) ? count - row * cols : cols;
      float left = centre - (inRow * (float)w + (inRow - 1) * gap) / 2.0f + col * ((float)w + gap);
      out[i].x = (int16_t)(left + 0.5f); out[i].y = (int16_t)(top + row * (h + gap) + 0.5f);
      out[i].w = (int16_t)w;             out[i].h = (int16_t)(h + 0.5f);
    }
    return count;
  }
  return 0;
}

#endif
