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
#include <Preferences.h>
#include "driver/twai.h"
#include "gauge_config.h"   // copied in at compile time (see PREREQUISITES.md)
#include "gauge_app.h"
#include "link_packets.h"   // copied in at compile time: same packets as the screens
#include "web_monitor.h"   // raw CAN tracker, used by the /can page
#include "gps_time.h"       // copied in: GPS date/time, the MST1 packet
#include "speed_sensors.h" // the GPS reader (host-tested)
#include "custom_slots.h"    // copied in: the 16 custom sensor slots (the /custom page)
#include "color_presets.h"   // copied in: the 10 colour schemes
#include "button_actions.h"  // copied in: the actions the buttons can run (/buttons page)
#include "btn_logic.h"       // press / hold-time events (host-tested)
// Runtime serial messages are off unless SERIAL_STREAM is 1; boot messages always print (same macro as can_parsing.h).
#ifndef RT_LOG
#if defined(SERIAL_STREAM) && SERIAL_STREAM
#define RT_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define RT_LOG(...) do { } while (0)
#endif
#endif
#include "rf_cc1101.h"      // CC1101 433 MHz listen mode (tire sensors), the /rf page
#include "hub_pages.h"     // the web pages (kept out of the .ino on purpose, see the file)
#include "simulator.h"     // copied in at compile time with simulator.cpp: the bench simulator runs here (build 15 on)

#define INPUT_BUILD 31
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
// ---- custom sensor slots (custom_slots.h): set up on /custom, saved in NVS, names sent to the screens ----
volatile float g_custom_val[CUSTOM_SLOTS];
volatile bool g_custom_has[CUSTOM_SLOTS];
static CustomSlotConfig s_slots[CUSTOM_SLOTS];
static volatile bool s_slotsChanged = true;   // send the names now (also every SLOTS_MS)
static CustomSlotInfo s_noSlot;   // returned for a slot number out of range
// extern "C", several lines and no "static" inside the body on purpose: the Arduino builder's prototype scan read
// the one-line version as a static function ("declared 'extern' and later 'static'", hub build 22).
extern "C" const CustomSlotInfo *custom_slot_info(int slot) {
  if (slot < 0 || slot >= CUSTOM_SLOTS) return &s_noSlot;
  return &s_slots[slot].info;
}
static void slotsLoad() {
  Preferences p; p.begin("slots", true);
  if (p.getBytes("cfg", s_slots, sizeof s_slots) != sizeof s_slots) memset(s_slots, 0, sizeof s_slots);
  p.end();
  for (int i = 0; i < CUSTOM_SLOTS; i++) { s_slots[i].info.name[CUSTOM_NAME_LEN - 1] = 0; s_slots[i].info.unit[CUSTOM_UNIT_LEN - 1] = 0; }
}
static void slotsSave() { Preferences p; p.begin("slots", false); p.putBytes("cfg", s_slots, sizeof s_slots); p.end(); s_slotsChanged = true; }
// Every received CAN frame (from the tracker): decode every slot that reads this ID.
void hub_custom_can_frame(uint32_t id, const uint8_t *d, uint8_t len) {
  for (int i = 0; i < CUSTOM_SLOTS; i++) {
    const CustomSlotConfig &c = s_slots[i];
    if (c.src != CUSTOM_SRC_CAN || c.can.id != id || !c.info.name[0]) continue;
    float v;
    if (!custom_can_decode(&c.can, d, len, &v)) continue;
    g_custom_val[i] = v; g_custom_has[i] = true;
    s_seenMs[SIG_C1 + i] = millis() | 1; s_srcId[SIG_C1 + i] = id;
  }
}
static void slotsSendNames() {
  for (int first = 0; first < CUSTOM_SLOTS; first += SLOTS_PER_PACKET) {
    SlotsPacket p; memset(&p, 0, sizeof p);
    p.magic = SLOTS_MAGIC; p.first = first; p.n = SLOTS_PER_PACKET;
    for (int k = 0; k < SLOTS_PER_PACKET; k++) p.info[k] = s_slots[first + k].info;
    esp_now_send(BCAST, (const uint8_t *)&p, sizeof p);
    delay(4);
  }
}

