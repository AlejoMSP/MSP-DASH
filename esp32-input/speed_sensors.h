// speed_sensors.h (hub) - the pure math and parsing behind the speed sensors, kept free of Arduino calls so the
// host tests can check it (tests/host/test_speed_sensors.cpp).
//   - Hall sensor (NJK-5002C on GPIO 27): pulses -> mph, using pulses per mile; calibration against the GPS.
//   - GPS (GT-U7, NEO-6M compatible, NMEA at 9600 baud on GPIO 16/17): reads RMC (speed, fix) and GGA (satellites).
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// ---------------- hall sensor ----------------
#define HALL_PPM_DEFAULT   4000.0f   // pulses per mile until calibrated (about one magnet on the driveshaft)
#define HALL_PPM_MIN       100.0f
#define HALL_PPM_MAX       100000.0f
#define HALL_MIN_PERIOD_US 1000u     // pulses closer than 1 ms are noise (that would be over 1000 pulses a second)
#define HALL_STOP_US       1500000u  // no pulse for 1.5 s = stopped
#define KNOTS_TO_MPH       1.150779f
#define MPH_TO_KPH         1.609344f

// Speed from the time between the last two pulses. 0 when stopped (no pulse for HALL_STOP_US) or no period yet.
static inline float hall_mph(uint32_t periodUs, uint32_t usSinceLastPulse, float ppm) {
  if (periodUs < HALL_MIN_PERIOD_US || usSinceLastPulse > HALL_STOP_US || ppm <= 0) return 0.0f;
  // pulses per second / pulses per mile = miles per second; x 3600 = mph
  return (1000000.0f / (float)periodUs) / ppm * 3600.0f;
}

// Calibration: at a steady speed, the GPS says gpsMph while the hall sensor gives pulseHz. Returns the pulses per
// mile that makes the two agree, or 0 if the numbers are not good enough to calibrate from (too slow, out of range).
static inline float hall_ppm_from_gps(float pulseHz, float gpsMph) {
  if (gpsMph < 10.0f || pulseHz <= 0.0f) return 0.0f;     // below 10 mph the GPS speed is too rough
  float ppm = pulseHz * 3600.0f / gpsMph;
  return (ppm >= HALL_PPM_MIN && ppm <= HALL_PPM_MAX) ? ppm : 0.0f;
}

// ---------------- GPS (NMEA 0183) ----------------
typedef struct {
  char line[100];     // the sentence being received
  int len;
  bool fix;           // RMC status A (valid)
  float mph;          // RMC speed over ground
  int sats;           // GGA satellites in use
  uint32_t rmcCount;  // good RMC sentences
  uint32_t ggaCount;  // good GGA sentences
  uint32_t badCount;  // sentences with a wrong checksum
  bool newRmc;        // set when an RMC arrives; the caller clears it
  bool newGga;
} GpsParser;

static inline void gps_init(GpsParser *g) { memset(g, 0, sizeof *g); }

static inline int gps_hex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

// Field n (0 = the sentence name) of a comma-separated sentence, copied into out. Empty string if missing.
static inline void gps_field(const char *s, int n, char *out, int outLen) {
  int f = 0; const char *p = s;
  while (*p && f < n) { if (*p == ',') f++; p++; }
  int k = 0;
  if (f == n) while (*p && *p != ',' && *p != '*' && k < outLen - 1) out[k++] = *p++;
  out[k] = 0;
}

// One whole sentence, from '$' to before CR/LF. Returns true if it was a good RMC or GGA.
static inline bool gps_sentence(GpsParser *g, const char *s) {
  if (s[0] != '$') return false;
  const char *star = strchr(s, '*');
  if (!star || gps_hex(star[1]) < 0 || gps_hex(star[2]) < 0) { g->badCount++; return false; }
  uint8_t sum = 0;
  for (const char *p = s + 1; p < star; p++) sum ^= (uint8_t)*p;
  if (sum != (uint8_t)(gps_hex(star[1]) * 16 + gps_hex(star[2]))) { g->badCount++; return false; }
  char name[8], f[16];
  gps_field(s, 0, name, sizeof name);
  size_t nl = strlen(name);
  if (nl < 5) return false;
  const char *type = name + nl - 3;            // GPRMC, GNRMC, ... -> RMC
  if (!strcmp(type, "RMC")) {
    gps_field(s, 2, f, sizeof f); g->fix = (f[0] == 'A');
    gps_field(s, 7, f, sizeof f); g->mph = f[0] ? (float)atof(f) * KNOTS_TO_MPH : 0.0f;
    if (!g->fix) g->mph = 0.0f;
    g->rmcCount++; g->newRmc = true;
    return true;
  }
  if (!strcmp(type, "GGA")) {
    gps_field(s, 7, f, sizeof f); g->sats = f[0] ? atoi(f) : 0;
    g->ggaCount++; g->newGga = true;
    return true;
  }
  return false;
}

// Feed received bytes one at a time. Returns true when a good RMC or GGA has just been read.
static inline bool gps_feed(GpsParser *g, char c) {
  if (c == '$') { g->len = 0; g->line[g->len++] = c; return false; }
  if (g->len == 0) return false;                       // waiting for the start of a sentence
  if (c == '\r' || c == '\n') { g->line[g->len] = 0; g->len = 0; return gps_sentence(g, g->line); }
  if (g->len >= (int)sizeof(g->line) - 1) { g->len = 0; g->badCount++; return false; }   // too long: drop it
  g->line[g->len++] = c;
  return false;
}

// u-blox command: send a position 5 times a second instead of once (CFG-RATE, 200 ms). The GT-U7 understands it;
// a module that does not just keeps sending once a second.
static const uint8_t GPS_CFG_RATE_5HZ[] = { 0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 0xC8, 0x00, 0x01, 0x00, 0x01, 0x00, 0xDE, 0x6A };
