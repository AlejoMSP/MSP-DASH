// shift_light.h (hub) - the staged shift light across the screens (Setup page, hub 56 on).
// As RPM climbs from `start` to `shift`, the screens light yellow one at a time from left to right; at `shift` they all
// turn green; at `over` (0 = off) they all flash red. Level: 0 = off, 1..n = that many screens yellow, n+1 = all green,
// n+2 = all red. Going down needs SHIFT_HYST_RPM less than going up, so it does not flicker on a threshold.
// The hub sends the level and the screen order (LINK_CMD_SHIFT); each screen draws its own ring (gauge_app.c).
// Pure C: host-tested (tests/host/test_shift_light.cpp).
#pragma once
#include <stdint.h>
#include <string.h>

typedef struct __attribute__((packed)) { uint8_t version, on; uint16_t start, shift, over; } ShiftCfg;
#define SHIFT_VERSION 1
#define SHIFT_HYST_RPM 100
#define SHIFT_KEEPALIVE_MS 300   // while lit the hub repeats the level; a screen hears nothing for 1 s = off
static inline void shift_defaults(ShiftCfg *c) { c->version = SHIFT_VERSION; c->on = 0; c->start = 5000; c->shift = 6800; c->over = 7200; }
static inline int shift_valid(const ShiftCfg *c) {
  return c->version == SHIFT_VERSION && c->start >= 500 && c->shift > c->start && c->shift <= 12000 && (c->over == 0 || (c->over > c->shift && c->over <= 13000));
}
// The level for this RPM with no memory of the last one.
static inline int shift_level_raw(const ShiftCfg *c, int n, float rpm) {
  if (!c->on || n < 1) return 0;
  if (c->over && rpm >= c->over) return n + 2;
  if (rpm >= c->shift) return n + 1;
  int lvl = 0;
  for (int i = 0; i < n; i++) if (rpm >= c->start + (float)(c->shift - c->start) * i / n) lvl = i + 1;
  return lvl;
}
// With hysteresis: a lower level only once RPM is SHIFT_HYST_RPM below where the current one starts.
static inline int shift_level(const ShiftCfg *c, int n, float rpm, int prev) {
  int up = shift_level_raw(c, n, rpm);
  if (up >= prev) return up;
  int held = shift_level_raw(c, n, rpm + SHIFT_HYST_RPM);
  return held >= prev ? prev : up;
}
