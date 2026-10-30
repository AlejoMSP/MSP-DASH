// GPS date/time (esp32s3-amoled-bsp-base/gps_time.h) and the hub's RMC reading of it.
// Build: g++ -std=c++17 -I../../esp32s3-amoled-bsp-base -I../../esp32-input test_gps_time.cpp
#include "gps_time.h"
#include "speed_sensors.h"
#include <string.h>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static void withSum(char *out, const char *body) { uint8_t x = 0; for (const char *p = body; *p; p++) x ^= (uint8_t)*p; sprintf(out, "$%s*%02X\r\n", body, x); }
int main() {
  char t[32];
  printf("1. dates\n");
  CHECK(gt_epoch(1970, 1, 1, 0, 0, 0) == 0 && gt_epoch(2026, 10, 26, 18, 32, 5) == 1793039525u, "epoch seconds (checked against date -u)");
  { int y, m, d; gt_civil_from_days(gt_days_from_civil(2028, 2, 29), &y, &m, &d); CHECK(y == 2028 && m == 2 && d == 29, "leap day round trip"); }
  CHECK(gt_weekday(gt_days_from_civil(2026, 10, 26)) == 1, "2026-10-26 is a Monday");
  printf("2. US daylight saving, Eastern\n");
  CHECK(!gt_us_dst(gt_epoch(2026, 3, 8, 6, 59, 59), -300) && gt_us_dst(gt_epoch(2026, 3, 8, 7, 0, 0), -300), "starts 2026-03-08 2:00 EST (07:00 UTC)");
  CHECK(gt_us_dst(gt_epoch(2026, 11, 1, 5, 59, 59), -300) && !gt_us_dst(gt_epoch(2026, 11, 1, 6, 0, 0), -300), "ends 2026-11-01 2:00 EDT (06:00 UTC)");
  CHECK(!gt_us_dst(gt_epoch(2026, 1, 15, 12, 0, 0), -300) && gt_us_dst(gt_epoch(2026, 7, 4, 12, 0, 0), -300), "January standard, July daylight");
  CHECK(gt_us_dst(gt_epoch(2027, 3, 14, 10, 0, 0), -480) && !gt_us_dst(gt_epoch(2027, 3, 14, 9, 59, 0), -480), "Pacific 2027: 2:00 PST is 10:00 UTC");
  printf("3. text\n");
  uint32_t u = gt_epoch(2026, 10, 26, 18, 32, 5);
  gt_format(u, -240, 0, t, sizeof t); CHECK(!strcmp(t, "2026-10-26 14:32:05"), "log row time, local (EDT)");
  gt_format(u, -240, 1, t, sizeof t); CHECK(!strcmp(t, "2026-10-26_14-32-05"), "file name: no colons or spaces");
  gt_format(gt_epoch(2026, 1, 1, 3, 0, 0), -300, 0, t, sizeof t); CHECK(!strcmp(t, "2025-12-31 22:00:00"), "local time goes back across New Year");
  printf("4. RMC fields\n");
  CHECK(gt_from_rmc("183205.00", "261026") == u, "183205.00 / 261026 -> 2026-10-26 18:32:05 UTC");
  CHECK(gt_from_rmc("", "261026") == 0 && gt_from_rmc("183205", "") == 0, "an empty field: unknown");
  CHECK(gt_from_rmc("183205", "060180") == 0, "1980 (a module with no time yet): not trusted");
  CHECK(gt_from_rmc("253205", "261026") == 0 && gt_from_rmc("183205", "261326") == 0, "hour 25 or month 13: refused");
  printf("5. the hub's GPS reader keeps the time\n");
  GpsParser g; gps_init(&g); char s[120];
  withSum(s, "GPRMC,183205.00,V,,,,,,,261026,,,N"); for (char *p = s; *p; p++) gps_feed(&g, *p);
  CHECK(g.utc == u && !g.fix, "the time is taken even before the position is (status V)");
  withSum(s, "GPRMC,183206.00,A,4807.038,N,01131.000,E,010.0,084.4,261026,003.1,W"); for (char *p = s; *p; p++) gps_feed(&g, *p);
  CHECK(g.utc == u + 1 && g.fix, "and updated with each sentence");
  withSum(s, "GPRMC,,V,,,,,,,,,,N"); for (char *p = s; *p; p++) gps_feed(&g, *p);
  CHECK(g.utc == u + 1, "a sentence with no time keeps the last good one");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
