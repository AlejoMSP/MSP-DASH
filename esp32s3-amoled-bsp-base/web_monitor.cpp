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
#include <Update.h>
#include <Preferences.h>
#include "wifi_config.h" // local, git-ignored - see wifi_config.h.example
#include "esp_heap_caps.h"
#include "driver/twai.h"
#include <FFat.h>
#include "build_info.h"
#include "gauge_config_json.h"
#include "simulator.h"
#include "espnow_link.h"
#include "lcd_perf.h"
#include "web_theme.h"
#include <math.h> // NAN

static const char FW_COMPILED[] = __DATE__ " " __TIME__; // when THIS file was compiled - changes on every rebuild

// POST body buffers live in PSRAM, allocated once on first use. They used
// to be static arrays - 51KB of internal RAM held permanently, on top of
// the ~86KB the LVGL draw buffers need - which left the WiFi/TCP stack
// and web server too little heap and caused intermittent stalls (config
// pages loading blank). PSRAM is plentiful (8MB) and these are just
// scratch space for parsing JSON.
static char *allocPostBuf(size_t n) {
  char *p = (char *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = (char *)malloc(n); // no PSRAM available - fall back to the normal heap
  return p;
}

// ---- Saving settings posted by the /gauges and /web-config editors ----
// The answer to the browser is the REAL outcome: 200 only if the settings
// were written to flash, otherwise 500 with the reason, which the page shows.
// (The page used to say "Saved" no matter what happened.)
#define CONFIG_POST_MAX 32768  // 8 pages x 4 cells is ~16KB of JSON; 8 indicator pages x 12 lamps is ~25KB

static unsigned long s_rebootAtMs = 0;   // when the /reboot request asked the board to restart (0 = no request)

struct SaveResult { bool ok; char msg[128]; };
static SaveResult s_displaySave = { true, "" };
static SaveResult s_webSave = { true, "" };

static void applyConfigBody(const char *body, size_t total, bool isWeb) {
  SaveResult &res = isWeb ? s_webSave : s_displaySave;
  res.ok = false;
  Serial.printf("%s config POST: %u bytes\n", isWeb ? "Web dashboard" : "Display", (unsigned)total);
  if (total >= CONFIG_POST_MAX) {
    snprintf(res.msg, sizeof(res.msg), "Settings too large (%u bytes, limit %u)", (unsigned)total, (unsigned)CONFIG_POST_MAX);
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, total);
  if (err) {
    snprintf(res.msg, sizeof(res.msg), "The board could not read the settings (%s)", err.c_str());
    Serial.println(res.msg);
    return;
  }
  if (!doc.is<JsonArray>()) {
    strlcpy(res.msg, "The settings were not a list of pages", sizeof(res.msg));
    return;
  }
  gauge_config_pages_from_json(doc.as<JsonArray>(), isWeb ? gauge_config_get_web_pages() : gauge_config_get_pages());
  bool saved = isWeb ? gauge_config_web_save() : gauge_config_save();
  if (saved) {
    res.ok = true;
    res.msg[0] = '\0';
  } else {
    snprintf(res.msg, sizeof(res.msg), "NOT saved to flash: %s. The change is active until the next reboot.", gauge_config_last_error());
    Serial.println(res.msg);
  }
}

#define SD_MOUNT_POINT "/sd_card"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

static const char INDEX_HTML[] PROGMEM = PH_HEAD "<title>MSP-DASH</title>" PH_STYLE
R"rawliteral(<style>
    .dash-nav { display:flex; justify-content:space-between; padding:0 16px; margin-bottom:10px; }
    .dash-nav button { background:#4dd0e1; color:#000; border:none; border-radius:6px; padding:8px 14px; }
    .cellgrid { display:grid; gap:3px; padding:0 3px 3px; min-height:600px; }
    .layout-1 { grid-template-columns: 1fr; grid-template-rows: 1fr; }
    .layout-2up { grid-template-columns: 1fr; grid-template-rows: 1fr 1fr; }
    .layout-3up { grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr; }
    .layout-3up .c0 { grid-column: 1 / 3; }
    .layout-2x2 { grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr; }
    .dcell { background:#0d1b2a; border:1px solid #223; display:flex; flex-direction:column; align-items:center; justify-content:center; container-type:inline-size; padding:8px 0; }
    .dcell .cap { font-size:0.85em; color:#4dd0e1; }
    .dcell .units { font-size:0.85em; color:#a0a0a0; }
    .dcell .val { font-size:2.2em; font-weight:bold; margin-top:4px; white-space:nowrap; }
    /* The cell's "Value Text Size" setting (--fs), but never wider than the cell: 26% of the cell's width per character-ish.
       Browsers too old for cqw ignore this line and keep the fixed size above. */
    .dcell .val { font-size:min(var(--fs, 2.2em), 26cqw); }
    .dcell .warn { color:#ff4444; }
    .empty-msg { padding:20px; text-align:center; color:#888; }
    .statusline { display:flex; align-items:center; gap:8px; padding:0 16px 8px; font-size:0.78em; color:#aaa; }
    #dot { width:10px; height:10px; border-radius:50%; background:#666; flex:none; }
    #dot.st-ok { background:#2ecc71; }
    #dot.st-warn { background:#f5a623; }
    #dot.st-bad { background:#e74c3c; }
    .cellgrid.indhost { display:block; min-height:0; padding:0; }
    .indgrid { display:grid; grid-template-columns:repeat(var(--cols,2),1fr); gap:8px; padding:8px 8px 16px; }
    .lamp { container-type:inline-size; display:flex; align-items:center; justify-content:center; aspect-ratio:1.6 / 1; border-radius:14px;
            border:2px solid rgba(255,255,255,.25); font-weight:bold; text-align:center; overflow:hidden; }
    .lamp span { font-size:min(1.5em, 17cqw); padding:0 4px; }
    .simbar { display:none; background:#f5a623; color:#000; font-weight:bold; text-align:center; padding:6px; margin:0 3px 6px; border-radius:6px; font-size:0.85em; }
    .simbar.on { display:block; }
  </style>)rawliteral" PH_OPEN
R"rawliteral(
    <div class="dash-nav">
      <button onclick="changeWebPage(-1)">&larr; Prev</button>
      <button onclick="changeWebPage(1)">Next &rarr;</button>
    </div>
    <div class="statusline"><span id="dot"></span><span id="stext">Connecting...</span></div>
    <div class="simbar" id="simbar">SIMULATED DATA - not from the ECU</div>
    <div class="cellgrid" id="cellgrid"></div>
)rawliteral" PH_CLOSE
R"rawliteral(
<script>
    document.getElementById("build").textContent = "build ...";
    // NOTE: cells currently render as plain text (caption + live
    // number) regardless of configured gauge_type (needle/half-moon/
    // number) - true graphical needle rendering for the web view is
    // part of the separate HTML5 live-preview feature, not yet built.
    const LAYOUT_CLASSES = ["layout-1", "layout-2up", "layout-3up", "layout-2x2"];
    let webPages = [];
    let currentWebPage = 0;
    let signalValues = {};      // signals that currently have live data, keyed by signal id

// ---- indicator lamps (an "indicators page" is a grid of on/off lamps) ----
let lampOn = {};            // "page:lamp" -> is it on? Remembered between updates, for the hysteresis band
let blinkPhase = true;      // flips about twice a second, for lamps set to flash while on
const escHtml = s => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
const hex6 = n => "#" + ((Number(n) >>> 0) & 0xFFFFFF).toString(16).padStart(6, "0");

// Is a lamp on? The same rule as the round display (gauge_indicator_next in gauge_indicators.h; tests/host/test_front_page.js
// runs both through the same cases): above = on when value > threshold, below = on when value < threshold; once on it
// stays on until the value is `hysteresis` back past the threshold. No data, or no usable number: OFF.
function indicatorNext(wasOn, hasData, value, op, threshold, hysteresis) {
  if (!hasData || !Number.isFinite(value) || !Number.isFinite(threshold)) return false;
  const h = hysteresis > 0 ? hysteresis : 0;
  if (op === 1) return wasOn ? !(value >= threshold + h) : (value < threshold);
  return wasOn ? !(value <= threshold - h) : (value > threshold);
}

// Re-evaluates every lamp on every indicators page (hidden pages too, so a lamp's memory is right when you flip to its page).
function updateLamps() {
  webPages.forEach((pg, pi) => {
    if (pg.kind !== 1) return;
    (pg.indicators || []).forEach((l, li) => {
      const raw = signalValues[l.signal];
      lampOn[pi + ":" + li] = indicatorNext(!!lampOn[pi + ":" + li], raw !== undefined, raw, l.op, l.threshold, l.hysteresis);
    });
  });
}

function renderIndicators(page, pageIdx) {
  const lamps = page.indicators || [];
  if (lamps.length === 0) return "<p class='empty-msg'>No indicators on this page.<br>Visit Web Dashboard Config in the menu.</p>";
  const cols = Math.max(1, Math.min(4, page.cols || 2));
  return `<div class="indgrid" style="--cols:${cols}">` + lamps.map((l, li) => {
    const on = !!lampOn[pageIdx + ":" + li];
    const look = on && (!l.blink || blinkPhase);      // a flashing lamp alternates between its on and off look
    const text = look ? l.on_text : l.off_text;
    return `<div class="lamp" data-on="${look ? 1 : 0}" style="background:${hex6(look ? l.on_color : l.off_color)};color:${hex6(look ? l.on_text_color : l.off_text_color)}"><span>${escHtml(text || "")}</span></div>`;
  }).join("") + `</div>`;
}
    let live = { fps: 0, ids: 0 };
    let ws = null;
    let lastMsg = 0;            // when the last message arrived (0 = none since this connection opened)
    let retryMs = 1000;

    function render() {
      const grid = document.getElementById("cellgrid");
      if (webPages.length === 0) {
        grid.className = "cellgrid";
        grid.innerHTML = "<p class='empty-msg'>No web dashboard pages configured.<br>Visit Web Dashboard Config in the menu.</p>";
        return;
      }
      updateLamps();
      const page = webPages[currentWebPage];
      if (page.kind === 1) {
        grid.className = "cellgrid indhost";
        grid.innerHTML = renderIndicators(page, currentWebPage);
        return;
      }
      grid.className = "cellgrid " + LAYOUT_CLASSES[page.layout];
      grid.innerHTML = page.cells.map((c, i) => {
        if (!c.enabled) return `<div class="dcell c${i}"></div>`;
        const raw = signalValues[c.signal];
        const scaled = raw === undefined ? undefined : raw / c.value_divisor;
        const dec = [0, 1, 2].includes(Number(c.decimals)) ? Number(c.decimals) : 1;   // the editor's Decimals, like the round display
        const value = (raw === undefined) ? "--" : scaled.toFixed(dec);
        const displayVal = raw === undefined ? value : scaled;
        const isWarn = c.has_warning && raw !== undefined && (c.warning_above ? displayVal > c.warning_threshold : displayVal < c.warning_threshold);
        const size = c.value_font_size || 28;   // the editor's Value Text Size; 28 renders as the 35px these numbers always were
        // The editor's Caption size (0 = the normal 0.85em; a big caption shrinks to the cell's width), the Units line under the number,
        // and Gauge position (Top / Bottom move the whole block; Center is as it always was).
        const capPx = [14, 20, 28, 36, 48].includes(c.caption_font_size) ? c.caption_font_size : 0;
        const capStyle = capPx ? ` style="font-size:${capPx}px;font-size:min(${capPx}px,14cqw)"` : "";
        const unitsHtml = c.units ? `<div class="units"${capStyle}>${escHtml(c.units)}</div>` : "";
        const justify = c.v_align === 1 ? "flex-start" : (c.v_align === 2 ? "flex-end" : "");
        const cellStyle = justify ? ` style="justify-content:${justify}"` : "";
        return `<div class="dcell c${i}"${cellStyle}><div class="cap"${capStyle}>${escHtml(c.caption)}</div><div class="val ${isWarn?'warn':''}" data-size="${size}" style="--fs:${Math.round(size * 1.25)}px">${value}</div>${unitsHtml}</div>`;
      }).join("");
    }

    function changeWebPage(delta) {
      if (webPages.length === 0) return;
      currentWebPage = (currentWebPage + delta + webPages.length) % webPages.length;
      render();
    }

    // The status line says WHY the numbers are or aren't updating, instead of
    // leaving "--" to mean five different things.
    function setStatus(cls, text) {
      document.getElementById("dot").className = cls;
      document.getElementById("stext").textContent = text;
    }

    function updateStatus() {
      if (!ws || ws.readyState === 3 || ws.readyState === 2) { setStatus("st-bad", "Disconnected - retrying..."); return; }
      if (ws.readyState === 0) { setStatus("st-warn", "Connecting..."); return; }
      if (lastMsg === 0) { setStatus("st-warn", "Connected - waiting for the first update..."); return; }
      const age = Date.now() - lastMsg;
      if (age > 3000) { setStatus("st-warn", "No updates from the board for " + Math.round(age / 1000) + "s"); return; }
      const n = Object.keys(signalValues).length;
      if (n > 0) setStatus("st-ok", "Live - " + n + " signal" + (n === 1 ? "" : "s") + " - CAN " + live.fps + " frames/s");
      else if (live.fps === 0) setStatus("st-warn", "Connected, but no CAN frames are arriving (check CAN wiring and ECU power)");
      else setStatus("st-warn", "CAN frames arriving (" + live.fps + "/s) but none decoded - check the ECU type");
    }

    // Opens the live connection, and reopens it whenever it drops. It used
    // to open once and never retry, so one dropped connection left the page
    // on "--" forever with no sign anything was wrong.
    function connect() {
      if (ws && ws.readyState <= 1) return;           // already connecting or open
      const sock = new WebSocket("ws://" + location.host + "/ws");
      ws = sock;
      lastMsg = 0;
      sock.onopen = () => { retryMs = 1000; updateStatus(); };
      sock.onmessage = (event) => {
        let m;
        try { m = JSON.parse(event.data); } catch (e) { return; }
        lastMsg = Date.now();
        signalValues = m.v || {};
        live.fps = m.fps || 0;
        live.ids = m.ids || 0;
        document.getElementById("simbar").className = m.sim ? "simbar on" : "simbar";
        render();
        updateStatus();
      };
      sock.onclose = () => {
        if (ws === sock) ws = null;
        signalValues = {};          // no connection means we don't know the values: show "--", not frozen numbers
        lampOn = {};                // ... and every lamp goes dark
        document.getElementById("simbar").className = "simbar";
        render();
        updateStatus();
        setTimeout(connect, retryMs);
        retryMs = Math.min(retryMs * 2, 5000);
      };
      sock.onerror = () => { try { sock.close(); } catch (e) {} };
      updateStatus();
    }

    // Once a second: refresh the status text, and drop a connection that is
    // "open" but has gone silent (the board broadcasts every 500ms).
    setInterval(() => {
      if (ws && ws.readyState === 1 && lastMsg !== 0 && Date.now() - lastMsg > 8000) { try { ws.close(); } catch (e) {} }
      updateStatus();
    }, 1000);

    // Flashing lamps: flip the phase about twice a second and redraw - but only when the page on screen has one.
    setInterval(() => {
      blinkPhase = !blinkPhase;
      const pg = webPages[currentWebPage];
      if (pg && pg.kind === 1 && (pg.indicators || []).some(l => l.blink)) render();
    }, 480);

    // A phone that was asleep or in another app comes back with a dead connection.
    document.addEventListener("visibilitychange", () => { if (!document.hidden) connect(); });

    // The two requests run one after the other, not together - this board
    // handles one at a time much more reliably.
    async function loadPages() {
      try {
        const res = await fetch("/web-pages.json");
        webPages = await res.json();
        render();
      } catch (e) {
        document.getElementById("cellgrid").innerHTML = "<p class='empty-msg'>Could not load the page layout - retrying...</p>";
        setTimeout(loadPages, 3000);
        return;
      }
      loadBuild();
    }

    async function loadBuild() {
      try {
        const res = await fetch("/status.json");
        const st = await res.json();
        let t = (st.link && st.link.name ? st.link.name + " | " : "") + "build " + st.build + " - " + st.compiled;
        if (st.fps !== undefined) t += " | " + st.fps + " fps, drawing " + st.lvgl_busy + "%";
        if (st.link && st.link.name) document.title = "MSP-DASH - " + st.link.name;
        const L = st.link;
        if (L) {
          t += L.role === "remote"
            ? " | Remote board, " + (L.last_ms < 0 ? "no packets yet" : L.pps + " packets/s from " + L.from) + ", ch " + L.channel
            : " | Main board, sending " + L.pps + " packets/s, ch " + L.channel;
        }
        const el = document.getElementById("build");
        el.textContent = t;
        // Main board: the remotes it hears, sorted by IP, each a link to that board's own gauge editor
        const bs = (L && L.boards ? L.boards : []).filter(b => b.age_ms < 10000)
          .sort((a, b) => a.ip.split(".").map(Number)[3] - b.ip.split(".").map(Number)[3]);
        bs.forEach((b, i) => {
          el.appendChild(document.createElement("br"));
          const a = document.createElement("a");
          a.href = "http://" + b.ip + "/gauges"; a.target = "_blank";
          a.textContent = (b.name || ("Remote " + (i + 1))) + ": " + b.ip + " (build " + b.build + ")";
          el.appendChild(a);
          if (b.build !== st.build) el.appendChild(document.createTextNode(" - different build, flash it"));
        });
      } catch (e) {
        setTimeout(loadBuild, 5000);
      }
    }

    connect();
    loadPages();
  </script>
</body>
</html>
)rawliteral";
static const char CAN_HTML[] PROGMEM = PH_HEAD "<title>MSP-DASH Raw CAN Monitor</title>" PH_STYLE
R"rawliteral(<style>
    .tablewrap { overflow-x:auto; }
    table { width:100%; border-collapse:collapse; margin-top:8px; font-family:monospace; font-size:0.72em; }
    th, td { text-align:left; padding:6px 4px; border-bottom:1px solid #333; }
    th { color:#4dd0e1; font-family:-apple-system,BlinkMacSystemFont,sans-serif; }
  </style>)rawliteral" PH_OPEN
R"rawliteral(<div class="pad">
  <h1>Raw CAN Monitor</h1>
  <div class="tablewrap">
  <table>
    <thead><tr><th>ID (hex)</th><th>DLC</th><th>Data (hex)</th><th>Hz</th><th>Age (ms)</th></tr></thead>
    <tbody id="rows"></tbody>
  </table>
  </div>
  
</div>)rawliteral" PH_CLOSE
R"rawliteral(
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

static const char GAUGES_HTML[] PROGMEM = PH_HEAD "<title>MSP-DASH Gauge Config</title>" PH_STYLE
R"rawliteral(<style>
    h2 { font-size: 1em; color:#4dd0e1; margin:4px 0; }
    .page-card { background:#0a0a0a; border:1px solid #333; border-radius:8px; padding:12px; margin-bottom:14px; }
    .page-header { display:flex; justify-content:space-between; align-items:center; gap:8px; flex-wrap:wrap; }
    .cell { background:#111; border-radius:8px; padding:10px; margin-top:10px; }
    .cell label { display:block; font-size:0.78em; color:#4dd0e1; margin-top:5px; }
    .cell input, .cell select { width:100%; box-sizing:border-box; padding:4px; margin-top:2px; }
    .cell input[type=checkbox] { width:auto; margin:0 6px 0 0; }
    .row2 { display:flex; gap:8px; }
    .row2 > div { flex:1; }
    button { padding:8px 12px; background:#4dd0e1; color:#000; border:none; border-radius:6px; margin:2px; }
    button.danger { background:#F44336; color:#fff; }
    .bulk { background:#111; border-radius:8px; padding:14px; margin-bottom:14px; }
    .bulk-title { font-weight:bold; }
    .bulk-sub { font-size:0.78em; color:#888; margin:2px 0 12px; }
    .bulk-colors { display:grid; grid-template-columns:repeat(3,1fr); gap:10px; margin-bottom:12px; }
    .bulk-colors label { display:flex; flex-direction:column; gap:5px; font-size:0.78em; color:#4dd0e1; }
    .bulk-colors input[type=color] { width:100%; height:38px; padding:2px; border:1px solid #333; border-radius:6px; background:#000; }
    .bulk-apply { width:100%; margin:0; }
    .hint { font-size:0.75em; color:#888; margin:2px 0; }
    .dialbox { display:flex; gap:12px; align-items:center; margin-top:6px; }
    .dialform { flex:1; }
    .dialprev { flex:none; width:96px; text-align:center; }
    .dialcap { font-size:0.7em; color:#888; }
    .zonebox { background:#0d0d0d; border:1px solid #222; border-radius:8px; padding:8px; margin:8px 0; }
    .zonerow { display:flex; align-items:center; gap:8px; margin:4px 0; }
    .zonerow label { flex:1; }
    .zonerow input[type=number] { width:92px; margin:0; }
    .addpage { width:100%; margin:0 0 14px; background:none; border:1px dashed #4dd0e1; color:#4dd0e1; }
    #status, #boot { text-align:center; margin:8px 0; color:#4dd0e1; min-height:1.2em; }
    #status:empty, #boot:empty { display:none; }
  </style>)rawliteral" PH_OPEN
R"rawliteral(<div class="pad">
  <h1>Gauge Configuration</h1>
  <div id="status"></div>
  <div id="boot">Page script has not started yet...</div>

  <div class="bulk">
    <div class="bulk-title">Apply to ALL cells</div>
    <div class="bulk-sub">Sets every cell on every page. Click Save afterward to keep it.</div>
    <div class="bulk-colors">
      <label>Needle<input type="color" id="bulkNeedle" value="#ffffff"></label>
      <label>Tick / Dial<input type="color" id="bulkTick" value="#ffffff"></label>
      <label>Background<input type="color" id="bulkBg" value="#000000"></label>
    </div>
    <button type="button" class="bulk-apply" onclick="applyColorsToAll()">Apply colors to All</button>
    <div class="bulk-colors" style="margin-top:12px">
      <label>Needle gap at center %<input type="number" id="bulkHub" min="0" max="60" value="0"></label>
      <label>Needle short of tip %<input type="number" id="bulkTip" min="0" max="60" value="0"></label>
      <label>Needle thickness px<input type="number" id="bulkNw" min="1" max="20" value="6"></label>
    </div>
    <button type="button" class="bulk-apply" onclick="applyNeedleToAll()">Apply needle to All</button>
    <div class="bulk-colors" style="margin-top:12px">
      <label>Caption text size<select id="bulkCap"></select></label>
    </div>
    <button type="button" class="bulk-apply" onclick="applyCaptionToAll()">Apply caption size to All</button>
  </div>

  <button type="button" class="addpage" onclick="addPage()">+ Add Page</button>
  <button type="button" class="addpage" onclick="addIndicatorPage()">+ Add Indicators Page</button>
  <div id="pages"></div>
  <button onclick="save()">Save</button>
  <button onclick="saveAndReboot()">Save &amp; Reboot Board</button>
  <p class="hint">The round display only picks up changes after the board restarts. Save &amp; Reboot does both.</p>

  
</div>)rawliteral" PH_CLOSE
R"rawliteral(
<script>
    window.onerror = function(msg, src, line) { document.getElementById("boot").textContent = "Script error: " + msg + " (line " + line + ")"; };
    document.getElementById("boot").textContent = "";
    const SIGNALS = ["None","RPM","Coolant Temp","MAP/Boost","AFR","Oil Pressure","Oil Temp","Fuel Pressure","Ethanol %","Throttle Position","Battery Voltage"];
    const LAYOUTS = ["1 Cell (full screen)","2-Up (stacked)","3-Up (1 top, 2 bottom)","2x2 Grid"];
    const CELLS_PER_LAYOUT = [1, 2, 3, 4];
    const GAUGE_TYPES = ["Needle Meter", "Number", "Half-Moon", "Trend Graph"];
    // Readout text sizes (px). Must match GAUGE_VALUE_SIZES in gauge_style.h. A size too big for its cell shrinks to the biggest that fits.
    const VALUE_SIZES = [[14,"14 Small"],[28,"28 Medium"],[36,"36 Large"],[48,"48 XL"],[64,"64 2XL"],[80,"80 3XL"],[96,"96 4XL"],[120,"120 Max"]];
    let loaded = false;
    let pages = [];
    let signalActivity = []; // [{id, name, age_ms}], sorted most-active first

    function emptyCell() {
      return { enabled:false, signal:0, gauge_type:0, min:0, max:100, rest:0, caption:"",
        needle_color:0xFFFFFF, tick_color:0xFFFFFF, bg_color:0x000000, value_divisor:1.0,
        tick_count:26, major_nth:5, refresh_hz:10, show_value:false, value_font_size:28, decimals:1, h_align:0, tick_font_size:28, caption_font_size:0, caption_on_face:false, units:"", v_align:0, start_deg:225, end_deg:135,
        low_crit_on:false, low_crit:0, low_warn_on:false, low_warn:0, high_warn_on:false, high_warn:0, high_crit_on:false, high_crit:0, zone_style:1, needle_hub_pct:0, needle_tip_pct:0, needle_w:6, text_xy:false, cap_x:0, cap_y:0, val_x:0, val_y:0,
        has_warning:false, warning_threshold:0, warning_above:false, alert_fullscreen:false, history_sec:30, ref_enabled:false, ref_value:0 };
    }

    function signalOptionsHtml(selected) {
      // Sorted by activity (lowest age_ms first = most live), "never seen" last, name as secondary sort.
      const sorted = signalActivity.slice().sort((a, b) => {
        if (a.age_ms !== b.age_ms) return a.age_ms - b.age_ms;
        return a.name.localeCompare(b.name);
      });
      let html = `<option value="0" ${selected===0?"selected":""}>None</option>`;
      for (const s of sorted) {
        if (s.id === 0 || s.flag) continue;   // ECU on/off flags belong on an indicators page, not on a gauge
        const liveTag = s.age_ms < 5000 ? " (live)" : (s.age_ms > 1e8 ? " (no data)" : ` (${Math.round(s.age_ms/1000)}s ago)`);
        html += `<option value="${s.id}" ${selected===s.id?"selected":""}>${s.name}${liveTag}</option>`;
      }
      return html;
    }

    function renderCell(pIdx, cIdx, c) {
      return `
        <div class="cell">
          <label><input type="checkbox" ${c.enabled?"checked":""} onchange="pages[${pIdx}].cells[${cIdx}].enabled=this.checked; renderPages();"> Cell ${cIdx+1} enabled</label>
          ${!c.enabled ? "" : `
          <label>Signal <select onchange="setCellSignal(${pIdx},${cIdx},parseInt(this.value))">${signalOptionsHtml(c.signal)}</select></label>
          <label>Gauge Type <select onchange="changeGaugeType(${pIdx},${cIdx},parseInt(this.value))">
            ${GAUGE_TYPES.map((t,v)=>`<option value="${v}" ${c.gauge_type===v?"selected":""}>${t}</option>`).join("")}
          </select></label>
          <label>Caption <input type="text" value="${escHtml(c.caption)}" onchange="pages[${pIdx}].cells[${cIdx}].caption=this.value"></label>
          <div class="row2">
            <div><label>Caption size <select onchange="setCellField(${pIdx},${cIdx},'caption_font_size',parseInt(this.value))">${captionSizeOptions(c.caption_font_size)}</select></label></div>
            <div><label>Units <input type="text" maxlength="7" value="${escHtml(c.units || "")}" placeholder="e.g. kPa" oninput="setCellField(${pIdx},${cIdx},'units',this.value)"></label></div>
          </div>
          ${c.gauge_type !== 3 ? `<label>Gauge position in the cell
            <select onchange="setCellField(${pIdx},${cIdx},'v_align',parseInt(this.value))">${valignOptions(c.v_align)}</select></label>` : ""}
          ${(c.gauge_type === 0 || c.gauge_type === 2) ? `<label><input type="checkbox" ${c.caption_on_face ? "checked" : ""} onchange="setCellField(${pIdx},${cIdx},'caption_on_face',this.checked)"> Show the caption on the dial face (above the hub)</label>` : ""}
          <div class="row2">
            <div><label>Min <input type="number" value="${c.min}" onchange="pages[${pIdx}].cells[${cIdx}].min=parseInt(this.value)"></label></div>
            <div><label>Max <input type="number" value="${c.max}" onchange="pages[${pIdx}].cells[${cIdx}].max=parseInt(this.value)"></label></div>
            <div><label>Rest <input type="number" value="${c.rest}" onchange="pages[${pIdx}].cells[${cIdx}].rest=parseInt(this.value)"></label></div>
          </div>
          <div class="row2">
            <div><label>Value Divisor <input type="number" step="any" value="${c.value_divisor}" onchange="pages[${pIdx}].cells[${cIdx}].value_divisor=parseFloat(this.value)"></label></div>
            <div><label>Tick Count <input type="number" value="${c.tick_count}" onchange="pages[${pIdx}].cells[${cIdx}].tick_count=parseInt(this.value)"></label></div>
            <div><label>Major Every Nth <input type="number" value="${c.major_nth}" onchange="pages[${pIdx}].cells[${cIdx}].major_nth=parseInt(this.value)"></label></div>
          </div>
          <div class="row2">
            <div><label>Refresh Rate (Hz) <input type="number" value="${c.refresh_hz}" onchange="pages[${pIdx}].cells[${cIdx}].refresh_hz=parseInt(this.value)"></label></div>
            <div><label><input type="checkbox" ${c.show_value?"checked":""} onchange="pages[${pIdx}].cells[${cIdx}].show_value=this.checked"> Show value</label></div>
          </div>
          <label>Value Text Size
            <select onchange="pages[${pIdx}].cells[${cIdx}].value_font_size=parseInt(this.value)">
              ${VALUE_SIZES.map(([v,n])=>`<option value="${v}" ${c.value_font_size===v?"selected":""}>${n}</option>`).join("")}
            </select>
          </label>
          <label>Decimals
            <select onchange="setCellField(${pIdx},${cIdx},'decimals',parseInt(this.value))">${decimalsOptions(c.decimals)}</select></label>
          <label>Caption alignment
            <select onchange="setCellField(${pIdx},${cIdx},'h_align',parseInt(this.value))">${[[0,"Center"],[1,"Left"],[2,"Right"]].map(([v,n]) => `<option value="${v}" ${(c.h_align||0)===v?"selected":""}>${n}</option>`).join("")}</select></label>
          <label><input type="checkbox" ${c.text_xy?"checked":""} onchange="setTextXY(${pIdx},${cIdx},this.checked)"> Place text by coordinates</label>
          ${c.text_xy ? `<div class="row2">
            <div><label>Caption X <input type="number" min="-300" max="300" value="${c.cap_x||0}" onchange="setCellField(${pIdx},${cIdx},'cap_x',parseInt(this.value)||0)"></label></div>
            <div><label>Caption Y <input type="number" min="-300" max="300" value="${c.cap_y||0}" onchange="setCellField(${pIdx},${cIdx},'cap_y',parseInt(this.value)||0)"></label></div>
          </div><div class="row2">
            <div><label>Value X <input type="number" min="-300" max="300" value="${c.val_x||0}" onchange="setCellField(${pIdx},${cIdx},'val_x',parseInt(this.value)||0)"></label></div>
            <div><label>Value Y <input type="number" min="-300" max="300" value="${c.val_y||0}" onchange="setCellField(${pIdx},${cIdx},'val_y',parseInt(this.value)||0)"></label></div>
          </div>
          <p class="hint">Pixels from the cell's centre: X to the right, Y down. Units sit under the value. Caption alignment is ignored while this is on.</p>` : ""}
          <p class="hint">Value text size is the big number. A size too big for the cell shrinks to the largest that fits.</p>
          ${(c.gauge_type===0 || c.gauge_type===2) ? `
          <div class="row2">
            <div><label>Dial numbers
              <select onchange="pages[${pIdx}].cells[${cIdx}].tick_font_size=parseInt(this.value)">
                ${TICK_SIZES.map(([v,n])=>`<option value="${v}" ${(c.tick_font_size||28)===v?"selected":""}>${n}</option>`).join("")}
              </select>
            </label></div>
          </div>
          <div class="dialbox">
            <div class="dialform">
              <label>Dial start (degrees) <input type="number" step="1" value="${c.start_deg}" oninput="setAngle(${pIdx},${cIdx},'start_deg',this.value)"></label>
              <label>Dial end (degrees) <input type="number" step="1" value="${c.end_deg}" oninput="setAngle(${pIdx},${cIdx},'end_deg',this.value)"></label>
            </div>
            <div class="dialprev" id="dial_${pIdx}_${cIdx}">${dialSvg(c)}</div>
          </div>
          <p class="hint">0 is straight up and angles run clockwise (90 = 3 o'clock, 180 = straight down). The dial sweeps clockwise from start to end. Standard gauge: 225 to 135.</p>
          <div class="zonebox">
            <strong>Warning zones</strong>
            <p class="hint">Like TunerStudio: up to four thresholds, each switched on by itself. Values are in the dial's own units (between Min and Max). Red covers yellow where they overlap.</p>
            ${[["low_crit", "Low critical (red) up to"], ["low_warn", "Low warning (yellow) up to"], ["high_warn", "High warning (yellow) from"], ["high_crit", "High critical (red) from"]].map(([k, name]) => `
            <div class="zonerow">
              <label><input type="checkbox" ${c[k + "_on"] ? "checked" : ""} onchange="toggleZone(${pIdx},${cIdx},'${k}',this.checked)"> ${name}</label>
              <input type="number" step="1" value="${c[k]}" ${c[k + "_on"] ? "" : "disabled"} oninput="setZone(${pIdx},${cIdx},'${k}',this.value)">
            </div>`).join("")}
            <label>Show as
              <select onchange="setZoneStyle(${pIdx},${cIdx},parseInt(this.value))">
                <option value="1" ${(c.zone_style || 1) === 1 ? "selected" : ""}>Colored bands behind the ticks</option>
                <option value="2" ${c.zone_style === 2 ? "selected" : ""}>Colored tick marks</option>
                <option value="3" ${c.zone_style === 3 ? "selected" : ""}>Both</option>
              </select>
            </label>
          </div>
          <div class="row2">
            <div><label>Needle gap at center (%) <input type="number" min="0" max="60" step="1" value="${c.needle_hub_pct||0}" oninput="setTrim(${pIdx},${cIdx},'needle_hub_pct',this.value)"></label></div>
            <div><label>Needle short of tip (%) <input type="number" min="0" max="60" step="1" value="${c.needle_tip_pct||0}" oninput="setTrim(${pIdx},${cIdx},'needle_tip_pct',this.value)"></label></div>
          </div>
          <label>Needle thickness (px) <input type="number" min="1" max="20" step="1" value="${c.needle_w||6}" onchange="setCellField(${pIdx},${cIdx},'needle_w',Math.min(20,Math.max(1,parseInt(this.value)||6)))"></label>
          <p class="hint">Needle length trim, as a percent of the dial's radius. 0 and 0 is the normal needle. Both ends together can trim at most 85.</p>
          ` : ""}
          ${c.gauge_type===3 ? `
          <div class="row2">
            <div><label>History (seconds, 2-600) <input type="number" min="2" max="600" value="${c.history_sec}" onchange="pages[${pIdx}].cells[${cIdx}].history_sec=parseInt(this.value)"></label></div>
            <div><label><input type="checkbox" ${c.ref_enabled?"checked":""} onchange="pages[${pIdx}].cells[${cIdx}].ref_enabled=this.checked"> Reference line</label></div>
            <div><label>Reference value <input type="number" step="any" value="${c.ref_value}" onchange="pages[${pIdx}].cells[${cIdx}].ref_value=parseFloat(this.value)"></label></div>
          </div>
          <p style="font-size:0.75em;color:#888;margin:2px 0">Trend graphs take 100 samples across the history window and ignore Refresh Rate. Min and Max set the vertical range.</p>
          ` : ""}
          <label>Needle Color <input type="color" value="#${c.needle_color.toString(16).padStart(6,'0')}" onchange="pages[${pIdx}].cells[${cIdx}].needle_color=parseInt(this.value.substring(1),16)"></label>
          <label>Tick/Dial Color <input type="color" value="#${c.tick_color.toString(16).padStart(6,'0')}" onchange="pages[${pIdx}].cells[${cIdx}].tick_color=parseInt(this.value.substring(1),16)"></label>
          <label>Background Color <input type="color" value="#${c.bg_color.toString(16).padStart(6,'0')}" onchange="pages[${pIdx}].cells[${cIdx}].bg_color=parseInt(this.value.substring(1),16)"></label>
          <label><input type="checkbox" ${c.has_warning?"checked":""} onchange="pages[${pIdx}].cells[${cIdx}].has_warning=this.checked"> Warning enabled</label>
          <div class="row2">
            <div><label>Threshold <input type="number" value="${c.warning_threshold}" onchange="pages[${pIdx}].cells[${cIdx}].warning_threshold=parseFloat(this.value)"></label></div>
            <div><label>Direction
              <select onchange="pages[${pIdx}].cells[${cIdx}].warning_above=(this.value==='1')">
                <option value="1" ${c.warning_above?"selected":""}>Above</option>
                <option value="0" ${!c.warning_above?"selected":""}>Below</option>
              </select>
            </label></div>
          </div>
          <label><input type="checkbox" ${c.alert_fullscreen?"checked":""} onchange="pages[${pIdx}].cells[${cIdx}].alert_fullscreen=this.checked"> Full-screen alert on ANY page (needs Warning enabled; engine running only; BOOT or tap dismisses for 30s)</label>
          `}
        </div>
      `;
    }

    // ---- dial start/end angles ----
    // Dial numbers (the numbers around a needle dial): -1 hides them.
    const TICK_SIZES = [[-1,"Hidden"],[14,"14 Small"],[28,"28 Medium"],[36,"36 Large"]];

    // Mirrors gauge_dial_resolve() in gauge_style.h, so the preview shows what the round display will draw:
    // angles are compass degrees (0 = straight up, clockwise); equal angles or a sliver under 10 degrees fall back to the standard dial.
    function dialGeometry(c) {
      const half = c.gauge_type === 2;
      const norm = d => ((Math.round(d) % 360) + 360) % 360;
      let s = norm(c.start_deg), e = norm(c.end_deg), sw = norm(e - s), standard = false;
      if (!(sw >= 10)) { s = half ? 270 : 225; e = half ? 90 : 135; sw = norm(e - s); standard = true; }
      return { start: s, end: e, sweep: sw, standard: standard };
    }

    // Warning zones (gauge_style.h). Colours: GAUGE_ZONE_YELLOW / GAUGE_ZONE_RED.
    const ZONE_COLORS = { yellow: "#ffd600", red: "#ff2a2a" };
    // The four thresholds, in the order the display uses: 0 low critical, 1 low warning, 2 high warning, 3 high critical.
    const ZONE_KEYS = ["low_crit", "low_warn", "high_warn", "high_crit"];

    // The ranges of the scale to colour, IN DRAW ORDER (yellow first, red last, so red covers yellow). Mirrors
    // gauge_zone_ranges() in gauge_style.h: a "low" threshold covers the scale from its minimum up to the value, a "high" one
    // from the value up to its maximum; values are clamped to the scale; an empty range is left out.
    function zoneRanges(c) {
      const mn = parseInt(c.min) || 0, mx = parseInt(c.max) || 0, out = [];
      const clamp = v => Math.min(mx, Math.max(mn, v));
      for (const i of [1, 2, 0, 3]) {
        if (!c[ZONE_KEYS[i] + "_on"]) continue;
        const low = i < 2, at = parseInt(c[ZONE_KEYS[i]]) || 0;
        const a = clamp(low ? mn : at), b = clamp(low ? at : mx);
        if (a > b) continue;
        out.push({ start: a, end: b, color: (i === 0 || i === 3) ? "red" : "yellow" });
      }
      return out;
    }

    // The colour of a tick of this value ("red", "yellow" or null) when zones are drawn as tick marks: the last range that
    // holds it wins (both ends included), exactly as LVGL recolours ticks.
    function zoneOf(c, value) {
      let hit = null;
      for (const z of zoneRanges(c)) if (value >= z.start && value <= z.end) hit = z.color;
      return hit;
    }

    // The value of tick i of n, the way LVGL computes it (integer maths).
    function tickValue(c, i, n) {
      const mn = parseInt(c.min) || 0, mx = parseInt(c.max) || 0;
      return mn + Math.trunc(i * (mx - mn) / (n - 1));
    }

    // Mirrors gauge_resolve_needle_trim() in gauge_style.h: each end 0..60%, together at most 85 (the hub gap wins).
    function resolveTrim(c) {
      const cl = v => Math.max(0, Math.min(60, Math.round(Number(v) || 0)));
      const hub = cl(c.needle_hub_pct); let tip = cl(c.needle_tip_pct);
      if (hub + tip > 85) tip = 85 - hub;
      return { hub: hub, tip: tip };
    }

    // A small picture of the dial: the arc, its tick marks (coloured by zone), the needle with its gap and stop-short
    // (drawn part-way along the sweep), a green dot where the dial starts and a red one where it ends.
    function dialSvg(c) {
      const g = dialGeometry(c), R = 40, cx = 48, cy = 48;
      const pt = (deg, r) => [cx + r * Math.sin(deg * Math.PI / 180), cy - r * Math.cos(deg * Math.PI / 180)];
      const f = v => v.toFixed(1);
      const a0 = pt(g.start, R), a1 = pt(g.start + g.sweep, R);
      const n = Math.max(2, Math.min(60, parseInt(c.tick_count) || 26)), major = Math.max(1, parseInt(c.major_nth) || 5);
      // zone_style: 1 = bands behind the ticks, 2 = coloured tick marks, 3 = both (anything else is bands, like the display)
      const style = [1, 2, 3].includes(Number(c.zone_style)) ? Number(c.zone_style) : 1;
      const mnV = parseInt(c.min) || 0, spanV = (parseInt(c.max) || 0) - mnV;
      let bands = "";
      if ((style & 1) && spanV > 0) {
        for (const z of zoneRanges(c)) {
          if (z.start >= z.end) continue;                         // a band with no width draws nothing
          const s0 = g.start + g.sweep * (z.start - mnV) / spanV, s1 = g.start + g.sweep * (z.end - mnV) / spanV;
          const q0 = pt(s0, R - 4), q1 = pt(s1, R - 4);
          bands += `<path class="band ${z.color}" d="M${f(q0[0])} ${f(q0[1])} A${R - 4} ${R - 4} 0 ${(s1 - s0) > 180 ? 1 : 0} 1 ${f(q1[0])} ${f(q1[1])}" fill="none" stroke="${ZONE_COLORS[z.color]}" stroke-width="5"/>`;
        }
      }
      let ticks = "";
      for (let i = 0; i < n; i++) {
        const a = g.start + g.sweep * i / (n - 1), len = (i % major === 0) ? 9 : 5;
        const p0 = pt(a, R - 3), p1 = pt(a, R - 3 - len);
        const zone = (style & 2) ? zoneOf(c, tickValue(c, i, n)) : null;
        ticks += `<line class="tick${zone ? " " + zone : ""}" x1="${f(p0[0])}" y1="${f(p0[1])}" x2="${f(p1[0])}" y2="${f(p1[1])}" stroke="${zone ? ZONE_COLORS[zone] : "#888"}" stroke-width="${zone ? 2 : (i % major === 0 ? 1.6 : 1)}"/>`;
      }
      const trim = resolveTrim(c), needleAt = g.start + g.sweep * 0.65;
      const n0 = pt(needleAt, R * trim.hub / 100), n1 = pt(needleAt, R * (0.94 - trim.tip / 100));
      const needleColor = "#" + ((Number(c.needle_color) >>> 0) & 0xFFFFFF).toString(16).padStart(6, "0");
      return `<svg viewBox="0 0 96 96" width="96" height="96" role="img" aria-label="dial preview">` +
        `<circle cx="48" cy="48" r="${R}" fill="none" stroke="#222"/>` +
        `<path d="M${f(a0[0])} ${f(a0[1])} A${R} ${R} 0 ${g.sweep > 180 ? 1 : 0} 1 ${f(a1[0])} ${f(a1[1])}" fill="none" stroke="#4dd0e1" stroke-width="3"/>` +
        bands +
        ticks +
        `<line class="needle" x1="${f(n0[0])}" y1="${f(n0[1])}" x2="${f(n1[0])}" y2="${f(n1[1])}" stroke="${needleColor}" stroke-width="${((c.needle_w || 6) * 0.37).toFixed(2)}" stroke-linecap="round"/>` +
        (trim.hub === 0 ? `<circle class="hub" cx="48" cy="48" r="2.6" fill="#666"/>` : "") +
        `<circle cx="${f(a0[0])}" cy="${f(a0[1])}" r="3.5" fill="#2ecc71"/><circle cx="${f(a1[0])}" cy="${f(a1[1])}" r="3.5" fill="#e74c3c"/>` +
        `<text x="48" y="7" font-size="8" fill="#666" text-anchor="middle">0</text></svg>` +
        `<div class="dialcap">${g.sweep}&deg; sweep${g.standard ? " (standard dial)" : ""}</div>`;
    }

    // Typing in a number box redraws just that cell's picture, so the box keeps focus.
    function refreshDial(pIdx, cIdx) {
      const el = document.getElementById("dial_" + pIdx + "_" + cIdx);
      if (el) el.innerHTML = dialSvg(pages[pIdx].cells[cIdx]);
    }

    function setAngle(pIdx, cIdx, key, value) {
      const n = parseInt(value);
      if (!isNaN(n)) pages[pIdx].cells[cIdx][key] = n;
      refreshDial(pIdx, cIdx);
    }

    function setZone(pIdx, cIdx, key, value) {
      const n = parseInt(value);
      if (!isNaN(n)) pages[pIdx].cells[cIdx][key] = n;
      refreshDial(pIdx, cIdx);
    }

    function setTrim(pIdx, cIdx, key, value) {
      const n = parseInt(value);
      if (!isNaN(n)) pages[pIdx].cells[cIdx][key] = Math.max(0, Math.min(60, n));
      refreshDial(pIdx, cIdx);
    }

    // Switching a threshold on for the first time offers a sensible value inside the scale instead of 0.
    function toggleZone(pIdx, cIdx, key, on) {
      const c = pages[pIdx].cells[cIdx];
      c[key + "_on"] = on;
      if (on && !c[key]) {
        const mn = parseInt(c.min) || 0, mx = parseInt(c.max) || 0;
        const frac = { low_crit: 0.1, low_warn: 0.25, high_warn: 0.75, high_crit: 0.9 }[key];
        c[key] = Math.round(mn + (mx - mn) * frac);
      }
      renderPages();
    }

    function setZoneStyle(pIdx, cIdx, style) {
      pages[pIdx].cells[cIdx].zone_style = style;
      refreshDial(pIdx, cIdx);
    }

    // Switching between Needle and Half-Moon keeps that type's standard dial - unless the dial was customised.
    function changeGaugeType(pIdx, cIdx, type) {
      const c = pages[pIdx].cells[cIdx];
      c.gauge_type = type;
      if (type === 2 && c.start_deg === 225 && c.end_deg === 135) { c.start_deg = 270; c.end_deg = 90; }
      else if (type === 0 && c.start_deg === 270 && c.end_deg === 90) { c.start_deg = 225; c.end_deg = 135; }
      renderPages();
    }

    // Turning coordinates on starts from a sensible spot: value at the centre, caption a bit lower.
    function setTextXY(pIdx, cIdx, on) {
      const c = pages[pIdx].cells[cIdx];
      c.text_xy = on;
      if (on && !c.cap_x && !c.cap_y && !c.val_x && !c.val_y) { c.cap_y = 70; c.val_y = 0; }
      renderPages();
    }

    function changeLayout(pIdx, newLayout) {
      pages[pIdx].layout = newLayout;
      const needed = CELLS_PER_LAYOUT[newLayout];
      while (pages[pIdx].cells.length < needed) pages[pIdx].cells.push(emptyCell());
      // 2-Up: a cell that is not set up yet becomes a Half-Moon (the dial that fits a short, wide cell)
      if (newLayout === 1) pages[pIdx].cells.forEach((c, i) => { if (i < needed && !c.enabled && c.gauge_type === 0) { c.gauge_type = 2; c.start_deg = 270; c.end_deg = 90; } });
      renderPages();
    }

    function pageAction(pIdx, action) {
      if (action === "delete") {
        pages.splice(pIdx, 1);
      } else if (action === "empty") {
        if (pages[pIdx].kind === 1) pages[pIdx].indicators = [];
        else pages[pIdx].cells = pages[pIdx].cells.map(() => emptyCell());
      } else if (action === "copy") {
        const copy = JSON.parse(JSON.stringify(pages[pIdx]));
        pages.push(copy);
      }
      renderPages();
    }

    function addPage() {
      pages.push({ enabled:true, layout:0, cells:[emptyCell()] });
      renderPages();
    }

    function renderPages() {
      const container = document.getElementById("pages");
      container.innerHTML = pages.map((p, pIdx) => {
        if (p.kind === 1) return renderIndicatorPage(pIdx, p);
        const needed = CELLS_PER_LAYOUT[p.layout];
        while (p.cells.length < needed) p.cells.push(emptyCell());
        let cellsHtml = "";
        for (let c = 0; c < needed; c++) cellsHtml += renderCell(pIdx, c, p.cells[c]);
        return `
          <div class="page-card">
            <div class="page-header">
              <strong>Page ${pIdx + 1}</strong>
              <select onchange="changeLayout(${pIdx}, parseInt(this.value))">
                ${LAYOUTS.map((l,v)=>`<option value="${v}" ${p.layout===v?"selected":""}>${l}</option>`).join("")}
              </select>
              <button type="button" onclick="pageAction(${pIdx},'copy')">Copy</button>
              <button type="button" onclick="pageAction(${pIdx},'empty')">Empty</button>
              <button type="button" class="danger" onclick="pageAction(${pIdx},'delete')">Delete</button>
            </div>
            ${cellsHtml}
          </div>
        `;
      }).join("");
    }

    async function load() {
      document.getElementById("status").textContent = "Loading...";
      try {
        // Sequential, not Promise.all - this board juggles CAN
        // polling, LVGL rendering and the web server all at once, and
        // firing two requests at the exact same instant occasionally
        // stalls one of them with no error shown (looked like the page
        // just hanging with nothing below the bulk-color section).
        const pagesRes = await fetch("/gauges.json");
        pages = await pagesRes.json();
        const signalsRes = await fetch("/signals.json");
        signalActivity = await signalsRes.json();
        renderPages();
        loaded = true;
        document.getElementById("status").textContent = "";
      } catch (e) {
        document.getElementById("status").innerHTML = "Failed to load - " + e.message + ' <button type="button" onclick="load()">Retry</button>';
      }
    }

    async function save() {
      // Never save a list that failed to load - an empty array would wipe every page on the board.
      if (!loaded) { document.getElementById("status").textContent = "Not saved: page has not finished loading. Reload and try again."; return false; }
      if (pages.length === 0 && !confirm("This will remove ALL pages. Save anyway?")) return false;
      document.getElementById("status").textContent = "Saving...";
      try {
        const r = await fetch("/gauges", { method: "POST", headers: {"Content-Type":"application/json"}, body: JSON.stringify(pages) });
        if (!r.ok) throw new Error((await r.text()) || ("board returned " + r.status));
        document.getElementById("status").textContent = "Saved to the board's flash. The round display shows it after a reboot - press Save & Reboot Board.";
        return true;
      } catch (e) { document.getElementById("status").textContent = "Save FAILED: " + e.message; return false; }
    }

    // Save, restart the board so the round display rebuilds its pages, and wait for it to come back.
    async function saveAndReboot() {
      if (!(await save())) return;
      const st = document.getElementById("status");
      try { await fetch("/reboot", { method: "POST", headers: {"X-MSP": "1"} }); }
      catch (e) { st.textContent = "Saved, but the reboot request failed: " + e.message; return; }
      st.textContent = "Saved. The board is restarting...";
      const deadline = Date.now() + 45000;
      let wentDown = false;
      while (Date.now() < deadline) {
        await new Promise(r => setTimeout(r, 1500));
        try {
          const r = await fetch("/status.json", { cache: "no-store" });
          if (r.ok && wentDown) { const j = await r.json(); st.textContent = "Board restarted (build " + j.build + "). The round display now shows your changes."; return; }
        } catch (e) { wentDown = true; }
      }
      st.textContent = wentDown ? "Saved, but the board has not come back after 45 seconds. Check its power, then reload this page."
                                : "Saved, but the board never restarted. Press Save & Reboot Board again, or restart it by hand.";
    }

    // Caption text size on every cell (the same choices as each cell's own setting).
    function applyCaptionToAll() {
      const v = parseInt(document.getElementById("bulkCap").value) || 0;
      for (const p of pages) for (const c of p.cells) c.caption_font_size = v;
      renderPages();
      document.getElementById("status").textContent = "Caption size applied locally - click Save to persist.";
    }
    window.addEventListener("load", () => { const el = document.getElementById("bulkCap"); if (el && !el.options.length) el.innerHTML = captionSizeOptions(28); });

    // Needle length (gap + tip trim) and thickness on every cell, without touching colors.
    function applyNeedleToAll() {
      const num = (id, lo, hi, d) => { const v = parseInt(document.getElementById(id).value); return isNaN(v) ? d : Math.min(hi, Math.max(lo, v)); };
      const nw = num("bulkNw", 1, 20, 6), hub = num("bulkHub", 0, 60, 0), tip = Math.min(num("bulkTip", 0, 60, 0), 85 - hub);
      for (const p of pages) for (const c of p.cells) { c.needle_w = nw; c.needle_hub_pct = hub; c.needle_tip_pct = tip; }
      renderPages();
      document.getElementById("status").textContent = "Needle applied locally - click Save to persist.";
    }

    async function applyColorsToAll() {
      const needle = document.getElementById("bulkNeedle").value.substring(1);
      const tick = document.getElementById("bulkTick").value.substring(1);
      const bg = document.getElementById("bulkBg").value.substring(1);
      for (const p of pages) {
        for (const c of p.cells) {
          c.needle_color = parseInt(needle, 16);
          c.tick_color = parseInt(tick, 16);
          c.bg_color = parseInt(bg, 16);
        }
      }
      renderPages();
      document.getElementById("status").textContent = "Applied locally - click Save to persist.";
    }

)rawliteral"
#include "web_indicator_editor.h"
R"rawliteral(
    load();
  </script>
</body>
</html>
)rawliteral";

static const char WEBCFG_HTML[] PROGMEM = PH_HEAD "<title>MSP-DASH Web Dashboard Config</title>" PH_STYLE
R"rawliteral(<style>
    h2 { font-size: 1em; color:#4dd0e1; margin:4px 0; }
    .page-card { background:#0a0a0a; border:1px solid #333; border-radius:8px; padding:12px; margin-bottom:14px; }
    .page-header { display:flex; justify-content:space-between; align-items:center; gap:8px; flex-wrap:wrap; }
    .cell { background:#111; border-radius:8px; padding:10px; margin-top:10px; }
    .cell label { display:block; font-size:0.78em; color:#4dd0e1; margin-top:5px; }
    .cell input, .cell select { width:100%; box-sizing:border-box; padding:4px; margin-top:2px; }
    .cell input[type=checkbox] { width:auto; margin:0 6px 0 0; }
    .row2 { display:flex; gap:8px; }
    .row2 > div { flex:1; }
    button { padding:8px 12px; background:#4dd0e1; color:#000; border:none; border-radius:6px; margin:2px; }
    button.danger { background:#F44336; color:#fff; }
    .bulk { background:#111; border-radius:8px; padding:14px; margin-bottom:14px; }
    .bulk-title { font-weight:bold; }
    .bulk-sub { font-size:0.78em; color:#888; margin:2px 0 12px; }
    .bulk-colors { display:grid; grid-template-columns:repeat(3,1fr); gap:10px; margin-bottom:12px; }
    .bulk-colors label { display:flex; flex-direction:column; gap:5px; font-size:0.78em; color:#4dd0e1; }
    .bulk-colors input[type=color] { width:100%; height:38px; padding:2px; border:1px solid #333; border-radius:6px; background:#000; }
    .bulk-apply { width:100%; margin:0; }
    .hint { font-size:0.75em; color:#888; margin:2px 0; }
    .addpage { width:100%; margin:0 0 14px; background:none; border:1px dashed #4dd0e1; color:#4dd0e1; }
    #status, #boot { text-align:center; margin:8px 0; color:#4dd0e1; min-height:1.2em; }
    #status:empty, #boot:empty { display:none; }
  </style>)rawliteral" PH_OPEN
R"rawliteral(<div class="pad">
  <h1>Web Dashboard Configuration</h1>
  <div id="status"></div>
  <div id="boot">Page script has not started yet...</div>

  <div class="bulk">
    <div class="bulk-title">Apply colors to ALL cells</div>
    <div class="bulk-sub">Sets every cell on every page. Click Save afterward to keep it.</div>
    <div class="bulk-colors">
      <label>Needle<input type="color" id="bulkNeedle" value="#ffffff"></label>
      <label>Tick / Dial<input type="color" id="bulkTick" value="#ffffff"></label>
      <label>Background<input type="color" id="bulkBg" value="#000000"></label>
    </div>
    <button type="button" class="bulk-apply" onclick="applyColorsToAll()">Apply to All</button>
  </div>

  <button type="button" class="addpage" onclick="addPage()">+ Add Page</button>
  <button type="button" class="addpage" onclick="addIndicatorPage()">+ Add Indicators Page</button>
  <div id="pages"></div>
  <button onclick="save()">Save (reboot required to apply)</button>

  
</div>)rawliteral" PH_CLOSE
R"rawliteral(
<script>
    window.onerror = function(msg, src, line) { document.getElementById("boot").textContent = "Script error: " + msg + " (line " + line + ")"; };
    document.getElementById("boot").textContent = "";
    const SIGNALS = ["None","RPM","Coolant Temp","MAP/Boost","AFR","Oil Pressure","Oil Temp","Fuel Pressure","Ethanol %","Throttle Position","Battery Voltage"];
    const LAYOUTS = ["1 Cell (full screen)","2-Up (stacked)","3-Up (1 top, 2 bottom)","2x2 Grid"];
    const CELLS_PER_LAYOUT = [1, 2, 3, 4];
    const GAUGE_TYPES = ["Needle Meter", "Number", "Half-Moon", "Trend Graph"];
    // Readout text sizes (px). Must match GAUGE_VALUE_SIZES in gauge_style.h. A size too big for its cell shrinks to the biggest that fits.
    const VALUE_SIZES = [[14,"14 Small"],[28,"28 Medium"],[36,"36 Large"],[48,"48 XL"],[64,"64 2XL"],[80,"80 3XL"],[96,"96 4XL"],[120,"120 Max"]];
    let loaded = false;
    let pages = [];
    let signalActivity = []; // [{id, name, age_ms}], sorted most-active first

    function emptyCell() {
      return { enabled:false, signal:0, gauge_type:0, min:0, max:100, rest:0, caption:"",
        needle_color:0xFFFFFF, tick_color:0xFFFFFF, bg_color:0x000000, value_divisor:1.0,
        tick_count:26, major_nth:5, refresh_hz:10, show_value:false, value_font_size:28, decimals:1, h_align:0, tick_font_size:28, caption_font_size:0, caption_on_face:false, units:"", v_align:0, start_deg:225, end_deg:135,
        has_warning:false, warning_threshold:0, warning_above:false };
    }

    function signalOptionsHtml(selected) {
      // Sorted by activity (lowest age_ms first = most live), "never seen" last, name as secondary sort.
      const sorted = signalActivity.slice().sort((a, b) => {
        if (a.age_ms !== b.age_ms) return a.age_ms - b.age_ms;
        return a.name.localeCompare(b.name);
      });
      let html = `<option value="0" ${selected===0?"selected":""}>None</option>`;
      for (const s of sorted) {
        if (s.id === 0 || s.flag) continue;   // ECU on/off flags belong on an indicators page, not on a gauge
        const liveTag = s.age_ms < 5000 ? " (live)" : (s.age_ms > 1e8 ? " (no data)" : ` (${Math.round(s.age_ms/1000)}s ago)`);
        html += `<option value="${s.id}" ${selected===s.id?"selected":""}>${s.name}${liveTag}</option>`;
      }
      return html;
    }

    function renderCell(pIdx, cIdx, c) {
      return `
        <div class="cell">
          <label><input type="checkbox" ${c.enabled?"checked":""} onchange="pages[${pIdx}].cells[${cIdx}].enabled=this.checked; renderPages();"> Cell ${cIdx+1} enabled</label>
          ${!c.enabled ? "" : `
          <label>Signal <select onchange="setCellSignal(${pIdx},${cIdx},parseInt(this.value))">${signalOptionsHtml(c.signal)}</select></label>
          <label>Gauge Type <select onchange="pages[${pIdx}].cells[${cIdx}].gauge_type=parseInt(this.value)">
            ${GAUGE_TYPES.map((t,v)=>`<option value="${v}" ${c.gauge_type===v?"selected":""}>${t}</option>`).join("")}
          </select></label>
          <label>Caption <input type="text" value="${escHtml(c.caption)}" onchange="pages[${pIdx}].cells[${cIdx}].caption=this.value"></label>
          <div class="row2">
            <div><label>Caption size <select onchange="setCellField(${pIdx},${cIdx},'caption_font_size',parseInt(this.value))">${captionSizeOptions(c.caption_font_size)}</select></label></div>
            <div><label>Units <input type="text" maxlength="7" value="${escHtml(c.units || "")}" placeholder="e.g. kPa" oninput="setCellField(${pIdx},${cIdx},'units',this.value)"></label></div>
          </div>
          ${c.gauge_type !== 3 ? `<label>Gauge position in the cell
            <select onchange="setCellField(${pIdx},${cIdx},'v_align',parseInt(this.value))">${valignOptions(c.v_align)}</select></label>` : ""}
          <div class="row2">
            <div><label>Min <input type="number" value="${c.min}" onchange="pages[${pIdx}].cells[${cIdx}].min=parseInt(this.value)"></label></div>
            <div><label>Max <input type="number" value="${c.max}" onchange="pages[${pIdx}].cells[${cIdx}].max=parseInt(this.value)"></label></div>
            <div><label>Rest <input type="number" value="${c.rest}" onchange="pages[${pIdx}].cells[${cIdx}].rest=parseInt(this.value)"></label></div>
          </div>
          <div class="row2">
            <div><label>Value Divisor <input type="number" step="any" value="${c.value_divisor}" onchange="pages[${pIdx}].cells[${cIdx}].value_divisor=parseFloat(this.value)"></label></div>
            <div><label>Tick Count <input type="number" value="${c.tick_count}" onchange="pages[${pIdx}].cells[${cIdx}].tick_count=parseInt(this.value)"></label></div>
            <div><label>Major Every Nth <input type="number" value="${c.major_nth}" onchange="pages[${pIdx}].cells[${cIdx}].major_nth=parseInt(this.value)"></label></div>
          </div>
          <div class="row2">
            <div><label>Refresh Rate (Hz) <input type="number" value="${c.refresh_hz}" onchange="pages[${pIdx}].cells[${cIdx}].refresh_hz=parseInt(this.value)"></label></div>
            <div><label><input type="checkbox" ${c.show_value?"checked":""} onchange="pages[${pIdx}].cells[${cIdx}].show_value=this.checked"> Show value</label></div>
          </div>
          <label>Value Text Size
            <select onchange="pages[${pIdx}].cells[${cIdx}].value_font_size=parseInt(this.value)">
              ${VALUE_SIZES.map(([v,n])=>`<option value="${v}" ${c.value_font_size===v?"selected":""}>${n}</option>`).join("")}
            </select>
          </label>
          <label>Decimals
            <select onchange="setCellField(${pIdx},${cIdx},'decimals',parseInt(this.value))">${decimalsOptions(c.decimals)}</select></label>
          <p class="hint">Value text size is the big number. A size too big for the cell shrinks to the largest that fits.</p>
          <label>Needle Color <input type="color" value="#${c.needle_color.toString(16).padStart(6,'0')}" onchange="pages[${pIdx}].cells[${cIdx}].needle_color=parseInt(this.value.substring(1),16)"></label>
          <label>Tick/Dial Color <input type="color" value="#${c.tick_color.toString(16).padStart(6,'0')}" onchange="pages[${pIdx}].cells[${cIdx}].tick_color=parseInt(this.value.substring(1),16)"></label>
          <label>Background Color <input type="color" value="#${c.bg_color.toString(16).padStart(6,'0')}" onchange="pages[${pIdx}].cells[${cIdx}].bg_color=parseInt(this.value.substring(1),16)"></label>
          <label><input type="checkbox" ${c.has_warning?"checked":""} onchange="pages[${pIdx}].cells[${cIdx}].has_warning=this.checked"> Warning enabled</label>
          <div class="row2">
            <div><label>Threshold <input type="number" value="${c.warning_threshold}" onchange="pages[${pIdx}].cells[${cIdx}].warning_threshold=parseFloat(this.value)"></label></div>
            <div><label>Direction
              <select onchange="pages[${pIdx}].cells[${cIdx}].warning_above=(this.value==='1')">
                <option value="1" ${c.warning_above?"selected":""}>Above</option>
                <option value="0" ${!c.warning_above?"selected":""}>Below</option>
              </select>
            </label></div>
          </div>
          `}
        </div>
      `;
    }

    function changeLayout(pIdx, newLayout) {
      pages[pIdx].layout = newLayout;
      const needed = CELLS_PER_LAYOUT[newLayout];
      while (pages[pIdx].cells.length < needed) pages[pIdx].cells.push(emptyCell());
      renderPages();
    }

    function pageAction(pIdx, action) {
      if (action === "delete") {
        pages.splice(pIdx, 1);
      } else if (action === "empty") {
        if (pages[pIdx].kind === 1) pages[pIdx].indicators = [];
        else pages[pIdx].cells = pages[pIdx].cells.map(() => emptyCell());
      } else if (action === "copy") {
        const copy = JSON.parse(JSON.stringify(pages[pIdx]));
        pages.push(copy);
      }
      renderPages();
    }

    function addPage() {
      pages.push({ enabled:true, layout:0, cells:[emptyCell()] });
      renderPages();
    }

    function renderPages() {
      const container = document.getElementById("pages");
      container.innerHTML = pages.map((p, pIdx) => {
        if (p.kind === 1) return renderIndicatorPage(pIdx, p);
        const needed = CELLS_PER_LAYOUT[p.layout];
        while (p.cells.length < needed) p.cells.push(emptyCell());
        let cellsHtml = "";
        for (let c = 0; c < needed; c++) cellsHtml += renderCell(pIdx, c, p.cells[c]);
        return `
          <div class="page-card">
            <div class="page-header">
              <strong>Page ${pIdx + 1}</strong>
              <select onchange="changeLayout(${pIdx}, parseInt(this.value))">
                ${LAYOUTS.map((l,v)=>`<option value="${v}" ${p.layout===v?"selected":""}>${l}</option>`).join("")}
              </select>
              <button type="button" onclick="pageAction(${pIdx},'copy')">Copy</button>
              <button type="button" onclick="pageAction(${pIdx},'empty')">Empty</button>
              <button type="button" class="danger" onclick="pageAction(${pIdx},'delete')">Delete</button>
            </div>
            ${cellsHtml}
          </div>
        `;
      }).join("");
    }

    async function load() {
      document.getElementById("status").textContent = "Loading...";
      try {
        const pagesRes = await fetch("/web-pages.json");
        pages = await pagesRes.json();
        const signalsRes = await fetch("/signals.json");
        signalActivity = await signalsRes.json();
        renderPages();
        loaded = true;
        document.getElementById("status").textContent = "";
      } catch (e) {
        document.getElementById("status").innerHTML = "Failed to load - " + e.message + ' <button type="button" onclick="load()">Retry</button>';
      }
    }

    async function save() {
      // Never save a list that failed to load - an empty array would wipe every page on the board.
      if (!loaded) { document.getElementById("status").textContent = "Not saved: page has not finished loading. Reload and try again."; return; }
      if (pages.length === 0 && !confirm("This will remove ALL pages. Save anyway?")) return;
      document.getElementById("status").textContent = "Saving...";
      try {
        const r = await fetch("/web-pages", { method: "POST", headers: {"Content-Type":"application/json"}, body: JSON.stringify(pages) });
        if (!r.ok) throw new Error((await r.text()) || ("board returned " + r.status));
        document.getElementById("status").textContent = "Saved to the board's flash. Open the front page to see it - no reboot needed.";
      } catch (e) { document.getElementById("status").textContent = "Save FAILED: " + e.message; }
    }

    async function applyColorsToAll() {
      const needle = document.getElementById("bulkNeedle").value.substring(1);
      const tick = document.getElementById("bulkTick").value.substring(1);
      const bg = document.getElementById("bulkBg").value.substring(1);
      for (const p of pages) {
        for (const c of p.cells) {
          c.needle_color = parseInt(needle, 16);
          c.tick_color = parseInt(tick, 16);
          c.bg_color = parseInt(bg, 16);
        }
      }
      renderPages();
      document.getElementById("status").textContent = "Applied locally - click Save to persist.";
    }

)rawliteral"
#include "web_indicator_editor.h"
R"rawliteral(
    load();
  </script>
</body>
</html>
)rawliteral";
static const char SD_HTML[] PROGMEM = PH_HEAD "<title>MSP-DASH SD Card</title>" PH_STYLE
R"rawliteral(<style>
    .tablewrap { overflow-x:auto; }
    table { width:100%; border-collapse:collapse; margin-top:8px; font-size:0.85em; }
    th, td { text-align:left; padding:8px 6px; border-bottom:1px solid #333; }
    th { color:#4dd0e1; }
    td:first-child { word-break:break-all; }
    td:nth-child(2), td:nth-child(3) { white-space:nowrap; }
    a.dl { color:#4dd0e1; text-decoration:none; }
    #uploadForm { margin-top:20px; background:#111; padding:16px; border-radius:8px; }
  </style>)rawliteral" PH_OPEN
R"rawliteral(<div class="pad">
  <h1>SD Card Files</h1>
  <div class="tablewrap">
  <table>
    <thead><tr><th>File</th><th>Size</th><th></th></tr></thead>
    <tbody id="rows"></tbody>
  </table>
  </div>
  <form id="uploadForm">
    <input type="file" id="fileInput">
    <button type="submit">Upload to SD Card</button>
    <div id="uploadStatus"></div>
  </form>
  
</div>)rawliteral" PH_CLOSE
R"rawliteral(
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

static const char SIM_HTML[] PROGMEM = PH_HEAD "<title>MSP-DASH Bench Simulator</title>" PH_STYLE
R"rawliteral(<style>
    .warnbox { background:#2a1f00; border:1px solid #f5a623; border-radius:8px; padding:10px; font-size:0.85em; color:#f5c26b; }
    .statebox { margin:12px 0; padding:10px; border-radius:8px; background:#111; text-align:center; }
    .statebox.on { background:#f5a623; color:#000; font-weight:bold; }
    .btnrow { display:flex; gap:8px; justify-content:center; flex-wrap:wrap; }
    button { padding:10px 14px; background:#4dd0e1; color:#000; border:none; border-radius:6px; }
    #msg { text-align:center; margin:10px 0; min-height:1.2em; color:#4dd0e1; }
    #msg.bad { color:#ff6b6b; }
    .sl { margin:10px 0; }
    .sl label { display:flex; justify-content:space-between; font-size:0.85em; color:#4dd0e1; }
    .sl input { width:100%; }
  </style>)rawliteral" PH_OPEN
R"rawliteral(<div class="pad">
  <h1>Bench Simulator</h1>
  <div class="warnbox"><strong>This feeds FAKE values</strong> to the gauges, graphs and alerts so you can test with no ECU connected. It switches itself off after 10 minutes without a change, at every reboot, and the moment a real CAN frame arrives. SD logging is paused while it runs, and the round screen and the front page both say SIMULATED.</div>
  <div id="state" class="statebox">Loading...</div>
  <div class="btnrow">
    <button onclick="cmd('start','demo')">Start demo</button>
    <button onclick="cmd('start','manual')">Start manual</button>
    <button class="danger" onclick="cmd('stop')">Stop</button>
  </div>
  <div id="msg"></div>
  <div id="sliders"></div>
  
</div>)rawliteral" PH_CLOSE
R"rawliteral(
<script>
    let info = null;
    let building = "";
    let sending = false;
    let pending = null;

    function setMsg(t, bad) {
      const m = document.getElementById("msg");
      m.textContent = t;
      m.className = bad ? "bad" : "";
    }

    async function post(params) {
      const r = await fetch("/sim", { method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body: new URLSearchParams(params).toString() });
      const t = await r.text();
      if (!r.ok) throw new Error(t || ("board returned " + r.status));
      return t;
    }

    async function cmd(c, mode) {
      try { await post(mode ? { cmd: c, mode: mode } : { cmd: c }); setMsg(""); }
      catch (e) { setMsg(e.message, true); }
      refresh();
    }

    function fmtTime(s) { return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0"); }

    function render() {
      const st = document.getElementById("state");
      if (info.active) {
        st.className = "statebox on";
        st.textContent = "SIMULATING (" + (info.manual ? "manual" : "demo") + ") - switches off in " + fmtTime(info.seconds_left);
      } else {
        st.className = "statebox";
        st.textContent = "Off" + (info.stop_reason ? " - last stopped: " + info.stop_reason : "");
      }
      buildSliders();
    }

    function shown(s) { return Number(s.value).toFixed(s.step < 1 ? 1 : 0); }

    function buildSliders() {
      const box = document.getElementById("sliders");
      const key = info.active ? (info.manual ? "manual" : "demo") : "off";
      if (key !== building) {                    // rebuild only when the mode changes, never while a slider is being dragged
        building = key;
        if (key === "off") { box.innerHTML = ""; return; }
        box.innerHTML = info.signals.map(s =>
          `<div class="sl"><label>${s.name} <span id="v${s.id}"></span></label>` +
          `<input id="r${s.id}" type="range" min="${s.min}" max="${s.max}" step="${s.step}" ${info.manual ? "" : "disabled"} oninput="slide(${s.id}, this.value)"></div>`).join("");
      }
      for (const s of info.signals) {
        const lab = document.getElementById("v" + s.id), rng = document.getElementById("r" + s.id);
        if (lab) lab.textContent = shown(s);
        if (rng && document.activeElement !== rng) rng.value = s.value;
      }
    }

    function slide(id, val) {
      document.getElementById("v" + id).textContent = Number(val).toFixed(0);
      pending = { id: id, val: val };
      pump();
    }

    // One request at a time, always the newest slider position - never a pile of requests.
    async function pump() {
      if (sending) return;
      sending = true;
      while (pending) {
        const p = pending; pending = null;
        try { await post({ cmd: "set", sig: p.id, val: p.val }); }
        catch (e) { setMsg(e.message, true); }
      }
      sending = false;
    }

    async function refresh() {
      try {
        const r = await fetch("/sim.json");
        info = await r.json();
        render();
      } catch (e) {
        document.getElementById("state").textContent = "Could not reach the board - retrying...";
      }
    }

    setInterval(refresh, 1000);
    refresh();
  </script>
</body>
</html>
)rawliteral";

static const char LOGCHANS_HTML[] PROGMEM = PH_HEAD "<title>MSP-DASH Log Channels</title>" PH_STYLE
R"rawliteral(<style>
    label { display:flex; align-items:center; gap:10px; padding:10px; background:#111; border-radius:6px; margin-bottom:6px; }
    button { margin-top:10px; }
    #status, #boot { text-align:center; margin-top:10px; color:#4dd0e1; min-height:1.2em; }
    #status:empty, #boot:empty { display:none; }
  </style>)rawliteral" PH_OPEN
R"rawliteral(<div class="pad">
  <h1>Log Channels</h1>
  <p style="text-align:center; color:#888; font-size:0.9em;">Choose which signals get written to the SD card log - independent of what's shown on any display. Logging only writes rows once RPM exceeds 800.</p>
  <div id="channels"></div>
  <button onclick="save()">Save</button>
  <div id="status"></div>
  <div id="boot">Page script has not started yet...</div>
  
</div>)rawliteral" PH_CLOSE
R"rawliteral(
<script>
    window.onerror = function(msg, src, line) { document.getElementById("boot").textContent = "Script error: " + msg + " (line " + line + ")"; };
    document.getElementById("boot").textContent = "";
    let loaded = false;
    let channels = [];
    function render() {
      document.getElementById("channels").innerHTML = channels.map(c => `
        <label><input type="checkbox" ${c.enabled?"checked":""} onchange="channels[${c.id}].enabled=this.checked"> ${c.name}</label>
      `).join("");
    }
    async function load() {
      document.getElementById("status").textContent = "Loading...";
      try {
        const res = await fetch("/log-channels.json");
        channels = await res.json();
        render();
        loaded = true;
        document.getElementById("status").textContent = "";
      } catch (e) {
        document.getElementById("status").innerHTML = "Failed to load - " + e.message + ' <button type="button" onclick="load()">Retry</button>';
      }
    }
    async function save() {
      if (!loaded) { document.getElementById("status").textContent = "Not saved: page has not finished loading. Reload and try again."; return; }
      document.getElementById("status").textContent = "Saving...";
      try {
        const r = await fetch("/log-channels", { method: "POST", headers: {"Content-Type":"application/json"}, body: JSON.stringify(channels) });
        if (!r.ok) throw new Error("board returned " + r.status);
        document.getElementById("status").textContent = "Saved.";
      } catch (e) { document.getElementById("status").textContent = "Save FAILED: " + e.message; }
    }
    load();
  </script>
</body>
</html>
)rawliteral";

static const char OTA_HTML[] PROGMEM = PH_HEAD "<title>MSP-DASH Firmware Update</title>" PH_STYLE
R"rawliteral(<style>
    #updateForm { margin-top:20px; background:#111; padding:16px; border-radius:8px; }
    #bar { width:100%; background:#222; border-radius:6px; margin-top:10px; height:20px; overflow:hidden; }
    #barFill { height:100%; width:0%; background:#4dd0e1; }
    .warn { color:#ff8800; }
  </style>)rawliteral" PH_OPEN
R"rawliteral(<div class="pad">
  <h1>Firmware Update (OTA)</h1>
  <p id="buildinfo" style="text-align:center;color:#4dd0e1;margin:4px 0">Running build ...</p>
  <p class="warn">Upload the .ino.bin file from your build output. The board reboots automatically on success - do not close this page or power off the board during upload.</p>
  <form id="updateForm">
    <input type="file" id="fileInput" accept=".bin">
    <button type="submit">Upload and Flash</button>
    <div id="bar"><div id="barFill"></div></div>
    <div id="status"></div>
  </form>
  
</div>)rawliteral" PH_CLOSE
R"rawliteral(
<script>
    fetch("/status.json").then(r => r.json()).then(st => {
      document.getElementById("buildinfo").textContent = "Running build " + st.build + " (compiled " + st.compiled + ")";
    }).catch(() => { document.getElementById("buildinfo").textContent = ""; });
    document.getElementById("updateForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const file = document.getElementById("fileInput").files[0];
      if (!file) return;
      const status = document.getElementById("status");
      const barFill = document.getElementById("barFill");
      status.textContent = "Uploading...";

      const xhr = new XMLHttpRequest();
      xhr.open("POST", "/update");
      xhr.upload.onprogress = (evt) => {
        if (evt.lengthComputable) {
          const pct = Math.round((evt.loaded / evt.total) * 100);
          barFill.style.width = pct + "%";
          status.textContent = "Uploading... " + pct + "%";
        }
      };
      xhr.onload = () => {
        if (xhr.status === 200) {
          status.textContent = "Update successful - rebooting...";
        } else {
          status.textContent = "Update FAILED: " + xhr.responseText;
        }
      };
      xhr.onerror = () => { status.textContent = "Upload error - connection lost (board may have already rebooted)."; };

      const formData = new FormData();
      formData.append("file", file);
      xhr.send(formData);
    });
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

// The CAN frame table is written by the dedicated CAN receive task
// (can_parsing.h), read by the web server task, and has its Hz values
// recomputed from loop() - so every access goes through this short
// spinlock. Without it, the once-a-second Hz reset could race with a
// frame increment and briefly show a doubled rate on the /can page.
static portMUX_TYPE s_canTableMux = portMUX_INITIALIZER_UNLOCKED;

void web_monitor_track_can_frame(uint32_t id, uint8_t *data, uint8_t dlc) {
  if (dlc > 8) dlc = 8;
  unsigned long now = millis();
  portENTER_CRITICAL(&s_canTableMux);
  for (int i = 0; i < s_canTableCount; i++) {
    if (s_canTable[i].id == id) {
      memcpy(s_canTable[i].data, data, dlc);
      s_canTable[i].dlc = dlc;
      s_canTable[i].lastSeen = now;
      s_canTable[i].countSinceLastCalc++;
      portEXIT_CRITICAL(&s_canTableMux);
      return;
    }
  }
  if (s_canTableCount < MAX_CAN_IDS) {
    CanMonitorEntry *e = &s_canTable[s_canTableCount];
    e->id = id;
    memcpy(e->data, data, dlc);
    e->dlc = dlc;
    e->lastSeen = now;
    e->countSinceLastCalc = 1;
    e->hz = 0;
    s_canTableCount++;
  }
  portEXIT_CRITICAL(&s_canTableMux);
  // If the table is full, new/unseen IDs are silently dropped - 32 is
  // generous headroom for a typical ECU broadcast set, and this is a
  // diagnostic tool, not a requirement for correctness elsewhere.
}

// Total CAN frames/second across every ID seen, and how many distinct IDs -
// lets the web page tell "no CAN traffic at all" apart from "traffic is
// arriving but nothing was decoded".
static void canTableTotals(uint32_t *fps, int *ids) {
  uint32_t total = 0;
  portENTER_CRITICAL(&s_canTableMux);
  for (int i = 0; i < s_canTableCount; i++) total += s_canTable[i].hz;
  *ids = s_canTableCount;
  portEXIT_CRITICAL(&s_canTableMux);
  *fps = total;
}

static void recomputeCanHz() {
  portENTER_CRITICAL(&s_canTableMux);
  for (int i = 0; i < s_canTableCount; i++) {
    s_canTable[i].hz = s_canTable[i].countSinceLastCalc;
    s_canTable[i].countSinceLastCalc = 0;
  }
  portEXIT_CRITICAL(&s_canTableMux);
}

static String buildCanJson() {
  // Copy the table under the lock, then build the JSON from the copy -
  // string building is too slow to do while holding a spinlock.
  CanMonitorEntry snapshot[MAX_CAN_IDS];
  int count;
  portENTER_CRITICAL(&s_canTableMux);
  count = s_canTableCount;
  memcpy(snapshot, s_canTable, sizeof(CanMonitorEntry) * count);
  portEXIT_CRITICAL(&s_canTableMux);

  unsigned long now = millis();
  String json = "[";
  for (int i = 0; i < count; i++) {
    if (i > 0) json += ",";
    CanMonitorEntry *e = &snapshot[i];
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

// ---- static IP (per board, saved in NVS "net"; empty = DHCP) ----
// Set: /net?ip=192.168.55.103&gw=192.168.55.1&mask=255.255.255.0 (&dns= optional, default = gw). Back to DHCP: /net?dhcp=1
// Recovery if a board becomes unreachable: type "dhcp" + Enter in the USB serial monitor.
static bool parseIp(const String &s, IPAddress &out) { return s.length() && out.fromString(s); }

static void netSave(const String &ip, const String &gw, const String &mask, const String &dns) {
  Preferences pr; pr.begin("net", false);
  pr.putString("ip", ip); pr.putString("gw", gw); pr.putString("mask", mask); pr.putString("dns", dns);
  pr.end();
}

static bool netLoad(IPAddress &ip, IPAddress &gw, IPAddress &mask, IPAddress &dns) {
  Preferences pr; pr.begin("net", true);
  String a = pr.getString("ip", ""), b = pr.getString("gw", ""), c = pr.getString("mask", ""), d = pr.getString("dns", "");
  pr.end();
  if (!parseIp(a, ip) || !parseIp(b, gw) || !parseIp(c, mask)) return false;
  if (!parseIp(d, dns)) dns = gw;
  return true;
}

void web_monitor_init(void) {
  WiFi.mode(WIFI_STA);
  {
    IPAddress ip, gw, mask, dns;
    if (netLoad(ip, gw, mask, dns)) {
      WiFi.config(ip, gw, mask, dns);
      Serial.printf("Static IP %s (gateway %s)\n", ip.toString().c_str(), gw.toString().c_str());
    }
  }
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

  // Everything needed to answer "is the board running what I think, and is
  // CAN actually flowing?" - build number, uptime, memory, WiFi, CAN driver
  // health, and per-signal data status.
  server.on("/net", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("dhcp")) {
      netSave("", "", "", "");
      request->send(200, "text/plain", "DHCP set. Rebooting; find the new IP in your router.");
      s_rebootAtMs = millis() + 800; return;
    }
    auto P = [&](const char *k) { return request->hasParam(k) ? request->getParam(k)->value() : String(""); };
    String ip = P("ip"), gw = P("gw"), mask = P("mask"), dns = P("dns");
    IPAddress a, g, m, d;
    if (!parseIp(ip, a) || !parseIp(gw, g) || !parseIp(mask, m) || (dns.length() && !parseIp(dns, d))) {
      request->send(400, "text/plain", "use /net?ip=192.168.55.103&gw=192.168.55.1&mask=255.255.255.0 (&dns= optional) or /net?dhcp=1");
      return;
    }
    if (((uint32_t)a & (uint32_t)m) != ((uint32_t)g & (uint32_t)m) || a == g) {
      request->send(400, "text/plain", "ip and gw must be in the same subnet and not equal; nothing saved");
      return;
    }
    netSave(ip, gw, mask, dns);
    request->send(200, "text/plain", "Static IP " + ip + " saved. Rebooting; open http://" + ip + "/");
    s_rebootAtMs = millis() + 800;
  });

  // Board name (shown on its page and in the main board's list): /name?set=Remote%201 . No reboot needed.
  server.on("/name", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("set")) { request->send(400, "text/plain", "use /name?set=Remote 1"); return; }
    String n = request->getParam("set")->value(); n.trim();
    espnow_link_set_name(n.c_str());
    request->send(200, "text/plain", "Name set to " + n);
  });

  // Performance overlay on the screen: /perf?on=1 or /perf?on=0 (not saved; off after a reboot)
  server.on("/perf", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool on = request->hasParam("on") && request->getParam("on")->value() == "1";
    lcd_perf_overlay(on);
    request->send(200, "text/plain", String("Perf overlay ") + (on ? "on" : "off") + ": " + lcd_perf_fps() + " fps, drawing " + lcd_perf_busy_pct() + "%");
  });

  // Visible screen diameter for the gauge layout: /diameter?set=440 (300-560, 466 = the whole panel). Saved, reboots.
  server.on("/diameter", HTTP_GET, [](AsyncWebServerRequest *request) {
    int d = request->hasParam("set") ? request->getParam("set")->value().toInt() : 0;
    if (d < GAUGE_DIAMETER_MIN || d > GAUGE_DIAMETER_MAX) { request->send(400, "text/plain", "use /diameter?set=466 (300 to 560)"); return; }
    Preferences dp; dp.begin("disp", false); dp.putInt("diam", d); dp.end();
    request->send(200, "text/plain", "Diameter " + String(d) + " saved. Rebooting.");
    s_rebootAtMs = millis() + 800;
  });

  // Board role for the ESP-NOW link: /role?set=main or /role?set=remote, saved, then the board reboots.
  server.on("/role", HTTP_GET, [](AsyncWebServerRequest *request) {
    String r = request->hasParam("set") ? request->getParam("set")->value() : "";
    if (r != "main" && r != "remote") { request->send(400, "text/plain", "use /role?set=main or /role?set=remote"); return; }
    espnow_link_set_remote(r == "remote");
    request->send(200, "text/plain", "Role set to " + r + ". Rebooting.");
    s_rebootAtMs = millis() + 800;
  });

  server.on("/status.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["build"] = FW_BUILD_NUMBER;
    doc["compiled"] = FW_COMPILED;
    doc["uptime_s"] = (uint32_t)(millis() / 1000);
    doc["heap_internal_free"] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    doc["heap_internal_lowest"] = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    doc["psram_free"] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["ws_clients"] = ws.count();
    { Preferences dp; dp.begin("disp", true); doc["diameter"] = dp.getInt("diam", GAUGE_DIAMETER_BOARD_DEFAULT); dp.end(); }
    doc["fps"] = lcd_perf_fps();
    doc["lvgl_busy"] = lcd_perf_busy_pct();
    JsonObject link = doc["link"].to<JsonObject>();
    char mac[18]; espnow_link_peer_mac(mac);
    link["role"] = espnow_link_is_remote() ? "remote" : "main";
    { char nm[ESPNOW_NAME_LEN + 1]; espnow_link_name(nm); link["name"] = nm; }
    link["pps"] = espnow_link_pps();
    link["last_ms"] = espnow_link_last_ms();
    link["from"] = mac;
    link["channel"] = espnow_link_channel();
    link["my_mac"] = WiFi.macAddress();
    { LinkBoard bs[ESPNOW_MAX_BOARDS]; int nb = espnow_link_boards(bs);
      JsonArray arr = link["boards"].to<JsonArray>();
      for (int i = 0; i < nb; i++) { JsonObject o = arr.add<JsonObject>(); o["mac"] = bs[i].mac; o["ip"] = bs[i].ip; o["build"] = bs[i].build; o["name"] = bs[i].name; o["age_ms"] = bs[i].age_ms; } }
    { IPAddress i, g, m, d; doc["static_ip"] = netLoad(i, g, m, d); doc["ip"] = WiFi.localIP().toString(); }

    JsonObject can = doc["can"].to<JsonObject>();
    uint32_t fps; int ids;
    canTableTotals(&fps, &ids);
    can["frames_per_s"] = fps;
    can["ids_seen"] = ids;
    twai_status_info_t st;
    if (twai_get_status_info(&st) == ESP_OK) {
      const char *stateName = st.state == TWAI_STATE_RUNNING ? "running"
                            : st.state == TWAI_STATE_BUS_OFF ? "bus-off"
                            : st.state == TWAI_STATE_RECOVERING ? "recovering" : "stopped";
      can["state"] = stateName;
      can["rx_queue"] = st.msgs_to_rx;
      can["rx_missed"] = st.rx_missed_count;
      can["rx_overrun"] = st.rx_overrun_count;
      can["bus_errors"] = st.bus_error_count;
    } else {
      can["state"] = "driver not installed";
    }

    JsonObject simObj = doc["simulator"].to<JsonObject>();
    simObj["active"] = sim_is_active();
    simObj["manual"] = sim_is_manual();
    simObj["seconds_left"] = sim_seconds_left();
    simObj["last_stop"] = sim_last_stop_reason();

    JsonObject store = doc["storage"].to<JsonObject>();
    store["mounted"] = gauge_config_storage_ok();
    store["last_error"] = gauge_config_last_error();
    store["display_cfg_bytes"] = (uint32_t)gauge_config_saved_bytes(0);
    store["web_cfg_bytes"] = (uint32_t)gauge_config_saved_bytes(1);
    store["flash_free"] = (uint32_t)FFat.freeBytes();

    JsonArray sigs = doc["signals"].to<JsonArray>();
    for (int sig = 1; sig < SIG_COUNT; sig++) {
      JsonObject o = sigs.add<JsonObject>();
      o["name"] = gauge_config_signal_name((SignalId)sig);
      o["has_data"] = gauge_config_signal_has_data((SignalId)sig);
      o["age_ms"] = gauge_config_signal_age_ms((SignalId)sig);
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  // ---- Bench simulator (see simulator.h for its safety rules) ----
  server.on("/simulator", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", SIM_HTML);
  });

  server.on("/sim.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["active"] = sim_is_active();
    doc["manual"] = sim_is_manual();
    doc["seconds_left"] = sim_seconds_left();
    doc["stop_reason"] = sim_last_stop_reason();
    JsonArray arr = doc["signals"].to<JsonArray>();
    for (int sig = 1; sig < SIG_COUNT; sig++) {
      SimRange r = sim_range(sig);
      JsonObject o = arr.add<JsonObject>();
      o["id"] = sig;
      o["name"] = gauge_config_signal_name((SignalId)sig);
      o["min"] = r.min;
      o["max"] = r.max;
      o["step"] = r.step;
      o["value"] = sim_value(sig);
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  server.on("/sim", HTTP_POST, [](AsyncWebServerRequest *request) {
    String cmd = request->hasParam("cmd", true) ? request->getParam("cmd", true)->value() : String("");
    if (cmd == "start") {
      uint32_t fps; int ids;
      canTableTotals(&fps, &ids);
      if (fps > 0) {
        request->send(409, "text/plain", "Real CAN frames are arriving (" + String(fps) + " per second), so the simulator will not start. It only runs while the bus is silent.");
        return;
      }
      bool manual = request->hasParam("mode", true) && request->getParam("mode", true)->value() == "manual";
      sim_start(manual);
      request->send(200, "text/plain", "OK");
    } else if (cmd == "stop") {
      sim_stop("stopped from the web page");
      request->send(200, "text/plain", "OK");
    } else if (cmd == "set") {
      int sig = request->hasParam("sig", true) ? (int)request->getParam("sig", true)->value().toInt() : 0;
      float val = request->hasParam("val", true) ? request->getParam("val", true)->value().toFloat() : NAN;
      if (sim_set_manual(sig, val)) request->send(200, "text/plain", "OK");
      else request->send(409, "text/plain", "The simulator is not running in manual mode");
    } else {
      request->send(400, "text/plain", "Unknown command");
    }
  });

  // Restart the board (so the round display rebuilds its pages from the saved config). Needs a custom
  // header: a web page on another site cannot add one to a request without the browser asking this
  // board's permission first (which it never grants), so nobody can reboot the board from a random page.
  server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasHeader("X-MSP")) { request->send(403, "text/plain", "Forbidden"); return; }
    s_rebootAtMs = millis() + 800;   // let this reply go out first
    if (s_rebootAtMs == 0) s_rebootAtMs = 1;
    request->send(200, "text/plain", "Rebooting");
  });

  server.on("/page-nav", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("delta", true)) {
      int delta = request->getParam("delta", true)->value().toInt();
      g_page_nav_delta = (delta > 0) ? 1 : (delta < 0) ? -1 : 0;
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/gauges", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", GAUGES_HTML);
  });

  server.on("/web-config", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", WEBCFG_HTML);
  });

  server.on("/web-pages.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    gauge_config_pages_to_json(gauge_config_get_web_pages(), doc);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  server.on("/web-pages", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      // Runs after the whole body has been received and handled below, so the
      // answer can say whether the settings were REALLY saved.
      request->send(s_webSave.ok ? 200 : 500, "text/plain", s_webSave.ok ? "OK" : s_webSave.msg);
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      static char *buf = NULL;
      if (!buf) buf = allocPostBuf(CONFIG_POST_MAX);
      if (index == 0) {
        s_webSave.ok = false;
        strlcpy(s_webSave.msg, "The board did not finish receiving the settings", sizeof(s_webSave.msg));
      }
      if (!buf) { strlcpy(s_webSave.msg, "Out of memory on the board", sizeof(s_webSave.msg)); return; }
      if (index + len < CONFIG_POST_MAX) memcpy(buf + index, data, len);
      if (index + len == total) {
        if (total < CONFIG_POST_MAX) buf[total] = '\0';
        applyConfigBody(buf, total, true);
      }
    }
  );


  server.on("/signals.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int sig = 0; sig < SIG_COUNT; sig++) {
      JsonObject o = arr.add<JsonObject>();
      o["id"] = sig;
      o["name"] = gauge_config_signal_name((SignalId)sig);
      o["age_ms"] = gauge_config_signal_age_ms((SignalId)sig);
      o["flag"] = gauge_signal_is_flag(sig);   // an ECU on/off flag: 1 or 0
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  server.on("/gauges.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    gauge_config_pages_to_json(gauge_config_get_pages(), doc);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  // POST body arrives in chunks - accumulate into a static buffer, parse
  // once the full body has been received (index + len == total). Buffer
  // is larger now (nested pages/cells structure) - up to 8 pages x 4
  // cells x ~250 bytes/cell is comfortably under 8KB.
  server.on("/gauges", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      // Runs after the whole body has been received and handled below, so the
      // answer can say whether the settings were REALLY saved.
      request->send(s_displaySave.ok ? 200 : 500, "text/plain", s_displaySave.ok ? "OK" : s_displaySave.msg);
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      static char *buf = NULL;
      if (!buf) buf = allocPostBuf(CONFIG_POST_MAX);
      if (index == 0) {
        s_displaySave.ok = false;
        strlcpy(s_displaySave.msg, "The board did not finish receiving the settings", sizeof(s_displaySave.msg));
      }
      if (!buf) { strlcpy(s_displaySave.msg, "Out of memory on the board", sizeof(s_displaySave.msg)); return; }
      if (index + len < CONFIG_POST_MAX) memcpy(buf + index, data, len);
      if (index + len == total) {
        if (total < CONFIG_POST_MAX) buf[total] = '\0';
        applyConfigBody(buf, total, false);
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

  server.on("/log-channels", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", LOGCHANS_HTML);
  });

  server.on("/log-channels.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int sig = 1; sig < SIG_COUNT; sig++) { // skip SIG_NONE
      JsonObject o = arr.add<JsonObject>();
      o["id"] = sig;
      o["name"] = gauge_config_signal_name((SignalId)sig);
      o["enabled"] = gauge_config_is_log_enabled((SignalId)sig);
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  server.on("/log-channels", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      request->send(200, "text/plain", "OK");
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      static const size_t logChanBufSize = 2048;
      static char *logChanBuf = NULL;
      if (!logChanBuf) logChanBuf = allocPostBuf(logChanBufSize);
      if (!logChanBuf) return; // out of memory - drop this save rather than crash
      if (index == 0) memset(logChanBuf, 0, logChanBufSize);
      if (index + len < logChanBufSize) {
        memcpy(logChanBuf + index, data, len);
      }
      if (index + len == total) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, logChanBuf);
        if (!err) {
          JsonArray arr = doc.as<JsonArray>();
          for (JsonObject o : arr) {
            int id = o["id"] | 0;
            bool enabled = o["enabled"] | false;
            gauge_config_set_log_enabled((SignalId)id, enabled);
          }
          gauge_config_log_save();
        }
      }
    }
  );

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", OTA_HTML);
  });

  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      bool ok = !Update.hasError();
      AsyncWebServerResponse *response = request->beginResponse(ok ? 200 : 500, "text/plain", ok ? "OK" : "Update failed");
      response->addHeader("Connection", "close");
      request->send(response);
      if (ok) {
        delay(500); // let the response actually reach the browser before rebooting
        ESP.restart();
      }
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        Serial.printf("OTA update starting: %s\n", filename.c_str());
        // UPDATE_SIZE_UNKNOWN since AsyncWebServer doesn't give us the
        // total content-length at this callback point - Update.h
        // handles this fine, sizing itself from the actual data written.
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          Update.printError(Serial);
        }
      }
      if (Update.write(data, len) != len) {
        Update.printError(Serial);
      }
      if (final) {
        if (Update.end(true)) {
          Serial.printf("OTA update complete: %u bytes\n", (unsigned)(index + len));
        } else {
          Update.printError(Serial);
        }
      }
    }
  );

  server.addHandler(&ws);
  server.begin();
}

void web_monitor_loop(void) {
  unsigned long now = millis();

  // USB serial commands (115200, end with Enter):
  //   ip <ip> <gateway> <mask> [dns]   static IP, saved, reboots     dhcp           back to DHCP, reboots
  //   role main | role remote          ESP-NOW role, saved, reboots  info           role, IP, build
  static char line[80]; static uint8_t n = 0;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      line[n] = 0; n = 0;
      char a[20] = "", b[20] = "", c[20] = "", d[20] = "", cmd[8] = "";
      int k = sscanf(line, "%7s %19s %19s %19s %19s", cmd, a, b, c, d);
      if (k <= 0) continue;
      if (!strcmp(cmd, "dhcp")) { netSave("", "", "", ""); Serial.println("OK: DHCP, rebooting"); delay(100); ESP.restart(); }
      else if (!strcmp(cmd, "ip")) {
        IPAddress i, g, m, x;
        if (k < 4 || !parseIp(a, i) || !parseIp(b, g) || !parseIp(c, m) || (k == 5 && !parseIp(d, x))) Serial.println("usage: ip 192.168.55.231 192.168.55.1 255.255.255.0 [dns]");
        else if (((uint32_t)i & (uint32_t)m) != ((uint32_t)g & (uint32_t)m) || i == g) Serial.println("ERROR: ip and gateway must be in the same subnet and not equal; nothing saved");
        else { netSave(a, b, c, k == 5 ? d : ""); Serial.printf("OK: static IP %s, rebooting\n", a); delay(100); ESP.restart(); }
      }
      else if (!strcmp(cmd, "role")) {
        if (!strcmp(a, "main") || !strcmp(a, "remote")) { espnow_link_set_remote(!strcmp(a, "remote")); Serial.printf("OK: role %s, rebooting\n", a); delay(100); ESP.restart(); }
        else Serial.println("usage: role main | role remote");
      }
      else if (!strcmp(cmd, "name")) {
        const char *nm = line + 4; while (*nm == ' ') nm++;
        espnow_link_set_name(nm); Serial.printf("OK: name \"%s\"\n", nm);
      }
      else if (!strcmp(cmd, "perf")) {
        bool on = !strcmp(a, "on"); lcd_perf_overlay(on);
        Serial.printf("OK: perf overlay %s | %lu fps, drawing %lu%%\n", on ? "on" : "off", (unsigned long)lcd_perf_fps(), (unsigned long)lcd_perf_busy_pct());
      }
      else if (!strcmp(cmd, "diameter")) {
        int d = atoi(a);
        if (d < GAUGE_DIAMETER_MIN || d > GAUGE_DIAMETER_MAX) Serial.println("usage: diameter 466 (300 to 560)");
        else { Preferences dp; dp.begin("disp", false); dp.putInt("diam", d); dp.end(); Serial.printf("OK: diameter %d, rebooting\n", d); delay(100); ESP.restart(); }
      }
      else if (!strcmp(cmd, "info")) {
        IPAddress i, g, m, x; bool st = netLoad(i, g, m, x);
        char nm[ESPNOW_NAME_LEN + 1]; espnow_link_name(nm);
        Serial.printf("%s | build %d | role %s | IP %s (%s) | MAC %s\n", nm, FW_BUILD_NUMBER, espnow_link_is_remote() ? "remote" : "main",
                      WiFi.localIP().toString().c_str(), st ? "static" : "DHCP", WiFi.macAddress().c_str());
      }
      else Serial.println("commands: info | ip <ip> <gw> <mask> [dns] | dhcp | role main|remote | name <text> | perf on|off | diameter <px>");
    } else if (n < sizeof(line) - 1) line[n++] = ch;
  }

  if (s_rebootAtMs != 0 && (long)(now - s_rebootAtMs) >= 0) {
    Serial.println("Rebooting (requested from the web page)");
    delay(50);
    ESP.restart();
  }

  // Heap readout every 10s - lets us see whether free internal RAM
  // (and the largest single free block) is getting dangerously low
  // while the web pages are misbehaving.
  static unsigned long s_lastHeapLog = 0;
  if (now - s_lastHeapLog >= 10000) {
    s_lastHeapLog = now;
    Serial.printf("Build %d | Heap: internal free=%u largest=%u lowest-ever=%u | PSRAM free=%u\n",
      FW_BUILD_NUMBER,
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  }

  // Recompute Hz for each tracked CAN ID once per second.
  if (now - s_lastHzCalc >= 1000) {
    s_lastHzCalc = now;
    recomputeCanHz();
  }

  if (now - s_lastBroadcast < BROADCAST_INTERVAL_MS) return;
  s_lastBroadcast = now;

  ws.cleanupClients();
  if (ws.count() == 0) return; // no connected clients - skip building/sending JSON

  // Generic broadcast - every signal's current value, keyed by its
  // SignalId number. Replaces the old fixed set of named fields
  // (clt/map/afr/etc), since the web dashboard's cells can now be
  // configured to show ANY signal in ANY arrangement, independent of
  // the physical display's own configuration.
  // Message shape: {"v":{"<signal id>":value,...},"fps":<CAN frames/s>,"ids":<distinct CAN IDs>}
  // "v" only holds signals that currently have live data; the page shows
  // "--" for the rest. fps/ids let the page say WHY there is no data
  // (no CAN traffic at all vs traffic that isn't being decoded). The
  // message goes out every 500ms even when "v" is empty, so it doubles as
  // the page's heartbeat.
  JsonDocument doc;
  JsonObject values = doc["v"].to<JsonObject>();
  for (int sig = 1; sig < SIG_COUNT; sig++) { // skip SIG_NONE (0)
    // No live data (never seen, or the bus went quiet): omit it, and the
    // page shows "--" instead of 0.0 or a frozen last reading.
    if (!gauge_config_signal_has_data((SignalId)sig)) continue;
    char key[4];
    snprintf(key, sizeof(key), "%d", sig);
    values[key] = gauge_config_signal_value((SignalId)sig);
  }
  uint32_t canFps; int canIds;
  canTableTotals(&canFps, &canIds);
  doc["fps"] = canFps;
  doc["ids"] = canIds;
  doc["sim"] = sim_is_active(); // the page shows a SIMULATED bar whenever this is true
  String json;
  serializeJson(doc, json);
  ws.textAll(json);
}
