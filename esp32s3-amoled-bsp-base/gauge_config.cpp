/*
  gauge_config.cpp
  Persisted, data-driven gauge slot configuration - signal assignment,
  range, needle color, caption, and warning threshold per slot, all
  editable from the web config page and saved to NVS via ArduinoJson
  (the whole slot array serialized as one JSON string under a single
  Preferences key - simpler than dozens of individual keys for this
  many fields per slot).

  Defaults match the previously-hardcoded 7-gauge setup exactly, plus
  one extra disabled slot (defaulted to RPM) as a ready-to-enable spot
  for "add more gauges" without needing a firmware change.
*/

#include "gauge_config.h"
#include "gauge_app.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <string.h>

static Preferences prefs;
static GaugeSlotConfig s_slots[MAX_GAUGE_SLOTS];

static void applyDefaults(void) {
  // { signal, enabled, min, max, rest, caption, needle_color, tick_color, bg_color, value_divisor, tick_count, major_nth, refresh_hz, show_value, value_font_size, has_warning, warning_threshold, warning_above }
  s_slots[0] = { SIG_CLT,   true,  65, 250, 65,  "CLT",          0xFFFFFF, 0xFFFFFF, 0x000000, 1.0f,    26, 5, 1,  false, 28, true,  220, true  };
  s_slots[1] = { SIG_MAP,   true,  0,  250, 100, "MAP",          0xFFFFFF, 0xFFFFFF, 0x000000, 1.0f,    26, 5, 50, false, 28, false, 0,   false };
  s_slots[2] = { SIG_AFR,   true,  10, 20,  10,  "AFR",          0xFFFFFF, 0xFFFFFF, 0x000000, 1.0f,    26, 5, 50, false, 28, true,  15,  true  };
  s_slots[3] = { SIG_OILP,  true,  0,  100, 0,   "OIL PRESS",    0xFFFFFF, 0xFFFFFF, 0x000000, 1.0f,    26, 5, 50, false, 28, true,  40,  false };
  s_slots[4] = { SIG_OILT,  true,  65, 250, 65,  "OIL TEMP",     0xFFFFFF, 0xFFFFFF, 0x000000, 1.0f,    26, 5, 1,  false, 28, true,  220, true  };
  s_slots[5] = { SIG_FUELP, true,  0,  100, 0,   "FUEL PRESS",   0xFFFFFF, 0xFFFFFF, 0x000000, 1.0f,    26, 5, 50, false, 28, true,  40,  false };
  s_slots[6] = { SIG_FLEX,  true,  0,  100, 0,   "ETH %",        0xFFFFFF, 0xFFFFFF, 0x000000, 1.0f,    26, 5, 5,  false, 28, false, 0,   false };
  s_slots[7] = { SIG_RPM,   false, 1,  9,   1,   "RPM x1000",    0xFFFFFF, 0xFFFFFF, 0x000000, 1000.0f, 9,  1, 50, false, 28, false, 0,   false };
}

