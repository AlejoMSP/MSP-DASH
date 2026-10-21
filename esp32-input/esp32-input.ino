// esp32-input - MSP-DASH input board.
// Reads 2 buttons (next / previous page) and a knob (brightness) and sends them to every screen over ESP-NOW.
// Also on the home network for WiFi updates: same curl command as the screens, at its own IP.
//
// Bench board: ESP32-D (classic ESP32, fqbn esp32:esp32:esp32:FlashFreq=40,FlashMode=dio: at the default 80 MHz this board
// boot-looped reading garbage from flash). Later: the S2 Mini (change the pins below).
// Car inputs (see the web page at this board's IP for settings): lights-on wire -> 47k -> GPIO 35, and 10k from GPIO 35 to GND;
//   dimmer wire the same into GPIO 32 (add 10 uF from GPIO 32 to GND if the dimmer is PWM).
// Wiring: button between BTN_NEXT and GND, button between BTN_PREV and GND (internal pull-ups),
//         10k knob: outer legs to 3.3 V and GND, middle leg to POT_PIN (ADC1 only: ADC2 doesn't work with the radio on).
// Copy wifi_config.h.example to wifi_config.h and fill in the network, the same one the screens use.

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "wifi_config.h"
#include "input_btn.h"
#include <Preferences.h>
#include "driver/twai.h"
#include "gauge_config.h"   // copied in at compile time (see PREREQUISITES.md)
#include "gauge_app.h"
#include "link_packets.h"   // copied in at compile time: same packets as the screens
#include "web_monitor.h"   // raw CAN tracker, used by the /can page
#include "speed_sensors.h" // hall sensor math and the GPS reader (host-tested)
#include "simulator.h"     // copied in at compile time with simulator.cpp: the bench simulator runs here (build 15 on)

#define INPUT_BUILD 18
#define BOARD_NAME "Input"

// ---- pins (ESP32-D bench) ----
#define BTN_NEXT 25
#define BTN_PREV 26
#define POT_PIN  34
#define LIGHTS_PIN 35   // car's lights-on wire (12 V when the lights are on), through a 47k / 10k divider
#define DIMMER_PIN 32   // dash dimmer output, through a 47k / 10k divider (+ 10 uF if it's PWM)
#define DIVIDER (57.0f / 10.0f)
#define CAN_TX_PIN GPIO_NUM_21   // SN65HVD230: CTX / D
#define CAN_RX_PIN GPIO_NUM_22   // SN65HVD230: CRX / R   // (47k + 10k) / 10k: 14.4 V in -> 2.53 V at the pin

// ---- network: this board's fixed address ----
static const IPAddress MY_IP(192, 168, 55, 204), MY_GW(192, 168, 55, 1), MY_MASK(255, 255, 255, 0);
#define CAR_CHANNEL 1   // the screens park here when the home network isn't there; must match ESPNOW_CAR_CHANNEL


static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint16_t s_seq = 0;
static volatile uint32_t s_dataSent = 0;   // data packets sent to the screens

// ---- the hub: CAN values (written by the decoder from can_parsing.h) and the data packet the screens read ----
volatile float g_rpm, g_clt_f, g_boost_psi, g_afr, g_oilp_psi, g_oilt_f, g_fuelp_psi, g_flex_pct, g_tps, g_battv, g_egt_f, g_iat_f, g_ambt_f, g_hum_pct, g_fuel_pct, g_vss_mph, g_gps_mph, g_gps_sats;
volatile bool g_rpm_has_data, g_clt_has_data, g_boost_has_data, g_afr_has_data, g_oilp_has_data, g_oilt_has_data,
              g_fuelp_has_data, g_flex_has_data, g_tps_has_data, g_battv_has_data, g_egt_has_data, g_iat_has_data, g_ambt_has_data, g_hum_has_data, g_fuel_has_data, g_vss_has_data, g_gps_has_data, g_sats_has_data;
volatile uint16_t g_flag_bits, g_flag_seen;
static volatile uint32_t s_seenMs[SIG_COUNT];
#define STALE_MS 2000   // same as the screens: a signal not heard for 2 s shows "--"

// Where each signal last came from, for the /sensors page: a CAN ID, or one of the hub's own sensors.
#define SRC_HUB_EGT  0xFFFF0001u   // MAX6675
#define SRC_HUB_DHT  0xFFFF0002u   // DHT22
#define SRC_HUB_FUEL 0xFFFF0003u   // fuel pot / sender
#define SRC_HUB_SIM  0xFFFF0004u   // the bench simulator
#define SRC_HUB_HALL 0xFFFF0005u   // hall speed sensor
#define SRC_HUB_GPS  0xFFFF0006u   // GPS
static volatile uint32_t s_srcId[SIG_COUNT];
// Called by the CAN decoder (can_parsing.h, can_flags.h) from the CAN task: the source is the frame being decoded.
// The simulator calls it too (from loop()); its values are tagged as coming from the simulator.
extern "C" void gauge_config_mark_signal_active(SignalId sig) { if (sig > 0 && sig < SIG_COUNT) { s_seenMs[sig] = millis() | 1; s_srcId[sig] = sim_is_active() ? SRC_HUB_SIM : s_canCurId; } }
// Called by the hub's own sensor tasks. They do not write their values while the simulator runs (see simOwns).
static void markLocal(SignalId sig, uint32_t src) { if (sig > 0 && sig < SIG_COUNT) { s_seenMs[sig] = millis() | 1; s_srcId[sig] = src; } }
// True while the simulator feeds the values: the hub's own sensors then leave the shared values alone, so a real
// reading never flickers in between fake ones (the raw readings still show on the home page).
static inline bool simOwns(void) { return sim_is_active(); }
extern "C" bool gauge_config_signal_has_data(SignalId sig) {
  return sig > 0 && sig < SIG_COUNT && s_seenMs[sig] && millis() - s_seenMs[sig] < STALE_MS;
}
extern "C" float gauge_config_signal_value(SignalId sig) {
  switch (sig) {
    case SIG_RPM: return g_rpm;       case SIG_CLT: return g_clt_f;     case SIG_MAP: return g_boost_psi;
    case SIG_AFR: return g_afr;       case SIG_OILP: return g_oilp_psi; case SIG_OILT: return g_oilt_f;
    case SIG_FUELP: return g_fuelp_psi; case SIG_FLEX: return g_flex_pct; case SIG_TPS: return g_tps;
    case SIG_BATTV: return g_battv;   case SIG_EGT: return g_egt_f;     case SIG_IAT: return g_iat_f;
    case SIG_AMBT: return g_ambt_f;  case SIG_HUM: return g_hum_pct;
    case SIG_FUEL: return g_fuel_pct;  case SIG_VSS: return g_vss_mph;
    case SIG_GPS_SPD: return g_gps_mph; case SIG_GPS_SATS: return g_gps_sats;
    default: return gauge_signal_is_flag(sig) ? (((g_flag_bits >> (sig - SIG_FIRST_FLAG)) & 1u) ? 1.0f : 0.0f) : 0.0f;
  }
}

