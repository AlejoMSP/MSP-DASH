// espnow_link.cpp - see espnow_link.h.
#include "simulator.h"
#include "screen_clock.h"
#include "gps_time.h"
#include "button_actions.h"
#include "color_presets.h"
#include "sd_storage.h"
#include "sd_logging.h"
#include "espnow_link.h"
#include "gauge_app.h"
#include "gauge_config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>
#include "build_info.h"
#include "lcd_perf.h"

#include "link_packets.h"   // HelloPacket, HELLO_MAGIC, HELLO_MS
static_assert(LINK_NAME_LEN == ESPNOW_NAME_LEN, "hello name length must match the board name length");

typedef struct { uint8_t mac[6]; uint32_t ip; uint16_t build; char name[ESPNOW_NAME_LEN + 1]; uint32_t lastMs; bool used; } BoardSlot;
static BoardSlot s_boards[ESPNOW_MAX_BOARDS];
static uint32_t s_lastHelloMs = 0;


static bool s_remote = false;
static bool s_up = false;
static uint16_t s_seq = 0;
static uint32_t s_ppsWindowMs = 0, s_ppsCount = 0, s_pps = 0;
static uint8_t s_channel = 0;

// written by the receive callback (WiFi task), read in loop()
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static LinkPacket s_rx;
static volatile bool s_rxNew = false;
static volatile uint32_t s_rxCount = 0;
static uint32_t s_lastRxMs = 0;
static bool s_everRx = false, s_rxLive = false;
static uint8_t s_peer[6];

static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static volatile uint32_t s_txCount = 0;
static uint32_t s_txSeen = 0;

// Main board: send from its own task, so a slow screen redraw in loop() can't hold packets back (it did: 12/s instead of 50).
static void sendTask(void *) {
  for (;;) {
    LinkPacket p;
    p.magic = PKT_MAGIC; p.seq = s_seq++; p.count = SIG_COUNT; p.has_mask = 0;
    for (int s = 0; s < SIG_COUNT; s++) {
      bool has = s != SIG_NONE && gauge_config_signal_has_data((SignalId)s);
      p.v[s] = has ? gauge_config_signal_value((SignalId)s) : 0.0f;
      if (has) p.has_mask |= 1ull << s;
    }
    if (esp_now_send(BCAST, (const uint8_t *)&p, sizeof(p)) == ESP_OK) s_txCount++;
    vTaskDelay(pdMS_TO_TICKS(ESPNOW_SEND_MS));
  }
}

// Colour preset in use (hub "Color scheme" button); the cell colours themselves are saved with the config.
static int s_colorPreset = -1;
static volatile bool s_colorSavePending = false;

// Commands from the input board; every copy (it sends each 3 times) after the first is ignored by its seq
static volatile uint32_t s_lastCmdSeq = 0xFFFFFFFFu;
static bool handleCmd(const uint8_t *data, int len) {
  if (len < (int)offsetof(LinkCmdPacket, target) || ((const LinkCmdPacket *)data)->magic != LINK_CMD_MAGIC) return false;
  LinkCmdPacket c; memset(&c, 0, sizeof(c)); memcpy(&c, data, len < (int)sizeof(c) ? len : sizeof(c)); c.target[20] = 0;
  if (c.seq == s_lastCmdSeq) return true;
  s_lastCmdSeq = c.seq;
  if (c.target[0]) { char nm[ESPNOW_NAME_LEN + 1]; espnow_link_name(nm); if (strcasecmp(nm, c.target)) return true; }
  if (c.cmd == LINK_CMD_PAGE_NEXT) g_page_nav_delta = 1;
  else if (c.cmd == LINK_CMD_PAGE_PREV) g_page_nav_delta = -1;
  else if (c.cmd == LINK_CMD_BRIGHTNESS) { if (c.value != lcd_get_brightness_pct()) lcd_set_brightness_pct(c.value); }
  else if (c.cmd == LINK_CMD_SELECT) gauge_app_popup(c.target[0] ? "Buttons: this screen" : "Buttons: all screens");
  else if (c.cmd == LINK_CMD_ALERT_ACK) g_alert_ack_req = 1;
  else if (c.cmd == LINK_CMD_GOTO_PAGE) { if (c.value >= 1) g_requested_page = c.value - 1; }
  else if (c.cmd == LINK_CMD_COLOR) {
    int p = c.value;
    if (p == COLOR_PRESET_CYCLE) p = (s_colorPreset + 1) % COLOR_PRESET_COUNT;
    if (p >= 0 && p < COLOR_PRESET_COUNT) { s_colorPreset = p; g_color_preset_req = p; s_colorSavePending = true; }
  }
  else if (c.cmd == LINK_CMD_RACE) { g_race_req = c.value ? 1 : 0; sd_logging_force(c.value != 0); }
  else if (c.cmd == LINK_CMD_HINT) {
    int a = (c.value >> 8) & 0xFF, prm = c.value & 0xFF;
    char t[48];
    if (a == BA_GOTO_PAGE || a == BA_RACE) snprintf(t, sizeof t, "Release: %s %d", BA_NAMES[a], prm);
    else if (a == BA_COLOR && prm < COLOR_PRESET_COUNT) snprintf(t, sizeof t, "Release: colors %d", prm + 1);
    else snprintf(t, sizeof t, "Release: %s", a < BA_COUNT ? BA_NAMES[a] : "?");
    gauge_app_popup(t);
  }
  else if (c.cmd == LINK_CMD_SHOW_IP) {   // this screen's name and address (and the hub's), 5 s
    char t[48], nm[ESPNOW_NAME_LEN + 1];
    espnow_link_name(nm);
    snprintf(t, sizeof t, "%s\n%s\nhub .%d", nm, WiFi.localIP().toString().c_str(), c.value);
    gauge_app_popup_ms(t, 5000);
  }
  else if (c.cmd == LINK_CMD_LOG_MARK) {
    char t[40];
    if (sd_storage_is_mounted()) snprintf(t, sizeof t, sd_logging_mark(c.value) ? "Marker %d logged" : "Marker %d: not logging", c.value);
    else snprintf(t, sizeof t, "Marker %d", c.value);
    gauge_app_popup(t);
  }
  return true;
}

