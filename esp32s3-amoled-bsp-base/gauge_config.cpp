/*
  gauge_config.cpp
  Stage A of the page/cell redesign - data model only (no rendering or
  web UI changes yet, per the staged plan). Pages now contain a chosen
  layout template plus an array of cells, each cell holding everything
  a single gauge needs (signal, type, range, colors, warning, etc -
  essentially the full per-gauge config from before this redesign, now
  scoped per-cell instead of per-page).

  NVS persistence via ArduinoJson, same pattern as before - the whole
  page/cell array serialized as one JSON string under a single
  Preferences key.
*/

#include "gauge_config.h"
#include "gauge_app.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <string.h>
#include <Arduino.h>

static Preferences prefs;
static Preferences webPrefs;
static Preferences logPrefs;
static PageConfig s_pages[MAX_PAGES];
static PageConfig s_webPages[MAX_PAGES];
static uint32_t s_signalLastSeenMs[SIG_COUNT];
static bool s_logEnabled[SIG_COUNT];

int gauge_config_cells_for_layout(LayoutType layout) {
  switch (layout) {
    case LAYOUT_1CELL: return 1;
    case LAYOUT_2UP: return 2;
    case LAYOUT_3UP: return 3;
    case LAYOUT_2X2: return 4;
    default: return 1;
  }
}

static CellConfig makeCell(bool enabled, SignalId signal, GaugeType type, int32_t min, int32_t max, int32_t rest,
                            const char *caption, uint32_t needle, uint32_t tick, uint32_t bg, float divisor,
                            int32_t tickCount, int32_t majorNth, int32_t refreshHz, bool hasWarning,
                            float warnThresh, bool warnAbove) {
  CellConfig c;
  c.enabled = enabled;
  c.signal = signal;
  c.gauge_type = type;
  c.min_val = min;
  c.max_val = max;
  c.rest_val = rest;
  strlcpy(c.caption, caption, GAUGE_CAPTION_MAXLEN);
  c.needle_color = needle;
  c.tick_color = tick;
  c.bg_color = bg;
  c.value_divisor = divisor;
  c.tick_count = tickCount;
  c.major_nth = majorNth;
  c.refresh_hz = refreshHz;
  c.show_value = false;
  c.value_font_size = 28;
  c.has_warning = hasWarning;
  c.warning_threshold = warnThresh;
  c.warning_above = warnAbove;
  c.alert_fullscreen = false;
  c.history_sec = 30;
  c.ref_enabled = false;
  c.ref_value = 0.0f;
  return c;
}

static void applyDefaults(void) {
  memset(s_pages, 0, sizeof(s_pages));

  // Matches the pre-redesign 7-gauge default set, one signal per page
  // via LAYOUT_1CELL, plus one reserved-but-disabled RPM page, same as
  // before.
  struct { SignalId sig; int32_t min, max, rest; const char *cap; bool warn; float thresh; bool above; } defs[8] = {
    { SIG_CLT,   65, 250, 65,  "CLT",        true,  220, true  },
    { SIG_MAP,   0,  250, 100, "MAP",        false, 0,   false },
    { SIG_AFR,   10, 20,  10,  "AFR",        true,  15,  true  },
    { SIG_OILP,  0,  100, 0,   "OIL PRESS",  true,  40,  false },
    { SIG_OILT,  65, 250, 65,  "OIL TEMP",   true,  220, true  },
    { SIG_FUELP, 0,  100, 0,   "FUEL PRESS", true,  40,  false },
    { SIG_FLEX,  0,  100, 0,   "ETH %",      false, 0,   false },
    { SIG_RPM,   1,  9,   1,   "RPM x1000",  false, 0,   false },
  };

  for (int i = 0; i < 8; i++) {
    s_pages[i].enabled = (i < 7); // RPM page starts disabled, same as before
    s_pages[i].layout = LAYOUT_1CELL;
    float divisor = (defs[i].sig == SIG_RPM) ? 1000.0f : 1.0f;
    int32_t tickCount = (defs[i].sig == SIG_RPM) ? 9 : 26;
    int32_t majorNth = (defs[i].sig == SIG_RPM) ? 1 : 5;
    int32_t refreshHz = (defs[i].sig == SIG_CLT || defs[i].sig == SIG_OILT) ? 1
                       : (defs[i].sig == SIG_FLEX) ? 5 : 50;
    s_pages[i].cells[0] = makeCell(true, defs[i].sig, GAUGE_TYPE_NEEDLE, defs[i].min, defs[i].max, defs[i].rest,
                                    defs[i].cap, 0xFFFFFF, 0xFFFFFF, 0x000000, divisor, tickCount, majorNth,
                                    refreshHz, defs[i].warn, defs[i].thresh, defs[i].above);
  }
}

