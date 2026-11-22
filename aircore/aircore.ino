// aircore.ino - drives the stock NB Miata tach and speedo (air-core gauges) from the MSP-DASH hub's data.
// Boards:
//   Wemos D1 Mini (ESP8266), fqbn esp8266:esp8266:d1_mini: ONE gauge per board (type "role tach" or "role speedo"
//     once), one DRV8833 each, on D1 / D2 / D5 / D6.
//   Lolin ESP32-S2 Mini, fqbn esp32:esp32:lolin_s2_mini: BOTH gauges on one board, two DRV8833.
//   ESP32 DevKitC (WROOM), fqbn esp32:esp32:esp32: BOTH gauges, two DRV8833, on GPIO 16-19 and 21-23, 25
//     (hardware PWM, steady whatever Wi-Fi does; no boot or flash pins).
// It only listens: it picks up the hub's ESP-NOW data packet (RPM and speed) on whatever channel the hub is on, and
// needs no Wi-Fi network. Setup and calibration over the USB serial port (115200): type "help". See README.md.
#include <Arduino.h>
#include "aircore.h"
#include "aircore_link.h"

#define AIRCORE_BUILD 6
#define PWM_HZ_DEFAULT 20000  // above hearing, so the coils do not whine (the D1 Mini may need lower: "pwm" command)
static uint32_t s_pwmHz = PWM_HZ_DEFAULT;
static uint8_t s_slow = 1;    // 1 = slow decay (brake between pulses, the default), 0 = fast decay: "decay" command
#define PWM_FULL 1023
#define STEP_MS 5             // needle update
#define LOST_MS 1500          // no data for this long: needles go to the start of the scale, and the hunt for the hub starts
#define HOP_MS 300            // channel hunt: time on each channel
#define HOLD_MS 60000         // "angle" holds the needle this long (calibration)

#if defined(ESP8266)
  // ---- ESP8266 (D1 Mini): one gauge, settings in the EEPROM area ----
  #include <ESP8266WiFi.h>
  #include <espnow.h>
  #include <EEPROM.h>
  extern "C" {
  #include <user_interface.h>
  }
  #define GAUGES 1
  #define LOCK()   noInterrupts()
  #define UNLOCK() interrupts()
#else
  // ---- ESP32-S2 Mini: both gauges, settings in NVS ----
  #include <WiFi.h>
  #include <esp_now.h>
  #include <esp_wifi.h>
  #include <Preferences.h>
  #define GAUGES 2
  static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
  #define LOCK()   portENTER_CRITICAL(&s_mux)
  #define UNLOCK() portEXIT_CRITICAL(&s_mux)
#endif

typedef struct {
  const char *name;
  uint8_t pins[4];            // DRV8833 IN1, IN2 (sine coil) and IN3, IN4 (cosine coil)
  int sig;
  AirCoreCfg cfg;
  AirCorePoints pts;          // multi-point calibration (2+ points: used instead of zero / span)
  float needle;               // where it is now, degrees
  float holdDeg; uint32_t holdUntil;
} Gauge;
#if defined(ESP8266)
// D1 = GPIO 5, D2 = GPIO 4, D5 = GPIO 14, D6 = GPIO 12 (none of them a boot pin). Its role picks tach or speedo.
static Gauge G[GAUGES] = { { "tach", { 5, 4, 14, 12 }, LINK_SIG_RPM, {}, {}, 0, 0, 0 } };
#elif defined(CONFIG_IDF_TARGET_ESP32)
// ESP32 DevKitC: tach IN1-IN4 on GPIO 16, 17, 18, 19; speedo on 21, 22, 23, 25 (none a boot, flash or input-only pin).
static Gauge G[GAUGES] = {
  { "tach",   { 16, 17, 18, 19 }, LINK_SIG_RPM, {}, {}, 0, 0, 0 },
  { "speedo", { 21, 22, 23, 25 }, LINK_SIG_VSS, {}, {}, 0, 0, 0 },
};
#else
static Gauge G[GAUGES] = {
  { "tach",   { 3, 5, 7, 9 },     LINK_SIG_RPM, {}, {}, 0, 0, 0 },
  { "speedo", { 11, 12, 16, 18 }, LINK_SIG_VSS, {}, {}, 0, 0, 0 },
};
#endif

