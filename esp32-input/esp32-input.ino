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

#define INPUT_BUILD 4
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

// ---- the command packet: must match LinkCmdPacket in the screens' espnow_link.h ----
#define LINK_CMD_MAGIC 0x4D534331u   // "MSC1"
enum { LINK_CMD_PAGE_NEXT = 1, LINK_CMD_PAGE_PREV = 2, LINK_CMD_BRIGHTNESS = 3 };
typedef struct __attribute__((packed)) { uint32_t magic; uint16_t seq; uint8_t cmd; int16_t value; char target[21]; } LinkCmdPacket;

static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint16_t s_seq = 0;
static volatile uint32_t s_dataSent = 0;   // data packets sent to the screens

// ---- the hub: CAN values (written by the decoder from can_parsing.h) and the data packet the screens read ----
volatile float g_rpm, g_clt_f, g_boost_kpa, g_afr, g_oilp_kpa, g_oilt_f, g_fuelp_kpa, g_flex_pct, g_tps, g_battv, g_egt_f, g_iat_f;
volatile bool g_rpm_has_data, g_clt_has_data, g_boost_has_data, g_afr_has_data, g_oilp_has_data, g_oilt_has_data,
              g_fuelp_has_data, g_flex_has_data, g_tps_has_data, g_battv_has_data, g_egt_has_data, g_iat_has_data;
volatile uint16_t g_flag_bits, g_flag_seen;
static volatile uint32_t s_seenMs[SIG_COUNT];
#define STALE_MS 2000   // same as the screens: a signal not heard for 2 s shows "--"

extern "C" void gauge_config_mark_signal_active(SignalId sig) { if (sig > 0 && sig < SIG_COUNT) s_seenMs[sig] = millis() | 1; }
extern "C" bool gauge_config_signal_has_data(SignalId sig) {
  return sig > 0 && sig < SIG_COUNT && s_seenMs[sig] && millis() - s_seenMs[sig] < STALE_MS;
}
extern "C" float gauge_config_signal_value(SignalId sig) {
  switch (sig) {
    case SIG_RPM: return g_rpm;       case SIG_CLT: return g_clt_f;     case SIG_MAP: return g_boost_kpa;
    case SIG_AFR: return g_afr;       case SIG_OILP: return g_oilp_kpa; case SIG_OILT: return g_oilt_f;
    case SIG_FUELP: return g_fuelp_kpa; case SIG_FLEX: return g_flex_pct; case SIG_TPS: return g_tps;
    case SIG_BATTV: return g_battv;   case SIG_EGT: return g_egt_f;     case SIG_IAT: return g_iat_f;
    default: return gauge_signal_is_flag(sig) ? (((g_flag_bits >> (sig - SIG_FIRST_FLAG)) & 1u) ? 1.0f : 0.0f) : 0.0f;
  }
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
    server.send(200, "text/html", R"HTML(<html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH Input</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:480px;margin:auto;padding:12px}h2{color:#4dd0e1}
label{display:block;margin:10px 0 4px}input,select{width:100%;padding:6px;box-sizing:border-box;background:#111;color:#ddd;border:1px solid #333}
button{margin-top:10px;padding:8px 12px;background:#4dd0e1;border:0;color:#000}.live{background:#111;padding:10px;border-radius:6px}
.row{display:flex;gap:8px}.row>*{flex:1}.note{color:#999;font-size:13px}</style></head><body>
<h2>MSP-DASH Input</h2><div class="live" id="live">Loading...</div>
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
document.getElementById("live").innerHTML="Lights: "+st.lightsV.toFixed(1)+" V ("+(st.lightsOn?"ON":"off")+")<br>Dimmer: "+st.dimV.toFixed(1)+" V<br>Brightness sent: "+st.bri+" %<br>Build "+st.build+" | "+st.ip+" | channel "+st.ch+"<br>Last command: "+st.last+"<br>CAN signals live: "+st.live+" | data packets sent: "+st.sent;
if(!poll.done){ids.forEach(i=>document.getElementById(i).value=st.cfg[i]);poll.done=1;}}catch(e){}}
async function save(){const q=ids.map(i=>i+"="+encodeURIComponent(document.getElementById(i).value)).join("&");
document.getElementById("msg").textContent=await (await fetch("/save?"+q)).text();}
poll();setInterval(poll,1000);
</script></body></html>)HTML");
  });
  server.on("/state", HTTP_GET, []() {
    auto liveCount = []() { int n = 0; for (int i = 1; i < SIG_COUNT; i++) if (gauge_config_signal_has_data((SignalId)i)) n++; return n; };
    char b[520];
    snprintf(b, sizeof(b), "{\"lightsV\":%.2f,\"lightsOn\":%s,\"dimV\":%.2f,\"bri\":%d,\"build\":%d,\"ip\":\"%s\",\"ch\":%d,\"last\":\"%s\","
             "\"live\":%d,\"sent\":%u,\"cfg\":{\"mode\":%d,\"day\":%d,\"nmin\":%d,\"nmax\":%d,\"dlo\":%.1f,\"dhi\":%.1f,\"lon\":%.1f}}",
             s_lightsV, s_lightsV >= s_cfg.lightsOnV ? "true" : "false", s_dimV, s_briPct, INPUT_BUILD, WiFi.localIP().toString().c_str(),
             WiFi.channel(), s_last, liveCount(), (unsigned)s_dataSent, s_cfg.mode, s_cfg.dayPct, s_cfg.nightMin, s_cfg.nightMax, s_cfg.dimLoV, s_cfg.dimHiV, s_cfg.lightsOnV);
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

#include "can_parsing.h"   // copied in at compile time; uses the values and functions above

// Same packet as the screens' espnow_link.cpp ("MSD1"): every signal plus a has-data mask, 20 times a second
#define PKT_MAGIC 0x4D534431u
typedef struct __attribute__((packed)) { uint32_t magic; uint16_t seq; uint16_t count; uint32_t has_mask; float v[SIG_COUNT]; } LinkPacket;
static void dataTask(void *) {
  uint16_t seq = 0;
  for (;;) {
    LinkPacket p; p.magic = PKT_MAGIC; p.seq = seq++; p.count = SIG_COUNT; p.has_mask = 0;
    for (int i = 0; i < SIG_COUNT; i++) {
      bool has = i != SIG_NONE && gauge_config_signal_has_data((SignalId)i);
      p.v[i] = has ? gauge_config_signal_value((SignalId)i) : 0.0f;
      if (has) p.has_mask |= 1u << i;
    }
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

  // CAN: the hub is now the only CAN reader; the screens are all remotes
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  g.rx_queue_len = 64;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK || twai_start() != ESP_OK) Serial.println("CAN start FAILED");
  else { can_start_task(); Serial.println("CAN running on GPIO 21 (TX) / 22 (RX)"); }
  xTaskCreatePinnedToCore(dataTask, "espnow_tx", 4096, NULL, 2, NULL, 0);

  if (s_wifiUp) setupWeb();
}

void loop() {
  if (pressed(s_next)) sendCmd(LINK_CMD_PAGE_NEXT, 0, "");
  if (pressed(s_prev)) sendCmd(LINK_CMD_PAGE_PREV, 0, "");
  static uint32_t lastKnob = 0;
  if (millis() - lastKnob >= 50) { lastKnob = millis(); readKnob(); }
  if (s_wifiUp) server.handleClient();
  delay(2);
}