void gauge_config_init(void) {
  prefs.begin("gaugecfg", false);
  String json = prefs.getString("slots", "");

  if (json.length() == 0) {
    applyDefaults();
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    applyDefaults();
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  int i = 0;
  for (JsonObject o : arr) {
    if (i >= MAX_GAUGE_SLOTS) break;
    s_slots[i].signal = (SignalId)(int)o["signal"];
    s_slots[i].enabled = o["enabled"];
    s_slots[i].min_val = o["min"];
    s_slots[i].max_val = o["max"];
    s_slots[i].rest_val = o["rest"];
    strlcpy(s_slots[i].caption, o["caption"] | "", GAUGE_CAPTION_MAXLEN);
    s_slots[i].needle_color = (uint32_t)(long)o["color"];
    s_slots[i].tick_color = (uint32_t)(long)o["tick_color"];
    s_slots[i].bg_color = (uint32_t)(long)o["bg_color"];
    s_slots[i].value_divisor = o["value_divisor"] | 1.0f;
    s_slots[i].tick_count = o["tick_count"] | 26;
    s_slots[i].major_nth = o["major_nth"] | 5;
    s_slots[i].refresh_hz = o["refresh_hz"] | 10;
    s_slots[i].show_value = o["show_value"] | false;
    s_slots[i].value_font_size = o["value_font_size"] | 28;
    s_slots[i].has_warning = o["has_warning"];
    s_slots[i].warning_threshold = o["warning_threshold"];
    s_slots[i].warning_above = o["warning_above"];
    i++;
  }
}

GaugeSlotConfig *gauge_config_get_slots(void) {
  return s_slots;
}

void gauge_config_swap_slots(int a, int b) {
  if (a < 0 || a >= MAX_GAUGE_SLOTS || b < 0 || b >= MAX_GAUGE_SLOTS) return;
  GaugeSlotConfig tmp = s_slots[a];
  s_slots[a] = s_slots[b];
  s_slots[b] = tmp;
}

void gauge_config_apply_colors_to_all(uint32_t needle_color, uint32_t tick_color, uint32_t bg_color) {
  for (int i = 0; i < MAX_GAUGE_SLOTS; i++) {
    s_slots[i].needle_color = needle_color;
    s_slots[i].tick_color = tick_color;
    s_slots[i].bg_color = bg_color;
  }
}

void gauge_config_save(void) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < MAX_GAUGE_SLOTS; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["signal"] = (int)s_slots[i].signal;
    o["enabled"] = s_slots[i].enabled;
    o["min"] = s_slots[i].min_val;
    o["max"] = s_slots[i].max_val;
    o["rest"] = s_slots[i].rest_val;
    o["caption"] = s_slots[i].caption;
    o["color"] = s_slots[i].needle_color;
    o["tick_color"] = s_slots[i].tick_color;
    o["bg_color"] = s_slots[i].bg_color;
    o["value_divisor"] = s_slots[i].value_divisor;
    o["tick_count"] = s_slots[i].tick_count;
    o["major_nth"] = s_slots[i].major_nth;
    o["refresh_hz"] = s_slots[i].refresh_hz;
    o["show_value"] = s_slots[i].show_value;
    o["value_font_size"] = s_slots[i].value_font_size;
    o["has_warning"] = s_slots[i].has_warning;
    o["warning_threshold"] = s_slots[i].warning_threshold;
    o["warning_above"] = s_slots[i].warning_above;
  }
  String out;
  serializeJson(doc, out);
  prefs.putString("slots", out);
}

const char *gauge_config_signal_name(SignalId sig) {
  switch (sig) {
    case SIG_RPM: return "RPM";
    case SIG_CLT: return "Coolant Temp";
    case SIG_MAP: return "MAP/Boost";
    case SIG_AFR: return "AFR";
    case SIG_OILP: return "Oil Pressure";
    case SIG_OILT: return "Oil Temp";
    case SIG_FUELP: return "Fuel Pressure";
    case SIG_FLEX: return "Ethanol %";
    case SIG_TPS: return "Throttle Position";
    case SIG_BATTV: return "Battery Voltage";
    default: return "None";
  }
}

float gauge_config_signal_value(SignalId sig) {
  switch (sig) {
    case SIG_RPM: return g_rpm;
    case SIG_CLT: return g_clt_f;
    case SIG_MAP: return g_boost_kpa;
    case SIG_AFR: return g_afr;
    case SIG_OILP: return g_oilp_kpa;
    case SIG_OILT: return g_oilt_f;
    case SIG_FUELP: return g_fuelp_kpa;
    case SIG_FLEX: return g_flex_pct;
    case SIG_TPS: return g_tps;
    case SIG_BATTV: return g_battv;
    default: return 0;
  }
}

bool gauge_config_signal_has_data(SignalId sig) {
  switch (sig) {
    case SIG_RPM: return g_rpm_has_data;
    case SIG_CLT: return g_clt_has_data;
    case SIG_MAP: return g_boost_has_data;
    case SIG_AFR: return g_afr_has_data;
    case SIG_OILP: return g_oilp_has_data;
    case SIG_OILT: return g_oilt_has_data;
    case SIG_FUELP: return g_fuelp_has_data;
    case SIG_FLEX: return g_flex_has_data;
    case SIG_TPS: return g_tps_has_data;
    case SIG_BATTV: return g_battv_has_data;
    default: return false;
  }
}