// ---- screen status: every remote screen sends a hello every 2 s; no hello for 6 s = offline (red) ----
#define SCREEN_MAX 6
#define SCREEN_OFFLINE_MS 6000
typedef struct { uint8_t mac[6]; uint32_t ip; uint16_t build; char name[LINK_NAME_LEN + 1]; uint32_t lastMs; uint32_t hellos; bool used; } ScreenSlot;
static ScreenSlot s_screens[SCREEN_MAX];
static portMUX_TYPE s_scrMux = portMUX_INITIALIZER_UNLOCKED;
static void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len < (int)offsetof(HelloPacket, name) || ((const HelloPacket *)data)->magic != HELLO_MAGIC) return;
  HelloPacket h; memset(&h, 0, sizeof h); memcpy(&h, data, len < (int)sizeof h ? len : sizeof h);
  portENTER_CRITICAL(&s_scrMux);
  int slot = -1, oldest = 0;
  for (int i = 0; i < SCREEN_MAX; i++) {
    if (s_screens[i].used && !memcmp(s_screens[i].mac, info->src_addr, 6)) { slot = i; break; }
    if (slot < 0 && !s_screens[i].used) slot = i;
  }
  if (slot < 0) { for (int i = 1; i < SCREEN_MAX; i++) if (s_screens[i].lastMs < s_screens[oldest].lastMs) oldest = i; slot = oldest; }
  ScreenSlot *c = &s_screens[slot];
  if (!c->used || memcmp(c->mac, info->src_addr, 6)) { memset(c, 0, sizeof *c); memcpy(c->mac, info->src_addr, 6); }
  c->ip = h.ip; c->build = h.build; memcpy(c->name, h.name, LINK_NAME_LEN); c->name[LINK_NAME_LEN] = 0;
  c->lastMs = millis(); c->hellos++; c->used = true;
  portEXIT_CRITICAL(&s_scrMux);
}

static WebServer server(80);
static char s_last[48] = "none yet";
static int s_briPct = -1;
static bool s_wifiUp = false;

// Each command goes out 3 times (broadcast has no acknowledgement); the screens act on the first copy only.
static void sendCmd(uint8_t cmd, int16_t value, const char *target) {
  LinkCmdPacket p = {};
  p.magic = LINK_CMD_MAGIC; p.seq = ++s_seq; p.cmd = cmd; p.value = value;
  strncpy(p.target, target, sizeof(p.target) - 1);
  for (int i = 0; i < 3; i++) { esp_now_send(BCAST, (const uint8_t *)&p, sizeof(p)); delay(4); }
  snprintf(s_last, sizeof(s_last), "%s %d", cmd == LINK_CMD_PAGE_NEXT ? "next page" : cmd == LINK_CMD_PAGE_PREV ? "previous page" : "brightness", value);
  Serial.printf("sent: %s\n", s_last);
}

// ---- buttons: 30 ms debounce, act on press ----
static Btn s_next = {BTN_NEXT, true, 0}, s_prev = {BTN_PREV, true, 0};
static bool pressed(Btn &b) {
  bool now = digitalRead(b.pin);
  if (now != b.last && millis() - b.changed > 30) { b.last = now; b.changed = millis(); return now == LOW; }
  return false;
}

// ---- brightness settings (saved; edited on this board's web page) ----
// Mode auto: lights off = day %. Lights on = night min % at the dimmer's lowest, rising to night max % at its highest.
// Mode knob: the potentiometer sets it directly (bench / manual).
struct BriCfg { int mode; int dayPct; int nightMin; int nightMax; float dimLoV; float dimHiV; float lightsOnV; };
static BriCfg s_cfg = { 0, 100, 20, 60, 2.0f, 12.0f, 6.0f };   // mode 0 = auto, 1 = knob
static void cfgLoad() {
  Preferences p; p.begin("bri", true);
  s_cfg.mode = p.getInt("mode", 0); s_cfg.dayPct = p.getInt("day", 100); s_cfg.nightMin = p.getInt("nmin", 20);
  s_cfg.nightMax = p.getInt("nmax", 60); s_cfg.dimLoV = p.getFloat("dlo", 2.0f); s_cfg.dimHiV = p.getFloat("dhi", 12.0f);
  s_cfg.lightsOnV = p.getFloat("lon", 6.0f);
  p.end();
}
static void cfgSave() {
  Preferences p; p.begin("bri", false);
  p.putInt("mode", s_cfg.mode); p.putInt("day", s_cfg.dayPct); p.putInt("nmin", s_cfg.nightMin); p.putInt("nmax", s_cfg.nightMax);
  p.putFloat("dlo", s_cfg.dimLoV); p.putFloat("dhi", s_cfg.dimHiV); p.putFloat("lon", s_cfg.lightsOnV);
  p.end();
}

// ---- readings: smoothed volts at the car side of each divider, and the knob ----
static float s_lightsV = 0, s_dimV = 0, s_potAvg = -1;
static uint32_t s_lastBriMs = 0;
static float carVolts(int pin) { return analogReadMilliVolts(pin) / 1000.0f * DIVIDER; }
static int clampPct(int v) { return v < 5 ? 5 : (v > 100 ? 100 : v); }

static int wantedPct() {
  if (s_cfg.mode == 1) return clampPct(5 + (int)lroundf(s_potAvg * 95.0f / 4095.0f));
  if (s_lightsV < s_cfg.lightsOnV) return clampPct(s_cfg.dayPct);
  float span = s_cfg.dimHiV - s_cfg.dimLoV;
  float f = span > 0.1f ? (s_dimV - s_cfg.dimLoV) / span : 0.0f;
  f = f < 0 ? 0 : (f > 1 ? 1 : f);
  return clampPct(s_cfg.nightMin + (int)lroundf(f * (s_cfg.nightMax - s_cfg.nightMin)));
}

#include "can_parsing.h"   // copied in at compile time; uses the values and functions above

// ---- EGT: MAX6675 + K-type thermocouple, read by hand (bit-banged SPI) ----
// Wiring: SCK GPIO18, SO GPIO19, CS GPIO23, VCC 3V3. The chip needs ~220 ms per reading, so read every 250 ms.
// Each good reading goes on the CAN bus as ID 0x7F0 (CAN_ID_EGT_PLACEHOLDER in can_parsing.h) for rusEFI:
//   bytes 0-1 = EGT deg C, unsigned little endian; byte 2 = 1 if the thermocouple is open (not connected).
#define EGT_SCK 18
#define EGT_SO  19
#define EGT_CS  23
#define EGT_PERIOD_MS 250
#define EGT_HOLD_MS 1000   // send EGT only if the ECU was heard in the last second (an unanswered frame pushes the hub to bus-off)
static volatile float s_egtC = 0; static volatile bool s_egtOpen = true; static volatile uint32_t s_egtSent = 0; static volatile bool s_egtHeld = true;
static uint16_t max6675Read() {
  uint16_t v = 0;
  digitalWrite(EGT_CS, LOW); delayMicroseconds(2);
  for (int i = 15; i >= 0; i--) {
    digitalWrite(EGT_SCK, HIGH); delayMicroseconds(1);
    if (digitalRead(EGT_SO)) v |= 1u << i;
    digitalWrite(EGT_SCK, LOW); delayMicroseconds(1);
  }
  digitalWrite(EGT_CS, HIGH);
  return v;
}
static void egtTask(void *) {
  pinMode(EGT_CS, OUTPUT); digitalWrite(EGT_CS, HIGH);
  pinMode(EGT_SCK, OUTPUT); digitalWrite(EGT_SCK, LOW);
  pinMode(EGT_SO, INPUT);
  vTaskDelay(pdMS_TO_TICKS(500));                   // first conversion after power-up
  for (;;) {
    uint16_t raw = max6675Read();
    bool open = (raw & 0x0004) || raw == 0xFFFF;    // bit 2 = no thermocouple; all ones = chip missing
    float c = (raw >> 3) * 0.25f;
    s_egtOpen = open;
    if (!open) s_egtC = c;
    if (!open && !simOwns()) {
      g_egt_f = c * 9.0f / 5.0f + 32.0f;            // the hub does not hear its own frame, so set the value here too
      g_egt_has_data = true;
      markLocal(SIG_EGT, SRC_HUB_EGT);
    }
    vTaskDelay(pdMS_TO_TICKS(EGT_PERIOD_MS));
  }
}

