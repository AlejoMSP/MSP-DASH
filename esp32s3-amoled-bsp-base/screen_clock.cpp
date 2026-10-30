#include "screen_clock.h"
#include "gps_time.h"
#include <Arduino.h>

static volatile uint32_t s_utc = 0, s_atMs = 0;
static volatile int16_t s_off = 0;

void screen_clock_set(uint32_t utc, int16_t offsetMin) {
  if (utc < gt_epoch(2024, 1, 1, 0, 0, 0)) return;   // not a believable date
  s_atMs = millis(); s_utc = utc; s_off = offsetMin;
}
bool screen_clock_now(uint32_t *utc, int16_t *offsetMin) {
  uint32_t u = s_utc, at = s_atMs;
  if (!u) return false;
  *utc = u + (millis() - at) / 1000; *offsetMin = s_off;
  return true;
}
bool screen_clock_text(int fmt, char *out, int len) {
  uint32_t u; int16_t o;
  if (!screen_clock_now(&u, &o)) { if (len) out[0] = 0; return false; }
  gt_format(u, o, fmt, out, len);
  return true;
}
