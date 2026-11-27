// The air-core tach and speedo (aircore/): needle angle, slew, coil drive, the DRV8833 inputs, settings, and that the
// sketch's copy of the hub's packet layout matches the real one.
// Build: g++ -std=c++17 -I../../aircore -I../../esp32s3-amoled-bsp-base test_aircore.cpp
#include "aircore.h"
namespace ac {
#include "aircore_link.h"
}
#include "link_packets.h"
#include <stdio.h>
#include <stddef.h>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static bool near(float a, float b, float t) { return fabsf(a - b) <= t; }
int main() {
  printf("1. angle\n");
  AirCoreCfg t; aircore_defaults_tach(&t);
  CHECK(aircore_valid(&t) && near(aircore_angle(&t, 0), 0, 0.01f) && near(aircore_angle(&t, 4000), 120, 0.01f) && near(aircore_angle(&t, 8000), 240, 0.01f), "tach: 0, 4000, 8000 RPM = 0, 120, 240 deg");
  CHECK(near(aircore_angle(&t, 9500), 240, 0.01f) && near(aircore_angle(&t, -50), 0, 0.01f), "held inside the scale");
  t.zeroDeg = 15; t.spanDeg = -250; CHECK(near(aircore_angle(&t, 8000), -235, 0.01f), "zero offset and a scale that turns the other way");
  AirCoreCfg s; aircore_defaults_speedo(&s); CHECK(near(aircore_angle(&s, 75), 120, 0.01f), "speedo: 75 of 150 mph = half way");
  printf("2. slew\n");
  CHECK(near(aircore_slew(0, 100, 720, 0.005f), 3.6f, 0.001f), "720 deg/s for 5 ms: 3.6 deg");
  CHECK(near(aircore_slew(99, 100, 720, 0.005f), 100, 0.001f) && near(aircore_slew(100, 0, 720, 0.005f), 96.4f, 0.001f), "lands on the target; going down too");
  printf("3. coils\n");
  float a, b;
  aircore_coils(0, 1, &a, &b); CHECK(near(a, 0, 1e-5f) && near(b, 1, 1e-5f), "0 deg: cosine coil only");
  aircore_coils(90, 1, &a, &b); CHECK(near(a, 1, 1e-5f) && near(b, 0, 1e-5f), "90 deg: sine coil only");
  aircore_coils(180, 0.5f, &a, &b); CHECK(near(a, 0, 1e-5f) && near(b, -0.5f, 1e-5f), "180 deg at half strength: cosine reversed, half");
  aircore_coils(225, 1, &a, &b); CHECK(near(a, -0.7071f, 1e-3f) && near(b, -0.7071f, 1e-3f), "225 deg: both reversed");
  printf("4. DRV8833 inputs\n");
  uint32_t x, y;
  aircore_bridge(0.5f, 1023, &x, &y); CHECK(x == 512 && y == 0, "forward half: PWM on IN-A, IN-B low");
  aircore_bridge(-1.0f, 1023, &x, &y); CHECK(x == 0 && y == 1023, "full reverse: PWM on IN-B");
  aircore_bridge(1.7f, 1023, &x, &y); CHECK(x == 1023 && y == 0, "past full: held at full");
  aircore_bridge_slow(0.5f, 1023, &x, &y); CHECK(x == 1023 && y == 511, "slow decay, forward half: IN-A held high, IN-B low for half the time");
  aircore_bridge_slow(-0.25f, 1023, &x, &y); CHECK(x == 767 && y == 1023, "slow decay, reverse quarter: IN-B high, IN-A low a quarter of the time");
  aircore_bridge_slow(0, 1023, &x, &y); CHECK(x == 1023 && y == 1023, "slow decay, no drive: both high (brake, no current)");
  printf("5. settings\n");
  AirCoreCfg c; aircore_defaults_tach(&c); c.maxVal = 0; CHECK(!aircore_valid(&c), "max not above min: refused");
  aircore_defaults_tach(&c); c.strength = 1.5f; CHECK(!aircore_valid(&c), "strength over 1: refused");
  aircore_defaults_tach(&c); c.spanDeg = 0; CHECK(!aircore_valid(&c), "no span: refused");
  CHECK(sizeof(AirCoreCfg) == 24, "saved settings: 24 bytes");
  printf("6. multi-point calibration\n");
  { AirCoreCfg t2; aircore_defaults_tach(&t2); AirCorePoints p; aircore_points_clear(&p);
    aircore_points_set(&p, 0, 180);
    CHECK(near(aircore_angle_pts(&t2, &p, 4000), aircore_angle(&t2, 4000), 1e-4f), "one mark: zero / span still used");
    aircore_points_set(&p, 4000, 80); aircore_points_set(&p, 2000, 118); aircore_points_set(&p, 8000, -20);
    CHECK(p.n == 4 && p.v[0] == 0 && p.v[1] == 2000 && p.v[2] == 4000 && p.v[3] == 8000, "kept in order of value");
    CHECK(near(aircore_angle_pts(&t2, &p, 2000), 118, 1e-4f) && near(aircore_angle_pts(&t2, &p, 1000), 149, 1e-4f), "on a mark, and halfway between two");
    CHECK(near(aircore_angle_pts(&t2, &p, 6000), 30, 1e-4f), "between 4000 (80) and 8000 (-20): 30");
    CHECK(near(aircore_angle_pts(&t2, &p, -100), 180, 1e-4f) && near(aircore_angle_pts(&t2, &p, 9500), -20, 1e-4f), "held at the end marks");
    aircore_points_set(&p, 2000, 115); CHECK(p.n == 4 && near(p.a[1], 115, 1e-4f), "the same value again: moved, not added");
    CHECK(aircore_points_remove(&p, 4000) && p.n == 3 && p.v[2] == 8000 && !aircore_points_remove(&p, 4000), "removed; removing it again: no");
    CHECK(aircore_points_valid(&p), "valid");
    AirCorePoints bad = p; bad.v[1] = 9000; CHECK(!aircore_points_valid(&bad), "out of order: not valid (cleared on load)");
    AirCorePoints full; aircore_points_clear(&full); for (int i = 0; i < AIRCORE_POINTS; i++) aircore_points_set(&full, i * 100, 180 - i);
    CHECK(!aircore_points_set(&full, 5000, 0), "12 marks: no more"); }
  printf("7. smoothing\n");
  CHECK(aircore_lowpass(1000, 2000, 0.05f, 0) == 2000, "off: straight to the value");
  { float v = 1000; for (int i = 0; i < 24; i++) v = aircore_lowpass(v, 2000, 0.005f, 0.12f); CHECK(near(v, 1632, 5), "after one tau (120 ms): 63 % of the way"); }
  { float v = 3000; float lo = 1e9, hi = -1e9; for (int i = 0; i < 400; i++) { v = aircore_lowpass(v, (i / 10) % 2 ? 3050 : 2950, 0.005f, 0.12f); if (i > 200) { lo = fminf(lo, v); hi = fmaxf(hi, v); } }
    CHECK(hi - lo < 40, "a +-50 RPM wobble every 50 ms: under 40 RPM left on the needle"); }
  CHECK(aircore_smooth_default(1) == 120 && aircore_smooth_default(0) == 300, "defaults: tach 120 ms, speedo 300 ms");
  printf("8. the hub's packet\n");
  CHECK(LINK_PKT_MAGIC == PKT_MAGIC && LINK_PKT_V_OFFSET == offsetof(LinkPacket, v), "magic and where the values start match link_packets.h");
  CHECK(LINK_SIG_RPM == SIG_RPM && LINK_SIG_VSS == SIG_VSS && AC_MASK_SIMULATED == LINK_MASK_SIMULATED, "RPM, speed and the simulator flag match");
  CHECK(sizeof(ac::AcStatus) == sizeof(AcStatus) && sizeof(ac::AcCmd) == sizeof(AcCmd) && sizeof(AcStatus) <= 250, "status and command packets: same size on both sides, one ESP-NOW frame");
  CHECK(offsetof(ac::AcStatus, pwmHz) == offsetof(AcStatus, pwmHz) && offsetof(ac::AcStatus, pv) == offsetof(AcStatus, pv) && offsetof(ac::AcCmd, a) == offsetof(AcCmd, a), "same layout");
  CHECK(ac::AC_OP_SPAN == AC_OP_SPAN && ac::AC_MODE_ANGLE == AC_MODE_ANGLE && AC_POINTS == AIRCORE_POINTS, "same command numbers; as many marks as the board keeps");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