extern "C" float gauge_config_signal_value(SignalId sig) {
  if (gauge_signal_is_custom(sig)) return g_custom_val[sig - SIG_C1];
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
  const char *what = cmd == LINK_CMD_PAGE_NEXT ? "next page" : cmd == LINK_CMD_PAGE_PREV ? "previous page" : cmd == LINK_CMD_SELECT ? "buttons control"
                   : cmd == LINK_CMD_ALERT_ACK ? "clear warning" : cmd == LINK_CMD_LOG_MARK ? "log marker" : cmd == LINK_CMD_GOTO_PAGE ? "go to page"
                   : cmd == LINK_CMD_COLOR ? "color scheme" : cmd == LINK_CMD_RACE ? "race mode" : cmd == LINK_CMD_HINT ? "hint"
                   : cmd == LINK_CMD_SHOW_IP ? "show IP" : "brightness";
  if (cmd == LINK_CMD_SELECT) snprintf(s_last, sizeof(s_last), "%s %s", what, target[0] ? target : "all screens");
  else if (cmd == LINK_CMD_ALERT_ACK) snprintf(s_last, sizeof(s_last), "%s", what);
  else snprintf(s_last, sizeof(s_last), "%s %d%s%s", what, value, target[0] ? " to " : "", target);
  RT_LOG("sent: %s\n", s_last);   // runtime: off unless SERIAL_STREAM (can_parsing.h defines RT_LOG)
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

// Brightness from the buttons (Dim all / Brighten all): an offset on top of the automatic or knob level. It drops
// back to 0 when the base level moves by 5 % or more (lights switched, dimmer or knob turned). Race mode: 100 %.
static int s_briOffset = 0, s_briOffsetBase = 0;
static bool s_raceOn = false;
static int finalPct() {
  int base = wantedPct();
  if (s_raceOn) return 100;
  if (s_briOffset && abs(base - s_briOffsetBase) >= 5) s_briOffset = 0;
  return clampPct(base + s_briOffset);
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

// ---- GPS: GT-U7 (NEO-6M compatible) on Serial2: module TX -> GPIO 16, module RX <- GPIO 17, VCC 5 V ----
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
static GpsParser s_gps;
static volatile uint32_t s_gpsUtcMs = 0;   // millis() when s_gps.utc was last set
// Time zone for the screens' log names and timestamps (saved): standard offset in minutes, US daylight saving on/off.
static int s_tzStd = -300; static bool s_tzDst = true;
static void tzLoad() { Preferences p; p.begin("time", true); s_tzStd = p.getInt("std", -300); s_tzDst = p.getUChar("dst", 1); p.end(); }
static void tzSave() { Preferences p; p.begin("time", false); p.putInt("std", s_tzStd); p.putUChar("dst", s_tzDst); p.end(); }
static uint32_t gpsUtcNow() {   // 0 = the GPS has not given the time in the last minute
  uint32_t u = s_gps.utc, at = s_gpsUtcMs;
  if (!u || !at || millis() - at > 60000) return 0;
  return u + (millis() - at) / 1000;
}
static int tzOffsetNow(uint32_t utc) { return s_tzStd + ((s_tzDst && gt_us_dst(utc, s_tzStd)) ? 60 : 0); }
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
      if (s_gps.newRmc && s_gps.utc) s_gpsUtcMs = millis();
      if (s_gps.newRmc) { s_gps.newRmc = false; if (s_gps.fix) { g_gps_mph = s_gps.mph; g_gps_has_data = true; markLocal(SIG_GPS_SPD, SRC_HUB_GPS); } }
      if (s_gps.newGga) { s_gps.newGga = false; if (s_gps.fix) { g_gps_sats = s_gps.sats; g_sats_has_data = true; markLocal(SIG_GPS_SATS, SRC_HUB_GPS); } }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ---- CAN frame 0x7F0 to rusEFI, 4 times a second: EGT ----
//   bytes 0-1 EGT deg C (LE), byte 2 = 1 if the thermocouple is open. (Speed: the hall sensor is wired to rusEFI.)
// Sent only while the ECU was heard in the last second (an unanswered frame pushes the hub to bus-off).
static void canTxTask(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(EGT_PERIOD_MS));
    twai_message_t m = {};
    m.identifier = CAN_ID_EGT_PLACEHOLDER; m.data_length_code = 3;
    bool open = s_egtOpen;
    uint16_t ci = open ? 0 : (uint16_t)lroundf(s_egtC);
    m.data[0] = ci & 0xFF; m.data[1] = ci >> 8; m.data[2] = open ? 1 : 0;
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
  int pct = finalPct();
  if (s_briPct < 0 || abs(pct - s_briPct) >= 2 || millis() - s_lastBriMs > 2000) {
    s_briPct = pct; s_lastBriMs = millis();
    sendCmd(LINK_CMD_BRIGHTNESS, (int16_t)pct, "");
  }
}

// ---- web: status page and WiFi updates (curl -F "firmware=@...bin" http://192.168.55.204/update) ----
// ---- buttons: configurable on /buttons (button_actions.h, btn_logic.h). Left = GPIO 26 (L15), right = GPIO 25 (L14) ----
static BtnState s_btn = {};   // all zero = released (same as btn_init)
static BtnBinding s_bind[BTN_INPUTS][BTN_LEVELS];
static char s_target[LINK_NAME_LEN + 1] = "";   // the screen the buttons control; "" = all
static uint32_t s_lastBtnMs = 0;
static int s_markN = 0;
static int s_hintAction = -1, s_hintParam = -1;
#define BTN_TARGET_IDLE_MS 60000                  // back to all screens after a minute without a press
static void bindLoad() {
  Preferences p; p.begin("btncfg", true);
  if (p.getBytes("b", s_bind, sizeof s_bind) != sizeof s_bind) btn_defaults(s_bind);
  p.end();
  for (int i = 0; i < BTN_INPUTS; i++) for (int l = 0; l < BTN_LEVELS; l++) if (s_bind[i][l].action >= BA_COUNT) s_bind[i][l].action = BA_NONE;
}
static void bindSave() { Preferences p; p.begin("btncfg", false); p.putBytes("b", s_bind, sizeof s_bind); p.end(); }
static void chooseNextScreen() {
  const char *names[SCREEN_MAX]; static char copy[SCREEN_MAX][LINK_NAME_LEN + 1];
  portENTER_CRITICAL(&s_scrMux);
  for (int i = 0; i < SCREEN_MAX; i++) {
    bool on = s_screens[i].used && millis() - s_screens[i].lastMs < SCREEN_OFFLINE_MS;
    strcpy(copy[i], on ? s_screens[i].name : "");
  }
  portEXIT_CRITICAL(&s_scrMux);
  for (int i = 0; i < SCREEN_MAX; i++) names[i] = copy[i];
  int k = btn_next_target(s_target, names, SCREEN_MAX);
  strcpy(s_target, k < 0 ? "" : copy[k]);
  sendCmd(LINK_CMD_SELECT, 0, s_target);   // only that screen (or all) shows the pop-up
}
static void runAction(const BtnBinding *b) {
  switch (b->action) {
    case BA_NEXT_PAGE: sendCmd(LINK_CMD_PAGE_NEXT, 0, s_target); break;
    case BA_PREV_PAGE: sendCmd(LINK_CMD_PAGE_PREV, 0, s_target); break;
    case BA_NEXT_GAUGE: chooseNextScreen(); break;
    case BA_GOTO_PAGE: sendCmd(LINK_CMD_GOTO_PAGE, b->param ? b->param : 1, s_target); break;
    case BA_DIM:    s_briOffset = s_briOffset - 20 < -95 ? -95 : s_briOffset - 20; s_briOffsetBase = wantedPct(); s_briPct = -1; break;
    case BA_BRIGHT: s_briOffset = s_briOffset + 20 > 95 ? 95 : s_briOffset + 20; s_briOffsetBase = wantedPct(); s_briPct = -1; break;
    case BA_COLOR: sendCmd(LINK_CMD_COLOR, b->param, ""); break;
    case BA_RACE:
      s_raceOn = !s_raceOn; s_briPct = -1;
      sendCmd(LINK_CMD_RACE, s_raceOn ? 1 : 0, "");
      if (s_raceOn) sendCmd(LINK_CMD_GOTO_PAGE, b->param ? b->param : 1, "");
      break;
    case BA_CLEAR_WARNING: sendCmd(LINK_CMD_ALERT_ACK, 0, ""); break;
    case BA_LOG_MARK: sendCmd(LINK_CMD_LOG_MARK, ++s_markN, ""); break;
    case BA_SHOW_IP: sendCmd(LINK_CMD_SHOW_IP, WiFi.localIP()[3], ""); break;   // every screen: its name and IP
  }
}
static void handleButtons() {
  BtnEvent e = btn_step(&s_btn, millis(), digitalRead(BTN_PREV) == LOW, digitalRead(BTN_NEXT) == LOW);   // left, right
  if (e.type == BTN_EV_LEVEL) {   // held long enough for another action: tell the screens what letting go will do
    const BtnBinding *b = btn_pick(s_bind, e.input, e.level);
    if (b && (b->action != s_hintAction || b->param != s_hintParam)) {
      s_hintAction = b->action; s_hintParam = b->param;
      sendCmd(LINK_CMD_HINT, (int16_t)((b->action << 8) | b->param), s_target);
    }
  } else if (e.type == BTN_EV_RELEASE) {
    const BtnBinding *b = btn_pick(s_bind, e.input, e.level);
    s_hintAction = s_hintParam = -1; s_lastBtnMs = millis();
    if (b) runAction(b);
  }
  if (s_target[0] && millis() - s_lastBtnMs > BTN_TARGET_IDLE_MS) { s_target[0] = 0; sendCmd(LINK_CMD_SELECT, 0, ""); }
}

static void setupWeb() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", HOME_HTML);
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
    server.send(200, "text/html", SENSORS_HTML);
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
    server.send(200, "text/html", SIM_HTML);
  });
  // ---- custom sensor slots (/custom) ----
  server.on("/custom", HTTP_GET, []() { server.send(200, "text/html", CUSTOM_HTML); });
  server.on("/custom.json", HTTP_GET, []() {
    String j = "[";
    for (int i = 0; i < CUSTOM_SLOTS; i++) {
      const CustomSlotConfig &c = s_slots[i];
      String nm = c.info.name, un = c.info.unit;
      nm.replace("\\", "\\\\"); nm.replace("\"", "\\\""); un.replace("\\", "\\\\"); un.replace("\"", "\\\"");
      char b[300];
      snprintf(b, sizeof b, "%s{\"i\":%d,\"name\":\"%s\",\"unit\":\"%s\",\"dec\":%u,\"min\":%g,\"max\":%g,\"src\":%u,\"id\":%lu,\"start\":%u,\"bits\":%u,\"bit\":%u,\"be\":%u,\"sgn\":%u,\"scale\":%g,\"offset\":%g,\"has\":%s,\"value\":%g}",
               i ? "," : "", i, nm.c_str(), un.c_str(), c.info.decimals, (double)c.info.min, (double)c.info.max, c.src, (unsigned long)c.can.id, c.can.start, c.can.bits, c.can.bit,
               c.can.big_endian, c.can.is_signed, (double)c.can.scale, (double)c.can.offset, gauge_config_signal_has_data((SignalId)(SIG_C1 + i)) ? "true" : "false", (double)g_custom_val[i]);
      j += b;
    }
    server.send(200, "application/json", j + "]");
  });
  server.on("/custom/set", HTTP_GET, []() {
    int i = server.arg("i").toInt();
    if (i < 0 || i >= CUSTOM_SLOTS) { server.send(400, "text/plain", "No such slot"); return; }
    CustomSlotConfig c; memset(&c, 0, sizeof c);
    String name = server.arg("name"); name.trim();
    if (!name.length()) { server.send(400, "text/plain", "Give the sensor a name"); return; }
    custom_copy_text(c.info.name, CUSTOM_NAME_LEN, name.c_str());
    custom_copy_text(c.info.unit, CUSTOM_UNIT_LEN, server.arg("unit").c_str());
    int dec = server.arg("dec").toInt(); c.info.decimals = dec < 0 ? 0 : dec > 3 ? 3 : dec;
    c.info.min = server.arg("min").toFloat(); c.info.max = server.arg("max").toFloat();
    if (!(c.info.max > c.info.min)) { server.send(400, "text/plain", "Gauge max must be above gauge min"); return; }
    c.src = CUSTOM_SRC_CAN;
    c.can.id = strtoul(server.arg("id").c_str(), NULL, 0);
    c.can.start = server.arg("start").toInt(); c.can.bits = server.arg("bits").toInt(); c.can.bit = server.arg("bit").toInt();
    c.can.big_endian = server.arg("be") == "1"; c.can.is_signed = server.arg("sgn") == "1";
    c.can.scale = server.hasArg("scale") ? server.arg("scale").toFloat() : 1.0f; c.can.offset = server.arg("offset").toFloat();
    if (!custom_can_valid(&c.can)) { server.send(400, "text/plain", "That CAN field does not fit: check the ID (up to 0x7FF), start byte, size and scale (not 0)"); return; }
    for (int k = 0; k < CUSTOM_SLOTS; k++)
      if (k != i && s_slots[k].info.name[0] && !strcasecmp(s_slots[k].info.name, c.info.name)) { server.send(400, "text/plain", "Another slot already has that name"); return; }
    s_slots[i] = c; g_custom_has[i] = false; s_seenMs[SIG_C1 + i] = 0;
    slotsSave();
    server.send(200, "text/plain", "Saved. The screens get the name within a few seconds.");
  });
  server.on("/custom/clear", HTTP_GET, []() {
    int i = server.arg("i").toInt();
    if (i < 0 || i >= CUSTOM_SLOTS) { server.send(400, "text/plain", "No such slot"); return; }
    memset(&s_slots[i], 0, sizeof s_slots[i]); g_custom_has[i] = false; s_seenMs[SIG_C1 + i] = 0;
    slotsSave();
    server.send(200, "text/plain", "Cleared");
  });
  // ---- 433 MHz listen mode (/rf) ----
  server.on("/rf", HTTP_GET, []() { server.send(200, "text/html", RF_HTML); });
  server.on("/rf.json", HTTP_GET, []() {
    String j;
    j.reserve(12000);
    char b[320];
    snprintf(b, sizeof b, "{\"ok\":%s,\"part\":%u,\"version\":%u,\"ook\":%s,\"thr\":%d,\"auto\":%s,\"margin\":%d,\"floor\":%d,\"active\":%d,\"rssi\":%d,\"total\":%lu,\"hist\":[",
             s_rfOk ? "true" : "false", s_rfPart, s_rfVersion, s_rfOok ? "true" : "false", s_rfThr, s_rfAuto ? "true" : "false", s_rfMargin,
             (int)s_rfFloor, (int)s_rfActiveThr, (int)s_rfRssi, (unsigned long)s_rfBurstTotal);
    j += b;
    for (int k = s_rfHistCount - 1; k >= 0; k--) {   // oldest first: [peak, background] per second
      int i = (s_rfHistHead - 1 - k + RF_HIST) % RF_HIST;
      j += "["; j += String((int)s_rfHistPeak[i]); j += ","; j += String((int)s_rfHistFloor[i]); j += k ? "]," : "]";
    }
    j += "],\"bursts\":[";
    // newest bursts, then the strongest ones; only the first 48 pulses here (/rf/burst gives all of them)
    static RfBurst copy[RF_BURSTS + RF_TOP];
    portENTER_CRITICAL(&s_rfMux);
    int count = s_rfBurstCount, head = s_rfBurstHead, top = s_rfTopCount;
    for (int k = 0; k < count; k++) copy[k] = s_rfBursts[(head - 1 - k + RF_BURSTS) % RF_BURSTS];
    for (int k = 0; k < top; k++) copy[count + k] = s_rfTop[k];
    portEXIT_CRITICAL(&s_rfMux);
    auto one = [&](const RfBurst &r, bool first) {
      snprintf(b, sizeof b, "%s{\"ms\":%lu,\"age\":%lu,\"rssi\":%d,\"pulses\":%u,\"unit\":%u,\"nbits\":%u,\"inv\":%s,\"first\":%u,\"bits\":\"",
               first ? "" : ",", (unsigned long)r.ms, (unsigned long)(millis() - r.ms), r.rssi, r.pulses, r.unit, r.nbits, r.inverted ? "true" : "false", r.firstLevel);
      j += b; j += r.bits; j += "\",\"hex\":\""; j += r.hex; j += "\",\"w\":[";
      for (int i = 0; i < 48 && i < r.nw; i++) { if (i) j += ","; j += String(r.w[i]); }
      j += "]}";
    };
    for (int k = 0; k < count; k++) one(copy[k], k == 0);
    j += "],\"top\":[";
    for (int k = 0; k < top; k++) one(copy[count + k], k == 0);
    server.send(200, "application/json", j + "]}");
  });
  // all pulses of one of the strongest bursts (?ms= its time), as plain text for copying
  server.on("/rf/burst", HTTP_GET, []() {
    uint32_t ms = strtoul(server.arg("ms").c_str(), NULL, 10);
    static RfBurst r; bool found = false;
    portENTER_CRITICAL(&s_rfMux);
    for (int k = 0; k < s_rfTopCount && !found; k++) if (s_rfTop[k].ms == ms) { r = s_rfTop[k]; found = true; }
    for (int k = 0; k < s_rfBurstCount && !found; k++) if (s_rfBursts[k].ms == ms) { r = s_rfBursts[k]; found = true; }
    portEXIT_CRITICAL(&s_rfMux);
    if (!found) { server.send(404, "text/plain", "That burst is no longer kept"); return; }
    String t; t.reserve(r.nw * 5 + 100);
    t += "rssi "; t += String((int)r.rssi); t += " dBm, "; t += String((int)r.pulses); t += " pulses, first level "; t += String((int)r.firstLevel); t += ", ";   // String(int): += on an int8_t would add a character t += s_rfOok ? "OOK" : "2-FSK"; t += "\n";
    for (int i = 0; i < r.nw; i++) { t += String(r.w[i]); t += (i % 20 == 19) ? "\n" : " "; }
    server.send(200, "text/plain", t);
  });
  server.on("/rf/clear", HTTP_GET, []() {
    portENTER_CRITICAL(&s_rfMux); s_rfTopCount = 0; s_rfBurstCount = 0; portEXIT_CRITICAL(&s_rfMux);
    server.send(200, "text/plain", "Cleared");
  });
  server.on("/rf/set", HTTP_GET, []() {
    if (server.hasArg("mod")) s_rfOok = server.arg("mod") == "ook";
    if (server.hasArg("auto")) s_rfAuto = server.arg("auto") == "1";
    if (server.hasArg("margin")) { int m = server.arg("margin").toInt(); if (m < 3 || m > 40) { server.send(400, "text/plain", "Margin must be 3 to 40 dB"); return; } s_rfMargin = m; }
    if (server.hasArg("thr")) { int t = server.arg("thr").toInt(); if (t < -110 || t > -30) { server.send(400, "text/plain", "Threshold must be -110 to -30 dBm"); return; } s_rfThr = t; }
    rfSaveSettings(); s_rfReconfig = true;
    server.send(200, "text/plain", "Saved");
  });
  // ---- button actions (/buttons) ----
  server.on("/buttons", HTTP_GET, []() { server.send(200, "text/html", BUTTONS_HTML); });
  server.on("/buttons.json", HTTP_GET, []() {
    String j = "{\"actions\":[";
    for (int a = 0; a < BA_COUNT; a++) { if (a) j += ","; j += "\""; j += BA_NAMES[a]; j += "\""; }
    j += "],\"colors\":[";
    for (int c = 0; c < COLOR_PRESET_COUNT; c++) { if (c) j += ","; j += "\""; j += COLOR_PRESETS_C[c].name; j += "\""; }
    j += "],\"race\":"; j += s_raceOn ? "true" : "false";
    j += ",\"target\":\""; j += s_target; j += "\",\"bind\":[";
    for (int i = 0; i < BTN_INPUTS; i++) {
      if (i) j += ",";
      j += "[";
      for (int l = 0; l < BTN_LEVELS; l++) { if (l) j += ","; j += "["; j += String((int)s_bind[i][l].action); j += ","; j += String((int)s_bind[i][l].param); j += "]"; }
      j += "]";
    }
    server.send(200, "application/json", j + "]}");
  });
  server.on("/buttons/set", HTTP_GET, []() {
    int i = server.arg("in").toInt(), l = server.arg("lv").toInt(), a = server.arg("a").toInt(), p = server.arg("p").toInt();
    if (i < 0 || i >= BTN_INPUTS || l < 0 || l >= BTN_LEVELS || a < 0 || a >= BA_COUNT || p < 0 || p > 255) { server.send(400, "text/plain", "Bad setting"); return; }
    if ((a == BA_GOTO_PAGE || a == BA_RACE) && (p < 1 || p > 8)) { server.send(400, "text/plain", "Page must be 1 to 8"); return; }
    if (a == BA_COLOR && p >= COLOR_PRESET_COUNT && p != COLOR_PRESET_CYCLE) { server.send(400, "text/plain", "Pick a color scheme"); return; }
    s_bind[i][l].action = (uint8_t)a; s_bind[i][l].param = (uint8_t)p;
    bindSave();
    server.send(200, "text/plain", "Saved");
  });
  server.on("/buttons/defaults", HTTP_GET, []() { btn_defaults(s_bind); bindSave(); server.send(200, "text/plain", "Back to the defaults"); });
  // ---- clock from the GPS (/time.json, /time/set) ----
  server.on("/time.json", HTTP_GET, []() {
    uint32_t u = gpsUtcNow(); char t[24] = "";
    if (u) gt_format(u, tzOffsetNow(u), 0, t, sizeof t);
    char b[140];
    snprintf(b, sizeof b, "{\"known\":%s,\"local\":\"%s\",\"std\":%d,\"dst\":%s}", u ? "true" : "false", t, s_tzStd, s_tzDst ? "true" : "false");
    server.send(200, "application/json", b);
  });
  server.on("/time/set", HTTP_GET, []() {
    int std = server.arg("std").toInt();
    if (std < -720 || std > 840) { server.send(400, "text/plain", "Offset must be -12:00 to +14:00"); return; }
    s_tzStd = std; s_tzDst = server.arg("dst") == "1"; tzSave();
    server.send(200, "text/plain", "Saved");
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
             "\"egt\":\"%s\",\"sim\":\"%s\",\"dht\":\"%s\",\"fuel\":%.0f,\"egtsent\":%u,\"ecu\":\"%s\",\"live\":%d,\"sent\":%u,\"cfg\":{\"mode\":%d,\"day\":%d,\"nmin\":%d,\"nmax\":%d,\"dlo\":%.1f,\"dhi\":%.1f,\"lon\":%.1f}}",
             s_lightsV, s_lightsV >= s_cfg.lightsOnV ? "true" : "false", s_dimV, s_briPct, INPUT_BUILD, WiFi.localIP().toString().c_str(),
             WiFi.channel(), s_last, egtTxt, simTxt, dhtTxt, (double)s_fuelPct, (unsigned)s_egtSent, hub_ecu_detected(), liveCount(), (unsigned)s_dataSent, s_cfg.mode, s_cfg.dayPct, s_cfg.nightMin, s_cfg.nightMax, s_cfg.dimLoV, s_cfg.dimHiV, s_cfg.lightsOnV);
    server.send(200, "application/json", b);
  });
  // ---- speed: rusEFI (hall sensor on the ECU) next to the GPS ----
  server.on("/speed.json", HTTP_GET, []() {
    char b[220];
    snprintf(b, sizeof b, "{\"ecuok\":%s,\"ecumph\":%.2f,\"gpsok\":%s,\"fix\":%s,\"gpsmph\":%.2f,\"sats\":%d,\"rmc\":%u,\"bad\":%u}",
             gauge_config_signal_has_data(SIG_VSS) ? "true" : "false", (double)g_vss_mph,
             (s_gpsLastByteMs && millis() - s_gpsLastByteMs < 2000) ? "true" : "false", s_gps.fix ? "true" : "false",
             (double)s_gps.mph, s_gps.sats, (unsigned)s_gps.rmcCount, (unsigned)s_gps.badCount);
    server.send(200, "application/json", b);
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
      if (has) p.has_mask |= 1ull << i;
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

  slotsLoad();   // custom sensor slots, before CAN starts decoding them
  bindLoad();    // button actions (/buttons)
  tzLoad();      // time zone for the GPS clock
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
  xTaskCreatePinnedToCore(gpsTask, "gps", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(canTxTask, "can_tx", 3072, NULL, 2, NULL, 1);
  rfBegin();   // CC1101 on GPIO 13/14/27/33/39; carries on without it if not wired
  xTaskCreatePinnedToCore(dataTask, "espnow_tx", 4096, NULL, 2, NULL, 0);

  if (s_wifiUp) setupWeb();
}

void loop() {
  handleButtons();
  static uint32_t lastKnob = 0;
  if (millis() - lastKnob >= 50) { lastKnob = millis(); readKnob(); }
  sim_loop();
  { static uint32_t lastTime = 0;   // GPS date/time to the screens (log names and timestamps), every 10 s
    uint32_t u = gpsUtcNow();
    if (s_wifiUp && u && millis() - lastTime > 10000) {
      lastTime = millis();
      TimePacket tp; tp.magic = TIME_MAGIC; tp.utc = u; tp.offset_min = (int16_t)tzOffsetNow(u);
      esp_now_send(BCAST, (const uint8_t *)&tp, sizeof tp);
    } }
  { static uint32_t lastNames = 0;   // custom slot names to the screens: on a change and every 10 s
    if (s_wifiUp && (s_slotsChanged || millis() - lastNames > SLOTS_MS)) { s_slotsChanged = false; lastNames = millis(); slotsSendNames(); } }
  if (s_wifiUp) server.handleClient();
  delay(2);
}
