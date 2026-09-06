/*
  esp32-main
  Production sketch: reads CAN frames from EITHER rusEFI OR MegaSquirt
  (no mode switch needed - their CAN ID ranges don't overlap, so whichever
  ECU is actually connected is simply the one whose frames show up), plus
  GPS speed, and pushes all values to a digital 7-segment style Nextion
  dashboard (12 numeric readouts: rpm, mph, tps/tpst, eth, ego, fuelp,
  oilp, oilt, map, clt, mat, vvt).

  STATUS:
    rusEFI  - RPM/MAP/TPS1/BattVolt/MCUTemp are BENCH-CONFIRMED (real
              hardware, real values matched). CoolantTemp/OilPress/OilTemp/
              Lambda/IntakeTemp/FpLow/VVT come straight from the official
              DBC (high confidence, not yet bench-verified with live
              sensors - those fields read 0/default until wired).
    MegaSquirt - scale factors (/10, big-endian) and block layout are taken
              from a previously working reference sketch, not independently
              bench-verified in THIS project. Reasonably high confidence,
              but if you switch to MegaSquirt, running the CAN sniffer
              against it once (same sniff-and-compare method used for
              rusEFI) is worth doing before fully trusting these values.

  See docs/CAN-map.md for full signal tables and confirmation history.

  Also reads EGT via a MAX6675 + K-type thermocouple (bit-banged,
  console-only + web dashboard for now - not yet on the Nextion display).

  Also hosts a live web dashboard over WiFi (join existing network, per
  wifi_config.h - see wifi_config.h.example) using ESP Async WebServer +
  Async TCP, pushing every value over WebSocket as it's parsed. The web
  page shows more fields than the physical Nextion display (which is
  fixed at 12 slots) - EGT and BattVolt included, since Nextion silently
  ignores field names it has no component for.
*/


// =====================================================================
// includes
// =====================================================================

#include "driver/twai.h"
#include <TinyGPSPlus.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "wifi_config.h" // create from wifi_config.h.example - not tracked in git


// =====================================================================
// pin / wiring configuration
// =====================================================================

// CAN transceiver (SN65HVD230 or MCP2551 + level shifting on RX)
#define CAN_TX_PIN GPIO_NUM_21
#define CAN_RX_PIN GPIO_NUM_22
#define CAN_BITRATE TWAI_TIMING_CONFIG_500KBITS()

// Nextion display, UART2
#define NEXTION_RX_PIN 16
#define NEXTION_TX_PIN 17
#define NEXTION_BAUD 115200 // must match Nextion project's baud= startup setting

// GPS module (GT-U7 / NEO-6M chipset), UART1
#define GPS_RX_PIN 4  // ESP32 pin receiving GPS module's TX
#define GPS_TX_PIN 5  // ESP32 pin driving GPS module's RX
#define GPS_BAUD 9600

// EGT (MAX6675 + K-type thermocouple), bit-banged 3-wire interface
#define MAX6675_CS  32
#define MAX6675_SCK 33
#define MAX6675_SO  34 // input-only pin - fits since this is read-only


// =====================================================================
// rusEFI CAN IDs (base 0x200 / 512 decimal, confirmed via TunerStudio)
// Byte order: LITTLE-ENDIAN
// =====================================================================

#define RUSEFI_BASE1 0x201 // RPM, IgnitionTiming, InjDuty, IgnDuty, VehicleSpeed, FlexPct
#define RUSEFI_BASE2 0x202 // PPS, TPS1, TPS2, Wastegate
#define RUSEFI_BASE3 0x203 // MAP, CoolantTemp, IntakeTemp, AUX1Temp, AUX2Temp, MCUTemp, FuelLevel
#define RUSEFI_BASE4 0x204 // OilPress, OilTemperature, FuelTemperature, BattVolt
#define RUSEFI_BASE7 0x207 // Lam1, Lam2, FpLow, FpHigh
#define RUSEFI_BASE8 0x208 // Cam1I, Cam1Itar, Cam1E, Cam1Etar, Cam2I, Cam2Itar, Cam2E, Cam2Etar

#define ASSUMED_BARO_KPA 101.3f // rusEFI's DBC has no baro signal - fixed assumption per project decision


// =====================================================================
// MegaSquirt CAN IDs (base 1520 decimal / 0x5F0, from working reference
// sketch - see docs/CAN-map.md for the source and confidence caveat)
// Byte order: BIG-ENDIAN
// =====================================================================

