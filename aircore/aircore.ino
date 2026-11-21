// aircore.ino - drives the stock NB Miata tach and speedo (air-core gauges) from the MSP-DASH hub's data.
// Board: Lolin / Wemos ESP32-S2 Mini (fqbn esp32:esp32:lolin_s2_mini). Two DRV8833 boards, one per gauge.
// It only listens: it picks up the hub's ESP-NOW data packet (RPM and speed) on whatever channel the hub is on, and
// needs no Wi-Fi network. Setup and calibration over the USB serial port (115200): type "help". See README.md.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include "aircore.h"
#include "aircore_link.h"

#define AIRCORE_BUILD 1
#define PWM_HZ 20000          // above hearing, so the coils do not whine
#define PWM_BITS 10
#define PWM_FULL ((1u << PWM_BITS) - 1)
#define STEP_MS 5             // needle update
#define LOST_MS 1500          // no data for this long: needles go to the start of the scale, and the hunt for the hub starts
#define HOP_MS 300            // channel hunt: time on each channel
#define HOLD_MS 60000         // "angle" holds the needle this long (calibration)

typedef struct {
  const char *name;
  uint8_t pins[4];            // DRV8833 IN1, IN2 (sine coil) and IN3, IN4 (cosine coil)
  int sig;
  AirCoreCfg cfg;
  float needle;               // where it is now, degrees
  float holdDeg; uint32_t holdUntil;
} Gauge;
static Gauge G[2] = {
  { "tach",   { 3, 5, 7, 9 },    LINK_SIG_RPM, {}, 0, 0, 0 },
  { "speedo", { 11, 12, 16, 18 }, LINK_SIG_VSS, {}, 0, 0, 0 },
};

static volatile float s_val[2] = { 0, 0 };
static volatile bool s_has[2] = { false, false }, s_sim = false;
static volatile uint32_t s_lastRx = 0, s_rx = 0;
static uint8_t s_ch = 1;
static uint32_t s_hopMs = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void cfgSave(int g) { Preferences p; p.begin("aircore", false); p.putBytes(G[g].name, &G[g].cfg, sizeof(AirCoreCfg)); p.putUChar("ver", AIRCORE_VERSION); p.end(); }
static void cfgLoad() {
  Preferences p; p.begin("aircore", true);
  bool ok = p.getUChar("ver", 0) == AIRCORE_VERSION;
  for (int g = 0; g < 2; g++) {
    if (!ok || p.getBytes(G[g].name, &G[g].cfg, sizeof(AirCoreCfg)) != sizeof(AirCoreCfg) || !aircore_valid(&G[g].cfg)) {
      if (g == 0) aircore_defaults_tach(&G[g].cfg); else aircore_defaults_speedo(&G[g].cfg);
    }
  }
  p.end();
}

// The hub's data packet: RPM and speed out of it, if it has them.
static void onRecv(const esp_now_recv_info_t *info, const uint8_t *d, int len) {
  if (len < LINK_PKT_V_OFFSET + 4) return;
  uint32_t magic; memcpy(&magic, d, 4);
  if (magic != LINK_PKT_MAGIC) return;
  uint64_t has; memcpy(&has, d + 8, 8);
  portENTER_CRITICAL(&s_mux);
  for (int g = 0; g < 2; g++) {
    int at = LINK_PKT_V_OFFSET + 4 * G[g].sig;
    if (at + 4 <= len && (has >> G[g].sig) & 1) { float v; memcpy(&v, d + at, 4); s_val[g] = v; s_has[g] = true; }
    else s_has[g] = false;
  }
  s_sim = has & AC_MASK_SIMULATED;
  s_lastRx = millis(); s_rx++;
  portEXIT_CRITICAL(&s_mux);
}

static void setChannel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

static void drive(int g, float deg) {
  float s, c; aircore_coils(deg, G[g].cfg.strength, &s, &c);
  uint32_t a, b;
  aircore_bridge(s, PWM_FULL, &a, &b); ledcWrite(G[g].pins[0], a); ledcWrite(G[g].pins[1], b);
  aircore_bridge(c, PWM_FULL, &a, &b); ledcWrite(G[g].pins[2], a); ledcWrite(G[g].pins[3], b);
}

// Start-up sweep: both needles to the end of the scale and back, like the stock cluster.
static void sweep() {
  for (int pass = 0; pass < 2; pass++)
    for (int k = 0; k <= 100; k++) {
      float f = pass ? 1.0f - k / 100.0f : k / 100.0f;
      for (int g = 0; g < 2; g++) { G[g].needle = G[g].cfg.zeroDeg + f * G[g].cfg.spanDeg; drive(g, G[g].needle); }
      delay(7);
    }
}

