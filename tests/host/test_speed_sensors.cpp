// Tests esp32-input/speed_sensors.h: the hall sensor speed math and calibration, and the GPS NMEA reader.
// Build: g++ -std=c++17 -I../../esp32-input test_speed_sensors.cpp -o t
#include "speed_sensors.h"
#include <stdio.h>
#include <math.h>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static bool near(float a, float b, float tol) { return fabsf(a - b) <= tol; }
static int feedAll(GpsParser *g, const char *s) { int n = 0; for (const char *p = s; *p; p++) if (gps_feed(g, *p)) n++; return n; }
static void withSum(char *out, const char *body) {   // "$" body "*" checksum CRLF
  uint8_t x = 0; for (const char *p = body; *p; p++) x ^= (uint8_t)*p;
  sprintf(out, "$%s*%02X\r\n", body, x);
}

int main() {
  printf("1. hall sensor speed\n");
  // 4000 pulses per mile at 60 mph = 66.67 pulses a second = a pulse every 15000 us
  CHECK(near(hall_mph(15000, 1000, 4000), 60.0f, 0.01f), "4000 pulses per mile, a pulse every 15 ms = 60 mph");
  CHECK(near(hall_mph(15000, 1000, 2000), 120.0f, 0.02f), "half the pulses per mile = twice the speed");
  CHECK(hall_mph(15000, 1600000, 4000) == 0.0f, "no pulse for 1.6 s: stopped");
  CHECK(hall_mph(999, 0, 4000) == 0.0f, "pulses under 1 ms apart are noise, not speed");
  CHECK(hall_mph(0, 0, 4000) == 0.0f && hall_mph(15000, 0, 0) == 0.0f, "no period yet / no calibration: 0, never a crash");

  printf("2. calibration from the GPS\n");
  CHECK(near(hall_ppm_from_gps(66.6667f, 60.0f), 4000.0f, 0.5f), "66.67 Hz at 60 mph GPS = 4000 pulses per mile");
  CHECK(near(hall_mph(15000, 0, hall_ppm_from_gps(66.6667f, 60.0f)), 60.0f, 0.01f), "after calibrating, the hall speed matches the GPS");
  CHECK(hall_ppm_from_gps(10.0f, 8.0f) == 0.0f, "refused below 10 mph (GPS speed too rough)");
  CHECK(hall_ppm_from_gps(0.0f, 60.0f) == 0.0f, "refused with no pulses (sensor not seeing the magnet)");
  CHECK(hall_ppm_from_gps(0.1f, 60.0f) == 0.0f, "refused when the answer is silly (6 pulses per mile)");

  printf("3. GPS sentences\n");
  GpsParser g; gps_init(&g);
  char s[120];
  withSum(s, "GPRMC,123519,A,4807.038,N,01131.000,E,052.1,084.4,230394,003.1,W");
  CHECK(feedAll(&g, s) == 1 && g.fix && near(g.mph, 52.1f * 1.150779f, 0.01f) && g.newRmc, "RMC with a fix: 52.1 knots = 59.95 mph");
  withSum(s, "GNRMC,123520,A,4807.038,N,01131.000,E,000.0,084.4,230394,,,A");
  feedAll(&g, s); CHECK(g.fix && g.mph == 0.0f, "GNRMC (GPS + GLONASS name) works too, standing still = 0");
  withSum(s, "GPRMC,123521,V,,,,,,,230394,,,N");
  feedAll(&g, s); CHECK(!g.fix && g.mph == 0.0f, "status V (no fix): no fix, speed 0");
  withSum(s, "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  CHECK(feedAll(&g, s) == 1 && g.sats == 8 && g.newGga, "GGA: 8 satellites");
  uint32_t bad = g.badCount;
  CHECK(feedAll(&g, "$GPRMC,123519,A,4807.038,N,01131.000,E,099.9,084.4,230394,003.1,W*00\r\n") == 0 && g.badCount == bad + 1 && !g.fix, "a wrong checksum is thrown away (the old values stay)");
  CHECK(feedAll(&g, "garbage,,,\r\n$GPGSV,1,1,00*79\r\n") == 0, "other sentences and junk are ignored");
  withSum(s, "GPRMC,123519,A,4807.038,N,01131.000,E,010.0,084.4,230394,003.1,W");
  for (int i = 0; s[i]; i++) { gps_feed(&g, s[i]); }
  CHECK(g.fix && near(g.mph, 11.508f, 0.01f), "byte by byte, as the serial port delivers it");
  { GpsParser h; gps_init(&h); for (int i = 0; i < 300; i++) gps_feed(&h, i == 0 ? '$' : 'A');
    withSum(s, "GPGGA,1,2,N,3,E,1,05,1,1,M,1,M,,"); feedAll(&h, s);
    CHECK(h.sats == 5, "a run-on sentence longer than the buffer is dropped and the next one still reads"); }

  printf("4. the 5 Hz command for the GT-U7\n");
  { uint8_t a = 0, b = 0; for (size_t i = 2; i < sizeof(GPS_CFG_RATE_5HZ) - 2; i++) { a += GPS_CFG_RATE_5HZ[i]; b += a; }
    CHECK(GPS_CFG_RATE_5HZ[sizeof(GPS_CFG_RATE_5HZ) - 2] == a && GPS_CFG_RATE_5HZ[sizeof(GPS_CFG_RATE_5HZ) - 1] == b, "UBX checksum is right");
    CHECK(GPS_CFG_RATE_5HZ[6] == 0xC8 && GPS_CFG_RATE_5HZ[7] == 0x00, "200 ms between fixes"); }

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
