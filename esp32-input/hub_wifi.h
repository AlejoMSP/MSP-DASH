// hub_wifi.h - the hub's own Wi-Fi network (hub 57 on), so the phone and laptop reach its pages in the car.
// Mode: when the home network is not found (default), always, or off. Set on the Setup page, used from the next start.
// The network sits on the ESP-NOW channel the screens use (CAR_CHANNEL away from home, the home network's channel at
// home), so the screens keep getting data. Pure C: host-tested (tests/host/test_hub_wifi.cpp).
#pragma once
#include <stdint.h>
#include <string.h>

enum { AP_WHEN_AWAY = 0, AP_ALWAYS = 1, AP_OFF = 2, AP_MODES };
static const char *const AP_MODE_NAMES[AP_MODES] = { "When the home network is not found", "Always", "Off" };
typedef struct __attribute__((packed)) { uint8_t version, mode; char ssid[33]; char pass[65]; } ApCfg;
#define AP_VERSION 1
#define AP_SSID_DEFAULT "MSP-DASH"
#define AP_PASS_DEFAULT "mspdash123"   // change it on the Setup page
static inline void ap_defaults(ApCfg *c) {
  memset(c, 0, sizeof *c); c->version = AP_VERSION; c->mode = AP_WHEN_AWAY;
  strcpy(c->ssid, AP_SSID_DEFAULT); strcpy(c->pass, AP_PASS_DEFAULT);
}
// A name: 1 to 32 printable characters. A password: 8 to 63 printable characters (WPA2).
static inline int ap_text_ok(const char *s, int lo, int hi) {
  int n = 0;
  for (; s[n]; n++) if ((unsigned char)s[n] < 32 || (unsigned char)s[n] > 126) return 0;
  return n >= lo && n <= hi;
}
static inline int ap_valid(const ApCfg *c) {
  return c->version == AP_VERSION && c->mode < AP_MODES && ap_text_ok(c->ssid, 1, 32) && ap_text_ok(c->pass, 8, 63);
}
// Start it? homeJoined: the hub is on the home network.
static inline int ap_should_start(const ApCfg *c, int homeJoined) {
  return c->mode == AP_ALWAYS || (c->mode == AP_WHEN_AWAY && !homeJoined);
}
