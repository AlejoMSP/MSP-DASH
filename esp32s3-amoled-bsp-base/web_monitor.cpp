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
#include "gauge_config.h"
#include "ecu_config.h"
#include "sd_storage.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include "wifi_config.h" // local, git-ignored - see wifi_config.h.example

#define SD_MOUNT_POINT "/sd_card"

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
  <p style="text-align:center">
    <a href="/can" style="color:#4dd0e1">Raw CAN Monitor &rarr;</a> &nbsp;|&nbsp;
    <a href="/gauges" style="color:#4dd0e1">Gauge Config &rarr;</a> &nbsp;|&nbsp;
    <a href="/sd" style="color:#4dd0e1">SD Card &rarr;</a>
  </p>
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

static const char GAUGES_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MSP-DASH Gauge Config</title>
  <style>
    body { background:#000; color:#fff; font-family: sans-serif; margin:0; padding:20px; }
    h1 { font-size: 1.2em; text-align:center; }
    .slot { background:#111; border-radius:8px; padding:12px; margin-bottom:10px; }
    .slot label { display:block; font-size:0.8em; color:#4dd0e1; margin-top:6px; }
    .slot input, .slot select { width:100%; box-sizing:border-box; padding:4px; margin-top:2px; }
    .row2 { display:flex; gap:10px; }
    .row2 > div { flex:1; }
    button { width:100%; padding:14px; font-size:1.1em; background:#4dd0e1; color:#000; border:none; border-radius:6px; margin-top:16px; }
    #status { text-align:center; margin-top:10px; color:#4dd0e1; }
  </style>
</head>
<body>
  <h1>Gauge Configuration</h1>
  <p style="text-align:center"><a href="/" style="color:#4dd0e1">&larr; Back to monitor</a></p>
  <div id="slots"></div>
  <button onclick="save()">Save (reboot required to apply)</button>
  <div id="status"></div>
  <script>
    const SIGNALS = ["None","RPM","Coolant Temp","MAP/Boost","AFR","Oil Pressure","Oil Temp","Fuel Pressure","Ethanol %","Throttle Position","Battery Voltage"];
    let slots = [];

    function renderSlots() {
      const container = document.getElementById("slots");
      container.innerHTML = slots.map((s, i) => `
        <div class="slot">
          <label>Signal
            <select onchange="slots[${i}].signal=parseInt(this.value)">
              ${SIGNALS.map((name, v) => `<option value="${v}" ${s.signal===v?"selected":""}>${name}</option>`).join("")}
            </select>
          </label>
          <label><input type="checkbox" ${s.enabled?"checked":""} onchange="slots[${i}].enabled=this.checked"> Enabled</label>
          <label>Caption <input type="text" value="${s.caption}" onchange="slots[${i}].caption=this.value"></label>
          <div class="row2">
            <div><label>Min <input type="number" value="${s.min}" onchange="slots[${i}].min=parseInt(this.value)"></label></div>
            <div><label>Max <input type="number" value="${s.max}" onchange="slots[${i}].max=parseInt(this.value)"></label></div>
            <div><label>Rest <input type="number" value="${s.rest}" onchange="slots[${i}].rest=parseInt(this.value)"></label></div>
          </div>
          <label>Needle Color <input type="color" value="#${s.color.toString(16).padStart(6,'0')}" onchange="slots[${i}].color=parseInt(this.value.substring(1),16)"></label>
          <label>Tick/Dial Color <input type="color" value="#${s.tick_color.toString(16).padStart(6,'0')}" onchange="slots[${i}].tick_color=parseInt(this.value.substring(1),16)"></label>
          <label>Face/Background Color <input type="color" value="#${s.bg_color.toString(16).padStart(6,'0')}" onchange="slots[${i}].bg_color=parseInt(this.value.substring(1),16)"></label>
          <label><input type="checkbox" ${s.has_warning?"checked":""} onchange="slots[${i}].has_warning=this.checked"> Warning enabled</label>
          <div class="row2">
            <div><label>Threshold <input type="number" value="${s.warning_threshold}" onchange="slots[${i}].warning_threshold=parseFloat(this.value)"></label></div>
            <div><label>Direction
              <select onchange="slots[${i}].warning_above=(this.value==='1')">
                <option value="1" ${s.warning_above?"selected":""}>Above</option>
                <option value="0" ${!s.warning_above?"selected":""}>Below</option>
              </select>
            </label></div>
          </div>
        </div>
      `).join("");
    }

    async function load() {
      const res = await fetch("/gauges.json");
      slots = await res.json();
      renderSlots();
    }

    async function save() {
      document.getElementById("status").textContent = "Saving...";
      await fetch("/gauges", { method: "POST", headers: {"Content-Type":"application/json"}, body: JSON.stringify(slots) });
      document.getElementById("status").textContent = "Saved. Reboot the board to apply changes.";
    }

    load();
  </script>
</body>
</html>
)rawliteral";

static const char SD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MSP-DASH SD Card</title>
  <style>
    body { background:#000; color:#fff; font-family: sans-serif; margin:0; padding:20px; }
    h1 { font-size: 1.2em; text-align:center; }
    table { width:100%; border-collapse: collapse; margin-top:16px; }
    th, td { text-align:left; padding:8px 10px; border-bottom:1px solid #333; }
    th { color:#4dd0e1; }
    a.dl { color:#4dd0e1; text-decoration:none; }
    #uploadForm { margin-top:20px; background:#111; padding:16px; border-radius:8px; }
    button { padding:10px 16px; background:#4dd0e1; color:#000; border:none; border-radius:6px; }
  </style>
</head>
<body>
  <h1>SD Card Files</h1>
  <p style="text-align:center"><a href="/" style="color:#4dd0e1">&larr; Back to monitor</a></p>
  <table>
    <thead><tr><th>File</th><th>Size</th><th></th></tr></thead>
    <tbody id="rows"></tbody>
  </table>
  <form id="uploadForm">
    <input type="file" id="fileInput">
    <button type="submit">Upload to SD Card</button>
    <div id="uploadStatus"></div>
  </form>
  <script>
    async function refresh() {
      const res = await fetch("/sd/list");
      const files = await res.json();
      document.getElementById("rows").innerHTML = files.map(f =>
        `<tr><td>${f.name}</td><td>${f.size}</td><td><a class="dl" href="/sd/download?file=${encodeURIComponent(f.name)}">Download</a></td></tr>`
      ).join("");
    }
    document.getElementById("uploadForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const file = document.getElementById("fileInput").files[0];
      if (!file) return;
      const status = document.getElementById("uploadStatus");
      status.textContent = "Uploading...";
      const formData = new FormData();
      formData.append("file", file);
      await fetch("/sd/upload", { method: "POST", body: formData });
      status.textContent = "Upload complete.";
      refresh();
    });
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

  server.on("/gauges", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", GAUGES_HTML);
  });

  server.on("/gauges.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    GaugeSlotConfig *slots = gauge_config_get_slots();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < MAX_GAUGE_SLOTS; i++) {
      JsonObject o = arr.add<JsonObject>();
      o["signal"] = (int)slots[i].signal;
      o["enabled"] = slots[i].enabled;
      o["min"] = slots[i].min_val;
      o["max"] = slots[i].max_val;
      o["rest"] = slots[i].rest_val;
      o["caption"] = slots[i].caption;
      o["color"] = slots[i].needle_color;
      o["tick_color"] = slots[i].tick_color;
      o["bg_color"] = slots[i].bg_color;
      o["has_warning"] = slots[i].has_warning;
      o["warning_threshold"] = slots[i].warning_threshold;
      o["warning_above"] = slots[i].warning_above;
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  // POST body arrives in chunks - accumulate into a static buffer, parse
  // once the full body has been received (index + len == total).
  server.on("/gauges", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      request->send(200, "text/plain", "OK - reboot to apply");
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      static char bodyBuf[4096];
      if (index == 0) memset(bodyBuf, 0, sizeof(bodyBuf));
      if (index + len < sizeof(bodyBuf)) {
        memcpy(bodyBuf + index, data, len);
      }
      if (index + len == total) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, bodyBuf);
        if (!err) {
          GaugeSlotConfig *slots = gauge_config_get_slots();
          JsonArray arr = doc.as<JsonArray>();
          int i = 0;
          for (JsonObject o : arr) {
            if (i >= MAX_GAUGE_SLOTS) break;
            slots[i].signal = (SignalId)(int)o["signal"];
            slots[i].enabled = o["enabled"];
            slots[i].min_val = o["min"];
            slots[i].max_val = o["max"];
            slots[i].rest_val = o["rest"];
            strlcpy(slots[i].caption, o["caption"] | "", GAUGE_CAPTION_MAXLEN);
            slots[i].needle_color = (uint32_t)(long)o["color"];
            slots[i].tick_color = (uint32_t)(long)o["tick_color"];
            slots[i].bg_color = (uint32_t)(long)o["bg_color"];
            slots[i].has_warning = o["has_warning"];
            slots[i].warning_threshold = o["warning_threshold"];
            slots[i].warning_above = o["warning_above"];
            i++;
          }
          gauge_config_save();
        }
      }
    }
  );

  server.on("/sd", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", SD_HTML);
  });

  server.on("/sd/list", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    if (sd_storage_is_mounted()) {
      DIR *dir = opendir(SD_MOUNT_POINT);
      if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
          if (entry->d_type != DT_REG) continue; // regular files only
          char fullpath[300];
          snprintf(fullpath, sizeof(fullpath), "%s/%s", SD_MOUNT_POINT, entry->d_name);
          struct stat st;
          long size = (stat(fullpath, &st) == 0) ? st.st_size : 0;
          JsonObject o = arr.add<JsonObject>();
          o["name"] = entry->d_name;
          o["size"] = size;
        }
        closedir(dir);
      }
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  server.on("/sd/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("file") || !sd_storage_is_mounted()) {
      request->send(404, "text/plain", "File not found");
      return;
    }
    String filename = request->getParam("file")->value();
    char fullpath[300];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", SD_MOUNT_POINT, filename.c_str());

    struct stat st;
    if (stat(fullpath, &st) != 0) {
      request->send(404, "text/plain", "File not found");
      return;
    }
    size_t fileSize = st.st_size;

    // Static path string kept alive for the lifetime of this one
    // response's streaming callback (simplest safe approach given the
    // lambda can't easily capture a local buffer safely across calls
    // otherwise, since ESPAsyncWebServer invokes this callback
    // repeatedly over multiple event-loop iterations).
    static char s_downloadPath[300];
    strlcpy(s_downloadPath, fullpath, sizeof(s_downloadPath));
    static FILE *s_downloadFile = NULL;

    AsyncWebServerResponse *response = request->beginResponse(
      "application/octet-stream", fileSize,
      [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        if (index == 0) {
          s_downloadFile = fopen(s_downloadPath, "rb");
        }
        if (!s_downloadFile) return 0;
        size_t bytesRead = fread(buffer, 1, maxLen, s_downloadFile);
        if (bytesRead == 0) {
          fclose(s_downloadFile);
          s_downloadFile = NULL;
        }
        return bytesRead;
      }
    );
    response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    request->send(response);
  });

  server.on("/sd/upload", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      request->send(200, "text/plain", "OK");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      static FILE *s_uploadFile = NULL;
      if (index == 0) {
        char fullpath[300];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", SD_MOUNT_POINT, filename.c_str());
        s_uploadFile = fopen(fullpath, "wb");
      }
      if (s_uploadFile) {
        fwrite(data, 1, len, s_uploadFile);
      }
      if (final && s_uploadFile) {
        fclose(s_uploadFile);
        s_uploadFile = NULL;
      }
    }
  );

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