static volatile float s_val[GAUGES];
static volatile bool s_has[GAUGES], s_sim = false;
static volatile uint32_t s_lastRx = 0, s_rx = 0;
static uint8_t s_ch = 1;
static uint32_t s_hopMs = 0;

static void defaultsFor(Gauge &g) { if (g.sig == LINK_SIG_RPM) aircore_defaults_tach(&g.cfg); else aircore_defaults_speedo(&g.cfg); }

// ---- settings ----
#if defined(ESP8266)
// EEPROM layout: [0] version, [1] role (0 tach, 1 speedo), [4..] AirCoreCfg
#define EE_SIZE 256   // [64..] the calibration points
static void setRole(int speedo) { G[0].name = speedo ? "speedo" : "tach"; G[0].sig = speedo ? LINK_SIG_VSS : LINK_SIG_RPM; }
static void cfgSave(int) {
  EEPROM.write(0, AIRCORE_VERSION); EEPROM.write(1, G[0].sig == LINK_SIG_VSS ? 1 : 0);
  EEPROM.put(4, G[0].cfg); EEPROM.put(40, s_pwmHz); EEPROM.write(44, s_slow); EEPROM.put(64, G[0].pts); EEPROM.commit();
}
static void cfgLoad() {
  EEPROM.begin(EE_SIZE);
  bool ok = EEPROM.read(0) == AIRCORE_VERSION;
  setRole(ok && EEPROM.read(1) == 1);
  if (ok) { EEPROM.get(4, G[0].cfg); EEPROM.get(40, s_pwmHz); s_slow = EEPROM.read(44) != 0; EEPROM.get(64, G[0].pts); }
  if (!ok || !aircore_points_valid(&G[0].pts)) aircore_points_clear(&G[0].pts);
  if (s_pwmHz < 100 || s_pwmHz > 40000) s_pwmHz = PWM_HZ_DEFAULT;
  if (!ok || !aircore_valid(&G[0].cfg)) defaultsFor(G[0]);
}
#else
static void cfgSave(int g) { Preferences p; p.begin("aircore", false); p.putBytes(G[g].name, &G[g].cfg, sizeof(AirCoreCfg)); p.putUChar("ver", AIRCORE_VERSION); p.putULong("pwm", s_pwmHz); p.putUChar("slow", s_slow);
  p.putBytes((String(G[g].name) + "p").c_str(), &G[g].pts, sizeof(AirCorePoints)); p.end(); }
static void cfgLoad() {
  Preferences p; p.begin("aircore", true);
  bool ok = p.getUChar("ver", 0) == AIRCORE_VERSION;
  s_slow = p.getUChar("slow", 1) != 0;
  s_pwmHz = p.getULong("pwm", PWM_HZ_DEFAULT); if (s_pwmHz < 100 || s_pwmHz > 40000) s_pwmHz = PWM_HZ_DEFAULT;
  for (int g = 0; g < GAUGES; g++) {
    if (!ok || p.getBytes(G[g].name, &G[g].cfg, sizeof(AirCoreCfg)) != sizeof(AirCoreCfg) || !aircore_valid(&G[g].cfg)) defaultsFor(G[g]);
    if (p.getBytes((String(G[g].name) + "p").c_str(), &G[g].pts, sizeof(AirCorePoints)) != sizeof(AirCorePoints) || !aircore_points_valid(&G[g].pts)) aircore_points_clear(&G[g].pts);
  }
  p.end();
}
#endif

