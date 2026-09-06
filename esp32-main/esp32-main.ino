/*
  esp32-main
  Production sketch: reads the FULL rusEFI CAN signal set (every signal
  in rusEFI's official DBC, base 0x200/512 decimal - see docs/CAN-map.md),
  plus GPS speed and an EGT thermocouple, and pushes everything to a
  digital Nextion dashboard AND a live web dashboard over WiFi.

  STATUS: RPM/MAP/TPS1/BattVolt/MCUTemp are BENCH-CONFIRMED (real
  hardware, real values matched). Every other signal comes straight from
  the official DBC (high confidence, not bench-verified with live sensors
  for the ones not physically wired yet - those read 0/default).

  MegaSquirt and LinkECU support has been REMOVED for now (rusEFI-only
  focus per project decision) - to be revisited later. See git history
  if you need to bring that code back.

  See docs/CAN-map.md for full signal tables and confirmation history.

  Debug/Serial logging is OFF by default (toggle on /config) - the
  console doesn't get flooded with every CAN-derived value unless
  explicitly turned on for troubleshooting.

  Every gauge has, via /config: enabled on/off, display type (number/bar/
  needle - only applies to non-hero tile-grid gauges; the four hero
  cluster gauges - rpm/mph/fuel/clt - stay fixed needle dials but their
  min/max/redline are still configurable), refresh rate (ms), min/max
  range, and an optional redline threshold.

  NOTE ON SCOPE: this does NOT let you map an arbitrary new CAN ID/byte
  offset to a brand-new gauge via the web UI - that would be a separate,
  larger "generic CAN signal editor" feature. What's configurable here is
  the DISPLAY of every rusEFI signal already wired into this code (which
  is now all of them).
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
// rusEFI CAN IDs - FULL DBC (base 0x200 / 512 decimal, confirmed via
// TunerStudio). Byte order: LITTLE-ENDIAN throughout.
// =====================================================================

#define RUSEFI_BASE0  0x200 // WarningCounter, LastError, flags, CurrentGear, DistanceTraveled
#define RUSEFI_BASE1  0x201 // RPM, IgnitionTiming, InjDuty, IgnDuty, VehicleSpeed, FlexPct
#define RUSEFI_BASE2  0x202 // PPS, TPS1, TPS2, Wastegate
#define RUSEFI_BASE3  0x203 // MAP, CoolantTemp, IntakeTemp, AUX1Temp, AUX2Temp, MCUTemp, FuelLevel
#define RUSEFI_BASE4  0x204 // OilPress, OilTemperature, FuelTemperature, BattVolt
#define RUSEFI_BASE5  0x205 // CylAM, EstMAF, InjPW, KnockCt
#define RUSEFI_BASE6  0x206 // FuelUsed, FuelFlow, FuelTrim1, FuelTrim2
#define RUSEFI_BASE7  0x207 // Lam1, Lam2, FpLow, FpHigh
#define RUSEFI_BASE8  0x208 // Cam1I, Cam1Itar, Cam1E, Cam1Etar, Cam2I, Cam2Itar, Cam2E, Cam2Etar
#define RUSEFI_BASE9  0x209 // Egt1-8
#define RUSEFI_BASE10 0x20A // knock0-7
#define RUSEFI_BASE11 0x20B // BrakePedal

#define ASSUMED_BARO_KPA 101.3f // rusEFI's DBC has no baro signal - fixed assumption per project decision


// =====================================================================
// globals
// =====================================================================

HardwareSerial gpsSerial(1);
TinyGPSPlus gps;
unsigned long lastGpsSend = 0;
const unsigned long GPS_SEND_INTERVAL_MS = 250; // ~4Hz display update, independent of GPS fix rate

const unsigned long STALE_MS = 2000; // blocks round-robin at different rates; reserved for future staleness check

AsyncWebServer webServer(80);
AsyncWebSocket ws("/ws");
Preferences gaugePrefs;
Preferences sysPrefs;

bool debugLoggingEnabled = false; // OFF by default - toggle on /config


// =====================================================================
// gauge config - persisted in flash (NVS), edited via /config page.
// Disabling a gauge (or its refresh throttle) affects BOTH the Nextion
// display and the web dashboard, since they share nextionSend().
// =====================================================================

enum DisplayType { DISPLAY_NUMBER = 0, DISPLAY_BAR = 1, DISPLAY_NEEDLE = 2 };

struct GaugeConfig {
  const char* key;    // field name, matches what nextionSend() parses out
  const char* label;  // human-readable name for the config page
  const char* ecu;    // "rusefi" | "aem" | "gps" | "aux" - for grouping on /config
  bool isHero;        // true = fixed needle dial in the cluster (rpm/mph/fuel/clt)
  bool enabled;
  uint8_t displayType; // only meaningful when isHero==false
  unsigned long refreshMs;
  float minVal;
  float maxVal;
  bool hasRedline;
  float redline;
  unsigned long lastSentMs; // runtime only, not persisted
};

GaugeConfig gaugeConfigs[] = {
  // --- hero cluster ---
  { "rpm",       "RPM",                 "rusefi", true,  true, DISPLAY_NEEDLE, 0,   0,    8000,   true,  6800, 0 },
  { "mph",       "Speed (GPS)",         "gps",    true,  true, DISPLAY_NEEDLE, 0,   0,    140,    false, 0,    0 },
  { "fuel",      "Fuel Level",          "rusefi", true,  true, DISPLAY_NEEDLE, 250, 0,    100,    false, 0,    0 },
  { "clt",       "Coolant Temp",        "rusefi", true,  true, DISPLAY_NEEDLE, 0,   0,    260,    false, 0,    0 },

  // --- BASE1: RPM group extras ---
  { "igntim",    "Ignition Timing",     "rusefi", false, true, DISPLAY_NUMBER, 0,   -20,  50,     false, 0,    0 },
  { "injduty",   "Injector Duty",       "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    100,    false, 0,    0 },
  { "ignduty",   "Ignition Duty",       "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    100,    false, 0,    0 },
  { "vss",       "Vehicle Speed (CAN)", "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    250,    false, 0,    0 },
  { "eth",       "Ethanol %",           "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    100,    false, 0,    0 },

  // --- BASE2: throttle group ---
  { "pps",       "Pedal Position",      "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    100,    false, 0,    0 },
  { "tps",       "TPS1 (bar)",          "rusefi", false, true, DISPLAY_BAR,    0,   0,    100,    false, 0,    0 },
  { "tpst",      "TPS1 (text)",         "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    100,    false, 0,    0 },
  { "tps2",      "TPS2",                "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    100,    false, 0,    0 },
  { "wastegate", "Wastegate",           "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    100,    false, 0,    0 },

  // --- BASE3: MAP/temps group ---
  { "map",       "MAP",                 "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    250,    false, 0,    0 },
  { "boost",     "Boost",               "rusefi", false, true, DISPLAY_NUMBER, 0,   -15,  25,     false, 0,    0 },
  { "mat",       "Intake Air Temp",     "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    260,    false, 0,    0 },
  { "aux1t",     "AUX1 Temp",           "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    260,    false, 0,    0 },
  { "aux2t",     "AUX2 Temp",           "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    260,    false, 0,    0 },
  { "mcut",      "MCU Temp",            "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    260,    false, 0,    0 },

  // --- BASE4: oil/fuel/batt group ---
  { "oilp",      "Oil Pressure",        "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    700,    false, 0,    0 },
  { "oilt",      "Oil Temp",            "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    300,    false, 0,    0 },
  { "fuelt",     "Fuel Temp",           "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    260,    false, 0,    0 },
  { "battv",     "Battery Voltage",     "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    16,     false, 0,    0 },

  // --- BASE5: load/knock group ---
  { "cylam",     "Cyl Air Mass",        "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    2000,   false, 0,    0 },
  { "estmaf",    "Est. MAF",            "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    2000,   false, 0,    0 },
  { "injpw",     "Injector Pulsewidth", "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    50,     false, 0,    0 },
  { "knockct",   "Knock Count",         "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    65535,  false, 0,    0 },

  // --- BASE6: fuel usage/trims group ---
  { "fuelused",  "Fuel Used",           "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    100000, false, 0,    0 },
  { "fuelflow",  "Fuel Flow",           "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    50,     false, 0,    0 },
  { "trim1",     "Fuel Trim 1",         "rusefi", false, true, DISPLAY_NUMBER, 0,   -50,  50,     false, 0,    0 },
  { "trim2",     "Fuel Trim 2",         "rusefi", false, true, DISPLAY_NUMBER, 0,   -50,  50,     false, 0,    0 },

  // --- BASE7: fueling group ---
  { "ego",       "AFR (Lam1)",          "rusefi", false, true, DISPLAY_NUMBER, 0,   10,   20,     false, 0,    0 },
  { "lam2",      "Lambda 2",            "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    2,      false, 0,    0 },
  { "fuelp",     "Fuel Pressure (Low)", "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    500,    false, 0,    0 },
  { "fphigh",    "Fuel Pressure (High)","rusefi", false, true, DISPLAY_NUMBER, 0,   0,    10,     false, 0,    0 },

  // --- BASE8: cam timing group ---
  { "vvt",       "Cam1 Intake (VVT)",  "rusefi", false, true, DISPLAY_NUMBER, 0,   -60,  60,     false, 0,    0 },
  { "cam1itar",  "Cam1 Intake Target", "rusefi", false, true, DISPLAY_NUMBER, 0,   -100, 100,    false, 0,    0 },
  { "cam1e",     "Cam1 Exhaust",       "rusefi", false, true, DISPLAY_NUMBER, 0,   -100, 100,    false, 0,    0 },
  { "cam1etar",  "Cam1 Exhaust Target","rusefi", false, true, DISPLAY_NUMBER, 0,   -100, 100,    false, 0,    0 },
  { "cam2i",     "Cam2 Intake",        "rusefi", false, true, DISPLAY_NUMBER, 0,   -100, 100,    false, 0,    0 },
  { "cam2itar",  "Cam2 Intake Target", "rusefi", false, true, DISPLAY_NUMBER, 0,   -100, 100,    false, 0,    0 },
  { "cam2e",     "Cam2 Exhaust",       "rusefi", false, true, DISPLAY_NUMBER, 0,   -100, 100,    false, 0,    0 },
  { "cam2etar",  "Cam2 Exhaust Target","rusefi", false, true, DISPLAY_NUMBER, 0,   -100, 100,    false, 0,    0 },

  // --- BASE9: ECU-reported EGT (8ch) - distinct from the physical MAX6675 probe's "egt" key ---
  { "ecuegt1",   "ECU EGT 1",          "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    2280,   false, 0,    0 },
  { "ecuegt2",   "ECU EGT 2",          "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    2280,   false, 0,    0 },
  { "ecuegt3",   "ECU EGT 3",          "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    2280,   false, 0,    0 },
  { "ecuegt4",   "ECU EGT 4",          "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    2280,   false, 0,    0 },
  { "ecuegt5",   "ECU EGT 5",          "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    2280,   false, 0,    0 },
  { "ecuegt6",   "ECU EGT 6",          "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    2280,   false, 0,    0 },
  { "ecuegt7",   "ECU EGT 7",          "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    2280,   false, 0,    0 },
  { "ecuegt8",   "ECU EGT 8",          "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    2280,   false, 0,    0 },

  // --- BASE10: per-cylinder knock (8ch) ---
  { "knock0",    "Knock Cyl 1",        "rusefi", false, true, DISPLAY_NUMBER, 0,   -60,  10,     false, 0,    0 },
  { "knock1",    "Knock Cyl 2",        "rusefi", false, true, DISPLAY_NUMBER, 0,   -60,  10,     false, 0,    0 },
  { "knock2",    "Knock Cyl 3",        "rusefi", false, true, DISPLAY_NUMBER, 0,   -60,  10,     false, 0,    0 },
  { "knock3",    "Knock Cyl 4",        "rusefi", false, true, DISPLAY_NUMBER, 0,   -60,  10,     false, 0,    0 },
  { "knock4",    "Knock Cyl 5",        "rusefi", false, true, DISPLAY_NUMBER, 0,   -60,  10,     false, 0,    0 },
  { "knock5",    "Knock Cyl 6",        "rusefi", false, true, DISPLAY_NUMBER, 0,   -60,  10,     false, 0,    0 },
  { "knock6",    "Knock Cyl 7",        "rusefi", false, true, DISPLAY_NUMBER, 0,   -60,  10,     false, 0,    0 },
  { "knock7",    "Knock Cyl 8",        "rusefi", false, true, DISPLAY_NUMBER, 0,   -60,  10,     false, 0,    0 },

  // --- BASE0: status/diagnostic group ---
  { "warncnt",   "Warning Counter",    "rusefi", false, true, DISPLAY_NUMBER, 1000,0,    65535,  false, 0,    0 },
  { "lasterr",   "Last Error Code",    "rusefi", false, true, DISPLAY_NUMBER, 1000,0,    65535,  false, 0,    0 },
  { "revlim",    "Rev Limiter Active", "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    1,      false, 0,    0 },
  { "mainrelay", "Main Relay Active",  "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    1,      false, 0,    0 },
  { "fuelpump",  "Fuel Pump Active",   "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    1,      false, 0,    0 },
  { "cel",       "Check Engine Light", "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    1,      false, 0,    0 },
  { "egoheat",   "EGO Heater Active",  "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    1,      false, 0,    0 },
  { "lambdaprot","Lambda Protect",     "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    1,      false, 0,    0 },
  { "fan1",      "Fan 1 Active",       "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    1,      false, 0,    0 },
  { "fan2",      "Fan 2 Active",       "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    1,      false, 0,    0 },
  { "gear",      "Current Gear",       "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    8,      false, 0,    0 },
  { "odo",       "Distance Traveled",  "rusefi", false, true, DISPLAY_NUMBER, 1000,0,    999999, false, 0,    0 },

  // --- BASE11 ---
  { "brake",     "Brake Pedal",        "rusefi", false, true, DISPLAY_NUMBER, 0,   0,    1,      false, 0,    0 },

  // --- Auxiliary sensors (not from rusEFI CAN) ---
  { "egt",       "EGT (probe)",        "aux",    false, true, DISPLAY_NUMBER, 0,   0,    1800,   false, 0,    0 },
};
const int NUM_GAUGES = sizeof(gaugeConfigs) / sizeof(gaugeConfigs[0]);

int findGaugeIndex(const String &field) {
  for (int i = 0; i < NUM_GAUGES; i++) {
    if (field == gaugeConfigs[i].key) return i;
  }
  return -1;
}

// Returns true and updates lastSentMs if this field should actually be
// sent right now (enabled AND its refresh interval has elapsed).
bool gaugeReadyToSend(const String &field) {
  int idx = findGaugeIndex(field);
  if (idx < 0) return true; // unrecognized field - default to allowed
  GaugeConfig &g = gaugeConfigs[idx];
  if (!g.enabled) return false;
  unsigned long now = millis();
  if (g.refreshMs > 0 && (now - g.lastSentMs) < g.refreshMs) return false;
  g.lastSentMs = now;
  return true;
}

void loadGaugeConfig() {
  for (int i = 0; i < NUM_GAUGES; i++) {
    GaugeConfig &g = gaugeConfigs[i];
    String k = String(g.key);
    g.enabled     = gaugePrefs.getBool((k + "_en").c_str(), g.enabled);
    g.displayType = gaugePrefs.getUChar((k + "_dt").c_str(), g.displayType);
    g.refreshMs   = gaugePrefs.getULong((k + "_rf").c_str(), g.refreshMs);
    g.minVal      = gaugePrefs.getFloat((k + "_mn").c_str(), g.minVal);
    g.maxVal      = gaugePrefs.getFloat((k + "_mx").c_str(), g.maxVal);
    g.hasRedline  = gaugePrefs.getBool((k + "_hr").c_str(), g.hasRedline);
    g.redline     = gaugePrefs.getFloat((k + "_rl").c_str(), g.redline);
  }
  debugLoggingEnabled = sysPrefs.getBool("dbglog", false);
}

void saveGaugeConfig(AsyncWebServerRequest *request) {
  for (int i = 0; i < NUM_GAUGES; i++) {
    GaugeConfig &g = gaugeConfigs[i];
    String k = String(g.key);

    bool checked = request->hasParam((k + "_en").c_str(), true);
    g.enabled = checked;
    gaugePrefs.putBool((k + "_en").c_str(), checked);

    if (!g.isHero && request->hasParam((k + "_dt").c_str(), true)) {
      g.displayType = request->getParam((k + "_dt").c_str(), true)->value().toInt();
      gaugePrefs.putUChar((k + "_dt").c_str(), g.displayType);
    }
    if (request->hasParam((k + "_rf").c_str(), true)) {
      g.refreshMs = request->getParam((k + "_rf").c_str(), true)->value().toInt();
      gaugePrefs.putULong((k + "_rf").c_str(), g.refreshMs);
    }
    if (request->hasParam((k + "_mn").c_str(), true)) {
      g.minVal = request->getParam((k + "_mn").c_str(), true)->value().toFloat();
      gaugePrefs.putFloat((k + "_mn").c_str(), g.minVal);
    }
    if (request->hasParam((k + "_mx").c_str(), true)) {
      g.maxVal = request->getParam((k + "_mx").c_str(), true)->value().toFloat();
      gaugePrefs.putFloat((k + "_mx").c_str(), g.maxVal);
    }
    bool hasRl = request->hasParam((k + "_hr").c_str(), true);
    g.hasRedline = hasRl;
    gaugePrefs.putBool((k + "_hr").c_str(), hasRl);
    if (request->hasParam((k + "_rl").c_str(), true)) {
      g.redline = request->getParam((k + "_rl").c_str(), true)->value().toFloat();
      gaugePrefs.putFloat((k + "_rl").c_str(), g.redline);
    }
  }

  debugLoggingEnabled = request->hasParam("dbglog", true);
  sysPrefs.putBool("dbglog", debugLoggingEnabled);
}

// JSON array of all gauge configs, for the dashboard JS to fetch at load
String buildGaugeApiJson() {
  String json = "[";
  for (int i = 0; i < NUM_GAUGES; i++) {
    GaugeConfig &g = gaugeConfigs[i];
    if (i > 0) json += ",";
    json += "{\"key\":\"" + String(g.key) + "\",";
    json += "\"label\":\"" + String(g.label) + "\",";
    json += "\"isHero\":" + String(g.isHero ? "true" : "false") + ",";
    json += "\"enabled\":" + String(g.enabled ? "true" : "false") + ",";
    json += "\"displayType\":" + String(g.displayType) + ",";
    json += "\"min\":" + String(g.minVal, 2) + ",";
    json += "\"max\":" + String(g.maxVal, 2) + ",";
    json += "\"hasRedline\":" + String(g.hasRedline ? "true" : "false") + ",";
    json += "\"redline\":" + String(g.redline, 2);
    json += "}";
  }
  json += "]";
  return json;
}


// =====================================================================
// web dashboard - served HTML page
// Hero cluster (fuel/rpm/lcd/mph/clt) stays a fixed classic-instrument
// layout; all other gauges render dynamically as number/bar/needle-mini
// based on config fetched from /api/gauges at page load.
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
                   font-size:0.65em; font-weight:700; color:#222; letter-spacing:1px; white-space:nowrap; }
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

    .bar-section-generic { max-width: 500px; margin: 0 auto 12px auto; }
    .bar-label { display: flex; justify-content: space-between; font-size: 0.8em; color: #888;
                 text-transform: uppercase; letter-spacing: 1px; margin-bottom: 6px; }
    .bar-track { background: #1a1a1f; border-radius: 8px; height: 22px; overflow: hidden;
                 box-shadow: inset 0 0 8px rgba(0,0,0,0.6); }
    .bar-fill { height: 100%; width: 0%; background: linear-gradient(90deg, #4dd0e1, #26c6da);
                border-radius: 8px; transition: width 0.2s ease; }

    .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(120px,1fr)); gap: 10px;
            max-width: 1000px; margin: 0 auto; }
    .tile { background: #16161a; border: 1px solid #2a2a30; border-radius: 8px;
            padding: 12px 8px; text-align: center; }
    .tile .label { color: #4dd0e1; font-size: 0.65em; text-transform: uppercase; letter-spacing: 1px; }
    .tile .value { font-size: 1.3em; font-weight: 600; margin-top: 4px; }

    .mini-gauges { display:flex; flex-wrap:wrap; gap:10px; justify-content:center; max-width:1000px;
                   margin: 0 auto 20px auto; }
  </style>
</head>
<body>
  <h1>MSP-DASH Live</h1>
  <div id="status">connecting...</div>
  <div class="cfg-link"><a href="/config">&#9881; Configure Gauges</a></div>

  <div class="cluster" id="cluster"></div>
  <div class="mini-gauges" id="miniGauges"></div>
  <div id="genericBars"></div>
  <div class="grid" id="grid"></div>

  <script>
    // ---- SVG gauge face generator, shared by hero + mini needle gauges ----
    function buildGaugeFace(size, maxVal, minVal, redlineFrom, hasRedline) {
      const cx = size / 2, cy = size / 2, r = size * 0.42;
      const startDeg = -135, sweepDeg = 270;
      const range = maxVal - minVal;
      let svg = '<svg width="' + size + '" height="' + size + '" viewBox="0 0 ' + size + ' ' + size + '">';
      svg += '<circle cx="' + cx + '" cy="' + cy + '" r="' + (r + 6) + '" fill="#111" stroke="#555" stroke-width="2"/>';
      svg += '<circle cx="' + cx + '" cy="' + cy + '" r="' + r + '" fill="#f5f2ea"/>';

      if (hasRedline) {
        const fFrom = (redlineFrom - minVal) / range;
        const aFrom = (startDeg + fFrom * sweepDeg) * Math.PI / 180;
        const aTo = (startDeg + sweepDeg) * Math.PI / 180;
        const x1 = cx + r * Math.sin(aFrom), y1 = cy - r * Math.cos(aFrom);
        const x2 = cx + r * Math.sin(aTo), y2 = cy - r * Math.cos(aTo);
        const largeArc = (sweepDeg * (1 - fFrom)) > 180 ? 1 : 0;
        svg += '<path d="M ' + x1 + ' ' + y1 + ' A ' + r + ' ' + r + ' 0 ' + largeArc + ' 1 ' + x2 + ' ' + y2 +
               '" fill="none" stroke="#c0392b" stroke-width="6" opacity="0.85"/>';
      }

      const majorStep = range / 8;
      for (let i = 0; i <= 8; i++) {
        const val = minVal + i * majorStep;
        const frac = (val - minVal) / range;
        const ang = (startDeg + frac * sweepDeg) * Math.PI / 180;
        const rOuter = r - 4, rInner = r - 14;
        const x1 = cx + rOuter * Math.sin(ang), y1 = cy - rOuter * Math.cos(ang);
        const x2 = cx + rInner * Math.sin(ang), y2 = cy - rInner * Math.cos(ang);
        svg += '<line x1="' + x1 + '" y1="' + y1 + '" x2="' + x2 + '" y2="' + y2 + '" stroke="#222" stroke-width="2.5"/>';
        const rLabel = r - 24;
        const lx = cx + rLabel * Math.sin(ang), ly = cy - rLabel * Math.cos(ang);
        svg += '<text x="' + lx + '" y="' + (ly + 4) + '" font-size="' + (size * 0.07) + '" fill="#222" ' +
               'text-anchor="middle" font-family="monospace" font-weight="bold">' + Math.round(val) + '</text>';
      }
      svg += '</svg>';
      return svg;
    }

    function makeGauge(id, size, cfg, small) {
      const wrap = document.createElement('div');
      wrap.className = 'gauge-wrap';
      wrap.style.width = size + 'px';
      wrap.style.height = size + 'px';
      wrap.innerHTML = buildGaugeFace(size, cfg.max, cfg.min, cfg.redline, cfg.hasRedline) +
        '<div class="needle' + (small ? ' needle-small' : '') + '" id="needle_' + id + '"></div>' +
        '<div class="hub"></div>' +
        '<div class="gauge-title">' + cfg.label.toUpperCase() + '</div>' +
        '<div class="gauge-value" id="v_' + id + '">0</div>';
      return wrap;
    }

    let gaugeConfig = {}; // key -> config object, filled from /api/gauges
    const lastValues = {};

    function updateNeedle(field, value) {
      const cfg = gaugeConfig[field];
      if (!cfg) return;
      let frac = (value - cfg.min) / (cfg.max - cfg.min);
      frac = Math.max(0, Math.min(1, frac));
      const deg = -135 + frac * 270;
      const needle = document.getElementById('needle_' + field);
      if (needle) needle.style.transform = 'translate(-50%,-100%) rotate(' + deg + 'deg)';
    }

    const heroKeys = ['fuel', 'rpm', 'mph', 'clt'];

    function buildHeroCluster() {
      const cluster = document.getElementById('cluster');
      cluster.innerHTML = '';
      if (gaugeConfig['fuel'] && gaugeConfig['fuel'].enabled) {
        cluster.appendChild(makeGauge('fuel', 110, gaugeConfig['fuel'], true));
      }
      if (gaugeConfig['rpm'] && gaugeConfig['rpm'].enabled) {
        cluster.appendChild(makeGauge('rpm', 220, gaugeConfig['rpm'], false));
      }

      const lcd = document.createElement('div');
      lcd.className = 'lcd';
      lcd.innerHTML = '<div class="lcd-label" id="lcd_label">BOOST</div>' +
                       '<div class="lcd-value" id="lcd_value">0.0</div>' +
                       '<div class="lcd-unit" id="lcd_unit">kPa</div>';
      cluster.appendChild(lcd);

      if (gaugeConfig['mph'] && gaugeConfig['mph'].enabled) {
        const mphWrap = makeGauge('mph', 220, gaugeConfig['mph'], false);
        const gpsDot = document.createElement('div');
        gpsDot.className = 'gps-dot';
        gpsDot.id = 'gps_dot';
        mphWrap.appendChild(gpsDot);
        cluster.appendChild(mphWrap);
      }
      if (gaugeConfig['clt'] && gaugeConfig['clt'].enabled) {
        cluster.appendChild(makeGauge('clt', 110, gaugeConfig['clt'], true));
      }
    }

    const lcdStats = [
      { field: 'boost', label: 'BOOST', unit: 'kPa' },
      { field: 'oilp',  label: 'OIL PRESS', unit: 'kPa' },
      { field: 'ego',   label: 'AFR', unit: '' },
      { field: 'egt',   label: 'EGT', unit: 'F' }
    ];
    let lcdIndex = 0;
    function refreshLcd() {
      const stat = lcdStats[lcdIndex];
      const lbl = document.getElementById('lcd_label');
      if (lbl) {
        lbl.textContent = stat.label;
        document.getElementById('lcd_value').textContent = lastValues[stat.field] !== undefined ? lastValues[stat.field] : '--';
        document.getElementById('lcd_unit').textContent = stat.unit;
      }
      lcdIndex = (lcdIndex + 1) % lcdStats.length;
    }
    setInterval(refreshLcd, 3000);

    function buildDynamicGauges() {
      const miniWrap = document.getElementById('miniGauges');
      const barsWrap = document.getElementById('genericBars');
      const grid = document.getElementById('grid');
      miniWrap.innerHTML = ''; barsWrap.innerHTML = ''; grid.innerHTML = '';

      Object.keys(gaugeConfig).forEach(key => {
        if (heroKeys.includes(key)) return; // hero gauges handled separately
        const cfg = gaugeConfig[key];
        if (!cfg.enabled) return;

        if (cfg.displayType === 2) { // needle mini gauge
          miniWrap.appendChild(makeGauge(key, 100, cfg, true));
        } else if (cfg.displayType === 1) { // bar
          const section = document.createElement('div');
          section.className = 'bar-section-generic';
          section.innerHTML = '<div class="bar-label"><span>' + cfg.label + '</span><span id="v_' + key + '">--</span></div>' +
                               '<div class="bar-track"><div class="bar-fill" id="bar_' + key + '"></div></div>';
          barsWrap.appendChild(section);
        } else { // number tile
          const tile = document.createElement('div');
          tile.className = 'tile';
          tile.innerHTML = '<div class="label">' + cfg.label + '</div><div class="value" id="v_' + key + '">--</div>';
          grid.appendChild(tile);
        }
      });
    }

    function handleField(field, value) {
      lastValues[field] = value;
      const cfg = gaugeConfig[field];

      const el = document.getElementById("v_" + field);
      if (el) el.textContent = value;

      if (cfg) {
        const num = parseFloat(value);
        if (!isNaN(num)) {
          if (cfg.isHero || cfg.displayType === 2) {
            updateNeedle(field, num);
          } else if (cfg.displayType === 1) {
            const barEl = document.getElementById("bar_" + field);
            if (barEl) {
              let pct = ((num - cfg.min) / (cfg.max - cfg.min)) * 100;
              pct = Math.max(0, Math.min(100, pct));
              barEl.style.width = pct + "%";
            }
          }
        }
      }

      if (field === "gpsfix") {
        const dot = document.getElementById("gps_dot");
        if (dot) dot.classList.toggle("fix", value === "1");
      }
    }

    async function loadConfigAndStart() {
      try {
        const resp = await fetch('/api/gauges');
        const list = await resp.json();
        list.forEach(g => { gaugeConfig[g.key] = g; });
      } catch (e) {
        console.error('Failed to load gauge config', e);
      }
      buildHeroCluster();
      refreshLcd();
      buildDynamicGauges();
      connect();
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
    loadConfigAndStart();
  </script>
</body>
</html>
)HTMLPAGE";


// =====================================================================
// byte-reading helpers
// =====================================================================

uint16_t readU16LE(uint8_t* data, int offset) {
  return data[offset] | (data[offset + 1] << 8);
}
int16_t readS16LE(uint8_t* data, int offset) {
  return (int16_t)(data[offset] | (data[offset + 1] << 8));
}

// rusEFI's DBC confirms its temp signals are degC - this conversion is
// well-defined for every temp field in this sketch.
float cToF(float c) {
  return c * 9.0f / 5.0f + 32.0f;
}


// =====================================================================
// gauge config page - built dynamically, grouped per signal block
// =====================================================================

String gaugeRowHtml(GaugeConfig &g) {
  String row = "<div class='gauge-row'>";
  row += "<div class='gr-head'><label><input type='checkbox' name='" + String(g.key) + "_en'";
  if (g.enabled) row += " checked";
  row += "> " + String(g.label) + "</label></div>";

  row += "<div class='gr-fields'>";
  if (!g.isHero) {
    row += "<label>Type<select name='" + String(g.key) + "_dt'>";
    row += "<option value='0'" + String(g.displayType == 0 ? " selected" : "") + ">Number</option>";
    row += "<option value='1'" + String(g.displayType == 1 ? " selected" : "") + ">Bar</option>";
    row += "<option value='2'" + String(g.displayType == 2 ? " selected" : "") + ">Needle</option>";
    row += "</select></label>";
  }
  row += "<label>Refresh (ms)<input type='number' name='" + String(g.key) + "_rf' value='" + String(g.refreshMs) + "'></label>";
  row += "<label>Min<input type='number' step='any' name='" + String(g.key) + "_mn' value='" + String(g.minVal, 2) + "'></label>";
  row += "<label>Max<input type='number' step='any' name='" + String(g.key) + "_mx' value='" + String(g.maxVal, 2) + "'></label>";
  row += "<label class='rl'><input type='checkbox' name='" + String(g.key) + "_hr'";
  if (g.hasRedline) row += " checked";
  row += "> Redline<input type='number' step='any' name='" + String(g.key) + "_rl' value='" + String(g.redline, 2) + "'></label>";
  row += "</div></div>";
  return row;
}

String buildConfigPage() {
  String html = F(
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>MSP-DASH Config</title><style>"
    "body{background:#0a0a0d;color:#eee;font-family:'Segoe UI',system-ui,sans-serif;"
    "margin:0;padding:20px;max-width:640px;margin:0 auto;}"
    "h1{color:#4dd0e1;font-size:1.1em;letter-spacing:2px;text-transform:uppercase;}"
    "h2{color:#4dd0e1;font-size:0.9em;letter-spacing:1px;text-transform:uppercase;"
    "border-bottom:1px solid #2a2a30;padding-bottom:6px;margin-top:28px;}"
    "a{color:#4dd0e1;font-size:0.85em;text-decoration:none;}"
    ".sys-row{display:flex;justify-content:space-between;align-items:center;"
    "background:#16161a;border:1px solid #2a2a30;border-radius:8px;"
    "padding:12px 16px;margin-bottom:8px;}"
    ".gauge-row{background:#16161a;border:1px solid #2a2a30;border-radius:8px;"
    "padding:10px 14px;margin-bottom:8px;}"
    ".gr-head label{font-weight:600;}"
    ".gr-fields{display:flex;flex-wrap:wrap;gap:10px;margin-top:8px;font-size:0.8em;color:#999;}"
    ".gr-fields label{display:flex;flex-direction:column;gap:2px;}"
    ".gr-fields input[type=number]{width:70px;background:#0a0a0d;border:1px solid #2a2a30;"
    "color:#eee;border-radius:4px;padding:4px;}"
    ".gr-fields select{background:#0a0a0d;border:1px solid #2a2a30;color:#eee;border-radius:4px;padding:4px;}"
    ".rl{flex-direction:row !important;align-items:center;}"
    "input[type=checkbox]{width:18px;height:18px;}"
    "button{background:#4dd0e1;color:#0a0a0d;border:none;border-radius:8px;"
    "padding:12px 20px;font-size:1em;font-weight:600;margin-top:20px;width:100%;}"
    "</style></head><body>"
    "<a href='/'>&larr; Back to Dashboard</a>"
    "<h1>Configuration</h1>"
    "<form method='POST' action='/config'>"
    "<h2>System</h2>"
    "<div class='sys-row'><span>Serial Debug Logging</span>"
    "<input type='checkbox' name='dbglog'"
  );
  if (debugLoggingEnabled) html += " checked";
  html += F("></div>");

  html += "<h2>rusEFI</h2>";
  for (int i = 0; i < NUM_GAUGES; i++) {
    if (String(gaugeConfigs[i].ecu) == "rusefi") html += gaugeRowHtml(gaugeConfigs[i]);
  }
  html += "<h2>GPS</h2>";
  for (int i = 0; i < NUM_GAUGES; i++) {
    if (String(gaugeConfigs[i].ecu) == "gps") html += gaugeRowHtml(gaugeConfigs[i]);
  }
  html += "<h2>Auxiliary Sensors (EGT probe, etc.)</h2>";
  for (int i = 0; i < NUM_GAUGES; i++) {
    if (String(gaugeConfigs[i].ecu) == "aux") html += gaugeRowHtml(gaugeConfigs[i]);
  }

  html += F("<button type='submit'>Save</button></form></body></html>");
  return html;
}


// =====================================================================
// Nextion output
// =====================================================================

void nextionSend(const String &cmd) {
  // Parse "field.txt=\"value\"" or "field.val=value" out of the command
  // string up front, so a disabled/throttled gauge can be skipped
  // entirely - no Nextion write, no debug print, no web broadcast.
  int dot = cmd.indexOf('.');
  int eq = cmd.indexOf('=');
  if (dot <= 0 || eq <= dot) return; // malformed command, nothing to parse - drop silently

  String field = cmd.substring(0, dot);
  if (!gaugeReadyToSend(field)) return; // disabled via /config, or refresh interval not elapsed yet

  Serial2.print(cmd);
  Serial2.write(0xFF); Serial2.write(0xFF); Serial2.write(0xFF);
  if (debugLoggingEnabled) Serial.println(cmd); // OFF by default - toggle on /config

  String value = cmd.substring(eq + 1);
  value.replace("\"", ""); // strip quotes if present (text fields)
  String json = "{\"field\":\"" + field + "\",\"value\":\"" + value + "\"}";
  ws.textAll(json);
}


// =====================================================================
// rusEFI frame handling - full DBC, every signal
// =====================================================================

void handleRusefiFrame(uint32_t id, uint8_t* d) {
  switch (id) {
    case RUSEFI_BASE0: {
      uint16_t warnCntV = readU16LE(d, 0);
      uint16_t lastErrV = readU16LE(d, 2);
      uint8_t flags = d[4]; // bit0=RevLimAct,1=MainRelayAct,2=FuelPumpAct,3=CELAct,4=EGOHeatAct,5=LambdaProtectAct,6=Fan,7=Fan2
      uint8_t gearV = d[5];
      uint16_t distRaw = readU16LE(d, 6);
      float distV = distRaw * 0.1f;

      nextionSend("warncnt.txt=\"" + String(warnCntV) + "\"");
      nextionSend("lasterr.txt=\"" + String(lastErrV) + "\"");
      nextionSend("revlim.txt=\"" + String((flags >> 0) & 1) + "\"");
      nextionSend("mainrelay.txt=\"" + String((flags >> 1) & 1) + "\"");
      nextionSend("fuelpump.txt=\"" + String((flags >> 2) & 1) + "\"");
      nextionSend("cel.txt=\"" + String((flags >> 3) & 1) + "\"");
      nextionSend("egoheat.txt=\"" + String((flags >> 4) & 1) + "\"");
      nextionSend("lambdaprot.txt=\"" + String((flags >> 5) & 1) + "\"");
      nextionSend("fan1.txt=\"" + String((flags >> 6) & 1) + "\"");
      nextionSend("fan2.txt=\"" + String((flags >> 7) & 1) + "\"");
      nextionSend("gear.txt=\"" + String(gearV) + "\"");
      nextionSend("odo.txt=\"" + String(distV, 1) + "\"");
      break;
    }
    case RUSEFI_BASE1: {
      uint16_t rpm = readU16LE(d, 0);
      int16_t ignTimRaw = readS16LE(d, 2);
      float ignTim = ignTimRaw * 0.02f;
      uint8_t injDutyRaw = d[4];
      float injDuty = injDutyRaw * 0.5f;
      uint8_t ignDutyRaw = d[5];
      float ignDuty = ignDutyRaw * 0.5f;
      uint8_t vssRaw = d[6]; // kph
      uint8_t flexPct = d[7]; // FlexPct: %

      nextionSend("rpm.txt=\"" + String(rpm) + "\"");
      nextionSend("igntim.txt=\"" + String(ignTim, 1) + "\"");
      nextionSend("injduty.txt=\"" + String(injDuty, 1) + "\"");
      nextionSend("ignduty.txt=\"" + String(ignDuty, 1) + "\"");
      nextionSend("vss.txt=\"" + String(vssRaw) + "\"");
      nextionSend("eth.txt=\"" + String(flexPct) + "\"");
      break;
    }
    case RUSEFI_BASE2: {
      int16_t ppsRaw = readS16LE(d, 0);
      float ppsV = ppsRaw * 0.01f;
      int16_t tpsRaw = readS16LE(d, 2); // TPS1
      float tps = tpsRaw * 0.01f;
      int16_t tps2Raw = readS16LE(d, 4);
      float tps2V = tps2Raw * 0.01f;
      int16_t wgRaw = readS16LE(d, 6);
      float wgV = wgRaw * 0.01f;

      nextionSend("pps.txt=\"" + String(ppsV, 1) + "\"");
      nextionSend("tps.val=" + String((int)tps)); // progress bar, native 0-100 range
      nextionSend("tpst.txt=\"" + String(tps, 1) + "\"");
      nextionSend("tps2.txt=\"" + String(tps2V, 1) + "\"");
      nextionSend("wastegate.txt=\"" + String(wgV, 1) + "\"");
      break;
    }
    case RUSEFI_BASE3: {
      uint16_t mapRaw = readU16LE(d, 0);
      float mapKpa = mapRaw * 0.0333333f;
      float boost = mapKpa - ASSUMED_BARO_KPA; // no baro signal in rusEFI DBC - fixed assumption
      int8_t cltRaw = (int8_t)d[2];
      float clt = cToF(cltRaw - 40);
      int8_t matRaw = (int8_t)d[3];
      float mat = cToF(matRaw - 40);
      int8_t aux1Raw = (int8_t)d[4];
      float aux1V = cToF(aux1Raw - 40);
      int8_t aux2Raw = (int8_t)d[5];
      float aux2V = cToF(aux2Raw - 40);
      int8_t mcuRaw = (int8_t)d[6];
      float mcuV = cToF(mcuRaw - 40);
      uint8_t fuelRaw = d[7]; // FuelLevel: x0.5, %
      float fuel = fuelRaw * 0.5f;

      nextionSend("map.txt=\"" + String(mapKpa, 1) + "\"");
      nextionSend("boost.txt=\"" + String(boost, 1) + "\"");
      nextionSend("clt.txt=\"" + String(clt, 1) + "\"");
      nextionSend("mat.txt=\"" + String(mat, 1) + "\"");
      nextionSend("aux1t.txt=\"" + String(aux1V, 1) + "\"");
      nextionSend("aux2t.txt=\"" + String(aux2V, 1) + "\"");
      nextionSend("mcut.txt=\"" + String(mcuV, 1) + "\"");
      nextionSend("fuel.txt=\"" + String(fuel, 1) + "\"");
      break;
    }
    case RUSEFI_BASE4: {
      uint16_t oilPressRaw = readU16LE(d, 2); // OilPress: byte2-3
      float oilPressKpa = oilPressRaw * 0.0333333f;
      int8_t oilTempRaw = (int8_t)d[4];
      float oilTemp = cToF(oilTempRaw - 40);
      int8_t fuelTRaw = (int8_t)d[5];
      float fuelTV = cToF(fuelTRaw - 40);
      uint16_t battRaw = readU16LE(d, 6); // BattVolt: byte6-7
      float battV = battRaw * 0.001f;

      nextionSend("oilp.txt=\"" + String(oilPressKpa, 1) + "\"");
      nextionSend("oilt.txt=\"" + String(oilTemp, 1) + "\"");
      nextionSend("fuelt.txt=\"" + String(fuelTV, 1) + "\"");
      nextionSend("battv.txt=\"" + String(battV, 2) + "\"");
      break;
    }
    case RUSEFI_BASE5: {
      uint16_t cylamV = readU16LE(d, 0);
      uint16_t estmafRaw = readU16LE(d, 2);
      float estmafV = estmafRaw * 0.01f;
      uint16_t injpwRaw = readU16LE(d, 4);
      float injpwV = injpwRaw * 0.00333333f;
      uint16_t knockctV = readU16LE(d, 6);

      nextionSend("cylam.txt=\"" + String(cylamV) + "\"");
      nextionSend("estmaf.txt=\"" + String(estmafV, 2) + "\"");
      nextionSend("injpw.txt=\"" + String(injpwV, 2) + "\"");
      nextionSend("knockct.txt=\"" + String(knockctV) + "\"");
      break;
    }
    case RUSEFI_BASE6: {
      uint16_t fuelusedV = readU16LE(d, 0);
      uint16_t fuelflowRaw = readU16LE(d, 2);
      float fuelflowV = fuelflowRaw * 0.005f;
      int16_t trim1Raw = readS16LE(d, 4);
      float trim1V = trim1Raw * 0.01f;
      int16_t trim2Raw = readS16LE(d, 6);
      float trim2V = trim2Raw * 0.01f;

      nextionSend("fuelused.txt=\"" + String(fuelusedV) + "\"");
      nextionSend("fuelflow.txt=\"" + String(fuelflowV, 2) + "\"");
      nextionSend("trim1.txt=\"" + String(trim1V, 1) + "\"");
      nextionSend("trim2.txt=\"" + String(trim2V, 1) + "\"");
      break;
    }
    case RUSEFI_BASE7: {
      uint16_t lam1Raw = readU16LE(d, 0);
      float lambda = lam1Raw * 0.0001f;
      float afr = lambda * 14.7f; // gasoline stoich - adjust if E85/flex
      uint16_t lam2Raw = readU16LE(d, 2);
      float lam2V = lam2Raw * 0.0001f;
      uint16_t fpLowRaw = readU16LE(d, 4);
      float fpLowKpa = fpLowRaw * 0.0333333f;
      uint16_t fpHighRaw = readU16LE(d, 6);
      float fpHighV = fpHighRaw * 0.1f;

      nextionSend("ego.txt=\"" + String(afr, 1) + "\"");
      nextionSend("lam2.txt=\"" + String(lam2V, 4) + "\"");
      nextionSend("fuelp.txt=\"" + String(fpLowKpa, 1) + "\"");
      nextionSend("fphigh.txt=\"" + String(fpHighV, 1) + "\"");
      break;
    }
    case RUSEFI_BASE8: {
      int8_t cam1iRaw = (int8_t)d[0];
      int8_t cam1itarRaw = (int8_t)d[1];
      int8_t cam1eRaw = (int8_t)d[2];
      int8_t cam1etarRaw = (int8_t)d[3];
      int8_t cam2iRaw = (int8_t)d[4];
      int8_t cam2itarRaw = (int8_t)d[5];
      int8_t cam2eRaw = (int8_t)d[6];
      int8_t cam2etarRaw = (int8_t)d[7];

      nextionSend("vvt.txt=\"" + String(cam1iRaw) + "\""); // kept as "vvt" - matches existing Nextion HMI component
      nextionSend("cam1itar.txt=\"" + String(cam1itarRaw) + "\"");
      nextionSend("cam1e.txt=\"" + String(cam1eRaw) + "\"");
      nextionSend("cam1etar.txt=\"" + String(cam1etarRaw) + "\"");
      nextionSend("cam2i.txt=\"" + String(cam2iRaw) + "\"");
      nextionSend("cam2itar.txt=\"" + String(cam2itarRaw) + "\"");
      nextionSend("cam2e.txt=\"" + String(cam2eRaw) + "\"");
      nextionSend("cam2etar.txt=\"" + String(cam2etarRaw) + "\"");
      break;
    }
    case RUSEFI_BASE9: {
      // Egt1-8: raw x5 = degC directly (DBC scale 5, offset 0)
      const char* keys[8] = {"ecuegt1","ecuegt2","ecuegt3","ecuegt4","ecuegt5","ecuegt6","ecuegt7","ecuegt8"};
      for (int i = 0; i < 8; i++) {
        float egtC = d[i] * 5.0f;
        float egtF = cToF(egtC);
        nextionSend(String(keys[i]) + ".txt=\"" + String(egtF, 0) + "\"");
      }
      break;
    }
    case RUSEFI_BASE10: {
      const char* keys[8] = {"knock0","knock1","knock2","knock3","knock4","knock5","knock6","knock7"};
      for (int i = 0; i < 8; i++) {
        int8_t raw = (int8_t)d[i];
        nextionSend(String(keys[i]) + ".txt=\"" + String(raw) + "\"");
      }
      break;
    }
    case RUSEFI_BASE11: {
      uint8_t brakeV = d[0] & 1;
      nextionSend("brake.txt=\"" + String(brakeV) + "\"");
      break;
    }
  }
}


// =====================================================================
// EGT (MAX6675 + K-type thermocouple)
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
    if (debugLoggingEnabled) Serial.println("EGT: thermocouple open/disconnected");
    nextionSend("egt.txt=\"--\"");
  } else {
    double egtF = egtC * 9.0 / 5.0 + 32.0;
    if (debugLoggingEnabled) Serial.printf("EGT = %.1f C (%.1f F)\n", egtC, egtF);
    nextionSend("egt.txt=\"" + String(egtF, 1) + "\"");
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
  sysPrefs.begin("sysconf", false);  // NVS namespace "sysconf" - debug logging setting
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
  webServer.on("/api/gauges", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", buildGaugeApiJson());
  });
  webServer.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", buildConfigPage());
  });
  webServer.on("/config", HTTP_POST, [](AsyncWebServerRequest *request) {
    saveGaugeConfig(request);
    request->redirect("/config");
  });
  webServer.begin();

  Serial.println("esp32-main running - rusEFI (full DBC) + GPS -> Nextion + web dashboard");
}

void loop() {
  twai_message_t message;
  if (twai_receive(&message, pdMS_TO_TICKS(20)) == ESP_OK) {
    uint32_t id = message.identifier;
    uint8_t* d = message.data;

    if (id >= 0x200 && id <= 0x20B) {
      handleRusefiFrame(id, d);
    }
  }

  updateGps();
  updateEgt();
  ws.cleanupClients();

  // TODO: check per-block millis() timestamps vs STALE_MS and show a
  // "no data" indicator on the Nextion side if a channel goes stale.
}
