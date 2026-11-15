// imu_math.h (hub) - turns MPU6050 readings into the car's G and yaw rate: which chip axis points forward and which points
// left (set on the hub home page), a zero taken with the car still and level, and smoothing. No Arduino calls:
// host-tested (tests/host/test_imu_math.cpp).
#pragma once
#include <stdint.h>
#include <math.h>
#include <stdlib.h>

// An axis code: +1/+2/+3 = the chip's +X/+Y/+Z (printed on the module), -1/-2/-3 = the opposite way.
typedef struct __attribute__((packed)) {
  int8_t fwd, left;              // the chip axis pointing to the car's front, and to its left
  uint8_t zeroed;                // 1 once a zero was taken
  uint8_t version;
  float zero[4];                 // car-frame lateral, longitudinal, vertical (g) and yaw (deg/s) at rest
} ImuConfig;
#define IMU_CFG_VERSION 1

static inline void imu_defaults(ImuConfig *c) {
  c->fwd = 1; c->left = 2; c->zeroed = 0; c->version = IMU_CFG_VERSION;   // module flat, label up, X arrow to the front
  for (int i = 0; i < 4; i++) c->zero[i] = 0;
}
static inline bool imu_axes_valid(int fwd, int left) {
  return fwd && left && fwd >= -3 && fwd <= 3 && left >= -3 && left <= 3 && abs(fwd) != abs(left);
}
// The component of chip vector v along an axis code.
static inline float imu_along(const float v[3], int code) { return code > 0 ? v[code - 1] : -v[-code - 1]; }
// The "up" axis is forward x left (right-handed): returns the component of v along it.
static inline float imu_up(const float v[3], int fwd, int left) {
  float f[3] = {0, 0, 0}, l[3] = {0, 0, 0};
  f[abs(fwd) - 1] = fwd > 0 ? 1.0f : -1.0f; l[abs(left) - 1] = left > 0 ? 1.0f : -1.0f;
  float u[3] = { f[1] * l[2] - f[2] * l[1], f[2] * l[0] - f[0] * l[2], f[0] * l[1] - f[1] * l[0] };
  return v[0] * u[0] + v[1] * u[1] + v[2] * u[2];
}
// Raw car-frame values before the zero: out = { lateral (+ left), longitudinal (+ forward), vertical (+ up), yaw (+ left) }.
// An accelerometer at rest reads +1 g UP (it feels the floor pushing), so vertical reads about 1.0 until zeroed.
static inline void imu_car_frame(const ImuConfig *c, const float acc_g[3], const float gyro_dps[3], float out[4]) {
  out[0] = imu_along(acc_g, c->left);
  out[1] = imu_along(acc_g, c->fwd);
  out[2] = imu_up(acc_g, c->fwd, c->left);
  out[3] = imu_up(gyro_dps, c->fwd, c->left);
}
// The car's acceleration the way a driver reads it: + lateral = turning left, + longitudinal = speeding up, - = braking,
// vertical 0 at rest, yaw + = turning left. The zero takes out the mounting tilt and the gyro's drift.
// (A turn to the left pushes you to the right: the car itself accelerates to its left, which is what the chip measures.)
static inline void imu_apply(const ImuConfig *c, const float raw[4], float out[4]) {
  for (int i = 0; i < 4; i++) out[i] = raw[i] - (c->zeroed ? c->zero[i] : (i == 2 ? 1.0f : 0.0f));
}
// Smoothing: a first-order low-pass. alpha = dt / (tau + dt); 50 Hz with tau 0.1 s gives about 0.17.
static inline float imu_smooth(float prev, float now, float alpha) { return prev + alpha * (now - prev); }