static void onHello(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (custom_slots_on_packet(data, len)) return;   // custom sensor names from the hub
  if (len >= (int)sizeof(TimePacket) && ((const TimePacket *)data)->magic == TIME_MAGIC) {   // the hub's GPS clock
    const TimePacket *tp = (const TimePacket *)data; screen_clock_set(tp->utc, tp->offset_min); return;
  }
  if (handleCmd(data, len)) return;
  if (len < (int)offsetof(HelloPacket, name)) return;
  HelloPacket hb; memset(&hb, 0, sizeof(hb)); memcpy(&hb, data, len < (int)sizeof(hb) ? len : sizeof(hb));
  const HelloPacket *h = &hb;
  if (h->magic != HELLO_MAGIC) return;
  portENTER_CRITICAL(&s_mux);
  int slot = -1, oldest = 0;
  for (int i = 0; i < ESPNOW_MAX_BOARDS; i++) {
    if (s_boards[i].used && !memcmp(s_boards[i].mac, info->src_addr, 6)) { slot = i; break; }
    if (slot < 0 && !s_boards[i].used) slot = i;
  }
  if (slot < 0) {   // full: replace the one heard longest ago
    for (int i = 1; i < ESPNOW_MAX_BOARDS; i++) if (s_boards[i].lastMs < s_boards[oldest].lastMs) oldest = i;
    slot = oldest;
  }
  memcpy(s_boards[slot].mac, info->src_addr, 6);
  s_boards[slot].ip = h->ip; s_boards[slot].build = h->build;
  memcpy(s_boards[slot].name, h->name, ESPNOW_NAME_LEN); s_boards[slot].name[ESPNOW_NAME_LEN] = 0; s_boards[slot].lastMs = millis(); s_boards[slot].used = true;
  portEXIT_CRITICAL(&s_mux);
}

static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (handleCmd(data, len)) return;
  if (custom_slots_on_packet(data, len)) return;   // custom sensor names from the hub
  if (len >= (int)sizeof(TimePacket) && ((const TimePacket *)data)->magic == TIME_MAGIC) {
    const TimePacket *tp = (const TimePacket *)data; screen_clock_set(tp->utc, tp->offset_min); return;
  }
  if (len < (int)offsetof(LinkPacket, v) || ((const LinkPacket *)data)->magic != PKT_MAGIC) return;
  portENTER_CRITICAL(&s_mux);
  memset(&s_rx, 0, sizeof(s_rx));
  memcpy(&s_rx, data, len < (int)sizeof(s_rx) ? len : sizeof(s_rx));
  memcpy(s_peer, info->src_addr, 6);
  s_rxNew = true;
  s_rxCount++;
  portEXIT_CRITICAL(&s_mux);
}

