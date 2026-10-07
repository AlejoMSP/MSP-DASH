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
#include "wifi_config.h" // local, git-ignored - see wifi_config.h.example
#include "esp_heap_caps.h"

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

#define SD_MOUNT_POINT "/sd_card"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MSP-DASH</title>
  <style>
    * { box-sizing: border-box; }
    body { background:#222; color:#fff; font-family: sans-serif; margin:0; padding:0; display:flex; justify-content:center; }

    .phone-outer { width:100%; max-width:430px; min-height:100vh; background:#000; position:relative; }

    /* Phone-frame mockup - only applied on screens wide enough that
       we're clearly not actually viewing this on a phone. iPhone 15 Pro
       Max's CSS viewport (430x932) is the sizing target throughout. */
    @media (min-width: 500px) {
      body { padding:40px 0; align-items:flex-start; }
      .phone-outer {
        min-height:932px;
        border-radius:48px;
        border:10px solid #111;
        box-shadow: 0 20px 60px rgba(0,0,0,0.6);
        overflow:hidden;
      }
      .phone-outer::before {
        content:"";
        position:absolute;
        top:14px; left:50%; transform:translateX(-50%);
        width:120px; height:28px;
        background:#111;
        border-radius:16px;
        z-index:10;
      }
    }

    .topbar { display:flex; align-items:center; justify-content:space-between; padding:16px; }
    .topbar h1 { font-size:1.1em; margin:0; }
    .hamburger { background:none; border:1px solid #444; color:#fff; font-size:1.3em; padding:2px 10px; border-radius:6px; }
    .navmenu { display:none; flex-direction:column; background:#111; border-radius:8px; padding:10px; margin:0 16px 10px; }
    .navmenu.open { display:flex; }
    .navmenu a { color:#4dd0e1; padding:8px; text-decoration:none; }

    .dash-nav { display:flex; justify-content:space-between; padding:0 16px; margin-bottom:10px; }
    .dash-nav button { background:#4dd0e1; color:#000; border:none; border-radius:6px; padding:8px 14px; }

    .cellgrid { display:grid; gap:3px; padding:0 3px 3px; min-height:600px; }
    .layout-1 { grid-template-columns: 1fr; grid-template-rows: 1fr; }
    .layout-2up { grid-template-columns: 1fr; grid-template-rows: 1fr 1fr; }
    .layout-3up { grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr; }
    .layout-3up .c0 { grid-column: 1 / 3; }
    .layout-2x2 { grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr; }

    .dcell { background:#0d1b2a; border:1px solid #223; display:flex; flex-direction:column; align-items:center; justify-content:center; }
    .dcell .cap { font-size:0.85em; color:#4dd0e1; }
    .dcell .val { font-size:2.2em; font-weight:bold; margin-top:4px; }
    .dcell .warn { color:#ff4444; }
    .empty-msg { padding:20px; text-align:center; color:#888; }
  </style>
</head>
<body>
  <div class="phone-outer">
    <div class="topbar">
      <h1>MSP-DASH</h1>
      <button class="hamburger" onclick="document.getElementById('navmenu').classList.toggle('open')">&#9776;</button>
    </div>
    <div class="navmenu" id="navmenu">
      <a href="/can">Raw CAN Monitor &rarr;</a>
      <a href="/update">Firmware Update &rarr;</a>
      <a href="/gauges">Display Gauge Config &rarr;</a>
      <a href="/web-config">Web Dashboard Config &rarr;</a>
      <a href="/log-channels">Log Channels &rarr;</a>
      <a href="/sd">SD Card &rarr;</a>
    </div>
    <div class="dash-nav">
      <button onclick="changeWebPage(-1)">&larr; Prev</button>
      <button onclick="changeWebPage(1)">Next &rarr;</button>
    </div>
    <div class="cellgrid" id="cellgrid"></div>
  </div>

  <script>
    // NOTE: cells currently render as plain text (caption + live
    // number) regardless of configured gauge_type (needle/half-moon/
    // number) - true graphical needle rendering for the web view is
    // part of the separate HTML5 live-preview feature, not yet built.
    const LAYOUT_CLASSES = ["layout-1", "layout-2up", "layout-3up", "layout-2x2"];
    let webPages = [];
    let currentWebPage = 0;
    let signalValues = {};

    function render() {
      const grid = document.getElementById("cellgrid");
      if (webPages.length === 0) {
        grid.className = "cellgrid";
        grid.innerHTML = "<p class='empty-msg'>No web dashboard pages configured.<br>Visit Web Dashboard Config in the menu.</p>";
        return;
      }
      const page = webPages[currentWebPage];
      grid.className = "cellgrid " + LAYOUT_CLASSES[page.layout];
      grid.innerHTML = page.cells.map((c, i) => {
        if (!c.enabled) return `<div class="dcell c${i}"></div>`;
        const raw = signalValues[c.signal];
        const value = (raw === undefined) ? "--" : (raw / c.value_divisor).toFixed(1);
        const displayVal = raw === undefined ? value : parseFloat(value);
        const isWarn = c.has_warning && raw !== undefined && (c.warning_above ? displayVal > c.warning_threshold : displayVal < c.warning_threshold);
        return `<div class="dcell c${i}"><div class="cap">${c.caption}</div><div class="val ${isWarn?'warn':''}">${value}</div></div>`;
      }).join("");
    }

    function changeWebPage(delta) {
      if (webPages.length === 0) return;
      currentWebPage = (currentWebPage + delta + webPages.length) % webPages.length;
      render();
    }

    async function loadPages() {
      const res = await fetch("/web-pages.json");
      webPages = await res.json();
      render();
    }

    const ws = new WebSocket("ws://" + location.host + "/ws");
    ws.onmessage = (event) => {
      signalValues = JSON.parse(event.data);
      render();
    };

    loadPages();
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
  <p style="text-align:center"><a href="/" style="color:#4dd0e1">&larr; Back to monitor</a></p>
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
    h2 { font-size: 1em; color:#4dd0e1; margin:4px 0; }
    .page-card { background:#0a0a0a; border:1px solid #333; border-radius:8px; padding:12px; margin-bottom:14px; }
    .page-header { display:flex; justify-content:space-between; align-items:center; gap:8px; flex-wrap:wrap; }
    .cell { background:#111; border-radius:8px; padding:10px; margin-top:10px; }
    .cell label { display:block; font-size:0.78em; color:#4dd0e1; margin-top:5px; }
    .cell input, .cell select { width:100%; box-sizing:border-box; padding:4px; margin-top:2px; }
    .row2 { display:flex; gap:8px; }
    .row2 > div { flex:1; }
    button { padding:8px 12px; background:#4dd0e1; color:#000; border:none; border-radius:6px; margin:2px; }
    button.danger { background:#F44336; color:#fff; }
    .bulk { background:#111; border-radius:8px; padding:12px; margin-bottom:14px; }
    #status, #boot { text-align:center; margin:8px 0; color:#4dd0e1; min-height:1.2em; }
  </style>
</head>
<body>
  <h1>Gauge Configuration</h1>
  <p style="text-align:center"><a href="/" style="color:#4dd0e1">&larr; Back to monitor</a></p>
  <div id="status"></div>
  <div id="boot">Page script has not started yet...</div>

  <div class="bulk">
    <strong>Apply colors to ALL cells on ALL pages</strong>
    <div class="row2">
      <div><label>Needle <input type="color" id="bulkNeedle" value="#ffffff"></label></div>
      <div><label>Tick/Dial <input type="color" id="bulkTick" value="#ffffff"></label></div>
      <div><label>Background <input type="color" id="bulkBg" value="#000000"></label></div>
    </div>
    <button type="button" onclick="applyColorsToAll()">Apply to All</button>
  </div>

  <button type="button" onclick="addPage()">+ Add Page</button>
  <div id="pages"></div>
  <button onclick="save()">Save (reboot required to apply)</button>

  <script>
    window.onerror = function(msg, src, line) { document.getElementById("boot").textContent = "Script error: " + msg + " (line " + line + ")"; };
    document.getElementById("boot").textContent = "";
    const SIGNALS = ["None","RPM","Coolant Temp","MAP/Boost","AFR","Oil Pressure","Oil Temp","Fuel Pressure","Ethanol %","Throttle Position","Battery Voltage"];
    const LAYOUTS = ["1 Cell (full screen)","2-Up (stacked)","3-Up (1 top, 2 bottom)","2x2 Grid"];
    const CELLS_PER_LAYOUT = [1, 2, 3, 4];
    const GAUGE_TYPES = ["Needle Meter", "Number", "Half-Moon", "Trend Graph"];
    let loaded = false;
    let pages = [];
    let signalActivity = []; // [{id, name, age_ms}], sorted most-active first

    function emptyCell() {
      return { enabled:false, signal:0, gauge_type:0, min:0, max:100, rest:0, caption:"",
        needle_color:0xFFFFFF, tick_color:0xFFFFFF, bg_color:0x000000, value_divisor:1.0,
        tick_count:26, major_nth:5, refresh_hz:10, show_value:false, value_font_size:28,
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
        if (s.id === 0) continue;
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
          <label>Signal <select onchange="pages[${pIdx}].cells[${cIdx}].signal=parseInt(this.value)">${signalOptionsHtml(c.signal)}</select></label>
          <label>Gauge Type <select onchange="pages[${pIdx}].cells[${cIdx}].gauge_type=parseInt(this.value); renderPages();">
            ${GAUGE_TYPES.map((t,v)=>`<option value="${v}" ${c.gauge_type===v?"selected":""}>${t}</option>`).join("")}
          </select></label>
          <label>Caption <input type="text" value="${c.caption}" onchange="pages[${pIdx}].cells[${cIdx}].caption=this.value"></label>
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
            <div><label>Value Text Size
              <select onchange="pages[${pIdx}].cells[${cIdx}].value_font_size=parseInt(this.value)">
                <option value="14" ${c.value_font_size===14?"selected":""}>Small</option>
                <option value="28" ${c.value_font_size===28?"selected":""}>Medium</option>
                <option value="36" ${c.value_font_size===36?"selected":""}>Large</option>
              </select>
            </label></div>
          </div>
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
        pages[pIdx].cells = pages[pIdx].cells.map(() => emptyCell());
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
      if (!loaded) { document.getElementById("status").textContent = "Not saved: page has not finished loading. Reload and try again."; return; }
      if (pages.length === 0 && !confirm("This will remove ALL pages. Save anyway?")) return;
      document.getElementById("status").textContent = "Saving...";
      try {
        const r = await fetch("/gauges", { method: "POST", headers: {"Content-Type":"application/json"}, body: JSON.stringify(pages) });
        if (!r.ok) throw new Error("board returned " + r.status);
        document.getElementById("status").textContent = "Saved. Reboot the board to apply changes.";
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

    load();
  </script>
</body>
</html>
)rawliteral";

static const char WEBCFG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MSP-DASH Web Dashboard Config</title>
  <style>
    body { background:#000; color:#fff; font-family: sans-serif; margin:0; padding:20px; }
    h1 { font-size: 1.2em; text-align:center; }
    h2 { font-size: 1em; color:#4dd0e1; margin:4px 0; }
    .page-card { background:#0a0a0a; border:1px solid #333; border-radius:8px; padding:12px; margin-bottom:14px; }
    .page-header { display:flex; justify-content:space-between; align-items:center; gap:8px; flex-wrap:wrap; }
    .cell { background:#111; border-radius:8px; padding:10px; margin-top:10px; }
    .cell label { display:block; font-size:0.78em; color:#4dd0e1; margin-top:5px; }
    .cell input, .cell select { width:100%; box-sizing:border-box; padding:4px; margin-top:2px; }
    .row2 { display:flex; gap:8px; }
    .row2 > div { flex:1; }
    button { padding:8px 12px; background:#4dd0e1; color:#000; border:none; border-radius:6px; margin:2px; }
    button.danger { background:#F44336; color:#fff; }
    .bulk { background:#111; border-radius:8px; padding:12px; margin-bottom:14px; }
    #status, #boot { text-align:center; margin:8px 0; color:#4dd0e1; min-height:1.2em; }
  </style>
</head>
<body>
  <h1>Web Dashboard Configuration</h1>
  <p style="text-align:center"><a href="/" style="color:#4dd0e1">&larr; Back to monitor</a></p>
  <div id="status"></div>
  <div id="boot">Page script has not started yet...</div>

  <div class="bulk">
    <strong>Apply colors to ALL cells on ALL pages</strong>
    <div class="row2">
      <div><label>Needle <input type="color" id="bulkNeedle" value="#ffffff"></label></div>
      <div><label>Tick/Dial <input type="color" id="bulkTick" value="#ffffff"></label></div>
      <div><label>Background <input type="color" id="bulkBg" value="#000000"></label></div>
    </div>
    <button type="button" onclick="applyColorsToAll()">Apply to All</button>
  </div>

  <button type="button" onclick="addPage()">+ Add Page</button>
  <div id="pages"></div>
  <button onclick="save()">Save (reboot required to apply)</button>

  <script>
    window.onerror = function(msg, src, line) { document.getElementById("boot").textContent = "Script error: " + msg + " (line " + line + ")"; };
    document.getElementById("boot").textContent = "";
    const SIGNALS = ["None","RPM","Coolant Temp","MAP/Boost","AFR","Oil Pressure","Oil Temp","Fuel Pressure","Ethanol %","Throttle Position","Battery Voltage"];
    const LAYOUTS = ["1 Cell (full screen)","2-Up (stacked)","3-Up (1 top, 2 bottom)","2x2 Grid"];
    const CELLS_PER_LAYOUT = [1, 2, 3, 4];
    const GAUGE_TYPES = ["Needle Meter", "Number", "Half-Moon", "Trend Graph"];
    let loaded = false;
    let pages = [];
    let signalActivity = []; // [{id, name, age_ms}], sorted most-active first

    function emptyCell() {
      return { enabled:false, signal:0, gauge_type:0, min:0, max:100, rest:0, caption:"",
        needle_color:0xFFFFFF, tick_color:0xFFFFFF, bg_color:0x000000, value_divisor:1.0,
        tick_count:26, major_nth:5, refresh_hz:10, show_value:false, value_font_size:28,
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
        if (s.id === 0) continue;
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
          <label>Signal <select onchange="pages[${pIdx}].cells[${cIdx}].signal=parseInt(this.value)">${signalOptionsHtml(c.signal)}</select></label>
          <label>Gauge Type <select onchange="pages[${pIdx}].cells[${cIdx}].gauge_type=parseInt(this.value)">
            ${GAUGE_TYPES.map((t,v)=>`<option value="${v}" ${c.gauge_type===v?"selected":""}>${t}</option>`).join("")}
          </select></label>
          <label>Caption <input type="text" value="${c.caption}" onchange="pages[${pIdx}].cells[${cIdx}].caption=this.value"></label>
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
            <div><label>Value Text Size
              <select onchange="pages[${pIdx}].cells[${cIdx}].value_font_size=parseInt(this.value)">
                <option value="14" ${c.value_font_size===14?"selected":""}>Small</option>
                <option value="28" ${c.value_font_size===28?"selected":""}>Medium</option>
                <option value="36" ${c.value_font_size===36?"selected":""}>Large</option>
              </select>
            </label></div>
          </div>
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
        pages[pIdx].cells = pages[pIdx].cells.map(() => emptyCell());
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
        if (!r.ok) throw new Error("board returned " + r.status);
        document.getElementById("status").textContent = "Saved. Reboot the board to apply changes.";
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

static const char LOGCHANS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MSP-DASH Log Channels</title>
  <style>
    body { background:#000; color:#fff; font-family: sans-serif; margin:0; padding:20px; }
    h1 { font-size: 1.2em; text-align:center; }
    label { display:flex; align-items:center; gap:10px; padding:10px; background:#111; border-radius:6px; margin-bottom:6px; }
    button { padding:10px 16px; background:#4dd0e1; color:#000; border:none; border-radius:6px; margin-top:10px; }
    #status, #boot { text-align:center; margin-top:10px; color:#4dd0e1; min-height:1.2em; }
  </style>
</head>
<body>
  <h1>Log Channels</h1>
  <p style="text-align:center"><a href="/" style="color:#4dd0e1">&larr; Back to monitor</a></p>
  <p style="text-align:center; color:#888; font-size:0.9em;">Choose which signals get written to the SD card log - independent of what's shown on any display. Logging only writes rows once RPM exceeds 800.</p>
  <div id="channels"></div>
  <button onclick="save()">Save</button>
  <div id="status"></div>
  <div id="boot">Page script has not started yet...</div>
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

static const char OTA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MSP-DASH Firmware Update</title>
  <style>
    body { background:#000; color:#fff; font-family: sans-serif; margin:0; padding:20px; }
    h1 { font-size: 1.2em; text-align:center; }
    #updateForm { margin-top:20px; background:#111; padding:16px; border-radius:8px; }
    button { padding:10px 16px; background:#4dd0e1; color:#000; border:none; border-radius:6px; }
    #bar { width:100%; background:#222; border-radius:6px; margin-top:10px; height:20px; overflow:hidden; }
    #barFill { height:100%; width:0%; background:#4dd0e1; }
    .warn { color:#ff8800; }
  </style>
</head>
<body>
  <h1>Firmware Update (OTA)</h1>
  <p style="text-align:center"><a href="/" style="color:#4dd0e1">&larr; Back to monitor</a></p>
  <p class="warn">Upload the .ino.bin file from your build output. The board reboots automatically on success - do not close this page or power off the board during upload.</p>
  <form id="updateForm">
    <input type="file" id="fileInput" accept=".bin">
    <button type="submit">Upload and Flash</button>
    <div id="bar"><div id="barFill"></div></div>
    <div id="status"></div>
  </form>
  <script>
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
    PageConfig *pageConfigs = gauge_config_get_web_pages();
    JsonDocument doc;
    JsonArray pagesArr = doc.to<JsonArray>();
    for (int i = 0; i < MAX_PAGES; i++) {
      if (!pageConfigs[i].enabled) continue;
      JsonObject pageObj = pagesArr.add<JsonObject>();
      pageObj["enabled"] = pageConfigs[i].enabled;
      pageObj["layout"] = (int)pageConfigs[i].layout;
      JsonArray cellsArr = pageObj["cells"].to<JsonArray>();
      int numCells = gauge_config_cells_for_layout(pageConfigs[i].layout);
      for (int c = 0; c < numCells; c++) {
        CellConfig *cell = &pageConfigs[i].cells[c];
        JsonObject o = cellsArr.add<JsonObject>();
        o["enabled"] = cell->enabled;
        o["signal"] = (int)cell->signal;
        o["gauge_type"] = (int)cell->gauge_type;
        o["min"] = cell->min_val;
        o["max"] = cell->max_val;
        o["rest"] = cell->rest_val;
        o["caption"] = cell->caption;
        o["needle_color"] = cell->needle_color;
        o["tick_color"] = cell->tick_color;
        o["bg_color"] = cell->bg_color;
        o["value_divisor"] = cell->value_divisor;
        o["tick_count"] = cell->tick_count;
        o["major_nth"] = cell->major_nth;
        o["refresh_hz"] = cell->refresh_hz;
        o["show_value"] = cell->show_value;
        o["value_font_size"] = cell->value_font_size;
        o["has_warning"] = cell->has_warning;
        o["warning_threshold"] = cell->warning_threshold;
        o["warning_above"] = cell->warning_above;
        o["alert_fullscreen"] = cell->alert_fullscreen;
        o["history_sec"] = cell->history_sec;
        o["ref_enabled"] = cell->ref_enabled;
        o["ref_value"] = cell->ref_value;
      }
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  server.on("/web-pages", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      request->send(200, "text/plain", "OK");
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      static const size_t webBodyBufSize = 24576;
      static char *webBodyBuf = NULL;
      if (!webBodyBuf) webBodyBuf = allocPostBuf(webBodyBufSize);
      if (!webBodyBuf) return; // out of memory - drop this save rather than crash
      if (index == 0) memset(webBodyBuf, 0, webBodyBufSize);
      if (index + len < webBodyBufSize) {
        memcpy(webBodyBuf + index, data, len);
      }
      if (index + len == total) {
        Serial.printf("Web dashboard config POST: total body size %u bytes (buffer is %u)\n", (unsigned)total, (unsigned)webBodyBufSize);
        if (total >= webBodyBufSize) {
          Serial.println("WARNING: POST body may have been truncated - buffer too small!");
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, webBodyBuf);
        if (err) {
          Serial.printf("Web dashboard config JSON parse FAILED: %s\n", err.c_str());
        }
        if (!err) {
          PageConfig *pageConfigs = gauge_config_get_web_pages();
          JsonArray pagesArr = doc.as<JsonArray>();
          int i = 0;
          for (JsonObject pageObj : pagesArr) {
            if (i >= MAX_PAGES) break;
            pageConfigs[i].enabled = pageObj["enabled"] | false;
            pageConfigs[i].layout = (LayoutType)(int)(pageObj["layout"] | 0);
            JsonArray cellsArr = pageObj["cells"];
            int c = 0;
            for (JsonObject o : cellsArr) {
              if (c >= MAX_CELLS_PER_PAGE) break;
              CellConfig *cell = &pageConfigs[i].cells[c];
              cell->enabled = o["enabled"] | false;
              cell->signal = (SignalId)(int)(o["signal"] | 0);
              cell->gauge_type = (GaugeType)(int)(o["gauge_type"] | 0);
              cell->min_val = o["min"] | 0;
              cell->max_val = o["max"] | 100;
              cell->rest_val = o["rest"] | 0;
              strlcpy(cell->caption, (o["caption"] | ""), GAUGE_CAPTION_MAXLEN);
              cell->needle_color = (uint32_t)(long)(o["needle_color"] | 0xFFFFFF);
              cell->tick_color = (uint32_t)(long)(o["tick_color"] | 0xFFFFFF);
              cell->bg_color = (uint32_t)(long)(o["bg_color"] | 0x000000);
              cell->value_divisor = o["value_divisor"] | 1.0f;
              cell->tick_count = o["tick_count"] | 26;
              cell->major_nth = o["major_nth"] | 5;
              cell->refresh_hz = o["refresh_hz"] | 10;
              cell->show_value = o["show_value"] | false;
              cell->value_font_size = o["value_font_size"] | 28;
              cell->has_warning = o["has_warning"] | false;
              cell->warning_threshold = o["warning_threshold"] | 0.0f;
              cell->warning_above = o["warning_above"] | false;
              cell->alert_fullscreen = o["alert_fullscreen"] | false;
              cell->history_sec = o["history_sec"] | 30;
              cell->ref_enabled = o["ref_enabled"] | false;
              cell->ref_value = o["ref_value"] | 0.0f;
              c++;
            }
            i++;
          }
          for (; i < MAX_PAGES; i++) {
            pageConfigs[i].enabled = false;
          }
          gauge_config_web_save();
        }
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
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  server.on("/gauges.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    PageConfig *pageConfigs = gauge_config_get_pages();
    JsonDocument doc;
    JsonArray pagesArr = doc.to<JsonArray>();
    for (int i = 0; i < MAX_PAGES; i++) {
      if (!pageConfigs[i].enabled) continue;
      JsonObject pageObj = pagesArr.add<JsonObject>();
      pageObj["enabled"] = pageConfigs[i].enabled;
      pageObj["layout"] = (int)pageConfigs[i].layout;
      JsonArray cellsArr = pageObj["cells"].to<JsonArray>();
      int numCells = gauge_config_cells_for_layout(pageConfigs[i].layout);
      for (int c = 0; c < numCells; c++) {
        CellConfig *cell = &pageConfigs[i].cells[c];
        JsonObject o = cellsArr.add<JsonObject>();
        o["enabled"] = cell->enabled;
        o["signal"] = (int)cell->signal;
        o["gauge_type"] = (int)cell->gauge_type;
        o["min"] = cell->min_val;
        o["max"] = cell->max_val;
        o["rest"] = cell->rest_val;
        o["caption"] = cell->caption;
        o["needle_color"] = cell->needle_color;
        o["tick_color"] = cell->tick_color;
        o["bg_color"] = cell->bg_color;
        o["value_divisor"] = cell->value_divisor;
        o["tick_count"] = cell->tick_count;
        o["major_nth"] = cell->major_nth;
        o["refresh_hz"] = cell->refresh_hz;
        o["show_value"] = cell->show_value;
        o["value_font_size"] = cell->value_font_size;
        o["has_warning"] = cell->has_warning;
        o["warning_threshold"] = cell->warning_threshold;
        o["warning_above"] = cell->warning_above;
        o["alert_fullscreen"] = cell->alert_fullscreen;
        o["history_sec"] = cell->history_sec;
        o["ref_enabled"] = cell->ref_enabled;
        o["ref_value"] = cell->ref_value;
      }
    }
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
      request->send(200, "text/plain", "OK - reboot to apply");
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      // Sized generously: 8 pages x 4 cells x ~18 fields/cell can
      // genuinely exceed 8KB once multi-cell layouts (2x2 etc) are in
      // use - the old 8192 buffer silently truncated larger payloads,
      // causing deserializeJson() to fail and the whole save to be
      // silently dropped (looked like "nothing happened", including
      // deletes never actually persisting).
      static const size_t bodyBufSize = 24576;
      static char *bodyBuf = NULL;
      if (!bodyBuf) bodyBuf = allocPostBuf(bodyBufSize);
      if (!bodyBuf) return; // out of memory - drop this save rather than crash
      if (index == 0) memset(bodyBuf, 0, bodyBufSize);
      if (index + len < bodyBufSize) {
        memcpy(bodyBuf + index, data, len);
      }
      if (index + len == total) {
        Serial.printf("Gauge config POST: total body size %u bytes (buffer is %u)\n", (unsigned)total, (unsigned)bodyBufSize);
        if (total >= bodyBufSize) {
          Serial.println("WARNING: POST body may have been truncated - buffer too small!");
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, bodyBuf);
        if (err) {
          Serial.printf("Gauge config JSON parse FAILED: %s\n", err.c_str());
        }
        if (!err) {
          PageConfig *pageConfigs = gauge_config_get_pages();
          JsonArray pagesArr = doc.as<JsonArray>();
          int i = 0;
          for (JsonObject pageObj : pagesArr) {
            if (i >= MAX_PAGES) break;
            pageConfigs[i].enabled = pageObj["enabled"] | false;
            pageConfigs[i].layout = (LayoutType)(int)(pageObj["layout"] | 0);
            JsonArray cellsArr = pageObj["cells"];
            int c = 0;
            for (JsonObject o : cellsArr) {
              if (c >= MAX_CELLS_PER_PAGE) break;
              CellConfig *cell = &pageConfigs[i].cells[c];
              cell->enabled = o["enabled"] | false;
              cell->signal = (SignalId)(int)(o["signal"] | 0);
              cell->gauge_type = (GaugeType)(int)(o["gauge_type"] | 0);
              cell->min_val = o["min"] | 0;
              cell->max_val = o["max"] | 100;
              cell->rest_val = o["rest"] | 0;
              strlcpy(cell->caption, (o["caption"] | ""), GAUGE_CAPTION_MAXLEN);
              cell->needle_color = (uint32_t)(long)(o["needle_color"] | 0xFFFFFF);
              cell->tick_color = (uint32_t)(long)(o["tick_color"] | 0xFFFFFF);
              cell->bg_color = (uint32_t)(long)(o["bg_color"] | 0x000000);
              cell->value_divisor = o["value_divisor"] | 1.0f;
              cell->tick_count = o["tick_count"] | 26;
              cell->major_nth = o["major_nth"] | 5;
              cell->refresh_hz = o["refresh_hz"] | 10;
              cell->show_value = o["show_value"] | false;
              cell->value_font_size = o["value_font_size"] | 28;
              cell->has_warning = o["has_warning"] | false;
              cell->warning_threshold = o["warning_threshold"] | 0.0f;
              cell->warning_above = o["warning_above"] | false;
              cell->alert_fullscreen = o["alert_fullscreen"] | false;
              cell->history_sec = o["history_sec"] | 30;
              cell->ref_enabled = o["ref_enabled"] | false;
              cell->ref_value = o["ref_value"] | 0.0f;
              c++;
            }
            i++;
          }
          // Any remaining page slots beyond what was POSTed are disabled -
          // handles the case where pages were deleted in the UI.
          for (; i < MAX_PAGES; i++) {
            pageConfigs[i].enabled = false;
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

  // Heap readout every 10s - lets us see whether free internal RAM
  // (and the largest single free block) is getting dangerously low
  // while the web pages are misbehaving.
  static unsigned long s_lastHeapLog = 0;
  if (now - s_lastHeapLog >= 10000) {
    s_lastHeapLog = now;
    Serial.printf("Heap: internal free=%u largest=%u lowest-ever=%u | PSRAM free=%u\n",
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
  JsonDocument doc;
  for (int sig = 1; sig < SIG_COUNT; sig++) { // skip SIG_NONE (0)
    // No live data (never seen, or the bus went quiet): omit it, and the
    // page shows "--" instead of 0.0 or a frozen last reading.
    if (!gauge_config_signal_has_data((SignalId)sig)) continue;
    char key[4];
    snprintf(key, sizeof(key), "%d", sig);
    doc[key] = gauge_config_signal_value((SignalId)sig);
  }
  String json;
  serializeJson(doc, json);
  ws.textAll(json);
}
