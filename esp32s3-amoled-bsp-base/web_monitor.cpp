/*
  web_monitor.cpp
  Stage 1 of the wireless-configuration plan: WiFi AP + a live gauge
  monitoring webpage. No configuration/control yet - just a read-only
  view of the same values driving the physical gauges, broadcast over
  WebSocket every 500ms.

  Uses the same "ESP Async WebServer" + "Async TCP" libraries already
  proven working on the WROOM board (esp32-main) elsewhere in this
  project - same general pattern (AsyncWebServer + AsyncWebSocket +
  broadcast JSON), adapted for this board's 7 sensor values instead of
  the WROOM board's 74 gauges.

  WiFi mode: AP (access point) rather than joining an existing network -
  more practical for an automotive/garage context where a reliable home
  WiFi isn't always in range. Connect your phone directly to this
  network, then browse to the board's AP IP (192.168.4.1 by default).
*/

#include "web_monitor.h"
#include "gauge_app.h"
#include "ecu_config.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <string.h>
#include "wifi_config.h" // local, git-ignored - see wifi_config.h.example

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MSP-DASH Monitor</title>
  <style>
    body { background:#000; color:#fff; font-family: sans-serif; margin:0; padding:20px; }
    h1 { font-size: 1.2em; text-align:center; }
    .grid { display:grid; grid-template-columns: 1fr 1fr; gap:16px; margin-top:20px; }
    .cell { background:#111; border-radius:8px; padding:16px; text-align:center; }
    .label { font-size:0.9em; color:#4dd0e1; }
    .value { font-size:2em; font-weight:bold; margin-top:6px; }
    .warn { color:#ff4444; }
  </style>
</head>
<body>
  <h1>MSP-DASH Live Monitor</h1>
  <p style="text-align:center"><a href="/can" style="color:#4dd0e1">Raw CAN Monitor &rarr;</a></p>
  <div style="text-align:center; margin-top:10px;">
    <label>ECU: <select id="ecuSelect">
      <option value="0">rusEFI</option>
      <option value="1">MegaSquirt</option>
      <option value="2">LinkECU</option>
    </select></label>
    <button onclick="saveEcu()">Save</button>
  </div>
  <div class="grid">
    <div class="cell"><div class="label">CLT (F)</div><div class="value" id="clt">--</div></div>
    <div class="cell"><div class="label">MAP (kPa)</div><div class="value" id="map">--</div></div>
    <div class="cell"><div class="label">AFR</div><div class="value" id="afr">--</div></div>
    <div class="cell"><div class="label">OIL PRESS (kPa)</div><div class="value" id="oilp">--</div></div>
    <div class="cell"><div class="label">OIL TEMP (F)</div><div class="value" id="oilt">--</div></div>
    <div class="cell"><div class="label">FUEL PRESS (kPa)</div><div class="value" id="fuelp">--</div></div>
    <div class="cell"><div class="label">ETH %</div><div class="value" id="flex">--</div></div>
  </div>
  <script>
    fetch("/ecu").then(r => r.json()).then(d => { document.getElementById("ecuSelect").value = d.ecu; });
    function saveEcu() {
      const val = document.getElementById("ecuSelect").value;
      fetch("/ecu", { method: "POST", headers: {"Content-Type":"application/x-www-form-urlencoded"}, body: "ecu=" + val });
    }
    const ws = new WebSocket("ws://" + location.host + "/ws");
    ws.onmessage = (event) => {
      const d = JSON.parse(event.data);
      document.getElementById("clt").textContent = d.clt.toFixed(0);
      document.getElementById("map").textContent = d.map.toFixed(0);
      document.getElementById("afr").textContent = d.afr.toFixed(1);
      document.getElementById("oilp").textContent = d.oilp.toFixed(0);
      document.getElementById("oilt").textContent = d.oilt.toFixed(0);
      document.getElementById("fuelp").textContent = d.fuelp.toFixed(0);
      document.getElementById("flex").textContent = d.flex.toFixed(0);
    };
  </script>
</body>
</html>
)rawliteral";

static const char CAN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MSP-DASH Raw CAN Monitor</title>
  <style>
    body { background:#000; color:#fff; font-family: monospace; margin:0; padding:20px; }
    h1 { font-size: 1.2em; text-align:center; font-family: sans-serif; }
    table { width:100%; border-collapse: collapse; margin-top:16px; }
    th, td { text-align:left; padding:6px 10px; border-bottom:1px solid #333; }
    th { color:#4dd0e1; font-family: sans-serif; }
  </style>
</head>
<body>
  <h1>Raw CAN Monitor</h1>
  <p style="text-align:center"><a href="/" style="color:#4dd0e1">&larr; Back to gauges</a></p>
  <table>
    <thead><tr><th>ID (hex)</th><th>DLC</th><th>Data (hex)</th><th>Hz</th><th>Last seen (ms ago)</th></tr></thead>
    <tbody id="rows"></tbody>
  </table>
  <script>
    async function refresh() {
      const res = await fetch("/can.json");
      const rows = await res.json();
      const now = Date.now();
      const tbody = document.getElementById("rows");
      tbody.innerHTML = rows.map(r =>
        `<tr><td>0x${r.id.toString(16).toUpperCase().padStart(3,'0')}</td><td>${r.dlc}</td><td>${r.data}</td><td>${r.hz}</td><td>${r.ago}</td></tr>`
      ).join("");
    }
    setInterval(refresh, 1000);
    refresh();
  </script>
</body>
</html>
)rawliteral";

#define MAX_CAN_IDS 32
typedef struct {
  uint32_t id;
  uint8_t data[8];
  uint8_t dlc;
  unsigned long lastSeen;
  uint32_t countSinceLastCalc;
  uint32_t hz;
} CanMonitorEntry;

static CanMonitorEntry s_canTable[MAX_CAN_IDS];
static int s_canTableCount = 0;
static unsigned long s_lastHzCalc = 0;

static unsigned long s_lastBroadcast = 0;
static const unsigned long BROADCAST_INTERVAL_MS = 500;

void web_monitor_track_can_frame(uint32_t id, uint8_t *data, uint8_t dlc) {
  for (int i = 0; i < s_canTableCount; i++) {
    if (s_canTable[i].id == id) {
      memcpy(s_canTable[i].data, data, dlc);
      s_canTable[i].dlc = dlc;
      s_canTable[i].lastSeen = millis();
      s_canTable[i].countSinceLastCalc++;
      return;
    }
  }
  if (s_canTableCount < MAX_CAN_IDS) {
    CanMonitorEntry *e = &s_canTable[s_canTableCount];
    e->id = id;
    memcpy(e->data, data, dlc);
    e->dlc = dlc;
    e->lastSeen = millis();
    e->countSinceLastCalc = 1;
    e->hz = 0;
    s_canTableCount++;
  }
  // If the table is full, new/unseen IDs are silently dropped - 32 is
  // generous headroom for a typical ECU broadcast set, and this is a
  // diagnostic tool, not a requirement for correctness elsewhere.
}

static String buildCanJson() {
  unsigned long now = millis();
  String json = "[";
  for (int i = 0; i < s_canTableCount; i++) {
    if (i > 0) json += ",";
    CanMonitorEntry *e = &s_canTable[i];
    char dataHex[25] = {0};
    for (int b = 0; b < e->dlc; b++) {
      char byteStr[4];
      snprintf(byteStr, sizeof(byteStr), "%02X ", e->data[b]);
      strcat(dataHex, byteStr);
    }
    json += "{\"id\":" + String(e->id) + ",\"dlc\":" + String(e->dlc) +
            ",\"data\":\"" + String(dataHex) + "\",\"hz\":" + String(e->hz) +
            ",\"ago\":" + String(now - e->lastSeen) + "}";
  }
  json += "]";
  return json;
}

void web_monitor_init(void) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to WiFi network \"%s\"", WIFI_SSID);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("WiFi connected. IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi connection FAILED - web monitor will not be reachable.");
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/can", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", CAN_HTML);
  });

  server.on("/can.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", buildCanJson());
  });

  server.on("/ecu", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"ecu\":" + String((int)ecu_config_get()) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/ecu", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ecu", true)) {
      int val = request->getParam("ecu", true)->value().toInt();
      ecu_config_set((EcuSelection)val);
    }
    request->send(200, "text/plain", "OK");
  });

  server.addHandler(&ws);
  server.begin();
}

void web_monitor_loop(void) {
  unsigned long now = millis();

  // Recompute Hz for each tracked CAN ID once per second.
  if (now - s_lastHzCalc >= 1000) {
    s_lastHzCalc = now;
    for (int i = 0; i < s_canTableCount; i++) {
      s_canTable[i].hz = s_canTable[i].countSinceLastCalc;
      s_canTable[i].countSinceLastCalc = 0;
    }
  }

  if (now - s_lastBroadcast < BROADCAST_INTERVAL_MS) return;
  s_lastBroadcast = now;

  ws.cleanupClients();
  if (ws.count() == 0) return; // no connected clients - skip building/sending JSON

  char json[256];
  snprintf(json, sizeof(json),
    "{\"clt\":%.1f,\"map\":%.1f,\"afr\":%.2f,\"oilp\":%.1f,\"oilt\":%.1f,\"fuelp\":%.1f,\"flex\":%.1f}",
    (double)g_clt_f, (double)g_boost_kpa, (double)g_afr, (double)g_oilp_kpa,
    (double)g_oilt_f, (double)g_fuelp_kpa, (double)g_flex_pct);
  ws.textAll(json);
}