#define MS_BASE      1520        // 0x5F0
#define MS_BLOCK0    (MS_BASE+0) // Seconds, PW1, PW2, RPM
#define MS_BLOCK2    (MS_BASE+2) // BARO, MAP, MAT, CLT
#define MS_BLOCK3    (MS_BASE+3) // TPS, BATT, EGO1
#define MS_BLOCK13   (MS_BASE+13)// FUELP, OILP, OILT
#define MS_BLOCK42   (MS_BASE+42)// VSS1 (not used - mph is GPS-sourced)
#define MS_BLOCK44   (MS_BASE+44)// VVT angle
#define MS_BLOCK47   (MS_BASE+47)// Ethanol %


// =====================================================================
// globals
// =====================================================================

HardwareSerial gpsSerial(1);
TinyGPSPlus gps;
unsigned long lastGpsSend = 0;
const unsigned long GPS_SEND_INTERVAL_MS = 250; // ~4Hz display update, independent of GPS fix rate

const unsigned long STALE_MS = 2000; // blocks round-robin at different rates (rusEFI); reserved for future staleness check

AsyncWebServer webServer(80);
AsyncWebSocket ws("/ws");
Preferences gaugePrefs;


// =====================================================================
// gauge on/off config - persisted in flash (NVS), toggled via /config page
// Disabling a gauge stops it from updating BOTH the Nextion display and
// the web dashboard, since they share the same nextionSend() output path.
// =====================================================================

struct GaugeConfig {
  const char* key;   // field name, matches what nextionSend() parses out
  const char* label; // human-readable name for the config page
  bool enabled;
};

GaugeConfig gaugeConfigs[] = {
  { "rpm",    "RPM",              true },
  { "boost",  "Boost",            true },
  { "tps",    "TPS (bar)",        true },
  { "tpst",   "TPS (text)",       true },
  { "eth",    "Ethanol %",        true },
  { "ego",    "AFR",              true },
  { "fuelp",  "Fuel Pressure",    true },
  { "oilp",   "Oil Pressure",     true },
  { "oilt",   "Oil Temp",         true },
  { "map",    "MAP",              true },
  { "clt",    "Coolant Temp",     true },
  { "mat",    "Intake Air Temp",  true },
  { "vvt",    "VVT Angle",        true },
  { "battv",  "Battery Voltage",  true },
  { "egt",    "EGT",              true },
  { "mph",    "Speed (GPS)",      true },
  { "fuel",   "Fuel Level",       true },
};
const int NUM_GAUGES = sizeof(gaugeConfigs) / sizeof(gaugeConfigs[0]);

bool isGaugeEnabled(const String &field) {
  for (int i = 0; i < NUM_GAUGES; i++) {
    if (field == gaugeConfigs[i].key) return gaugeConfigs[i].enabled;
  }
  return true; // unrecognized field - default to allowed rather than silently dropping something new
}

void loadGaugeConfig() {
  for (int i = 0; i < NUM_GAUGES; i++) {
    gaugeConfigs[i].enabled = gaugePrefs.getBool(gaugeConfigs[i].key, true);
  }
}

void saveGaugeConfig(AsyncWebServerRequest *request) {
  for (int i = 0; i < NUM_GAUGES; i++) {
    bool checked = request->hasParam(gaugeConfigs[i].key, true); // checkbox present in POST body = checked
    gaugeConfigs[i].enabled = checked;
    gaugePrefs.putBool(gaugeConfigs[i].key, checked);
  }
}


// =====================================================================
// web dashboard - served HTML page
// Shows more fields than the physical Nextion display (fixed at 12
// slots): includes egt and battv, which have no Nextion component yet.
// =====================================================================