static void show() {
  Serial.printf("aircore build %d | channel %d | packets %lu | %s\n", AIRCORE_BUILD, s_ch, (unsigned long)s_rx,
                millis() - s_lastRx < LOST_MS ? (s_sim ? "hub heard (SIMULATED data)" : "hub heard") : "no hub: hunting channels");
  for (int g = 0; g < 2; g++) {
    const AirCoreCfg &c = G[g].cfg;
    Serial.printf("  %-6s value %s | needle %.1f deg | min %g max %g zero %g span %g strength %g slew %g\n", G[g].name,
                  s_has[g] ? String((float)s_val[g], 0).c_str() : "--", G[g].needle, c.minVal, c.maxVal, c.zeroDeg, c.spanDeg, c.strength, c.slewDegS);
  }
}
static void help() {
  Serial.println("Commands:");
  Serial.println("  show                          values, settings, channel");
  Serial.println("  tach angle 30                 hold the tach needle at 30 deg for a minute (find zero and span)");
  Serial.println("  tach release                  back to live data");
  Serial.println("  tach zero 12 | span 245       coil angle at the start of the scale | degrees to the end");
  Serial.println("  tach min 0 | max 8000         values at the start and end of the dial");
  Serial.println("  tach strength 0.8 | slew 720  drive (0.1-1) | fastest needle speed, deg/s");
  Serial.println("  (same for speedo)   sweep     both needles end to end   defaults   factory settings");
}
static void command(String line) {
  line.trim(); line.toLowerCase();
  if (!line.length()) return;
  if (line == "help") { help(); return; }
  if (line == "show") { show(); return; }
  if (line == "sweep") { sweep(); return; }
  if (line == "defaults") { aircore_defaults_tach(&G[0].cfg); aircore_defaults_speedo(&G[1].cfg); cfgSave(0); cfgSave(1); Serial.println("Factory settings back"); return; }
  int sp1 = line.indexOf(' '), sp2 = line.indexOf(' ', sp1 + 1);
  String who = line.substring(0, sp1), what = sp1 < 0 ? "" : line.substring(sp1 + 1, sp2 < 0 ? line.length() : sp2);
  float v = sp2 < 0 ? NAN : line.substring(sp2 + 1).toFloat();
  int g = who == "tach" ? 0 : who == "speedo" ? 1 : -1;
  if (g < 0) { Serial.println("? type help"); return; }
  if (what == "release") { G[g].holdUntil = 0; Serial.println("Live data"); return; }
  if (isnan(v)) { Serial.println("? a number is missing"); return; }
  if (what == "angle") { G[g].holdDeg = v; G[g].holdUntil = millis() + HOLD_MS; Serial.printf("%s held at %.1f deg\n", G[g].name, v); return; }
  AirCoreCfg c = G[g].cfg;
  if (what == "zero") c.zeroDeg = v; else if (what == "span") c.spanDeg = v; else if (what == "min") c.minVal = v; else if (what == "max") c.maxVal = v;
  else if (what == "strength") c.strength = v; else if (what == "slew") c.slewDegS = v;
  else { Serial.println("? type help"); return; }
  if (!aircore_valid(&c)) { Serial.println("Out of range, not saved"); return; }
  G[g].cfg = c; cfgSave(g); Serial.printf("%s %s = %g, saved\n", G[g].name, what.c_str(), v);
}

void setup() {
  Serial.begin(115200);
  cfgLoad();
  for (int g = 0; g < 2; g++) for (int k = 0; k < 4; k++) ledcAttach(G[g].pins[k], PWM_HZ, PWM_BITS);   // 8 pins = the S2's 8 PWM channels
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  setChannel(s_ch);
  if (esp_now_init() == ESP_OK) esp_now_register_recv_cb(onRecv); else Serial.println("ESP-NOW start FAILED");
  sweep();
  Serial.printf("\naircore build %d ready. Type help.\n", AIRCORE_BUILD);
}

void loop() {
  static uint32_t last = 0; static String line;
  while (Serial.available()) { char ch = Serial.read(); if (ch == '\n' || ch == '\r') { command(line); line = ""; } else if (line.length() < 60) line += ch; }
  uint32_t now = millis();
  bool heard = s_lastRx && now - s_lastRx < LOST_MS;
  if (!heard && now - s_hopMs >= HOP_MS) { s_hopMs = now; s_ch = s_ch % 13 + 1; setChannel(s_ch); }   // hunt for the hub
  if (now - last < STEP_MS) return;
  float dt = (now - last) / 1000.0f; last = now;
  for (int g = 0; g < 2; g++) {
    float v; bool has;
    portENTER_CRITICAL(&s_mux); v = s_val[g]; has = s_has[g] && heard; portEXIT_CRITICAL(&s_mux);
    float target = G[g].holdUntil && now < G[g].holdUntil ? G[g].holdDeg : aircore_angle(&G[g].cfg, has ? v : G[g].cfg.minVal);
    G[g].needle = aircore_slew(G[g].needle, target, G[g].cfg.slewDegS, dt);
    drive(g, G[g].needle);
  }
}