// Writes the received values exactly the way the CAN decoder would.
static void applyPacket(const LinkPacket *p) {
  if (p->has_mask & LINK_MASK_SIMULATED) sim_note_remote();   // the hub's simulator is running: show SIMULATED, do not log
  int n = p->count < SIG_COUNT ? p->count : SIG_COUNT;
  #define PUT(sig, var, has) if (sig < n && (p->has_mask >> sig & 1ull)) { var = p->v[sig]; has = true; gauge_config_mark_signal_active((SignalId)(sig)); }
  PUT(SIG_RPM, g_rpm, g_rpm_has_data)
  PUT(SIG_CLT, g_clt_f, g_clt_has_data)
  PUT(SIG_MAP, g_boost_psi, g_boost_has_data)
  PUT(SIG_AFR, g_afr, g_afr_has_data)
  PUT(SIG_OILP, g_oilp_psi, g_oilp_has_data)
  PUT(SIG_OILT, g_oilt_f, g_oilt_has_data)
  PUT(SIG_FUELP, g_fuelp_psi, g_fuelp_has_data)
  PUT(SIG_FLEX, g_flex_pct, g_flex_has_data)
  PUT(SIG_TPS, g_tps, g_tps_has_data)
  PUT(SIG_BATTV, g_battv, g_battv_has_data)
  PUT(SIG_EGT, g_egt_f, g_egt_has_data)
  PUT(SIG_IAT, g_iat_f, g_iat_has_data)
  PUT(SIG_AMBT, g_ambt_f, g_ambt_has_data)
  PUT(SIG_HUM, g_hum_pct, g_hum_has_data)
  PUT(SIG_FUEL, g_fuel_pct, g_fuel_has_data)
  PUT(SIG_VSS, g_vss_mph, g_vss_has_data)
  PUT(SIG_GPS_SPD, g_gps_mph, g_gps_has_data)
  PUT(SIG_GPS_SATS, g_gps_sats, g_sats_has_data)
  #undef PUT
  uint16_t bits = 0, seen = 0;
  for (int i = 0; i < SIG_FLAG_COUNT && SIG_FIRST_FLAG + i < n; i++) {
    int s = SIG_FIRST_FLAG + i;
    if (!(p->has_mask >> s & 1ull)) continue;
    seen |= (uint16_t)(1u << i);
    if (p->v[s] > 0.5f) bits |= (uint16_t)(1u << i);
    gauge_config_mark_signal_active((SignalId)s);
  }
  g_flag_bits = bits;
  g_flag_seen = seen;
  for (int i = 0; i < CUSTOM_SLOTS; i++) {   // the custom slots (the hub's /custom page)
    int s = SIG_C1 + i;
    if (s >= n || !(p->has_mask >> s & 1ull)) continue;
    g_custom_val[i] = p->v[s]; g_custom_has[i] = true;
    gauge_config_mark_signal_active((SignalId)s);
  }
}

static void clearAll(void) {   // back to "--", as when CAN has gone quiet
  g_rpm_has_data = g_clt_has_data = g_boost_has_data = g_afr_has_data = g_oilp_has_data = false;
  g_oilt_has_data = g_fuelp_has_data = g_flex_has_data = g_tps_has_data = g_battv_has_data = false;
  g_egt_has_data = g_iat_has_data = g_ambt_has_data = g_hum_has_data = g_fuel_has_data = g_vss_has_data = g_gps_has_data = g_sats_has_data = false;
  for (int i = 0; i < CUSTOM_SLOTS; i++) g_custom_has[i] = false;
  g_flag_seen = 0;
}

void espnow_link_init(void) {
  espnow_link_is_remote();   // loads the role
  if (WiFi.status() != WL_CONNECTED) {
    // No network (in the car): stop the WiFi search, which hops channels and would break ESP-NOW, and park on one channel.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CAR_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
  }
  wifi_second_chan_t sc; esp_wifi_get_channel(&s_channel, &sc);

  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init FAILED"); return; }
  esp_now_peer_info_t peer = {};   // both roles send broadcasts: main its values, remote its hello
  memcpy(peer.peer_addr, BCAST, 6);
  peer.channel = 0;                // whatever channel the radio is on
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
  if (s_remote) {
    esp_now_register_recv_cb(onRecv);
  } else {
    esp_now_register_recv_cb(onHello);
    xTaskCreatePinnedToCore(sendTask, "espnow_tx", 4096, NULL, 2, NULL, 0);
  }
  s_up = true;
  Serial.printf("ESP-NOW up: %s board, channel %u\n", s_remote ? "remote" : "main", s_channel);
}