static void applyWebDefaults(void) {
  memset(s_webPages, 0, sizeof(s_webPages));
  // A simple glanceable default for a phone - one page, 2x2 grid of
  // plain numbers (RPM, MAP/Boost, CLT, AFR) - independent of whatever
  // the physical display's pages show.
  s_webPages[0].enabled = true;
  s_webPages[0].layout = LAYOUT_2X2;
  s_webPages[0].cells[0] = makeCell(true, SIG_RPM, GAUGE_TYPE_NUMBER, 0, 9000, 0, "RPM", 0xFFFFFF, 0xFFFFFF, 0x000000, 1.0f, 26, 5, 10, false, 0, false);
  s_webPages[0].cells[1] = makeCell(true, SIG_MAP, GAUGE_TYPE_NUMBER, 0, 250, 100, "MAP", 0xFFFFFF, 0xFFFFFF, 0x000000, 1.0f, 26, 5, 10, false, 0, false);
  s_webPages[0].cells[2] = makeCell(true, SIG_CLT, GAUGE_TYPE_NUMBER, 65, 250, 65, "CLT", 0xFFFFFF, 0xFFFFFF, 0x000000, 1.0f, 26, 5, 10, true, 220, true);
  s_webPages[0].cells[3] = makeCell(true, SIG_AFR, GAUGE_TYPE_NUMBER, 10, 20, 10, "AFR", 0xFFFFFF, 0xFFFFFF, 0x000000, 1.0f, 26, 5, 10, true, 15, true);
}

static void serializeCell(JsonObject o, CellConfig *c) {
  o["enabled"] = c->enabled;
  o["signal"] = (int)c->signal;
  o["gauge_type"] = (int)c->gauge_type;
  o["min"] = c->min_val;
  o["max"] = c->max_val;
  o["rest"] = c->rest_val;
  o["caption"] = c->caption;
  o["needle_color"] = c->needle_color;
  o["tick_color"] = c->tick_color;
  o["bg_color"] = c->bg_color;
  o["value_divisor"] = c->value_divisor;
  o["tick_count"] = c->tick_count;
  o["major_nth"] = c->major_nth;
  o["refresh_hz"] = c->refresh_hz;
  o["show_value"] = c->show_value;
  o["value_font_size"] = c->value_font_size;
  o["has_warning"] = c->has_warning;
  o["warning_threshold"] = c->warning_threshold;
  o["warning_above"] = c->warning_above;
  o["alert_fullscreen"] = c->alert_fullscreen;
  o["history_sec"] = c->history_sec;
  o["ref_enabled"] = c->ref_enabled;
  o["ref_value"] = c->ref_value;
}

static void deserializeCell(JsonObject o, CellConfig *c) {
  c->enabled = o["enabled"] | false;
  c->signal = (SignalId)(int)(o["signal"] | 0);
  c->gauge_type = (GaugeType)(int)(o["gauge_type"] | 0);
  c->min_val = o["min"] | 0;
  c->max_val = o["max"] | 100;
  c->rest_val = o["rest"] | 0;
  strlcpy(c->caption, (o["caption"] | ""), GAUGE_CAPTION_MAXLEN);
  c->needle_color = (uint32_t)(long)(o["needle_color"] | 0xFFFFFF);
  c->tick_color = (uint32_t)(long)(o["tick_color"] | 0xFFFFFF);
  c->bg_color = (uint32_t)(long)(o["bg_color"] | 0x000000);
  c->value_divisor = o["value_divisor"] | 1.0f;
  c->tick_count = o["tick_count"] | 26;
  c->major_nth = o["major_nth"] | 5;
  c->refresh_hz = o["refresh_hz"] | 10;
  c->show_value = o["show_value"] | false;
  c->value_font_size = o["value_font_size"] | 28;
  c->has_warning = o["has_warning"] | false;
  c->warning_threshold = o["warning_threshold"] | 0.0f;
  c->warning_above = o["warning_above"] | false;
  c->alert_fullscreen = o["alert_fullscreen"] | false;
  c->history_sec = o["history_sec"] | 30;
  c->ref_enabled = o["ref_enabled"] | false;
  c->ref_value = o["ref_value"] | 0.0f;
}

void gauge_config_init(void) {
  prefs.begin("gaugecfg2", false); // new key namespace - old flat-slot format isn't compatible with this nested structure
  String json = prefs.getString("pages", "");

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

  JsonArray pagesArr = doc.as<JsonArray>();
  int i = 0;
  for (JsonObject pageObj : pagesArr) {
    if (i >= MAX_PAGES) break;
    s_pages[i].enabled = pageObj["enabled"] | false;
    s_pages[i].layout = (LayoutType)(int)(pageObj["layout"] | 0);
    JsonArray cellsArr = pageObj["cells"];
    int j = 0;
    for (JsonObject cellObj : cellsArr) {
      if (j >= MAX_CELLS_PER_PAGE) break;
      deserializeCell(cellObj, &s_pages[i].cells[j]);
      j++;
    }
    i++;
  }
}

PageConfig *gauge_config_get_pages(void) {
  return s_pages;
}

