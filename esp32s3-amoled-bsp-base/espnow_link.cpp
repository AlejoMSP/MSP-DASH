// espnow_link.cpp - see espnow_link.h.
#include "espnow_link.h"
#include "gauge_app.h"
#include "gauge_config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#define PKT_MAGIC 0x4D534431u   // "MSD1"

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint16_t seq;
  uint16_t count;          // SIG_COUNT of the sender; a remote only reads what both builds know
  uint32_t has_mask;       // bit s = signal s has data
  float v[SIG_COUNT];
} LinkPacket;

static bool s_remote = false;
static bool s_up = false;
static uint16_t s_seq = 0;
static uint32_t s_lastSendMs = 0, s_ppsWindowMs = 0, s_ppsCount = 0, s_pps = 0;
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

static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
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
  int n = p->count < SIG_COUNT ? p->count : SIG_COUNT;
  #define PUT(sig, var, has) if (sig < n && (p->has_mask >> sig & 1u)) { var = p->v[sig]; has = true; gauge_config_mark_signal_active((SignalId)(sig)); }
  PUT(SIG_RPM, g_rpm, g_rpm_has_data)
  PUT(SIG_CLT, g_clt_f, g_clt_has_data)
  PUT(SIG_MAP, g_boost_kpa, g_boost_has_data)
  PUT(SIG_AFR, g_afr, g_afr_has_data)
  PUT(SIG_OILP, g_oilp_kpa, g_oilp_has_data)
  PUT(SIG_OILT, g_oilt_f, g_oilt_has_data)
  PUT(SIG_FUELP, g_fuelp_kpa, g_fuelp_has_data)
  PUT(SIG_FLEX, g_flex_pct, g_flex_has_data)
  PUT(SIG_TPS, g_tps, g_tps_has_data)
  PUT(SIG_BATTV, g_battv, g_battv_has_data)
  #undef PUT
  uint16_t bits = 0, seen = 0;
  for (int i = 0; i < SIG_FLAG_COUNT && SIG_FIRST_FLAG + i < n; i++) {
    int s = SIG_FIRST_FLAG + i;
    if (!(p->has_mask >> s & 1u)) continue;
    seen |= (uint16_t)(1u << i);
    if (p->v[s] > 0.5f) bits |= (uint16_t)(1u << i);
    gauge_config_mark_signal_active((SignalId)s);
  }
  g_flag_bits = bits;
  g_flag_seen = seen;
}

static void clearAll(void) {   // back to "--", as when CAN has gone quiet
  g_rpm_has_data = g_clt_has_data = g_boost_has_data = g_afr_has_data = g_oilp_has_data = false;
  g_oilt_has_data = g_fuelp_has_data = g_flex_has_data = g_tps_has_data = g_battv_has_data = false;
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
  if (s_remote) {
    esp_now_register_recv_cb(onRecv);
  } else {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BCAST, 6);
    peer.channel = 0;            // whatever channel the radio is on
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }
  s_up = true;
  Serial.printf("ESP-NOW up: %s board, channel %u\n", s_remote ? "remote" : "main", s_channel);
}

void espnow_link_loop(void) {
  if (!s_up) return;
  uint32_t now = millis();
  if (now - s_ppsWindowMs >= 1000) {
    if (s_remote) { s_pps = s_rxCount - s_ppsCount; s_ppsCount = s_rxCount; }
    else { s_pps = s_ppsCount; s_ppsCount = 0; }
    s_ppsWindowMs = now;
  }

  if (!s_remote) {
    if (now - s_lastSendMs < ESPNOW_SEND_MS) return;
    s_lastSendMs = now;
    LinkPacket p;
    p.magic = PKT_MAGIC; p.seq = s_seq++; p.count = SIG_COUNT; p.has_mask = 0;
    for (int s = 0; s < SIG_COUNT; s++) {
      bool has = s != SIG_NONE && gauge_config_signal_has_data((SignalId)s);
      p.v[s] = has ? gauge_config_signal_value((SignalId)s) : 0.0f;
      if (has) p.has_mask |= 1u << s;
    }
    if (esp_now_send(BCAST, (const uint8_t *)&p, sizeof(p)) == ESP_OK) s_ppsCount++;
    return;
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
