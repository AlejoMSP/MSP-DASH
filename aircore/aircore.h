// aircore.h - the maths for the stock NB tach and speedo (air-core gauges), driven by an ESP32-S2 Mini through two
// DRV8833 boards. Pure C: host-tested (tests/host/test_aircore.cpp).
//
// An air-core gauge has two coils at right angles (sine and cosine). The needle points along the sum of their two
// magnetic fields, so driving the coils with sin(a) and cos(a) puts the needle at angle a. Each coil gets one H-bridge
// channel; the sign of the drive is the direction of the current.
#pragma once
#include <stdint.h>
#include <math.h>

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
static inline void aircore_bridge(float drive, uint32_t full, uint32_t *inA, uint32_t *inB) {
  float m = drive < 0 ? -drive : drive; if (m > 1) m = 1;
  uint32_t d = (uint32_t)(m * full + 0.5f);
  if (drive >= 0) { *inA = d; *inB = 0; } else { *inA = 0; *inB = d; }
}