void gauge_config_save(void) {
  JsonDocument doc;
  JsonArray pagesArr = doc.to<JsonArray>();
  for (int i = 0; i < MAX_PAGES; i++) {
    JsonObject pageObj = pagesArr.add<JsonObject>();
    pageObj["enabled"] = s_pages[i].enabled;
    pageObj["layout"] = (int)s_pages[i].layout;
    JsonArray cellsArr = pageObj["cells"].to<JsonArray>();
    for (int j = 0; j < MAX_CELLS_PER_PAGE; j++) {
      JsonObject cellObj = cellsArr.add<JsonObject>();
      serializeCell(cellObj, &s_pages[i].cells[j]);
    }
  }
  String out;
  serializeJson(doc, out);
  prefs.putString("pages", out);
}

void gauge_config_web_init(void) {
  webPrefs.begin("webcfg2", false);
  String json = webPrefs.getString("pages", "");

  if (json.length() == 0) {
    applyWebDefaults();
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    applyWebDefaults();
    return;
  }

  JsonArray pagesArr = doc.as<JsonArray>();
  int i = 0;
  for (JsonObject pageObj : pagesArr) {
    if (i >= MAX_PAGES) break;
    s_webPages[i].enabled = pageObj["enabled"] | false;
    s_webPages[i].layout = (LayoutType)(int)(pageObj["layout"] | 0);
    JsonArray cellsArr = pageObj["cells"];
    int j = 0;
    for (JsonObject cellObj : cellsArr) {
      if (j >= MAX_CELLS_PER_PAGE) break;
      deserializeCell(cellObj, &s_webPages[i].cells[j]);
      j++;
    }
    i++;
  }
}

PageConfig *gauge_config_get_web_pages(void) {
  return s_webPages;
}

void gauge_config_web_save(void) {
  JsonDocument doc;
  JsonArray pagesArr = doc.to<JsonArray>();
  for (int i = 0; i < MAX_PAGES; i++) {
    JsonObject pageObj = pagesArr.add<JsonObject>();
    pageObj["enabled"] = s_webPages[i].enabled;
    pageObj["layout"] = (int)s_webPages[i].layout;
    JsonArray cellsArr = pageObj["cells"].to<JsonArray>();
    for (int j = 0; j < MAX_CELLS_PER_PAGE; j++) {
      JsonObject cellObj = cellsArr.add<JsonObject>();
      serializeCell(cellObj, &s_webPages[i].cells[j]);
    }
  }
  String out;
  serializeJson(doc, out);
  webPrefs.putString("pages", out);
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
  bool seen;
  switch (sig) {
    case SIG_RPM: seen = g_rpm_has_data; break;
    case SIG_CLT: seen = g_clt_has_data; break;
    case SIG_MAP: seen = g_boost_has_data; break;
    case SIG_AFR: seen = g_afr_has_data; break;
    case SIG_OILP: seen = g_oilp_has_data; break;
    case SIG_OILT: seen = g_oilt_has_data; break;
    case SIG_FUELP: seen = g_fuelp_has_data; break;
    case SIG_FLEX: seen = g_flex_has_data; break;
    case SIG_TPS: seen = g_tps_has_data; break;
    case SIG_BATTV: seen = g_battv_has_data; break;
    default: seen = false; break;
  }
  if (!seen) return false;
  // The g_*_has_data flags are set on the first frame and never cleared,
  // so also require a recent frame - otherwise a dead bus leaves every
  // gauge frozen on its last value and logging keyed to RPM keeps going.
  return gauge_config_signal_age_ms(sig) < GAUGE_SIGNAL_STALE_MS;
}

void gauge_config_mark_signal_active(SignalId sig) {
  if (sig < 0 || sig >= SIG_COUNT) return;
  s_signalLastSeenMs[sig] = millis();
}

uint32_t gauge_config_signal_age_ms(SignalId sig) {
  if (sig < 0 || sig >= SIG_COUNT) return UINT32_MAX;
  if (s_signalLastSeenMs[sig] == 0) return UINT32_MAX; // never seen
  return millis() - s_signalLastSeenMs[sig];
}

void gauge_config_log_init(void) {
  logPrefs.begin("logchans", false);
  for (int i = 0; i < SIG_COUNT; i++) {
    // Default: log everything - user can narrow down via the Log
    // Channels web page. Stored as one bit per signal in a single
    // 32-bit value for simplicity (SIG_COUNT is 11, comfortably fits).
    char key[8];
    snprintf(key, sizeof(key), "s%d", i);
    s_logEnabled[i] = logPrefs.getBool(key, true);
  }
}

bool gauge_config_is_log_enabled(SignalId sig) {
  if (sig < 0 || sig >= SIG_COUNT) return false;
  return s_logEnabled[sig];
}

void gauge_config_set_log_enabled(SignalId sig, bool enabled) {
  if (sig < 0 || sig >= SIG_COUNT) return;
  s_logEnabled[sig] = enabled;
}

void gauge_config_log_save(void) {
  for (int i = 0; i < SIG_COUNT; i++) {
    char key[8];
    snprintf(key, sizeof(key), "s%d", i);
    logPrefs.putBool(key, s_logEnabled[i]);
  }
}