static const char DASHBOARD_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MSP-DASH Live</title>
  <style>
    * { box-sizing: border-box; }
    body {
      background: #05050a;
      color: #eee; font-family: 'Segoe UI', system-ui, sans-serif;
      margin: 0; padding: 16px; min-height: 100vh;
    }
    h1 { color: #4dd0e1; font-size: 1.1em; letter-spacing: 2px; text-transform: uppercase;
         margin: 0 0 4px 0; font-weight: 600; text-align:center; }
    #status { font-size: 0.75em; color: #666; margin-bottom: 10px; text-align:center; }
    #status.connected { color: #4caf50; }
    .cfg-link { text-align:center; margin-bottom:16px; }
    .cfg-link a { color:#4dd0e1; font-size:0.8em; text-decoration:none; border:1px solid #2a2a30;
                  border-radius:6px; padding:6px 14px; }

    /* Cluster layout: fuel | tach | center-lcd | speedo | temp */
    .cluster { display:flex; align-items:center; justify-content:center; flex-wrap:wrap;
               gap: 6px; margin-bottom: 24px; }
    .gauge-wrap { position:relative; }
    .gauge-wrap svg { display:block; }
    .needle {
      position:absolute; top:50%; left:50%; width:4px; height:38%;
      background: linear-gradient(to top, #c0392b, #e74c3c);
      transform-origin: bottom center; transform: translate(-50%,-100%) rotate(-135deg);
      border-radius: 3px 3px 0 0; box-shadow: 0 0 4px rgba(231,76,60,0.6);
      transition: transform 0.15s ease-out;
    }
    .needle-small { height:32%; width:3px; }
    .hub {
      position:absolute; top:50%; left:50%; width:14px; height:14px; border-radius:50%;
      background: radial-gradient(circle at 35% 35%, #888, #222); transform: translate(-50%,-50%);
      box-shadow: 0 0 4px rgba(0,0,0,0.8);
    }
    .gauge-title { position:absolute; bottom:26%; left:50%; transform:translateX(-50%);
                   font-size:0.7em; font-weight:700; color:#222; letter-spacing:1px; }
    .gauge-value { position:absolute; bottom:14%; left:50%; transform:translateX(-50%);
                   font-size:0.85em; font-weight:700; color:#000; font-family:monospace; }
    .gps-dot { position:absolute; top:10%; left:50%; transform:translateX(-50%);
               width:10px; height:10px; border-radius:50%; background:#555;
               box-shadow:0 0 4px rgba(0,0,0,0.6); transition: background 0.3s ease, box-shadow 0.3s ease; }
    .gps-dot.fix { background:#4caf50; box-shadow:0 0 8px rgba(76,175,80,0.9); }

    .lcd { width:120px; height:180px; background:#111; border-radius:8px; border:3px solid #333;
           display:flex; flex-direction:column; align-items:center; justify-content:center;
           box-shadow: inset 0 0 10px rgba(0,0,0,0.8); }
    .lcd-label { color:#4dd0e1; font-size:0.6em; text-transform:uppercase; letter-spacing:1px;
                 font-family:monospace; }
    .lcd-value { color:#4caf50; font-size:1.5em; font-family:monospace; font-weight:700;
                 text-shadow: 0 0 6px rgba(76,175,80,0.6); margin:6px 0; }
    .lcd-unit { color:#4caf50; font-size:0.65em; font-family:monospace; opacity:0.7; }

    .bar-section { max-width: 500px; margin: 0 auto 24px auto; }
    .bar-label { display: flex; justify-content: space-between; font-size: 0.8em; color: #888;
                 text-transform: uppercase; letter-spacing: 1px; margin-bottom: 6px; }
    .bar-track { background: #1a1a1f; border-radius: 8px; height: 22px; overflow: hidden;
                 box-shadow: inset 0 0 8px rgba(0,0,0,0.6); }
    .bar-fill { height: 100%; width: 0%; background: linear-gradient(90deg, #4dd0e1, #26c6da);
                border-radius: 8px; transition: width 0.2s ease; }

    .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(120px,1fr)); gap: 10px;
            max-width: 900px; margin: 0 auto; }
    .tile { background: #16161a; border: 1px solid #2a2a30; border-radius: 8px;
            padding: 12px 8px; text-align: center; }
    .tile .label { color: #4dd0e1; font-size: 0.7em; text-transform: uppercase; letter-spacing: 1px; }
    .tile .value { font-size: 1.4em; font-weight: 600; margin-top: 4px; }
  </style>
</head>
<body>
  <h1>MSP-DASH Live</h1>
  <div id="status">connecting...</div>
  <div class="cfg-link"><a href="/config">&#9881; Configure Gauges</a></div>

  <div class="cluster" id="cluster"></div>

  <div class="bar-section">
    <div class="bar-label"><span>Throttle</span><span id="v_tpst">0.0%</span></div>
    <div class="bar-track"><div class="bar-fill" id="bar_tps"></div></div>
  </div>

  <div class="grid" id="grid"></div>

  <script>
    // ---- Gauge face generator (SVG background + CSS-rotated needle overlay) ----
    // Sweep convention: 0 at -135deg (bottom-left), max at +135deg (bottom-right),
    // 270deg total, matching classic automotive gauge layout.
    function buildGaugeFace(size, maxVal, majorStep, redlineFrom) {
      const cx = size / 2, cy = size / 2, r = size * 0.42;
      const startDeg = -135, sweepDeg = 270;
      let svg = '<svg width="' + size + '" height="' + size + '" viewBox="0 0 ' + size + ' ' + size + '">';
      svg += '<circle cx="' + cx + '" cy="' + cy + '" r="' + (r + 6) + '" fill="#111" stroke="#555" stroke-width="2"/>';
      svg += '<circle cx="' + cx + '" cy="' + cy + '" r="' + r + '" fill="#f5f2ea"/>';

      // redline arc (if given)
      if (redlineFrom !== undefined) {
        const fFrom = redlineFrom / maxVal;
        const aFrom = (startDeg + fFrom * sweepDeg) * Math.PI / 180;
        const aTo = (startDeg + sweepDeg) * Math.PI / 180;
        const x1 = cx + r * Math.sin(aFrom), y1 = cy - r * Math.cos(aFrom);
        const x2 = cx + r * Math.sin(aTo), y2 = cy - r * Math.cos(aTo);
        const largeArc = (sweepDeg * (1 - fFrom)) > 180 ? 1 : 0;
        svg += '<path d="M ' + x1 + ' ' + y1 + ' A ' + r + ' ' + r + ' 0 ' + largeArc + ' 1 ' + x2 + ' ' + y2 +
               '" fill="none" stroke="#c0392b" stroke-width="6" opacity="0.85"/>';
      }

      // ticks + labels
      const steps = maxVal / majorStep;
      for (let i = 0; i <= steps; i++) {
        const val = i * majorStep;
        const frac = val / maxVal;
        const ang = (startDeg + frac * sweepDeg) * Math.PI / 180;
        const rOuter = r - 4, rInner = r - 14;
        const x1 = cx + rOuter * Math.sin(ang), y1 = cy - rOuter * Math.cos(ang);
        const x2 = cx + rInner * Math.sin(ang), y2 = cy - rInner * Math.cos(ang);
        svg += '<line x1="' + x1 + '" y1="' + y1 + '" x2="' + x2 + '" y2="' + y2 + '" stroke="#222" stroke-width="2.5"/>';
        const rLabel = r - 24;
        const lx = cx + rLabel * Math.sin(ang), ly = cy - rLabel * Math.cos(ang);
        svg += '<text x="' + lx + '" y="' + (ly + 4) + '" font-size="' + (size * 0.07) + '" fill="#222" ' +
               'text-anchor="middle" font-family="monospace" font-weight="bold">' + val + '</text>';

        // minor tick (halfway to next major)
        if (i < steps) {
          const midFrac = (val + majorStep / 2) / maxVal;
          const midAng = (startDeg + midFrac * sweepDeg) * Math.PI / 180;
          const mx1 = cx + rOuter * Math.sin(midAng), my1 = cy - rOuter * Math.cos(midAng);
          const mx2 = cx + (r - 9) * Math.sin(midAng), my2 = cy - (r - 9) * Math.cos(midAng);
          svg += '<line x1="' + mx1 + '" y1="' + my1 + '" x2="' + mx2 + '" y2="' + my2 + '" stroke="#555" stroke-width="1.5"/>';
        }
      }
      svg += '</svg>';
      return svg;
    }

    function makeGauge(id, size, maxVal, majorStep, title, unit, redlineFrom, small) {
      const wrap = document.createElement('div');
      wrap.className = 'gauge-wrap';
      wrap.style.width = size + 'px';
      wrap.style.height = size + 'px';
      wrap.innerHTML = buildGaugeFace(size, maxVal, majorStep, redlineFrom) +
        '<div class="needle' + (small ? ' needle-small' : '') + '" id="needle_' + id + '"></div>' +
        '<div class="hub"></div>' +
        '<div class="gauge-title">' + title + '</div>' +
        '<div class="gauge-value" id="v_' + id + '">0</div>';
      return wrap;
    }

    const gaugeRanges = {
      rpm:   { max: 8000, major: 1000, redline: 6800 },
      mph:   { max: 140,  major: 20 },
      fuel:  { max: 100,  major: 25 },
      clt:   { max: 260,  major: 65 }
    };

    const cluster = document.getElementById('cluster');
    cluster.appendChild(makeGauge('fuel', 110, gaugeRanges.fuel.max, gaugeRanges.fuel.major, 'FUEL', '%', undefined, true));
    cluster.appendChild(makeGauge('rpm', 220, gaugeRanges.rpm.max, gaugeRanges.rpm.major, 'RPM', '', gaugeRanges.rpm.redline, false));

    const lcd = document.createElement('div');
    lcd.className = 'lcd';
    lcd.innerHTML = '<div class="lcd-label" id="lcd_label">BOOST</div>' +
                     '<div class="lcd-value" id="lcd_value">0.0</div>' +
                     '<div class="lcd-unit" id="lcd_unit">kPa</div>';
    cluster.appendChild(lcd);

    cluster.appendChild(makeGauge('mph', 220, gaugeRanges.mph.max, gaugeRanges.mph.major, 'MPH', '', undefined, false));
    // GPS fix status dot, appended after the mph gauge is in the DOM
    const mphWrap = cluster.lastElementChild;
    const gpsDot = document.createElement('div');
    gpsDot.className = 'gps-dot';
    gpsDot.id = 'gps_dot';
    mphWrap.appendChild(gpsDot);
    cluster.appendChild(makeGauge('clt', 110, gaugeRanges.clt.max, gaugeRanges.clt.major, 'TEMP', 'F', undefined, true));

    function updateNeedle(field, value) {
      const range = gaugeRanges[field];
      if (!range) return;
      let frac = value / range.max;
      frac = Math.max(0, Math.min(1, frac));
      const deg = -135 + frac * 270;
      const needle = document.getElementById('needle_' + field);
      if (needle) needle.style.transform = 'translate(-50%,-100%) rotate(' + deg + 'deg)';
    }

    // Center LCD cycles through secondary stats every 3s
    const lcdStats = [
      { field: 'boost', label: 'BOOST', unit: 'kPa' },
      { field: 'oilp',  label: 'OIL PRESS', unit: 'kPa' },
      { field: 'ego',   label: 'AFR', unit: '' },
      { field: 'egt',   label: 'EGT', unit: 'C' }
    ];
    let lcdIndex = 0;
    const lastValues = {};
    function refreshLcd() {
      const stat = lcdStats[lcdIndex];
      document.getElementById('lcd_label').textContent = stat.label;
      document.getElementById('lcd_value').textContent = lastValues[stat.field] !== undefined ? lastValues[stat.field] : '--';
      document.getElementById('lcd_unit').textContent = stat.unit;
      lcdIndex = (lcdIndex + 1) % lcdStats.length;
    }
    setInterval(refreshLcd, 3000);
    refreshLcd();

    // Secondary tiles (everything not shown as a cluster gauge or bar).
    // tpst mirrors the tps bar's own numeric label above, so it's excluded.
    const tileFields = ["eth","ego","fuelp","oilp","oilt","map","boost","mat","vvt","battv","egt"];
    const grid = document.getElementById("grid");
    tileFields.forEach(f => {
      const tile = document.createElement("div");
      tile.className = "tile";
      tile.innerHTML = '<div class="label">' + f + '</div><div class="value" id="v_' + f + '">--</div>';
      grid.appendChild(tile);
    });

    function handleField(field, value) {
      lastValues[field] = value;

      const el = document.getElementById("v_" + field);
      if (el) el.textContent = value;

      if (gaugeRanges[field]) {
        const num = parseFloat(value);
        if (!isNaN(num)) updateNeedle(field, num);
      }
      if (field === "tps") {
        const num = parseFloat(value);
        if (!isNaN(num)) document.getElementById("bar_tps").style.width = Math.max(0, Math.min(100, num)) + "%";
      }
      if (field === "tpst") {
        const el2 = document.getElementById("v_tpst");
        if (el2) el2.textContent = value + "%";
      }
      if (field === "gpsfix") {
        const dot = document.getElementById("gps_dot");
        if (dot) dot.classList.toggle("fix", value === "1");
      }
    }

    let ws;
    function connect() {
      ws = new WebSocket("ws://" + location.host + "/ws");
      const status = document.getElementById("status");
      ws.onopen = () => { status.textContent = "connected"; status.className = "connected"; };
      ws.onclose = () => { status.textContent = "disconnected - retrying..."; status.className = ""; setTimeout(connect, 2000); };
      ws.onerror = () => ws.close();
      ws.onmessage = (evt) => {
        try {
          const msg = JSON.parse(evt.data);
          handleField(msg.field, msg.value);
        } catch (e) { /* ignore malformed messages */ }
      };
    }
    connect();
  </script>
</body>
</html>
)HTMLPAGE";


// =====================================================================
// byte-reading helpers
// =====================================================================

// rusEFI: little-endian
uint16_t readU16LE(uint8_t* data, int offset) {
  return data[offset] | (data[offset + 1] << 8);
}
int16_t readS16LE(uint8_t* data, int offset) {
  return (int16_t)(data[offset] | (data[offset + 1] << 8));
}

// MegaSquirt: big-endian
uint16_t readU16BE(uint8_t* data, int offset) {
  return (data[offset] << 8) | data[offset + 1];
}
int16_t readS16BE(uint8_t* data, int offset) {
  return (int16_t)((data[offset] << 8) | data[offset + 1]);
}

// Shared C->F conversion. All temp fields pushed to Nextion/web are now
// Fahrenheit - rusEFI's DBC confirms its temp signals are degC, so this
// conversion is well-defined there. MegaSquirt's temp units depend on how
// the ECU itself is configured (PCLink can output either C or F) - this
// assumes Celsius input for MegaSquirt too, which is the common default,
// but has NOT been bench-confirmed for MegaSquirt in this project.
float cToF(float c) {
  return c * 9.0f / 5.0f + 32.0f;
}


// =====================================================================
// gauge config page - built dynamically so checkbox states always
// reflect current config (a static PROGMEM page can't do this)
// =====================================================================

String buildConfigPage() {
  String html = F(
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>MSP-DASH Config</title><style>"
    "body{background:#0a0a0d;color:#eee;font-family:'Segoe UI',system-ui,sans-serif;"
    "margin:0;padding:20px;max-width:500px;margin:0 auto;}"
    "h1{color:#4dd0e1;font-size:1.1em;letter-spacing:2px;text-transform:uppercase;}"
    "a{color:#4dd0e1;font-size:0.85em;text-decoration:none;}"
    ".row{display:flex;justify-content:space-between;align-items:center;"
    "background:#16161a;border:1px solid #2a2a30;border-radius:8px;"
    "padding:12px 16px;margin-bottom:8px;}"
    "input[type=checkbox]{width:20px;height:20px;}"
    "button{background:#4dd0e1;color:#0a0a0d;border:none;border-radius:8px;"
    "padding:12px 20px;font-size:1em;font-weight:600;margin-top:16px;width:100%;}"
    "</style></head><body>"
    "<a href='/'>&larr; Back to Dashboard</a>"
    "<h1>Configure Gauges</h1>"
    "<form method='POST' action='/config'>"
  );

  for (int i = 0; i < NUM_GAUGES; i++) {
    html += "<div class='row'><span>" + String(gaugeConfigs[i].label) + "</span>";
    html += "<input type='checkbox' name='" + String(gaugeConfigs[i].key) + "'";
    if (gaugeConfigs[i].enabled) html += " checked";
    html += "></div>";
  }

  html += F("<button type='submit'>Save</button></form></body></html>");
  return html;
}


// =====================================================================
// Nextion output
// =====================================================================

void nextionSend(const String &cmd) {
  // Parse "field.txt=\"value\"" or "field.val=value" out of the command
  // string up front, so a disabled gauge can be skipped entirely - no
  // Nextion write, no debug print, no web broadcast.
  int dot = cmd.indexOf('.');
  int eq = cmd.indexOf('=');
  if (dot <= 0 || eq <= dot) return; // malformed command, nothing to parse - drop silently

  String field = cmd.substring(0, dot);
  if (!isGaugeEnabled(field)) return; // gauge turned off via /config - suppress entirely

  Serial2.print(cmd);
  Serial2.write(0xFF); Serial2.write(0xFF); Serial2.write(0xFF);
  Serial.println(cmd); // debug mirror - remove once display confirmed working

  String value = cmd.substring(eq + 1);
  value.replace("\"", ""); // strip quotes if present (text fields)
  String json = "{\"field\":\"" + field + "\",\"value\":\"" + value + "\"}";
  ws.textAll(json);
}


// =====================================================================
// rusEFI frame handling
// =====================================================================

void handleRusefiFrame(uint32_t id, uint8_t* d) {
  switch (id) {
    case RUSEFI_BASE1: {
      uint16_t rpm = readU16LE(d, 0);
      // VehicleSpeed (byte6, kph) intentionally not read - mph is GPS-sourced
      uint8_t flexPct = d[7]; // FlexPct: byte7, %

      nextionSend("rpm.txt=\"" + String(rpm) + "\"");
      nextionSend("eth.txt=\"" + String(flexPct) + "\"");
      break;
    }
    case RUSEFI_BASE2: {
      int16_t tpsRaw = readS16LE(d, 2); // TPS1: byte2-3, signed
      float tps = tpsRaw * 0.01f;

      nextionSend("tps.val=" + String((int)tps)); // progress bar, native 0-100 range
      nextionSend("tpst.txt=\"" + String(tps, 1) + "\"");
      break;
    }
    case RUSEFI_BASE3: {
      uint16_t mapRaw = readU16LE(d, 0);
      float mapKpa = mapRaw * 0.0333333f;
      float boost = mapKpa - ASSUMED_BARO_KPA; // no baro signal in rusEFI DBC - fixed assumption
      int8_t cltRaw = (int8_t)d[2];
      float clt = cToF(cltRaw - 40); // rusEFI DBC: degC, converted to degF
      int8_t matRaw = (int8_t)d[3];
      float mat = cToF(matRaw - 40); // rusEFI DBC: degC, converted to degF
      uint8_t fuelRaw = d[7]; // FuelLevel: byte7, x0.5, %
      float fuel = fuelRaw * 0.5f;

      nextionSend("map.txt=\"" + String(mapKpa, 1) + "\"");
      nextionSend("boost.txt=\"" + String(boost, 1) + "\""); // no Nextion component yet - web dashboard only
      nextionSend("clt.txt=\"" + String(clt, 1) + "\"");
      nextionSend("mat.txt=\"" + String(mat, 1) + "\"");
      nextionSend("fuel.txt=\"" + String(fuel, 1) + "\""); // no Nextion component yet - web dashboard only. MegaSquirt path doesn't provide this signal.
      break;
    }
    case RUSEFI_BASE4: {
      uint16_t oilPressRaw = readU16LE(d, 2); // OilPress: byte2-3
      float oilPressKpa = oilPressRaw * 0.0333333f;
      int8_t oilTempRaw = (int8_t)d[4];
      float oilTemp = cToF(oilTempRaw - 40); // rusEFI DBC: degC, converted to degF
      uint16_t battRaw = readU16LE(d, 6); // BattVolt: byte6-7
      float battV = battRaw * 0.001f;

      nextionSend("oilp.txt=\"" + String(oilPressKpa, 1) + "\"");
      nextionSend("oilt.txt=\"" + String(oilTemp, 1) + "\"");
      nextionSend("battv.txt=\"" + String(battV, 2) + "\""); // no Nextion component yet - web dashboard only
      break;
    }
    case RUSEFI_BASE7: {
      uint16_t lam1Raw = readU16LE(d, 0);
      float lambda = lam1Raw * 0.0001f;
      float afr = lambda * 14.7f; // gasoline stoich - adjust if E85/flex
      uint16_t fpLowRaw = readU16LE(d, 4); // FpLow: byte4-5
      float fpLowKpa = fpLowRaw * 0.0333333f;

      nextionSend("ego.txt=\"" + String(afr, 1) + "\"");
      nextionSend("fuelp.txt=\"" + String(fpLowKpa, 1) + "\"");
      break;
    }
    case RUSEFI_BASE8: {
      int8_t vvtRaw = (int8_t)d[0]; // Cam1I: byte0, signed
      nextionSend("vvt.txt=\"" + String(vvtRaw) + "\"");
      break;
    }
  }
}


// =====================================================================
// MegaSquirt frame handling
// =====================================================================

void handleMegasquirtFrame(uint32_t id, uint8_t* d) {
  switch (id) {
    case MS_BLOCK0: {
      float rpm = readU16BE(d, 6); // word3: Seconds/PW1/PW2/RPM
      nextionSend("rpm.txt=\"" + String((int)rpm) + "\"");
      break;
    }
    case MS_BLOCK2: {
      float baro = readU16BE(d, 0) / 10.0f; // word0
      float map_ = readU16BE(d, 2) / 10.0f; // word1
      float mat  = cToF(readU16BE(d, 4) / 10.0f); // word2 - ASSUMES Celsius source config, not bench-confirmed
      float clt  = cToF(readU16BE(d, 6) / 10.0f); // word3 - ASSUMES Celsius source config, not bench-confirmed
      float boost = map_ - baro; // real baro available here, unlike rusEFI

      nextionSend("map.txt=\"" + String(map_, 1) + "\"");
      nextionSend("boost.txt=\"" + String(boost, 1) + "\""); // no Nextion component yet - web dashboard only
      nextionSend("mat.txt=\"" + String(mat, 1) + "\"");
      nextionSend("clt.txt=\"" + String(clt, 1) + "\"");
      break;
    }
    case MS_BLOCK3: {
      float tps  = readU16BE(d, 0) / 10.0f; // word0
      float ego1 = readU16BE(d, 4) / 10.0f; // word2 - already AFR units per source sketch, not lambda

      nextionSend("tps.val=" + String((int)tps));
      nextionSend("tpst.txt=\"" + String(tps, 1) + "\"");
      nextionSend("ego.txt=\"" + String(ego1, 1) + "\"");
      break;
    }
    case MS_BLOCK13: {
      float fuelp = readU16BE(d, 0) / 10.0f; // word0
      float oilp  = readU16BE(d, 2) / 10.0f; // word1
      float oilt  = cToF(readU16BE(d, 4) / 10.0f); // word2 - ASSUMES Celsius source config, not bench-confirmed

      nextionSend("fuelp.txt=\"" + String(fuelp, 1) + "\"");
      nextionSend("oilp.txt=\"" + String(oilp, 1) + "\"");
      nextionSend("oilt.txt=\"" + String(oilt, 1) + "\"");
      break;
    }
    case MS_BLOCK44: {
      float vvt = readU16BE(d, 0) / 10.0f; // word0
      nextionSend("vvt.txt=\"" + String(vvt, 1) + "\"");
      break;
    }
    case MS_BLOCK47: {
      float eth = readU16BE(d, 0) / 10.0f; // word0
      nextionSend("eth.txt=\"" + String(eth, 1) + "\"");
      break;
    }
  }
}


// =====================================================================
// EGT (MAX6675 + K-type thermocouple)
// Console-only for now, not yet on the Nextion display.
// Bit-banged per MAX6675 datasheet: CS low, clock out 16 bits MSB-first,
// CS high. Needs ~220ms between reads per datasheet - timed accordingly.
// =====================================================================

unsigned long lastEgtRead = 0;
const unsigned long EGT_READ_INTERVAL_MS = 500;

double readThermocoupleC() {
  uint16_t v;
  pinMode(MAX6675_CS, OUTPUT);
  pinMode(MAX6675_SO, INPUT);
  pinMode(MAX6675_SCK, OUTPUT);

  digitalWrite(MAX6675_CS, LOW);
  delay(1);

  v = shiftIn(MAX6675_SO, MAX6675_SCK, MSBFIRST);
  v <<= 8;
  v |= shiftIn(MAX6675_SO, MAX6675_SCK, MSBFIRST);

  digitalWrite(MAX6675_CS, HIGH);

  if (v & 0x4) {
    return NAN; // bit 2 set = thermocouple open/disconnected
  }

  v >>= 3; // discard 3 status bits
  return v * 0.25; // 0.25 degC per count
}

void updateEgt() {
  unsigned long now = millis();
  if (now - lastEgtRead < EGT_READ_INTERVAL_MS) return;
  lastEgtRead = now;

  double egtC = readThermocoupleC();
  if (isnan(egtC)) {
    Serial.println("EGT: thermocouple open/disconnected");
    nextionSend("egt.txt=\"--\""); // web dashboard shows disconnected state too
  } else {
    double egtF = egtC * 9.0 / 5.0 + 32.0;
    Serial.printf("EGT = %.1f C (%.1f F)\n", egtC, egtF);
    nextionSend("egt.txt=\"" + String(egtF, 1) + "\""); // no Nextion component yet - web dashboard only
  }
}


// =====================================================================
// GPS handling
// =====================================================================

// For system status indicators (not user-toggleable sensor gauges) -
// broadcasts straight to the web dashboard, skipping the Nextion write
// and the /config on/off gate entirely.
void broadcastStatus(const String &field, const String &value) {
  String json = "{\"field\":\"" + field + "\",\"value\":\"" + value + "\"}";
  ws.textAll(json);
}

void updateGps() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  unsigned long now = millis();
  if (now - lastGpsSend >= GPS_SEND_INTERVAL_MS) {
    lastGpsSend = now;
    if (gps.speed.isValid()) {
      nextionSend("mph.txt=\"" + String(gps.speed.mph(), 0) + "\"");
      broadcastStatus("gpsfix", "1");
    } else {
      nextionSend("mph.txt=\"--\""); // no GPS fix yet
      broadcastStatus("gpsfix", "0");
    }
  }
}


// =====================================================================
// setup / loop
// =====================================================================

void setup() {
  Serial.begin(115200);
  Serial2.begin(NEXTION_BAUD, SERIAL_8N1, NEXTION_RX_PIN, NEXTION_TX_PIN);

  gaugePrefs.begin("gauges", false); // NVS namespace "gauges" - persists across reboots
  loadGaugeConfig();

  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = CAN_BITRATE;
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  twai_driver_install(&g_config, &t_config, &f_config);
  twai_start();

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // WiFi + web dashboard
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi connect failed/timed out - web dashboard unavailable, continuing anyway");
  }

  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      Serial.printf("Web client #%u connected\n", client->id());
    } else if (type == WS_EVT_DISCONNECT) {
      Serial.printf("Web client #%u disconnected\n", client->id());
    }
  });
  webServer.addHandler(&ws);
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", DASHBOARD_HTML);
  });
  webServer.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", buildConfigPage());
  });
  webServer.on("/config", HTTP_POST, [](AsyncWebServerRequest *request) {
    saveGaugeConfig(request);
    request->redirect("/config");
  });
  webServer.begin();

  Serial.println("esp32-main running - rusEFI + MegaSquirt + GPS -> Nextion digital dashboard + web");
}

void loop() {
  twai_message_t message;
  if (twai_receive(&message, pdMS_TO_TICKS(20)) == ESP_OK) {
    uint32_t id = message.identifier;
    uint8_t* d = message.data;

    if (id >= 0x200 && id <= 0x20B) {
      handleRusefiFrame(id, d);
    } else if (id >= 1520 && id <= 1520 + 52) {
      handleMegasquirtFrame(id, d);
    }
  }

  updateGps();
  updateEgt();
  ws.cleanupClients();

  // TODO: check per-block millis() timestamps vs STALE_MS and show a
  // "no data" indicator on the Nextion side if a channel goes stale.
}