// ---- Hall speed sensor: NJK-5002C (NPN, normally open) on GPIO 27, a magnet passing it pulls the pin low ----
// Wiring: brown = +5 V (5-30 V), blue = ground, black = signal -> GPIO 27, with 10k from GPIO 27 to 3.3 V.
// The interrupt runs on core 0: the DHT22 read switches interrupts off on core 1 for ~5 ms.
#define HALL_PIN 27
static volatile uint32_t s_hallCount = 0, s_hallLastUs = 0;
static float s_ppm = HALL_PPM_DEFAULT;             // pulses per mile (saved; calibrate from the GPS)
static volatile float s_hallMph = 0, s_hallHz = 0;
static void IRAM_ATTR hallIsr() {
  uint32_t now = micros();
  if (now - s_hallLastUs < HALL_MIN_PERIOD_US) return;   // noise
  s_hallLastUs = now; s_hallCount++;
}
static void ppmLoad() { Preferences p; p.begin("spd", true); s_ppm = p.getFloat("ppm", HALL_PPM_DEFAULT); p.end(); if (s_ppm < HALL_PPM_MIN || s_ppm > HALL_PPM_MAX) s_ppm = HALL_PPM_DEFAULT; }
static void ppmSave() { Preferences p; p.begin("spd", false); p.putFloat("ppm", s_ppm); p.end(); }
static void hallTask(void *) {
  pinMode(HALL_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(HALL_PIN), hallIsr, FALLING);   // attached here, so it runs on this task's core (0)
  uint32_t prevCount = s_hallCount, prevUs = s_hallLastUs;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(100));
    uint32_t count = s_hallCount, lastUs = s_hallLastUs, now = micros();
    uint32_t n = count - prevCount;
    if (n > 0 && prevCount != 0) {                   // average period over every pulse since the last look
      uint32_t period = (lastUs - prevUs) / n;
      s_hallHz = period ? 1000000.0f / period : 0;
      s_hallMph = hall_mph(period, now - lastUs, s_ppm);
    } else if (now - lastUs > HALL_STOP_US) { s_hallMph = 0; s_hallHz = 0; }
    if (n > 0) { prevCount = count; prevUs = lastUs; }
    if (!simOwns()) { g_vss_mph = s_hallMph; g_vss_has_data = true; markLocal(SIG_VSS, SRC_HUB_HALL); }
  }
}

