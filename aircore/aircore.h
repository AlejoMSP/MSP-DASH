// aircore.h - the maths for the stock NB tach and speedo (air-core gauges), driven by an ESP32-S2 Mini through two
// DRV8833 boards. Pure C: host-tested (tests/host/test_aircore.cpp).
//
// An air-core gauge has two coils at right angles (sine and cosine). The needle points along the sum of their two
// magnetic fields, so driving the coils with sin(a) and cos(a) puts the needle at angle a. Each coil gets one H-bridge
// channel; the sign of the drive is the direction of the current.
#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>

typedef struct __attribute__((packed)) {
  float minVal, maxVal;     // the value at the start and at the end of the dial (RPM, or mph)
  float zeroDeg;            // coil angle for minVal: where the needle rests at the start of the scale
  float spanDeg;            // degrees from the start to the end of the scale (negative = the other way round)
  float strength;           // 0.1-1.0: drive as a share of full; less = cooler coils, weaker needle
  float slewDegS;           // fastest the needle may move, degrees a second (smooth, like the stock one)
} AirCoreCfg;
#define AIRCORE_VERSION 1

static inline void aircore_defaults_tach(AirCoreCfg *c) { c->minVal = 0; c->maxVal = 8000; c->zeroDeg = 0; c->spanDeg = 240; c->strength = 0.8f; c->slewDegS = 720; }
static inline void aircore_defaults_speedo(AirCoreCfg *c) { c->minVal = 0; c->maxVal = 150; c->zeroDeg = 0; c->spanDeg = 240; c->strength = 0.8f; c->slewDegS = 360; }
static inline int aircore_valid(const AirCoreCfg *c) {
  return c->maxVal > c->minVal && c->spanDeg >= -350 && c->spanDeg <= 350 && c->spanDeg != 0 && c->zeroDeg >= -360 && c->zeroDeg <= 360
         && c->strength >= 0.1f && c->strength <= 1.0f && c->slewDegS >= 10 && c->slewDegS <= 5000;
}
// The needle angle for a value, held inside the scale.
static inline float aircore_angle(const AirCoreCfg *c, float v) {
  if (v < c->minVal) v = c->minVal;
  if (v > c->maxVal) v = c->maxVal;
  return c->zeroDeg + (v - c->minVal) / (c->maxVal - c->minVal) * c->spanDeg;
}
// Move toward the target, at most slewDegS for the time passed.
static inline float aircore_slew(float now, float target, float slewDegS, float dtS) {
  float step = slewDegS * dtS, d = target - now;
  if (d > step) return now + step;
  if (d < -step) return now - step;
  return target;
}
// Coil drive for an angle: sine and cosine coil, each -1..1 (times strength).
static inline void aircore_coils(float deg, float strength, float *sinOut, float *cosOut) {
  float r = deg * 3.14159265f / 180.0f;
  *sinOut = sinf(r) * strength; *cosOut = cosf(r) * strength;
}
// One coil's two DRV8833 inputs from a drive of -1..1: PWM on one side, the other low (fast decay). full = max duty.
// Fast decay: between pulses the coil current is pushed back against the supply, so it falls quickly; with a coil
// this slow (inductive) against a 20 kHz PWM the average current is well below duty x V / R and not in proportion.
static inline void aircore_bridge(float drive, uint32_t full, uint32_t *inA, uint32_t *inB) {
  float m = drive < 0 ? -drive : drive; if (m > 1) m = 1;
  uint32_t d = (uint32_t)(m * full + 0.5f);
  if (drive >= 0) { *inA = d; *inB = 0; } else { *inA = 0; *inB = d; }
}
// Slow decay (aircore build 5, the default): one input held high, the other PWM'd low for the on-time; between pulses
// both are high, the DRV8833 shorts the coil (brake) and its current keeps flowing. The average current then follows
// duty x V / R: stronger and in proportion, so the needle holds where it is told.
static inline void aircore_bridge_slow(float drive, uint32_t full, uint32_t *inA, uint32_t *inB) {
  float m = drive < 0 ? -drive : drive; if (m > 1) m = 1;
  uint32_t d = (uint32_t)(m * full + 0.5f);
  if (drive >= 0) { *inA = full; *inB = full - d; } else { *inA = full - d; *inB = full; }
}

// ---- multi-point calibration (aircore build 6) ----
// The gauge's return magnet makes the needle lag the coils' field more the further it swings, so one zero and span
// cannot put every mark in the right place. Points: the coil angle for a value (0 RPM at 180, 2000 at 118, ...).
// With two or more points the needle angle comes from them (straight lines between points, held at the end points);
// with fewer, from zero and span as before.
#define AIRCORE_POINTS 12
typedef struct __attribute__((packed)) { uint8_t n; float v[AIRCORE_POINTS]; float a[AIRCORE_POINTS]; } AirCorePoints;
static inline void aircore_points_clear(AirCorePoints *p) { memset(p, 0, sizeof *p); }
static inline int aircore_points_valid(const AirCorePoints *p) {
  if (p->n > AIRCORE_POINTS) return 0;
  for (int i = 0; i < p->n; i++) { if (p->a[i] < -720 || p->a[i] > 720 || p->v[i] != p->v[i]) return 0; if (i && p->v[i] <= p->v[i - 1]) return 0; }
  return 1;
}
// Add or move a point (a value already there gets the new angle). Kept in order of value. 0 = table full.
static inline int aircore_points_set(AirCorePoints *p, float v, float a) {
  for (int i = 0; i < p->n; i++) if (p->v[i] == v) { p->a[i] = a; return 1; }
  if (p->n >= AIRCORE_POINTS) return 0;
  int i = p->n;
  while (i > 0 && p->v[i - 1] > v) { p->v[i] = p->v[i - 1]; p->a[i] = p->a[i - 1]; i--; }
  p->v[i] = v; p->a[i] = a; p->n++;
  return 1;
}
static inline int aircore_points_remove(AirCorePoints *p, float v) {
  for (int i = 0; i < p->n; i++) if (p->v[i] == v) {
    for (int k = i; k + 1 < p->n; k++) { p->v[k] = p->v[k + 1]; p->a[k] = p->a[k + 1]; }
    p->n--; return 1;
  }
  return 0;
}
// The needle angle for a value: from the points if there are two or more, else from zero and span.
static inline float aircore_angle_pts(const AirCoreCfg *c, const AirCorePoints *p, float v) {
  if (!p || p->n < 2) return aircore_angle(c, v);
  if (v <= p->v[0]) return p->a[0];
  if (v >= p->v[p->n - 1]) return p->a[p->n - 1];
  int i = 1; while (v > p->v[i]) i++;
  float f = (v - p->v[i - 1]) / (p->v[i] - p->v[i - 1]);
  return p->a[i - 1] + f * (p->a[i] - p->a[i - 1]);
}

// ---- smoothing (aircore build 12) ----
// The ECU's RPM moves a little from packet to packet (20 a second), and the needle followed every step: it jittered.
// A first-order low-pass on the live value: after tau the needle has gone 63 % of the way to a new value. tau 0 = off.
static inline float aircore_lowpass(float now, float target, float dtS, float tauS) {
  if (tauS <= 0 || dtS <= 0) return target;
  float k = 1.0f - expf(-dtS / tauS);
  return now + (target - now) * k;
}
#define AIRCORE_SMOOTH_MAX_MS 2000
static inline float aircore_smooth_default(int isTach) { return isTach ? 120.0f : 300.0f; }