// ---- the hub's data packet: RPM and speed out of it, if it has them ----
static void handlePacket(const uint8_t *d, int len) {
  if (len < LINK_PKT_V_OFFSET + 4) return;
  uint32_t magic; memcpy(&magic, d, 4);
  if (magic != LINK_PKT_MAGIC) return;
  uint64_t has; memcpy(&has, d + 8, 8);
  LOCK();
  for (int g = 0; g < GAUGES; g++) {
    int at = LINK_PKT_V_OFFSET + 4 * G[g].sig;
    if (at + 4 <= len && ((has >> G[g].sig) & 1)) { float v; memcpy(&v, d + at, 4); s_val[g] = v; s_has[g] = true; }
    else s_has[g] = false;
  }
  s_sim = has & AC_MASK_SIMULATED;
  s_lastRx = millis(); s_rx++;
  UNLOCK();
}
#if defined(ESP8266)
static void onRecv(uint8_t *mac, uint8_t *d, uint8_t len) { handlePacket(d, len); }
static void setChannel(uint8_t ch) { wifi_set_channel(ch); }
#else
static void onRecv(const esp_now_recv_info_t *info, const uint8_t *d, int len) { handlePacket(d, len); }
static void setChannel(uint8_t ch) { esp_wifi_set_promiscuous(true); esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE); esp_wifi_set_promiscuous(false); }
#endif

// ---- coils ----
static void pwmApply() {   // the PWM frequency (the D1 Mini makes PWM in software: lower is steadier there)
#if defined(ESP8266)
  analogWriteFreq(s_pwmHz); analogWriteRange(PWM_FULL);
#else
  for (int g = 0; g < GAUGES; g++) for (int k = 0; k < 4; k++) { ledcDetach(G[g].pins[k]); ledcAttach(G[g].pins[k], s_pwmHz, 10); }
#endif
}
static void pwmWrite(uint8_t pin, uint32_t duty) {
#if defined(ESP8266)
  analogWrite(pin, duty);
#else
  ledcWrite(pin, duty);
#endif
}
static void drive(int g, float deg) {
  float s, c; aircore_coils(deg, G[g].cfg.strength, &s, &c);
  uint32_t a, b;
  void (*bridge)(float, uint32_t, uint32_t *, uint32_t *) = s_slow ? aircore_bridge_slow : aircore_bridge;
  bridge(s, PWM_FULL, &a, &b); pwmWrite(G[g].pins[0], a); pwmWrite(G[g].pins[1], b);
  bridge(c, PWM_FULL, &a, &b); pwmWrite(G[g].pins[2], a); pwmWrite(G[g].pins[3], b);
}

// Start-up sweep: the needle(s) to the end of the scale and back, like the stock cluster.
static void sweep() {
  for (int pass = 0; pass < 2; pass++)
    for (int k = 0; k <= 100; k++) {
      float f = pass ? 1.0f - k / 100.0f : k / 100.0f;
      for (int g = 0; g < GAUGES; g++) {
        float lo = G[g].pts.n >= 2 ? G[g].pts.v[0] : G[g].cfg.minVal, hi = G[g].pts.n >= 2 ? G[g].pts.v[G[g].pts.n - 1] : G[g].cfg.maxVal;
        G[g].needle = aircore_angle_pts(&G[g].cfg, &G[g].pts, lo + f * (hi - lo)); drive(g, G[g].needle);
      }
      delay(7);
    }
}