// ---- GPS: GT-U7 (NEO-6M compatible) on Serial2: module TX -> GPIO 16, module RX <- GPIO 17, VCC 5 V ----
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
static GpsParser s_gps;
static volatile uint32_t s_gpsBytes = 0, s_gpsLastByteMs = 0;
static void gpsTask(void *) {
  gps_init(&s_gps);
  Serial2.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  vTaskDelay(pdMS_TO_TICKS(1000));
  Serial2.write(GPS_CFG_RATE_5HZ, sizeof GPS_CFG_RATE_5HZ);      // 5 fixes a second
  for (;;) {
    while (Serial2.available()) {
      char c = (char)Serial2.read(); s_gpsBytes++; s_gpsLastByteMs = millis();
      if (!gps_feed(&s_gps, c) || simOwns()) continue;
      if (s_gps.newRmc) { s_gps.newRmc = false; if (s_gps.fix) { g_gps_mph = s_gps.mph; g_gps_has_data = true; markLocal(SIG_GPS_SPD, SRC_HUB_GPS); } }
      if (s_gps.newGga) { s_gps.newGga = false; if (s_gps.fix) { g_gps_sats = s_gps.sats; g_sats_has_data = true; markLocal(SIG_GPS_SATS, SRC_HUB_GPS); } }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ---- CAN frame 0x7F0 to rusEFI, 10 times a second: EGT and speed ----
//   bytes 0-1 EGT deg C (LE), byte 2 = 1 if the thermocouple is open, bytes 3-4 speed in kph x 10 (LE).
// Sent only while the ECU was heard in the last second (an unanswered frame pushes the hub to bus-off).
static void canTxTask(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(100));
    twai_message_t m = {};
    m.identifier = CAN_ID_EGT_PLACEHOLDER; m.data_length_code = 5;
    bool open = s_egtOpen;
    uint16_t ci = open ? 0 : (uint16_t)lroundf(s_egtC);
    float kph = s_hallMph * MPH_TO_KPH;
    uint16_t k10 = (uint16_t)lroundf(kph * 10.0f > 65000.0f ? 65000.0f : kph * 10.0f);
    m.data[0] = ci & 0xFF; m.data[1] = ci >> 8; m.data[2] = open ? 1 : 0; m.data[3] = k10 & 0xFF; m.data[4] = k10 >> 8;
    s_egtHeld = !s_canLastRxMs || millis() - s_canLastRxMs > EGT_HOLD_MS;
    if (!s_egtHeld && twai_transmit(&m, pdMS_TO_TICKS(5)) == ESP_OK) s_egtSent++;
  }
}

// ---- DHT22 ambient temp + humidity on GPIO4 (3-pin module, pull-up built in), read every 2.5 s ----
#define DHT_PIN 4
#define DHT_PERIOD_MS 2500
static volatile float s_dhtC = 0, s_dhtH = 0; static volatile bool s_dhtOk = false; static volatile uint32_t s_dhtFail = 0;
static portMUX_TYPE s_dhtMux = portMUX_INITIALIZER_UNLOCKED;
static int dhtWait(int level, int maxUs) {           // us spent while the line stays at 'level', -1 on timeout
  uint32_t t0 = micros();
  while (digitalRead(DHT_PIN) == level) if ((int)(micros() - t0) > maxUs) return -1;
  return (int)(micros() - t0);
}
static bool dhtRead(float *c, float *h) {
  uint8_t d[5] = {0};
  pinMode(DHT_PIN, OUTPUT); digitalWrite(DHT_PIN, LOW); delay(2);   // start signal: low for at least 1 ms
  bool ok = true;
  portENTER_CRITICAL(&s_dhtMux);                     // ~5 ms with interrupts off so the bit timing is exact
  pinMode(DHT_PIN, INPUT_PULLUP);                    // let go: the sensor answers 20-40 us after release
  if (dhtWait(HIGH, 100) < 0 || dhtWait(LOW, 100) < 0 || dhtWait(HIGH, 100) < 0) ok = false;   // sensor answers low 80 / high 80
  for (int i = 0; ok && i < 40; i++) {
    if (dhtWait(LOW, 80) < 0) { ok = false; break; }
    int hi = dhtWait(HIGH, 100); if (hi < 0) { ok = false; break; }
    if (hi > 40) d[i / 8] |= 0x80 >> (i % 8);        // ~27 us high = 0, ~70 us high = 1
  }
  portEXIT_CRITICAL(&s_dhtMux);
  if (!ok || (uint8_t)(d[0] + d[1] + d[2] + d[3]) != d[4]) return false;
  *h = ((d[0] << 8) | d[1]) * 0.1f;
  int16_t t = ((d[2] & 0x7F) << 8) | d[3];
  *c = (d[2] & 0x80 ? -t : t) * 0.1f;
  return *h <= 100.0f;
}
static void dhtTask(void *) {
  vTaskDelay(pdMS_TO_TICKS(2000));                   // the sensor needs ~1 s after power-up
  for (;;) {
    float c, h;
    if (dhtRead(&c, &h)) {
      s_dhtC = c; s_dhtH = h; s_dhtOk = true;
      if (!simOwns()) {
      g_ambt_f = c * 9.0f / 5.0f + 32.0f; g_ambt_has_data = true; markLocal(SIG_AMBT, SRC_HUB_DHT);
      g_hum_pct = h;                      g_hum_has_data = true;  markLocal(SIG_HUM, SRC_HUB_DHT);
      }
    } else { s_dhtFail++; }
    vTaskDelay(pdMS_TO_TICKS(DHT_PERIOD_MS));
  }
}

// ---- Fuel level: pot (mock sender) on GPIO36, 0-3.3 V = 0-100 %, smoothed, read every 100 ms ----
#define FUEL_PIN 36
static volatile float s_fuelPct = 0;   // the pot's own reading, shown on the home page even while the simulator runs
static void fuelTask(void *) {
  float avg = analogRead(FUEL_PIN);
  for (;;) {
    avg += (analogRead(FUEL_PIN) - avg) * 0.1f;      // slow filter: fuel sloshes, the gauge should not
    s_fuelPct = avg * 100.0f / 4095.0f;
    if (simOwns()) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    g_fuel_pct = avg * 100.0f / 4095.0f;
    g_fuel_has_data = true;
    markLocal(SIG_FUEL, SRC_HUB_FUEL);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// every 50 ms: smooth the inputs; send when the result moves 2 % or more, and every 2 s so a screen that reboots catches up
static void readKnob() {
  int raw = analogRead(POT_PIN);
  s_potAvg = s_potAvg < 0 ? raw : s_potAvg * 0.85f + raw * 0.15f;
  s_lightsV = s_lightsV * 0.8f + carVolts(LIGHTS_PIN) * 0.2f;
  s_dimV = s_dimV * 0.8f + carVolts(DIMMER_PIN) * 0.2f;
  int pct = wantedPct();
  if (s_briPct < 0 || abs(pct - s_briPct) >= 2 || millis() - s_lastBriMs > 2000) {
    s_briPct = pct; s_lastBriMs = millis();
    sendCmd(LINK_CMD_BRIGHTNESS, (int16_t)pct, "");
  }
}

// ---- web: status page and WiFi updates (curl -F "firmware=@...bin" http://192.168.55.204/update) ----
static void setupWeb() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", R"HTML(<html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH Hub</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:480px;margin:auto;padding:12px}h2{color:#4dd0e1}
label{display:block;margin:10px 0 4px}input,select{width:100%;padding:6px;box-sizing:border-box;background:#111;color:#ddd;border:1px solid #333}
button{margin-top:10px;padding:8px 12px;background:#4dd0e1;border:0;color:#000}.live{background:#111;padding:10px;border-radius:6px}
.row{display:flex;gap:8px}.row>*{flex:1}.note{color:#999;font-size:13px}
.dot{display:inline-block;width:12px;height:12px;border-radius:50%;margin-right:8px;vertical-align:middle}.on{background:#2e7d32}.off{background:#e53935}
.scr{padding:4px 0}.scr a{color:#4dd0e1}</style></head><body>
<h2>MSP-DASH Hub</h2>
<h3 style="color:#4dd0e1;margin:8px 0">Screens</h3><div class="live" id="scr">Waiting for screens...</div>
<h3 style="color:#4dd0e1;margin:8px 0">Hub</h3><div class="live" id="live">Loading...</div>
<h3 style="color:#4dd0e1;margin:8px 0">Speed</h3><div class="live" id="spd">Loading...</div>
<label>Hall sensor pulses per mile<input type="number" id="ppm" step="any" min="100" max="100000"></label>
<div class="row"><button type="button" onclick="savePpm()">Save pulses per mile</button><button type="button" onclick="calGps()">Calibrate from GPS</button></div>
<span id="smsg" class="note"></span>
<p class="note">To calibrate: drive at a steady speed above 10 mph with a GPS fix and press "Calibrate from GPS".</p>
<label>Brightness mode<select id="mode"><option value="0">Auto: car lights and dimmer</option><option value="1">Knob</option></select></label>
<label>Day brightness, lights off (%)<input type="number" id="day" min="5" max="100"></label>
<div class="row"><label>Night, dimmer lowest (%)<input type="number" id="nmin" min="5" max="100"></label>
<label>Night, dimmer highest (%)<input type="number" id="nmax" min="5" max="100"></label></div>
<div class="row"><label>Dimmer lowest (V)<input type="number" id="dlo" step="0.1"></label>
<label>Dimmer highest (V)<input type="number" id="dhi" step="0.1"></label></div>
<div class="row"><button type="button" onclick="grab('dlo')">Use now as lowest</button><button type="button" onclick="grab('dhi')">Use now as highest</button></div>
<label>Lights count as on above (V)<input type="number" id="lon" step="0.1"></label>
<button type="button" onclick="save()">Save</button> <span id="msg" class="note"></span>
<p class="note">Turn the lights on, roll the dimmer all the way down and press "Use now as lowest", all the way up and press "Use now as highest", then Save.</p>
<p><a href="/update" style="color:#4dd0e1">Firmware update</a></p>
<script>
let st={};const ids=["mode","day","nmin","nmax","dlo","dhi","lon"];
function grab(id){document.getElementById(id).value=st.dimV.toFixed(1);}
async function poll(){try{st=await (await fetch("/state")).json();
document.getElementById("live").innerHTML="Lights: "+st.lightsV.toFixed(1)+" V ("+(st.lightsOn?"ON":"off")+")<br>Dimmer: "+st.dimV.toFixed(1)+" V<br>Brightness sent: "+st.bri+" %<br>Build "+st.build+" | "+st.ip+" | channel "+st.ch+"<br>Last command: "+st.last+"<br>Signals live: "+st.live+" | data packets sent: "+st.sent+" | <a href=/sensors>Sensors</a> | <a href=/can>Raw CAN</a> | <a href=/simulator>Simulator</a><br>Simulator: "+st.sim+"<br>Ambient: "+st.dht+"<br>Fuel: "+st.fuel+" %<br>EGT: "+st.egt+" | sent to CAN: "+st.egtsent;
if(!poll.done){ids.forEach(i=>document.getElementById(i).value=st.cfg[i]);poll.done=1;}}catch(e){}}
async function save(){const q=ids.map(i=>i+"="+encodeURIComponent(document.getElementById(i).value)).join("&");
document.getElementById("msg").textContent=await (await fetch("/save?"+q)).text();}
function ago(ms){return ms<60000?Math.round(ms/1000)+" s ago":Math.round(ms/60000)+" min ago";}
async function scr(){try{const l=await (await fetch("/screens.json")).json();
l.sort((a,b)=>a.ip<b.ip?-1:1);
document.getElementById("scr").innerHTML=l.length?l.map(s=>'<div class="scr"><span class="dot '+(s.online?'on':'off')+'"></span><b>'+(s.name||'(no name)')+'</b> '+
(s.online?'online':'OFFLINE, last heard '+ago(s.age))+'<br><span class="note">'+'<a href="http://'+s.ip+'/">'+s.ip+'</a> | build '+s.build+' | '+s.mac+'</span></div>').join(''):
'No screens heard yet. Screens set to main do not say hello.';}catch(e){}}
async function spd(){try{const g=await (await fetch("/speed.json")).json();
const gps=!g.gpsok?"not connected (no data on GPIO 16)":!g.fix?"no fix yet ("+g.sats+" satellites)":g.gpsmph.toFixed(1)+" mph, "+g.sats+" satellites";
const diff=g.fix&&g.gpsmph>5?" | difference "+((g.hallmph-g.gpsmph)/g.gpsmph*100).toFixed(1)+" %":"";
document.getElementById("spd").innerHTML="Hall: "+g.hallmph.toFixed(1)+" mph ("+g.hz.toFixed(1)+" pulses/s, "+g.pulses+" pulses)<br>GPS: "+gps+diff+"<br>Sent to rusEFI: "+(g.held?"no (ECU not heard)":(g.hallmph*1.609344).toFixed(1)+" kph");
if(!spd.done){document.getElementById("ppm").value=g.ppm;spd.done=1;}}catch(e){}}
async function savePpm(){document.getElementById("smsg").textContent=await (await fetch("/ppm?set="+encodeURIComponent(document.getElementById("ppm").value))).text();}
async function calGps(){const r=await fetch("/hallcal");const t=await r.text();document.getElementById("smsg").textContent=t;if(r.ok){spd.done=0;}}
poll();setInterval(poll,1000);scr();setInterval(scr,1000);spd();setInterval(spd,500);
</script></body></html>)HTML");
  });
  // ---- raw CAN monitor: every ID seen, latest bytes, count, age ----
  server.on("/can.json", HTTP_GET, []() {
    CanMonEntry snap[CANMON_MAX]; int n; uint32_t tot;
    portENTER_CRITICAL(&s_canMux); n = s_canMonN; tot = s_canTotal; memcpy(snap, s_canMon, sizeof(CanMonEntry) * n); portEXIT_CRITICAL(&s_canMux);
    twai_status_info_t st; bool ok = twai_get_status_info(&st) == ESP_OK;
    String j = "{\"total\":" + String(tot) + ",\"state\":" + String(ok ? (int)st.state : -1) +
               ",\"rxerr\":" + String(ok ? st.rx_error_counter : 0) + ",\"missed\":" + String(ok ? st.rx_missed_count : 0) + ",\"ids\":[";
    uint32_t now = millis();
    for (int i = 0; i < n; i++) {
      char b[96]; int k = snprintf(b, sizeof b, "%s{\"id\":%u,\"n\":%u,\"age\":%u,\"d\":\"", i ? "," : "", (unsigned)snap[i].id, (unsigned)snap[i].count, (unsigned)(now - snap[i].lastMs));
      for (int x = 0; x < snap[i].len; x++) k += snprintf(b + k, sizeof b - k, "%02X ", snap[i].d[x]);
      snprintf(b + k, sizeof b - k, "\"}");
      j += b;
    }
    j += "]}";
    server.send(200, "application/json", j);
  });
  server.on("/can", HTTP_GET, []() {
    server.send(200, "text/html", F(
      "<!doctype html><meta name=viewport content='width=device-width'><title>Hub CAN</title>"
      "<style>body{font-family:sans-serif;background:#111;color:#eee}td,th{padding:3px 10px;font-family:monospace}th{text-align:left}.old{color:#666}</style>"
      "<h3>Hub raw CAN</h3><div id=s></div><table><thead><tr><th>ID</th><th>Bytes</th><th>Count</th><th>Age ms</th></tr></thead><tbody id=t></tbody></table>"
      "<p><a href='/'>Back</a></p><script>"
      "const ST=['stopped','running','bus-off','recovering'];"
      "async function u(){try{const r=await fetch('/can.json');const j=await r.json();"
      "s.textContent='Frames: '+j.total+' | bus: '+(ST[j.state]||'?')+' | rx errors: '+j.rxerr+' | missed: '+j.missed;"
      "j.ids.sort((a,b)=>a.id-b.id);t.innerHTML=j.ids.map(e=>'<tr class='+(e.age>2000?'old':'')+'><td>0x'+e.id.toString(16).toUpperCase().padStart(3,'0')+'</td><td>'+e.d+'</td><td>'+e.n+'</td><td>'+e.age+'</td></tr>').join('');}catch(e){}}"
      "setInterval(u,500);u();</script>"));
  });
  // ---- sensors: every signal that has ever had data, its value, age and where it comes from ----
  server.on("/sensors.json", HTTP_GET, []() {
    uint32_t now = millis();
    String j = "[";
    bool first = true;
    for (int i = 1; i < SIG_COUNT; i++) {
      uint32_t seen = s_seenMs[i]; if (!seen) continue;
      float v = gauge_config_signal_value((SignalId)i);
      char b[120];
      snprintf(b, sizeof b, "%s{\"sig\":%d,\"v\":%.2f,\"age\":%u,\"src\":%u}", first ? "" : ",", i, (v == v && v > -1e30f && v < 1e30f) ? (double)v : 0.0,
               (unsigned)(now - seen), (unsigned)s_srcId[i]);
      j += b; first = false;
    }
    j += "]";
    server.send(200, "application/json", j);
  });
  server.on("/sensors", HTTP_GET, []() {
    server.send(200, "text/html", R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH Sensors</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:640px;margin:auto;padding:12px}h2{color:#4dd0e1}a{color:#4dd0e1}
table{width:100%;border-collapse:collapse}td,th{padding:5px 8px;text-align:left;border-bottom:1px solid #222}th{color:#999;font-weight:normal}
.v{font-family:monospace;text-align:right}.tag{padding:1px 6px;border-radius:4px;font-size:12px}.hub{background:#1b5e20}.sim{background:#e65100}.can{background:#0d47a1}
.stale td{color:#666}.note{color:#999;font-size:13px}</style></head><body>
<h2>Sensors</h2><div class="note" id="sum"></div>
<table><thead><tr><th>Signal</th><th class="v">Value</th><th>Source</th><th class="v">Age</th></tr></thead><tbody id="t"></tbody></table>
<p class="note">Hub = a sensor wired to this board. ECU = decoded from a CAN frame. Grey = not heard for 2 s.</p>
<h3 style="color:#4dd0e1;margin:16px 0 4px">On the bus, not decoded</h3>
<div class="note" id="usum"></div>
<table><thead><tr><th>CAN ID</th><th>From</th><th>Bytes</th><th class="v">Age</th></tr></thead><tbody id="u"></tbody></table>
<p class="note">Frames heard on the CAN bus that no sensor reads yet. They show up here by themselves as soon as they appear.</p>
<p><a href="/">Back</a> | <a href="/can">Raw CAN</a></p>
<script>
const NAME={1:"RPM",2:"Coolant Temp",3:"MAP/Boost",4:"AFR",5:"Oil Pressure",6:"Oil Temp",7:"Fuel Pressure",8:"Ethanol",9:"Throttle Position",10:"Battery Voltage",
11:"Rev Limiter",12:"Main Relay",13:"Fuel Pump",14:"Check Engine",15:"O2 Heater",16:"Lambda Protection",17:"Fan 1",18:"Fan 2",19:"Brake Pedal",
20:"EGT",21:"Intake Air Temp",22:"Ambient Temp",23:"Humidity",24:"Fuel Level",25:"Speed",26:"GPS Speed",27:"GPS Satellites"};
const UNIT={1:"rpm",2:"F",3:"psi",4:"",5:"psi",6:"F",7:"psi",8:"%",9:"%",10:"V",20:"F",21:"F",22:"F",23:"%",24:"%",25:"mph",26:"mph",27:"sats"};
const DEC={1:0,2:0,3:0,4:1,5:0,6:0,7:0,8:0,9:0,10:1,20:0,21:0,22:1,23:0,24:0,25:0,26:0,27:0};
const HUB={4294901761:"MAX6675 thermocouple (GPIO 18/19/23)",4294901762:"DHT22 (GPIO 4)",4294901763:"Fuel sender pot (GPIO 36)",4294901764:"Bench simulator (FAKE)",4294901765:"Hall speed sensor (GPIO 27)",4294901766:"GT-U7 GPS (GPIO 16/17)"};
function ecu(id){return id>=0x200&&id<=0x20F?"rusEFI":id>=0x5F0&&id<=0x63F?"MegaSquirt/Link":"";}
async function u(){try{const l=(await (await fetch("/sensors.json")).json()).sort((a,b)=>(NAME[a.sig]||"").localeCompare(NAME[b.sig]||""));let h=0,c=0,e=new Set();
document.getElementById("t").innerHTML=l.map(s=>{const isHub=HUB[s.src]!==undefined;isHub?h++:c++;const k=ecu(s.src);if(!isHub&&k)e.add(k);
const flag=s.sig>=11&&s.sig<=19;const val=flag?(s.v>0.5?"ON":"off"):s.v.toFixed(DEC[s.sig]??1)+" "+(UNIT[s.sig]||"");
const src=s.src===4294901764?'<span class="tag sim">Sim</span> '+HUB[s.src]:isHub?'<span class="tag hub">Hub</span> '+HUB[s.src]:'<span class="tag can">ECU</span> CAN 0x'+s.src.toString(16).toUpperCase()+(k?" ("+k+")":"");
return '<tr class="'+(s.age>2000?'stale':'')+'"><td>'+(NAME[s.sig]||("Signal "+s.sig))+'</td><td class="v">'+val+'</td><td>'+src+'</td><td class="v">'+(s.age<1000?s.age+" ms":(s.age/1000).toFixed(0)+" s")+'</td></tr>';}).join("");
document.getElementById("sum").textContent=h+" from the hub, "+c+" from the ECU"+(e.size?" ("+[...e].join(", ")+")":"");}catch(x){}}
async function und(){try{const used=new Set((await (await fetch("/sensors.json")).json()).map(s=>s.src));
const j=await (await fetch("/can.json")).json();
const l=j.ids.filter(x=>!used.has(x.id)&&x.age<10000).sort((a,b)=>a.id-b.id);
document.getElementById("usum").textContent=l.length?l.length+" frame"+(l.length===1?"":"s")+" not read by any sensor":"Every frame on the bus is read by a sensor (or the bus is quiet).";
document.getElementById("u").innerHTML=l.map(x=>'<tr class="'+(x.age>2000?'stale':'')+'"><td class="v" style="text-align:left">0x'+x.id.toString(16).toUpperCase().padStart(3,"0")+'</td><td>'+(ecu(x.id)||"unknown")+'</td><td class="v" style="text-align:left">'+x.d+'</td><td class="v">'+(x.age<1000?x.age+" ms":(x.age/1000).toFixed(0)+" s")+'</td></tr>').join("");}catch(x){}}
u();setInterval(u,500);und();setInterval(und,1000);
</script></body></html>)HTML");
  });
  // ---- bench simulator (moved here from the screens in build 15; safety rules in simulator.h) ----
  server.on("/sim.json", HTTP_GET, []() {
    String j = "{\"active\":" + String(sim_is_active() ? "true" : "false") + ",\"manual\":" + String(sim_is_manual() ? "true" : "false") +
               ",\"seconds_left\":" + String(sim_seconds_left()) + ",\"stop_reason\":\"" + String(sim_last_stop_reason()) + "\",\"signals\":[";
    for (int sig = 1; sig < SIG_COUNT; sig++) {
      SimRange r = sim_range(sig);
      char b[120];
      snprintf(b, sizeof b, "%s{\"id\":%d,\"min\":%g,\"max\":%g,\"step\":%g,\"value\":%g}", sig > 1 ? "," : "", sig, (double)r.min, (double)r.max, (double)r.step, (double)sim_value(sig));
      j += b;
    }
    j += "]}";
    server.send(200, "application/json", j);
  });
  server.on("/sim", HTTP_POST, []() {
    String cmd = server.arg("cmd");
    if (cmd == "start") {
      uint32_t last = s_canLastRxMs;
      if (last && millis() - last < 1000) { server.send(409, "text/plain", "Real CAN frames are arriving, so the simulator will not start. It only runs while the bus is silent."); return; }
      sim_start(server.arg("mode") == "manual");
      server.send(200, "text/plain", "OK");
    } else if (cmd == "stop") {
      sim_stop("stopped from the web page");
      server.send(200, "text/plain", "OK");
    } else if (cmd == "set") {
      float val = server.hasArg("val") ? server.arg("val").toFloat() : NAN;
      if (sim_set_manual(server.arg("sig").toInt(), val)) server.send(200, "text/plain", "OK");
      else server.send(409, "text/plain", "The simulator is not running in manual mode");
    } else server.send(400, "text/plain", "Unknown command");
  });
  server.on("/simulator", HTTP_GET, []() {
    server.send(200, "text/html", R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH Simulator</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:480px;margin:auto;padding:12px}h2{color:#4dd0e1}a{color:#4dd0e1}
.warnbox{background:#2a1f00;border:1px solid #f5a623;border-radius:8px;padding:10px;font-size:.85em;color:#f5c26b}
.statebox{margin:12px 0;padding:10px;border-radius:8px;background:#111;text-align:center}.statebox.on{background:#f5a623;color:#000;font-weight:bold}
.btnrow{display:flex;gap:8px;justify-content:center;flex-wrap:wrap}button{padding:10px 14px;background:#4dd0e1;color:#000;border:0;border-radius:6px}
#msg{text-align:center;margin:10px 0;min-height:1.2em;color:#4dd0e1}#msg.bad{color:#ff6b6b}
.sl{margin:10px 0}.sl label{display:flex;justify-content:space-between;font-size:.85em;color:#4dd0e1}.sl input{width:100%}</style></head><body>
<h2>Bench Simulator</h2>
<div class="warnbox"><b>This feeds FAKE values</b> to every screen, so you can test with no ECU connected. It switches itself off after 10 minutes without a change, at every reboot of the hub, and the moment a real CAN frame arrives. The screens say SIMULATED and the main screen does not log while it runs.</div>
<div id="state" class="statebox">Loading...</div>
<div class="btnrow"><button onclick="cmd('start','demo')">Start demo</button><button onclick="cmd('start','manual')">Start manual</button><button onclick="cmd('stop')">Stop</button></div>
<div id="msg"></div><div id="sliders"></div>
<p><a href="/">Back</a> | <a href="/sensors">Sensors</a></p>
<script>
const NAME={1:"RPM",2:"Coolant Temp (F)",3:"Boost (psi)",4:"AFR",5:"Oil Pressure (psi)",6:"Oil Temp (F)",7:"Fuel Pressure (psi)",8:"Ethanol %",9:"Throttle %",10:"Battery (V)",
11:"Rev Limiter",12:"Main Relay",13:"Fuel Pump",14:"Check Engine",15:"O2 Heater",16:"Lambda Protection",17:"Fan 1",18:"Fan 2",19:"Brake Pedal",
20:"EGT (F)",21:"Intake Air Temp (F)",22:"Ambient Temp (F)",23:"Humidity %",24:"Fuel Level %",25:"Speed (mph)",26:"GPS Speed (mph)",27:"GPS Satellites"};
let info=null,building="",sending=false,pending=null;
function setMsg(t,bad){const m=document.getElementById("msg");m.textContent=t;m.className=bad?"bad":"";}
async function post(p){const r=await fetch("/sim",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams(p).toString()});
const t=await r.text();if(!r.ok)throw new Error(t||("board returned "+r.status));return t;}
async function cmd(c,mode){try{await post(mode?{cmd:c,mode:mode}:{cmd:c});setMsg("");}catch(e){setMsg(e.message,true);}refresh();}
function fmtTime(s){return Math.floor(s/60)+":"+String(s%60).padStart(2,"0");}
function shown(s){return Number(s.value).toFixed(s.step<1?1:0);}
function render(){const st=document.getElementById("state");
if(info.active){st.className="statebox on";st.textContent="SIMULATING ("+(info.manual?"manual":"demo")+") - switches off in "+fmtTime(info.seconds_left);}
else{st.className="statebox";st.textContent="Off"+(info.stop_reason?" - last stopped: "+info.stop_reason:"");}
const box=document.getElementById("sliders"),key=info.active?(info.manual?"manual":"demo"):"off";
if(key!==building){building=key;if(key==="off"){box.innerHTML="";return;}
box.innerHTML=info.signals.map(s=>'<div class="sl"><label>'+(NAME[s.id]||("Signal "+s.id))+' <span id="v'+s.id+'"></span></label><input id="r'+s.id+'" type="range" min="'+s.min+'" max="'+s.max+'" step="'+s.step+'" '+(info.manual?"":"disabled")+' oninput="slide('+s.id+',this.value)"></div>').join("");}
for(const s of info.signals){const lab=document.getElementById("v"+s.id),rng=document.getElementById("r"+s.id);if(lab)lab.textContent=shown(s);if(rng&&document.activeElement!==rng)rng.value=s.value;}}
function slide(id,val){document.getElementById("v"+id).textContent=val;pending={id:id,val:val};pump();}
async function pump(){if(sending)return;sending=true;while(pending){const p=pending;pending=null;try{await post({cmd:"set",sig:p.id,val:p.val});}catch(e){setMsg(e.message,true);}}sending=false;}
async function refresh(){try{info=await (await fetch("/sim.json")).json();render();}catch(e){document.getElementById("state").textContent="Could not reach the hub - retrying...";}}
setInterval(refresh,1000);refresh();
</script></body></html>)HTML");
  });
  server.on("/screens.json", HTTP_GET, []() {
    ScreenSlot snap[SCREEN_MAX];
    portENTER_CRITICAL(&s_scrMux); memcpy(snap, s_screens, sizeof snap); portEXIT_CRITICAL(&s_scrMux);
    uint32_t now = millis();
    String j = "[";
    bool first = true;
    for (int i = 0; i < SCREEN_MAX; i++) {
      if (!snap[i].used) continue;
      char nm[LINK_NAME_LEN + 1]; int k = 0;   // keep the name JSON-safe
      for (int x = 0; snap[i].name[x] && k < LINK_NAME_LEN; x++) { char ch = snap[i].name[x]; if (ch >= 32 && ch != '"' && ch != '\\') nm[k++] = ch; }
      nm[k] = 0;
      uint32_t age = now - snap[i].lastMs;
      char b[200];
      snprintf(b, sizeof b, "%s{\"name\":\"%s\",\"ip\":\"%u.%u.%u.%u\",\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"build\":%u,\"age\":%u,\"online\":%s,\"hellos\":%u}",
               first ? "" : ",", nm, (unsigned)(snap[i].ip & 255), (unsigned)(snap[i].ip >> 8 & 255), (unsigned)(snap[i].ip >> 16 & 255), (unsigned)(snap[i].ip >> 24),
               snap[i].mac[0], snap[i].mac[1], snap[i].mac[2], snap[i].mac[3], snap[i].mac[4], snap[i].mac[5],
               (unsigned)snap[i].build, (unsigned)age, age < SCREEN_OFFLINE_MS ? "true" : "false", (unsigned)snap[i].hellos);
      j += b; first = false;
    }
    j += "]";
    server.send(200, "application/json", j);
  });
  server.on("/state", HTTP_GET, []() {
    char egtTxt[56]; if (s_egtOpen) strcpy(egtTxt, "open / not connected"); else snprintf(egtTxt, sizeof egtTxt, "%.0f C / %.0f F", s_egtC, s_egtC * 1.8f + 32);
    if (s_egtHeld) strcat(egtTxt, " (not sent: ECU not heard)");
    char simTxt[80];
    if (sim_is_active()) snprintf(simTxt, sizeof simTxt, "<b style='color:#f5a623'>ON (%s), %d s left</b>", sim_is_manual() ? "manual" : "demo", sim_seconds_left());
    else strcpy(simTxt, "off");
    char dhtTxt[40]; if (!s_dhtOk) snprintf(dhtTxt, sizeof dhtTxt, "no reading (fails %u)", (unsigned)s_dhtFail); else snprintf(dhtTxt, sizeof dhtTxt, "%.1f F, %.0f%% humidity", s_dhtC * 1.8f + 32, s_dhtH);
    auto liveCount = []() { int n = 0; for (int i = 1; i < SIG_COUNT; i++) if (gauge_config_signal_has_data((SignalId)i)) n++; return n; };
    char b[900];
    snprintf(b, sizeof(b), "{\"lightsV\":%.2f,\"lightsOn\":%s,\"dimV\":%.2f,\"bri\":%d,\"build\":%d,\"ip\":\"%s\",\"ch\":%d,\"last\":\"%s\","
             "\"egt\":\"%s\",\"sim\":\"%s\",\"dht\":\"%s\",\"fuel\":%.0f,\"egtsent\":%u,\"live\":%d,\"sent\":%u,\"cfg\":{\"mode\":%d,\"day\":%d,\"nmin\":%d,\"nmax\":%d,\"dlo\":%.1f,\"dhi\":%.1f,\"lon\":%.1f}}",
             s_lightsV, s_lightsV >= s_cfg.lightsOnV ? "true" : "false", s_dimV, s_briPct, INPUT_BUILD, WiFi.localIP().toString().c_str(),
             WiFi.channel(), s_last, egtTxt, simTxt, dhtTxt, (double)s_fuelPct, (unsigned)s_egtSent, liveCount(), (unsigned)s_dataSent, s_cfg.mode, s_cfg.dayPct, s_cfg.nightMin, s_cfg.nightMax, s_cfg.dimLoV, s_cfg.dimHiV, s_cfg.lightsOnV);
    server.send(200, "application/json", b);
  });
  // ---- speed: hall sensor and GPS ----
  server.on("/speed.json", HTTP_GET, []() {
    char b[260];
    snprintf(b, sizeof b, "{\"hallmph\":%.2f,\"hz\":%.2f,\"pulses\":%u,\"ppm\":%.1f,\"gpsok\":%s,\"fix\":%s,\"gpsmph\":%.2f,\"sats\":%d,\"rmc\":%u,\"bad\":%u,\"held\":%s}",
             (double)s_hallMph, (double)s_hallHz, (unsigned)s_hallCount, (double)s_ppm,
             (s_gpsLastByteMs && millis() - s_gpsLastByteMs < 2000) ? "true" : "false", s_gps.fix ? "true" : "false",
             (double)s_gps.mph, s_gps.sats, (unsigned)s_gps.rmcCount, (unsigned)s_gps.badCount, s_egtHeld ? "true" : "false");
    server.send(200, "application/json", b);
  });
  server.on("/ppm", HTTP_GET, []() {
    float v = server.arg("set").toFloat();
    if (v < HALL_PPM_MIN || v > HALL_PPM_MAX) { server.send(400, "text/plain", "Pulses per mile must be 100 to 100000"); return; }
    s_ppm = v; ppmSave();
    server.send(200, "text/plain", "Saved: " + String(v, 1) + " pulses per mile");
  });
  server.on("/hallcal", HTTP_GET, []() {
    bool gpsLive = s_gps.fix && s_gpsLastByteMs && millis() - s_gpsLastByteMs < 2000;
    if (!gpsLive) { server.send(409, "text/plain", "Not calibrated: the GPS has no fix"); return; }
    if (s_gps.mph < 10.0f) { server.send(409, "text/plain", "Not calibrated: drive above 10 mph (GPS says " + String(s_gps.mph, 1) + ")"); return; }
    float ppm = hall_ppm_from_gps(s_hallHz, s_gps.mph);
    if (ppm <= 0) { server.send(409, "text/plain", "Not calibrated: the hall sensor is not seeing the magnet"); return; }
    s_ppm = ppm; ppmSave();
    server.send(200, "text/plain", "Calibrated: " + String(ppm, 1) + " pulses per mile");
  });
  server.on("/save", HTTP_GET, []() {
    auto I = [](const char *k, int d) { return server.hasArg(k) ? server.arg(k).toInt() : d; };
    auto rdF = [](const char *k, float d) { return server.hasArg(k) ? server.arg(k).toFloat() : d; };
    s_cfg.mode = I("mode", s_cfg.mode) == 1 ? 1 : 0;
    s_cfg.dayPct = clampPct(I("day", s_cfg.dayPct));
    s_cfg.nightMin = clampPct(I("nmin", s_cfg.nightMin)); s_cfg.nightMax = clampPct(I("nmax", s_cfg.nightMax));
    s_cfg.dimLoV = rdF("dlo", s_cfg.dimLoV); s_cfg.dimHiV = rdF("dhi", s_cfg.dimHiV); s_cfg.lightsOnV = rdF("lon", s_cfg.lightsOnV);
    cfgSave();
    server.send(200, "text/plain", "Saved.");
  });
  server.on("/update", HTTP_GET, []() {
    server.send(200, "text/html", "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='firmware'><input type='submit' value='Update'></form>");
  });
  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
    delay(300); ESP.restart();
  }, []() {
    HTTPUpload &u = server.upload();
    if (u.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
    else if (u.status == UPLOAD_FILE_WRITE) Update.write(u.buf, u.currentSize);
    else if (u.status == UPLOAD_FILE_END) Update.end(true);
  });
  server.begin();
}


// Same packet as the screens' espnow_link.cpp ("MSD1"): every signal plus a has-data mask, 20 times a second
static void dataTask(void *) {
  uint16_t seq = 0;
  for (;;) {
    LinkPacket p; p.magic = PKT_MAGIC; p.seq = seq++; p.count = SIG_COUNT; p.has_mask = 0;
    for (int i = 0; i < SIG_COUNT; i++) {
      bool has = i != SIG_NONE && gauge_config_signal_has_data((SignalId)i);
      p.v[i] = has ? gauge_config_signal_value((SignalId)i) : 0.0f;
      if (has) p.has_mask |= 1u << i;
    }
    if (sim_is_active()) p.has_mask |= LINK_MASK_SIMULATED;   // the screens show SIMULATED and the main screen does not log
    if (esp_now_send(BCAST, (const uint8_t *)&p, sizeof(p)) == ESP_OK) s_dataSent++;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);   // about 0 - 3.1 V at the pins
  cfgLoad();

  // Same rule as the screens: join the home network if it's there (ESP-NOW then shares its channel),
  // otherwise skip it and park on the car channel.
  WiFi.mode(WIFI_STA);
  WiFi.config(MY_IP, MY_GW, MY_MASK, MY_GW);
  bool homeSeen = false;
  int n = WiFi.scanNetworks(false, false, false, 100);
  for (int i = 0; i < n && !homeSeen; i++) if (WiFi.SSID(i) == WIFI_SSID) homeSeen = true;
  WiFi.scanDelete();
  if (homeSeen) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(200);
  }
  s_wifiUp = WiFi.status() == WL_CONNECTED;
  if (!s_wifiUp) {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(CAR_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
  }
  Serial.printf("MSP-DASH input board, build %d: %s, channel %d\n", INPUT_BUILD, s_wifiUp ? WiFi.localIP().toString().c_str() : "no WiFi", WiFi.channel());

  if (esp_now_init() != ESP_OK) Serial.println("ESP-NOW init FAILED");
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BCAST, 6);
  peer.channel = 0; peer.ifidx = WIFI_IF_STA; peer.encrypt = false;
  esp_now_add_peer(&peer);
  esp_now_register_recv_cb(onEspNowRecv);   // screen hellos, for the status lights

  // CAN: the hub is now the only CAN reader; the screens are all remotes
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  g.rx_queue_len = 64;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK || twai_start() != ESP_OK) Serial.println("CAN start FAILED");
  else { can_start_task(); Serial.println("CAN running on GPIO 21 (TX) / 22 (RX)"); }
  xTaskCreatePinnedToCore(egtTask, "egt", 3072, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(dhtTask, "dht", 3072, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(fuelTask, "fuel", 2048, NULL, 1, NULL, 1);
  ppmLoad();
  xTaskCreatePinnedToCore(hallTask, "hall", 3072, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(gpsTask, "gps", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(canTxTask, "can_tx", 3072, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(dataTask, "espnow_tx", 4096, NULL, 2, NULL, 0);

  if (s_wifiUp) setupWeb();
}

void loop() {
  if (pressed(s_next)) sendCmd(LINK_CMD_PAGE_NEXT, 0, "");
  if (pressed(s_prev)) sendCmd(LINK_CMD_PAGE_PREV, 0, "");
  static uint32_t lastKnob = 0;
  if (millis() - lastKnob >= 50) { lastKnob = millis(); readKnob(); }
  sim_loop();
  if (s_wifiUp) server.handleClient();
  delay(2);
}
