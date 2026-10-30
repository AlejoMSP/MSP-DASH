// gps_time.h - date and time from the hub's GPS, shared by the hub (sends it) and the screens (log file names and
// timestamps). Pure C: host-tested (tests/host/test_gps_time.cpp). The hub copies this file.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// "MST1": the hub sends UTC seconds since 1970 and the local offset every 10 s while the GPS knows the time.
#define TIME_MAGIC 0x4D535431u
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint32_t utc;          // seconds since 1970-01-01 00:00 UTC
  int16_t offset_min;    // local time = utc + offset (already includes daylight saving)
} TimePacket;

// Days since 1970-01-01 for a date (proleptic Gregorian). y >= 1970.
static inline int32_t gt_days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  int32_t yoe = y - era * 400;
  int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
static inline void gt_civil_from_days(int32_t z, int *y, int *m, int *d) {
  z += 719468;
  int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  int32_t doe = z - era * 146097;
  int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t yy = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1; *m = mp < 10 ? mp + 3 : mp - 9; *y = yy + (*m <= 2);
}
static inline uint32_t gt_epoch(int y, int mo, int d, int h, int mi, int s) {
  return (uint32_t)gt_days_from_civil(y, mo, d) * 86400u + (uint32_t)(h * 3600 + mi * 60 + s);
}
// Day of week, 0 = Sunday.
static inline int gt_weekday(int32_t days) { return (int)((days % 7 + 11) % 7); }

// US daylight saving: from the second Sunday of March 2:00 local standard time to the first Sunday of November
// 2:00 local daylight time. stdOffsetMin: the zone's standard offset (Eastern = -300).
static inline bool gt_us_dst(uint32_t utc, int stdOffsetMin) {
  int y, m, d; gt_civil_from_days((int32_t)((utc + stdOffsetMin * 60) / 86400), &y, &m, &d);
  int32_t mar1 = gt_days_from_civil(y, 3, 1), nov1 = gt_days_from_civil(y, 11, 1);
  int32_t sun2Mar = mar1 + (7 - gt_weekday(mar1)) % 7 + 7, sun1Nov = nov1 + (7 - gt_weekday(nov1)) % 7;
  uint32_t start = (uint32_t)sun2Mar * 86400u + 2 * 3600 - stdOffsetMin * 60;            // 2:00 standard, in UTC
  uint32_t end = (uint32_t)sun1Nov * 86400u + 2 * 3600 - (stdOffsetMin + 60) * 60;       // 2:00 daylight, in UTC
  return utc >= start && utc < end;
}

// Local time as text. fmt 0: "2026-10-26 14:32:05" (log rows), 1: "2026-10-26_14-32-05" (file names).
static inline void gt_format(uint32_t utc, int offsetMin, int fmt, char *out, int len) {
  int64_t t = (int64_t)utc + (int64_t)offsetMin * 60;
  int32_t days = (int32_t)(t / 86400); int32_t sec = (int32_t)(t % 86400);
  int y, m, d; gt_civil_from_days(days, &y, &m, &d);
  if (fmt == 1) snprintf(out, len, "%04d-%02d-%02d_%02d-%02d-%02d", y, m, d, sec / 3600, sec / 60 % 60, sec % 60);
  else snprintf(out, len, "%04d-%02d-%02d %02d:%02d:%02d", y, m, d, sec / 3600, sec / 60 % 60, sec % 60);
}

// GPS RMC fields "hhmmss.ss" and "ddmmyy" -> UTC seconds. 0 when either is empty or makes no sense.
static inline uint32_t gt_from_rmc(const char *hms, const char *dmy) {
  int h, mi, s, d, mo, yy;
  if (!hms || !dmy || sscanf(hms, "%2d%2d%2d", &h, &mi, &s) != 3 || sscanf(dmy, "%2d%2d%2d", &d, &mo, &yy) != 3) return 0;
  if (h > 23 || mi > 59 || s > 60 || d < 1 || d > 31 || mo < 1 || mo > 12) return 0;
  int y = 2000 + yy;
  if (y < 2024 || y > 2079) return 0;   // a module with no time yet can report 1980 (read as 2080): not trusted
  return gt_epoch(y, mo, d, h, mi, s > 59 ? 59 : s);
}