void espnow_link_loop(void) {
  if (s_colorSavePending && s_colorPreset >= 0) {   // outside the radio callback: write the new colours to the config
    s_colorSavePending = false;
    const ColorPreset *cp = &COLOR_PRESETS_C[s_colorPreset];
    PageConfig *pg = gauge_config_get_pages();
    for (int p = 0; p < MAX_PAGES; p++) for (int k = 0; k < MAX_CELLS_PER_PAGE; k++) {
      CellConfig *cc = &pg[p].cells[k];
      cc->needle_color = cp->needle; cc->tick_color = cp->tick; cc->bg_color = cp->bg;
    }
    gauge_config_save();
  }
  if (!s_up) return;
  custom_slots_loop();
  uint32_t now = millis();
  if (now - s_ppsWindowMs >= 1000) {
    if (s_remote) { s_pps = s_rxCount - s_ppsCount; s_ppsCount = s_rxCount; }
    else { uint32_t t = s_txCount; s_pps = t - s_txSeen; s_txSeen = t; }
    s_ppsWindowMs = now;
  }

  if (!s_remote) return;   // sending runs in sendTask()

  if (now - s_lastHelloMs >= HELLO_MS) {
    s_lastHelloMs = now;
    HelloPacket h = { HELLO_MAGIC, (uint32_t)WiFi.localIP(), (uint16_t)FW_BUILD_NUMBER, {0} };
    char nm[ESPNOW_NAME_LEN + 1]; espnow_link_name(nm); memcpy(h.name, nm, ESPNOW_NAME_LEN);
    esp_now_send(BCAST, (const uint8_t *)&h, sizeof(h));
  }

  if (s_rxNew) {
    LinkPacket p;
    portENTER_CRITICAL(&s_mux);
    p = s_rx; s_rxNew = false;
    portEXIT_CRITICAL(&s_mux);
    applyPacket(&p);
    s_lastRxMs = now; s_everRx = true; s_rxLive = true;
  } else if (s_rxLive && now - s_lastRxMs > ESPNOW_TIMEOUT_MS) {
    clearAll();
    s_rxLive = false;
  }
}

bool espnow_link_is_remote(void) {
  static bool loaded = false;
  if (!loaded) { Preferences pr; pr.begin("link", true); s_remote = pr.getBool("remote", false); pr.end(); loaded = true; }
  return s_remote;
}

void espnow_link_set_remote(bool remote) {
  Preferences pr;
  pr.begin("link", false);
  pr.putBool("remote", remote);
  pr.end();
}

uint32_t espnow_link_pps(void) { return s_pps; }
int32_t espnow_link_last_ms(void) { return s_everRx ? (int32_t)(millis() - s_lastRxMs) : -1; }
uint8_t espnow_link_channel(void) { return s_channel; }
void espnow_link_peer_mac(char out[18]) {
  if (!s_everRx) { strcpy(out, "--"); return; }
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", s_peer[0], s_peer[1], s_peer[2], s_peer[3], s_peer[4], s_peer[5]);
}

int espnow_link_boards(LinkBoard out[ESPNOW_MAX_BOARDS]) {
  BoardSlot b[ESPNOW_MAX_BOARDS];
  portENTER_CRITICAL(&s_mux);
  memcpy(b, s_boards, sizeof(b));
  portEXIT_CRITICAL(&s_mux);
  int n = 0; uint32_t now = millis();
  for (int i = 0; i < ESPNOW_MAX_BOARDS; i++) {
    if (!b[i].used) continue;
    LinkBoard &o = out[n++];
    snprintf(o.mac, sizeof(o.mac), "%02X:%02X:%02X:%02X:%02X:%02X", b[i].mac[0], b[i].mac[1], b[i].mac[2], b[i].mac[3], b[i].mac[4], b[i].mac[5]);
    snprintf(o.ip, sizeof(o.ip), "%u.%u.%u.%u", (unsigned)(b[i].ip & 0xFF), (unsigned)(b[i].ip >> 8 & 0xFF), (unsigned)(b[i].ip >> 16 & 0xFF), (unsigned)(b[i].ip >> 24));
    memcpy(o.name, b[i].name, sizeof(o.name));
    o.build = b[i].build; o.age_ms = (int32_t)(now - b[i].lastMs);
  }
  return n;
}

void espnow_link_name(char out[ESPNOW_NAME_LEN + 1]) {
  Preferences pr; pr.begin("link", true);
  String n = pr.getString("name", "");
  pr.end();
  if (!n.length()) n = espnow_link_is_remote() ? "Remote" : "Main";
  strncpy(out, n.c_str(), ESPNOW_NAME_LEN); out[ESPNOW_NAME_LEN] = 0;
}

void espnow_link_set_name(const char *name) {
  char t[ESPNOW_NAME_LEN + 1]; strncpy(t, name ? name : "", ESPNOW_NAME_LEN); t[ESPNOW_NAME_LEN] = 0;
  Preferences pr; pr.begin("link", false); pr.putString("name", t); pr.end();
}
