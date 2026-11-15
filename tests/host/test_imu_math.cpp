// The hub's accelerometer math (esp32-input/imu_math.h): mounting axes, the zero, signs a driver expects, smoothing.
// Build: g++ -std=c++17 -I../../esp32-input test_imu_math.cpp
#include "imu_math.h"
#include <stdio.h>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static bool near(float a, float b) { return fabsf(a - b) < 0.001f; }
static void run(const ImuConfig *c, float ax, float ay, float az, float gx, float gy, float gz, float out[4]) {
  float a[3] = { ax, ay, az }, g[3] = { gx, gy, gz }, raw[4]; imu_car_frame(c, a, g, raw); imu_apply(c, raw, out);
}
int main() {
  ImuConfig c; imu_defaults(&c); float o[4];
  printf("1. flat, label up, X to the front (the default)\n");
  run(&c, 0, 0, 1, 0, 0, 0, o);       CHECK(near(o[0], 0) && near(o[1], 0) && near(o[2], 0) && near(o[3], 0), "at rest: all zero (gravity taken out even before a zero)");
  run(&c, 0.5f, 0, 1, 0, 0, 0, o);    CHECK(near(o[1], 0.5f), "speeding up: +0.5 G longitudinal");
  run(&c, -0.9f, 0, 1, 0, 0, 0, o);   CHECK(near(o[1], -0.9f), "braking: -0.9 G");
  run(&c, 0, 0.8f, 1, 0, 0, 0, o);    CHECK(near(o[0], 0.8f), "turning left: +0.8 G lateral");
  run(&c, 0, 0, 1, 0, 0, 30, o);      CHECK(near(o[3], 30), "turning left: +30 deg/s yaw (counter-clockwise from above)");
  run(&c, 0, 0, 1.4f, 0, 0, 0, o);    CHECK(near(o[2], 0.4f), "a bump: +0.4 G vertical");
  printf("2. other mountings\n");
  c.fwd = -1; c.left = 2;   // upside down, X pointing back
  run(&c, 0, 0, -1, 0, 0, 0, o);      CHECK(near(o[2], 0), "upside down at rest: vertical 0 (up is the chip's -Z)");
  run(&c, -0.5f, 0, -1, 0, 0, 0, o);  CHECK(near(o[1], 0.5f), "upside down, X back: speeding up still +0.5");
  run(&c, 0, 0, -1, 0, 0, -30, o);    CHECK(near(o[3], 30), "upside down: a left turn is still + yaw");
  c.fwd = 1; c.left = 3;    // on its edge: X front, Z left, so up is -Y
  run(&c, 0, -1, 0, 0, 0, 0, o);      CHECK(near(o[2], 0), "on its edge at rest: vertical 0");
  run(&c, 0, -1, 0.7f, 0, 0, 0, o);   CHECK(near(o[0], 0.7f), "on its edge: a left turn on the chip's Z");
  CHECK(imu_axes_valid(1, 2) && imu_axes_valid(-3, 1) && !imu_axes_valid(1, -1) && !imu_axes_valid(2, 2) && !imu_axes_valid(0, 1) && !imu_axes_valid(4, 1), "forward and left must be different axes");
  printf("3. the zero\n");
  imu_defaults(&c);
  float a[3] = { 0.05f, -0.03f, 0.998f }, g[3] = { 0.2f, -0.1f, 0.7f }, raw[4];   // mounted slightly tilted; gyro drift
  imu_car_frame(&c, a, g, raw);
  for (int i = 0; i < 4; i++) c.zero[i] = raw[i];
  c.zeroed = 1;
  run(&c, 0.05f, -0.03f, 0.998f, 0.2f, -0.1f, 0.7f, o); CHECK(near(o[0], 0) && near(o[1], 0) && near(o[2], 0) && near(o[3], 0), "after zeroing at rest: the tilt and the drift read 0");
  run(&c, 0.35f, -0.03f, 0.998f, 0.2f, -0.1f, 0.7f, o); CHECK(near(o[1], 0.3f), "and +0.3 G accelerating reads 0.3");
  printf("4. smoothing\n");
  float f = 0; for (int i = 0; i < 50; i++) f = imu_smooth(f, 1.0f, 0.17f);
  CHECK(f > 0.99f, "settles to the new value within a second at 50 Hz");
  f = 0; f = imu_smooth(f, 1.0f, 0.17f); CHECK(near(f, 0.17f), "one step moves 17 %");
  CHECK(sizeof(ImuConfig) == 20, "saved setting: 20 bytes");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