static void show() {
  Serial.printf("aircore build %d | %s decay | pwm %lu Hz | channel %d | packets %lu | %s\n", AIRCORE_BUILD, s_slow ? "slow" : "fast", (unsigned long)s_pwmHz, s_ch, (unsigned long)s_rx,
                millis() - s_lastRx < LOST_MS ? (s_sim ? "hub heard (SIMULATED data)" : "hub heard") : "no hub: hunting channels");
  for (int g = 0; g < GAUGES; g++) {
    const AirCoreCfg &c = G[g].cfg;
    char v[16]; if (s_has[g]) snprintf(v, sizeof v, "%.0f", (float)s_val[g]); else strcpy(v, "--");
    Serial.printf("  %-6s value %s | needle %.1f deg | min %g max %g zero %g span %g strength %g slew %g\n", G[g].name, v, G[g].needle,
                  c.minVal, c.maxVal, c.zeroDeg, c.spanDeg, c.strength, c.slewDegS);
    if (G[g].pts.n) {
      Serial.printf("         points (%s):", G[g].pts.n >= 2 ? "in use" : "need 2 or more");
      for (int i = 0; i < G[g].pts.n; i++) Serial.printf(" %g=%g", G[g].pts.v[i], G[g].pts.a[i]);
      Serial.println();
    }
  }
}
static void help() {
  Serial.println("Commands:");
  Serial.println("  show                          values, settings, channel");
#if defined(ESP8266)
  Serial.println("  role tach | role speedo       which gauge this board drives (factory settings for it)");
#endif
  Serial.println("  tach angle 30                 hold the tach needle at 30 deg for a minute (find zero and span)");
  Serial.println("  tach release                  back to live data");
  Serial.println("  tach zero 12 | span 245       coil angle at the start of the scale | degrees to the end");
  Serial.println("  tach min 0 | max 8000         values at the start and end of the dial");
  Serial.println("  tach strength 0.8 | slew 720  drive (0.1-1) | fastest needle speed, deg/s");
  Serial.println("  tach mark 2000                 the angle the needle is held at now (tach angle ...) becomes the 2000 mark");
  Serial.println("  tach point 2000 118           the 2000 mark at coil angle 118, typed in");
  Serial.println("  tach unpoint 2000 | clearpoints   remove one mark | all of them (back to zero / span)");
  Serial.println("  (same for speedo)   sweep     needle(s) end to end   defaults   factory settings");
  Serial.println("  pwm 2000                      coil PWM frequency, 100-40000 Hz (lower = steadier on the D1 Mini, may hum)");
  Serial.println("  decay slow | decay fast       how the coil current is driven between pulses (slow: stronger, steadier)");
}
static void command(String line) {
  line.trim(); line.toLowerCase();
  if (!line.length()) return;
  if (line == "help") { help(); return; }
  if (line == "show") { show(); return; }
  if (line == "sweep") { sweep(); return; }
  if (line == "defaults") { for (int g = 0; g < GAUGES; g++) { defaultsFor(G[g]); cfgSave(g); } Serial.println("Factory settings back"); return; }
  if (line == "decay slow" || line == "decay fast") {
    s_slow = line == "decay slow"; for (int g = 0; g < GAUGES; g++) cfgSave(g);
    Serial.printf("%s decay, saved\n", s_slow ? "slow" : "fast"); return;
  }
  if (line.startsWith("pwm ")) {
    long hz = line.substring(4).toInt();
    if (hz < 100 || hz > 40000) { Serial.println("100 to 40000 Hz"); return; }
    s_pwmHz = hz; pwmApply(); for (int g = 0; g < GAUGES; g++) cfgSave(g);
    Serial.printf("pwm %ld Hz, saved\n", hz); return;
  }
#if defined(ESP8266)
  if (line == "role tach" || line == "role speedo") {
    setRole(line == "role speedo"); defaultsFor(G[0]); cfgSave(0);
    Serial.printf("This board drives the %s (factory settings for it, saved)\n", G[0].name); return;
  }
#endif
  int sp1 = line.indexOf(' '), sp2 = line.indexOf(' ', sp1 + 1);
  String who = line.substring(0, sp1), what = sp1 < 0 ? "" : line.substring(sp1 + 1, sp2 < 0 ? line.length() : sp2);
  float v = sp2 < 0 ? NAN : line.substring(sp2 + 1).toFloat();
  int g = -1;
  for (int k = 0; k < GAUGES; k++) if (who == G[k].name) g = k;
  if (g < 0) { Serial.printf("? type help%s\n", GAUGES == 1 ? (String(" (this board drives the ") + G[0].name + ")").c_str() : ""); return; }
  if (what == "release") { G[g].holdUntil = 0; Serial.println("Live data"); return; }
  if (what == "clearpoints") { aircore_points_clear(&G[g].pts); cfgSave(g); Serial.printf("%s: marks cleared (zero / span again), saved\n", G[g].name); return; }
  if (isnan(v)) { Serial.println("? a number is missing"); return; }
  if (what == "angle") { G[g].holdDeg = v; G[g].holdUntil = millis() + HOLD_MS; Serial.printf("%s held at %.1f deg\n", G[g].name, v); return; }
  if (what == "mark" || what == "point" || what == "unpoint") {
    float val = v, ang = G[g].holdDeg;
    if (what == "point") {   // "tach point 2000 118": value and angle typed in
      String rest = line.substring(sp2 + 1); rest.trim(); int sp3 = rest.indexOf(' ');
      if (sp3 < 0) { Serial.println("? tach point <value> <angle>"); return; }
      val = rest.substring(0, sp3).toFloat(); ang = rest.substring(sp3 + 1).toFloat();
    } else if (what == "mark" && !(G[g].holdUntil && millis() < G[g].holdUntil)) { Serial.println("Hold the needle on the mark first (tach angle ...), then mark it"); return; }
    if (what == "unpoint") { Serial.println(aircore_points_remove(&G[g].pts, val) ? "Mark removed, saved" : "No mark at that value"); cfgSave(g); return; }
    if (!aircore_points_set(&G[g].pts, val, ang)) { Serial.println("12 marks already: remove one first"); return; }
    cfgSave(g); Serial.printf("%s: %g at %.1f deg (%d marks), saved\n", G[g].name, val, ang, G[g].pts.n); return;
  }
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
#if defined(ESP8266)
  analogWriteFreq(s_pwmHz); analogWriteRange(PWM_FULL);
  for (int k = 0; k < 4; k++) { pinMode(G[0].pins[k], OUTPUT); analogWrite(G[0].pins[k], 0); }
  WiFi.mode(WIFI_STA); WiFi.disconnect();
  setChannel(s_ch);
  if (esp_now_init() == 0) { esp_now_set_self_role(ESP_NOW_ROLE_SLAVE); esp_now_register_recv_cb(onRecv); } else Serial.println("ESP-NOW start FAILED");
#else
  for (int g = 0; g < GAUGES; g++) for (int k = 0; k < 4; k++) ledcAttach(G[g].pins[k], s_pwmHz, 10);   // 8 pins: the S2's 8 PWM channels (the ESP32 has 16)
  WiFi.mode(WIFI_STA); WiFi.disconnect(false, false);
  setChannel(s_ch);
  if (esp_now_init() == ESP_OK) esp_now_register_recv_cb(onRecv); else Serial.println("ESP-NOW start FAILED");
#endif
  sweep();
  Serial.printf("\naircore build %d ready (%s). Type help.\n", AIRCORE_BUILD, GAUGES == 1 ? G[0].name : "tach + speedo");
}

void loop() {
  static uint32_t last = 0; static String line;
  while (Serial.available()) { char ch = Serial.read(); if (ch == '\n' || ch == '\r') { command(line); line = ""; } else if (line.length() < 60) line += ch; }
  uint32_t now = millis();
  bool heard = s_lastRx && now - s_lastRx < LOST_MS;
  if (!heard && now - s_hopMs >= HOP_MS) { s_hopMs = now; s_ch = s_ch % 13 + 1; setChannel(s_ch); }   // hunt for the hub
  if (now - last < STEP_MS) return;
  float dt = (now - last) / 1000.0f; last = now;
  for (int g = 0; g < GAUGES; g++) {
    float v; bool has;
    LOCK(); v = s_val[g]; has = s_has[g] && heard; UNLOCK();
    float rest = G[g].pts.n >= 2 ? G[g].pts.v[0] : G[g].cfg.minVal;
    float target = G[g].holdUntil && now < G[g].holdUntil ? G[g].holdDeg : aircore_angle_pts(&G[g].cfg, &G[g].pts, has ? v : rest);
    G[g].needle = aircore_slew(G[g].needle, target, G[g].cfg.slewDegS, dt);
    drive(g, G[g].needle);
  }
}
