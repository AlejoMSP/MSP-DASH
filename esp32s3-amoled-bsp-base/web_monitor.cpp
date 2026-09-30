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
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

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

static unsigned long s_lastBroadcast = 0;
static const unsigned long BROADCAST_INTERVAL_MS = 500;

void web_monitor_init(void) {
  WiFi.softAP("MSP-DASH", "turbomiata"); // SSID/password - change as desired
  Serial.print("Web monitor AP started, IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", INDEX_HTML);
  });

  server.addHandler(&ws);
  server.begin();
}

void web_monitor_loop(void) {
  unsigned long now = millis();
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
