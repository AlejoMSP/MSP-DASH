/*
  gauge_config.cpp
  Stage A of the page/cell redesign - data model only (no rendering or
  web UI changes yet, per the staged plan). Pages now contain a chosen
  layout template plus an array of cells, each cell holding everything
  a single gauge needs (signal, type, range, colors, warning, etc -
  essentially the full per-gauge config from before this redesign, now
  scoped per-cell instead of per-page).

  Persistence: the display configuration is one JSON file
  on the board's FAT flash partition - see the Persistence section below for
  why this is NOT stored in NVS.
*/

#include "gauge_config.h"
#include "custom_slots.h"
#include "gauge_config_json.h"
#include "gauge_style.h"
#include "gauge_indicators.h"
#include "gauge_app.h"
#include <Preferences.h>   // still used for the small log-channel flags
#include <FFat.h>
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <cmath>
#include <Arduino.h>
#include "esp_heap_caps.h"

static Preferences logPrefs;
static PageConfig s_pages[MAX_PAGES];
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
  memset(&c, 0, sizeof(c));   // every field starts at 0 / empty: settings added later (units text, caption size, position ...) must not start as garbage
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
  c.decimals = GAUGE_DECIMALS_DEFAULT;
  c.h_align = GAUGE_HALIGN_CENTER;
  { int32_t dmin, dmax;       // the signal's template scale (gauge_style.h)
    if (gauge_signal_default_range(signal, &dmin, &dmax)) { c.min_val = dmin; c.max_val = dmax; c.rest_val = (rest >= dmin && rest <= dmax) ? rest : dmin; } }
  c.tick_font_size = 28;
  gauge_dial_defaults(0, &c.start_deg, &c.end_deg);
  c.low_crit_on = c.low_warn_on = c.high_warn_on = c.high_crit_on = false;
  c.low_crit = c.low_warn = c.high_warn = c.high_crit = 0;
  c.zone_style = GAUGE_ZONE_BANDS;
  c.needle_hub_pct = 0; c.needle_tip_pct = 0;
  c.needle_w = GAUGE_NEEDLE_W_DEFAULT; c.text_xy = false; c.cap_x = c.cap_y = c.val_x = c.val_y = 0;
  c.dial_x = c.dial_y = 0;
  c.peak_hold = false; c.peak_hold_s = GAUGE_PEAK_HOLD_S_DEFAULT;
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
    { SIG_MAP,   -15, 30, 0,  "BOOST",      false, 0,   false },
    { SIG_AFR,   10, 20,  10,  "AFR",        true,  15,  true  },
    { SIG_OILP,  0,  100, 0,   "OIL PRESS",  true,  10,  false },
    { SIG_OILT,  65, 250, 65,  "OIL TEMP",   true,  220, true  },
    { SIG_FUELP, 0,  100, 0,   "FUEL PRESS", true,  30,  false },
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


// Lamp text is drawn with the display's normal fonts, which cover printable ASCII: anything else (control
// characters, emoji, other scripts) is dropped rather than shown as empty boxes.
static void copyLampText(char *dst, size_t cap, const char *src) {
  size_t n = 0;
  for (const char *p = src ? src : ""; *p && n + 1 < cap; p++) {
    unsigned char c = (unsigned char)*p;
    if (c >= 0x20 && c <= 0x7E) dst[n++] = (char)c;
  }
  dst[n] = '\0';
}

static void serializeCell(JsonObject o, const CellConfig *c) {
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
  o["tick_font_size"] = c->tick_font_size;
  o["caption_font_size"] = c->caption_font_size;
  o["caption_on_face"] = c->caption_on_face;
  o["units"] = c->units;
  o["decimals"] = c->decimals;
  o["h_align"] = c->h_align;
  o["v_align"] = c->v_align;
  o["start_deg"] = c->start_deg;
  o["end_deg"] = c->end_deg;
  o["low_crit_on"] = c->low_crit_on;
  o["low_crit"] = c->low_crit;
  o["low_warn_on"] = c->low_warn_on;
  o["low_warn"] = c->low_warn;
  o["high_warn_on"] = c->high_warn_on;
  o["high_warn"] = c->high_warn;
  o["high_crit_on"] = c->high_crit_on;
  o["high_crit"] = c->high_crit;
  o["zone_style"] = c->zone_style;
  o["needle_hub_pct"] = c->needle_hub_pct;
  o["needle_tip_pct"] = c->needle_tip_pct;
  o["needle_w"] = c->needle_w;
  o["dial_x"] = c->dial_x; o["dial_y"] = c->dial_y;
  o["peak_hold"] = c->peak_hold; o["peak_hold_s"] = c->peak_hold_s;
  o["text_xy"] = c->text_xy;
  o["cap_x"] = c->cap_x; o["cap_y"] = c->cap_y; o["val_x"] = c->val_x; o["val_y"] = c->val_y;
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
  // Read as plain ints and range-check BEFORE converting to the enum types
  // (storing an out-of-range value in an enum is undefined behavior).
  int sig = o["signal"] | 0;
  if (sig < 0 || sig >= SIG_COUNT) sig = SIG_NONE;
  c->signal = (SignalId)sig;
  int gtype = o["gauge_type"] | 0;
  if (gtype < 0 || gtype >= GAUGE_TYPE_COUNT) gtype = GAUGE_TYPE_NEEDLE;
  c->gauge_type = (GaugeType)gtype;
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
  c->value_font_size = gauge_snap_value_size(o["value_font_size"] | 28);
  // A config saved before these three settings existed has no keys for them: the dial numbers then
  // follow the readout size (as they always did) and the dial is the standard one for its type.
  c->caption_font_size = gauge_snap_caption_size(o["caption_font_size"] | 0);   // 0 (never set) stays 0: the display's default font
  c->caption_on_face = o["caption_on_face"] | false;
  copyLampText(c->units, sizeof(c->units), o["units"] | "");
  c->h_align = gauge_resolve_halign(o["h_align"] | 0);
  c->decimals = gauge_resolve_decimals(o["decimals"] | GAUGE_DECIMALS_DEFAULT);
  c->v_align = gauge_resolve_valign(o["v_align"] | 0);
  c->tick_font_size = gauge_resolve_tick_size(o["tick_font_size"] | 0, c->value_font_size);
  {
    int isHalf = (c->gauge_type == GAUGE_TYPE_HALFMOON);
    int32_t defStart, defEnd, sweep;
    gauge_dial_defaults(isHalf, &defStart, &defEnd);
    gauge_dial_resolve(isHalf, o["start_deg"] | defStart, o["end_deg"] | defEnd, &c->start_deg, &c->end_deg, &sweep);
  }
  // Warning zones and needle trim: a config without these keys has no zones and the normal needle.
  if (!o["zone_style"].isNull()) {
    c->low_crit_on = o["low_crit_on"] | false;   c->low_crit = o["low_crit"] | 0;
    c->low_warn_on = o["low_warn_on"] | false;   c->low_warn = o["low_warn"] | 0;
    c->high_warn_on = o["high_warn_on"] | false; c->high_warn = o["high_warn"] | 0;
    c->high_crit_on = o["high_crit_on"] | false; c->high_crit = o["high_crit"] | 0;
    c->zone_style = gauge_resolve_zone_style(o["zone_style"] | GAUGE_ZONE_BANDS);
  } else if (!o["yellow_enabled"].isNull() || !o["red_enabled"].isNull() || !o["zones_low"].isNull()) {
    // Saved by build 108, which had one yellow and one red zone on ONE side of the scale, drawn as coloured ticks.
    // Same look after the update: yellow becomes the warning threshold and red the critical one, on that side.
    bool low = o["zones_low"] | false;
    bool yOn = o["yellow_enabled"] | false, rOn = o["red_enabled"] | false;
    int32_t yAt = o["yellow_value"] | 0, rAt = o["red_value"] | 0;
    c->low_warn_on = low && yOn;   c->low_warn = low ? yAt : 0;
    c->low_crit_on = low && rOn;   c->low_crit = low ? rAt : 0;
    c->high_warn_on = !low && yOn; c->high_warn = low ? 0 : yAt;
    c->high_crit_on = !low && rOn; c->high_crit = low ? 0 : rAt;
    c->zone_style = GAUGE_ZONE_TICKS;
  } else {
    c->zone_style = GAUGE_ZONE_BANDS;
  }
  c->peak_hold = o["peak_hold"] | false; c->peak_hold_s = gauge_resolve_peak_s(o["peak_hold_s"] | GAUGE_PEAK_HOLD_S_DEFAULT);
  c->dial_x = gauge_resolve_text_xy(o["dial_x"] | 0); c->dial_y = gauge_resolve_text_xy(o["dial_y"] | 0);
  c->needle_w = gauge_resolve_needle_w(o["needle_w"] | GAUGE_NEEDLE_W_DEFAULT);
  c->text_xy = o["text_xy"] | false;
  c->cap_x = gauge_resolve_text_xy(o["cap_x"] | 0); c->cap_y = gauge_resolve_text_xy(o["cap_y"] | 0);
  c->val_x = gauge_resolve_text_xy(o["val_x"] | 0); c->val_y = gauge_resolve_text_xy(o["val_y"] | 0);
  gauge_resolve_needle_trim(o["needle_hub_pct"] | 0, o["needle_tip_pct"] | 0, &c->needle_hub_pct, &c->needle_tip_pct);
  c->has_warning = o["has_warning"] | false;
  c->warning_threshold = o["warning_threshold"] | 0.0f;
  c->warning_above = o["warning_above"] | false;
  c->alert_fullscreen = o["alert_fullscreen"] | false;
  c->history_sec = o["history_sec"] | 30;
  c->ref_enabled = o["ref_enabled"] | false;
  c->ref_value = o["ref_value"] | 0.0f;

  // Sanitize. The web editor lets anyone type anything, and a config that is
  // now really saved to flash would otherwise be loaded at EVERY boot: a zero
  // divisor gives inf/NaN readings, a max not above min or a zero major-tick
  // spacing would divide by zero inside LVGL - a boot loop until the flash is
  // erased. Clamp to values the display code can always handle.
  if (!std::isfinite(c->value_divisor) || c->value_divisor == 0.0f) c->value_divisor = 1.0f;
  if (c->max_val <= c->min_val) c->max_val = c->min_val + 1;
  if (c->tick_count < 2) c->tick_count = 2;
  if (c->tick_count > 200) c->tick_count = 200;
  if (c->major_nth < 1) c->major_nth = 1;
  if (c->refresh_hz < 1) c->refresh_hz = 1;
  if (c->history_sec < 2) c->history_sec = 2;
  if (c->history_sec > 600) c->history_sec = 600;
  if (!std::isfinite(c->warning_threshold)) c->warning_threshold = 0.0f;
  if (!std::isfinite(c->ref_value)) c->ref_value = 0.0f;
  c->needle_color &= 0xFFFFFF;
  c->tick_color &= 0xFFFFFF;
  c->bg_color &= 0xFFFFFF;
}

// ---------------------------------------------------------------------------
// Indicator lamps
// ---------------------------------------------------------------------------
static void serializeIndicator(JsonObject o, const IndicatorConfig *l) {
  o["signal"] = (int)l->signal;
  o["op"] = (int)l->op;
  o["threshold"] = l->threshold;
  o["hysteresis"] = l->hysteresis;
  o["on_text"] = l->on_text;
  o["off_text"] = l->off_text;
  o["on_color"] = l->on_color;
  o["off_color"] = l->off_color;
  o["on_text_color"] = l->on_text_color;
  o["off_text_color"] = l->off_text_color;
  o["blink"] = l->blink;
}

static void deserializeIndicator(JsonObject o, IndicatorConfig *l) {
  memset(l, 0, sizeof(*l));
  int sig = o["signal"] | 0;
  if (sig < 0 || sig >= SIG_COUNT) sig = SIG_NONE;
  l->signal = (SignalId)sig;
  l->op = ((int)(o["op"] | 0) == GAUGE_IND_BELOW) ? GAUGE_IND_BELOW : GAUGE_IND_ABOVE;
  float thr = o["threshold"] | 0.0f;
  l->threshold = std::isfinite(thr) ? thr : 0.0f;
  float hyst = o["hysteresis"] | 0.0f;
  l->hysteresis = (std::isfinite(hyst) && hyst > 0.0f) ? hyst : 0.0f;
  copyLampText(l->on_text, sizeof(l->on_text), o["on_text"] | "");
  copyLampText(l->off_text, sizeof(l->off_text), o["off_text"] | "");
  // A lamp saved without colours gets a readable default: red when on, dark grey when off.
  l->on_color = (o["on_color"] | 0xE53935u) & 0xFFFFFFu;
  l->off_color = (o["off_color"] | 0x2A2A2Au) & 0xFFFFFFu;
  l->on_text_color = (o["on_text_color"] | 0xFFFFFFu) & 0xFFFFFFu;
  l->off_text_color = (o["off_text_color"] | 0x808080u) & 0xFFFFFFu;
  l->blink = o["blink"] | false;
}

// ---------------------------------------------------------------------------
// JSON <-> pages (shared by the flash files and the web endpoints)
// ---------------------------------------------------------------------------
void gauge_config_pages_to_json(const PageConfig *pages, JsonDocument &doc) {
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < MAX_PAGES; i++) {
    if (!pages[i].enabled) continue; // disabled pages are not written - the editors already compact the list
    JsonObject pageObj = arr.add<JsonObject>();
    pageObj["enabled"] = true;
    if (pages[i].kind == PAGE_INDICATORS) {        // a page of on/off lamps: no layout, no cells
      pageObj["kind"] = (int)PAGE_INDICATORS;
      pageObj["cols"] = pages[i].ind_cols;
      JsonArray lamps = pageObj["indicators"].to<JsonArray>();
      int count = pages[i].ind_count;
      if (count < 0) count = 0;
      if (count > MAX_INDICATORS_PER_PAGE) count = MAX_INDICATORS_PER_PAGE;
      for (int j = 0; j < count; j++) serializeIndicator(lamps.add<JsonObject>(), &pages[i].indicators[j]);
      continue;
    }
    pageObj["layout"] = (int)pages[i].layout;
    JsonArray cellsArr = pageObj["cells"].to<JsonArray>();
    int n = gauge_config_cells_for_layout(pages[i].layout);
    for (int j = 0; j < n; j++) serializeCell(cellsArr.add<JsonObject>(), &pages[i].cells[j]);
  }
}

void gauge_config_pages_from_json(JsonArray arr, PageConfig *pages) {
  memset(pages, 0, sizeof(PageConfig) * MAX_PAGES);
  int i = 0;
  for (JsonObject pageObj : arr) {
    if (i >= MAX_PAGES) break;
    pages[i].enabled = pageObj["enabled"] | false;
    int kind = pageObj["kind"] | 0;                 // a page saved before indicator pages existed has no "kind": a gauge page
    if (kind < 0 || kind >= PAGE_KIND_COUNT) kind = PAGE_GAUGES;
    pages[i].kind = (PageKind)kind;
    if (kind == PAGE_INDICATORS) {
      int cols = pageObj["cols"] | 2;
      if (cols < 1) cols = 1;
      if (cols > GAUGE_IND_MAX_COLS) cols = GAUGE_IND_MAX_COLS;
      pages[i].ind_cols = cols;
      int n = 0;
      for (JsonObject lampObj : pageObj["indicators"].as<JsonArray>()) {
        if (n >= MAX_INDICATORS_PER_PAGE) break;
        deserializeIndicator(lampObj, &pages[i].indicators[n]);
        n++;
      }
      pages[i].ind_count = n;
      i++;
      continue;
    }
    int layout = pageObj["layout"] | 0;
    if (layout < 0 || layout >= LAYOUT_COUNT) layout = LAYOUT_1CELL;
    pages[i].layout = (LayoutType)layout;
    JsonArray cellsArr = pageObj["cells"];
    int j = 0;
    for (JsonObject cellObj : cellsArr) {
      if (j >= MAX_CELLS_PER_PAGE) break;
      deserializeCell(cellObj, &pages[i].cells[j]);
      j++;
    }
    i++;
  }
}

// ---------------------------------------------------------------------------
// Persistence
//
// Each configuration is saved as a JSON FILE on the board's FAT flash
// partition ("ffat", ~9.9MB, already part of the partition scheme), NOT in
// NVS. NVS refuses any stored string over 4000 bytes, and a full config is
// far larger: an earlier version saved all 8 pages x 4 cells (about 12KB) as
// one NVS string, every save failed silently (Preferences::putString just
// logs and returns 0, and the return value was ignored), and every edit was
// lost on reboot. Files have no such limit, and only the pages/cells in use
// are written. Every save now reports whether it really worked.
// ---------------------------------------------------------------------------
#ifndef GAUGE_CFG_DIR
#define GAUGE_CFG_DIR "/ffat" // the host tests point this at a temp folder
#endif
#define CFG_DISPLAY_FILE GAUGE_CFG_DIR "/cfg_display.json"
#define CFG_WEB_FILE     GAUGE_CFG_DIR "/cfg_web.json"
#define CFG_MAX_BYTES    65536

static bool s_fsTried = false;
static bool s_fsOk = false;
static char s_lastError[96] = "";
static size_t s_savedBytes[2] = {0, 0};

static void setError(const char *msg) {
  strlcpy(s_lastError, msg, sizeof(s_lastError));
  Serial.printf("Config storage: %s\n", msg);
}

static bool fsEnsure(void) {
  if (s_fsTried) return s_fsOk;
  s_fsTried = true;
  // true = format the partition if it has never been formatted (first boot only; takes a few seconds)
  s_fsOk = FFat.begin(true, GAUGE_CFG_DIR);
  if (!s_fsOk) setError("flash filesystem (ffat) failed to mount");
  return s_fsOk;
}

// Large temporary buffers go in PSRAM - internal RAM is tight.
static char *bigAlloc(size_t n) {
  char *p = (char *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = (char *)malloc(n);
  return p;
}

// Writes data to "<path>.tmp", then swaps it into place. FAT cannot rename over
// an existing file, so the old file is removed first; if power is lost between
// those two steps the loader recovers from the .tmp copy.
static bool writeFileAtomic(const char *path, const char *data, size_t len) {
  char tmp[80];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  FILE *f = fopen(tmp, "wb");
  if (!f) { setError("could not create the settings file"); return false; }
  size_t written = fwrite(data, 1, len, f);
  bool ok = (written == len) && (fflush(f) == 0);
  if (fclose(f) != 0) ok = false;
  if (!ok) { remove(tmp); setError("writing the settings file failed (flash full?)"); return false; }
  remove(path);
  if (rename(tmp, path) != 0) { setError("could not move the settings file into place"); return false; }
  return true;
}

// Reads a whole file into a PSRAM buffer (caller frees). NULL if missing, empty or implausibly big.
static char *readWholeFile(const char *path, size_t *outLen) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n <= 0 || n > CFG_MAX_BYTES) { fclose(f); return NULL; }
  char *buf = bigAlloc((size_t)n + 1);
  if (!buf) { fclose(f); return NULL; }
  size_t got = fread(buf, 1, (size_t)n, f);
  fclose(f);
  if (got != (size_t)n) { free(buf); return NULL; }
  buf[n] = '\0';
  *outLen = (size_t)n;
  return buf;
}

// Loads the first readable, valid copy: the file itself, else the ".tmp" left
// by an interrupted save. Returns false (and leaves `pages` untouched) if
// neither exists or parses - the caller then uses defaults.
static bool loadConfig(const char *path, PageConfig *pages) {
  if (!fsEnsure()) return false;
  char tmp[80];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  const char *candidates[2] = { path, tmp };
  for (int c = 0; c < 2; c++) {
    size_t len = 0;
    char *buf = readWholeFile(candidates[c], &len);
    if (!buf) continue;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, buf, len);
    free(buf);
    if (err || !doc.is<JsonArray>()) {
      Serial.printf("Config storage: %s is not valid (%s), skipping it\n", candidates[c], err ? err.c_str() : "not a list");
      continue;
    }
    gauge_config_pages_from_json(doc.as<JsonArray>(), pages);
    return true;
  }
  return false;
}

static bool saveConfig(const char *path, const PageConfig *pages, int slot) {
  if (!fsEnsure()) return false;
  JsonDocument doc;
  gauge_config_pages_to_json(pages, doc);
  size_t need = measureJson(doc);
  char *buf = bigAlloc(need + 1);
  if (!buf) { setError("out of memory while saving"); return false; }
  serializeJson(doc, buf, need + 1);
  bool ok = writeFileAtomic(path, buf, need);
  free(buf);
  if (ok) { s_savedBytes[slot] = need; s_lastError[0] = '\0'; }
  return ok;
}

void gauge_config_init(void) {
  if (!loadConfig(CFG_DISPLAY_FILE, s_pages)) applyDefaults();
}

PageConfig *gauge_config_get_pages(void) {
  return s_pages;
}

bool gauge_config_save(void) {
  return saveConfig(CFG_DISPLAY_FILE, s_pages, 0);
}

// The old separate web-dashboard config (removed in build 160: the front page now lists the screen's own pages).
// Delete its leftover file so it stops taking up flash.
void gauge_config_web_cleanup(void) {
  if (fsEnsure() && remove(CFG_WEB_FILE) == 0) Serial.println("Removed the old web dashboard config file");
}

bool gauge_config_storage_ok(void) { return s_fsOk; }
const char *gauge_config_last_error(void) { return s_lastError; }
size_t gauge_config_saved_bytes(int which) { return (which == 0 || which == 1) ? s_savedBytes[which] : 0; }

const char *gauge_config_signal_name(SignalId sig) {
  if (gauge_signal_is_custom(sig)) {   // named on the hub's /custom page; "Custom 3" until it is
    static char fallback[CUSTOM_SLOTS][12];
    int i = sig - SIG_C1;
    const CustomSlotInfo *in = custom_slot_info(i);
    if (in->name[0]) return in->name;
    snprintf(fallback[i], sizeof fallback[i], "Custom %d", i + 1);
    return fallback[i];
  }
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
    case SIG_EGT: return "EGT";
    case SIG_IAT: return "Intake Air Temp";
    case SIG_AMBT: return "Ambient Temp";
    case SIG_HUM: return "Humidity";
    case SIG_FUEL: return "Fuel Level";
    case SIG_VSS: return "Speed";
    case SIG_GPS_SPD: return "GPS Speed";
    case SIG_GPS_SATS: return "GPS Satellites";
    case SIG_REVLIM: return "Rev Limiter Active";
    case SIG_MAINRELAY: return "Main Relay";
    case SIG_FUELPUMP: return "Fuel Pump";
    case SIG_CEL: return "Check Engine Light";
    case SIG_EGOHEAT: return "O2 Heater";
    case SIG_LAMBDAPROT: return "Lambda Protection";
    case SIG_FAN: return "Fan 1";
    case SIG_FAN2: return "Fan 2";
    case SIG_BRAKE: return "Brake Pedal";
    default: return "None";
  }
}

float gauge_config_signal_value(SignalId sig) {
  if (gauge_signal_is_custom(sig)) return g_custom_val[sig - SIG_C1];
  switch (sig) {
    case SIG_RPM: return g_rpm;
    case SIG_CLT: return g_clt_f;
    case SIG_MAP: return g_boost_psi;
    case SIG_AFR: return g_afr;
    case SIG_OILP: return g_oilp_psi;
    case SIG_OILT: return g_oilt_f;
    case SIG_FUELP: return g_fuelp_psi;
    case SIG_FLEX: return g_flex_pct;
    case SIG_TPS: return g_tps;
    case SIG_BATTV: return g_battv;
    case SIG_EGT: return g_egt_f;
    case SIG_IAT: return g_iat_f;
    case SIG_AMBT: return g_ambt_f;
    case SIG_HUM: return g_hum_pct;
    case SIG_FUEL: return g_fuel_pct;
    case SIG_VSS: return g_vss_mph;
    case SIG_GPS_SPD: return g_gps_mph;
    case SIG_GPS_SATS: return g_gps_sats;
    default:
      if (gauge_signal_is_flag(sig)) return ((g_flag_bits >> (sig - SIG_FIRST_FLAG)) & 1u) ? 1.0f : 0.0f;
      return 0;
  }
}

bool gauge_config_signal_has_data(SignalId sig) {
  bool seen;
  if (gauge_signal_is_custom(sig)) seen = g_custom_has[sig - SIG_C1];
  else switch (sig) {
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
    case SIG_EGT: seen = g_egt_has_data; break;
    case SIG_IAT: seen = g_iat_has_data; break;
    case SIG_AMBT: seen = g_ambt_has_data; break;
    case SIG_HUM: seen = g_hum_has_data; break;
    case SIG_FUEL: seen = g_fuel_has_data; break;
    case SIG_VSS: seen = g_vss_has_data; break;
    case SIG_GPS_SPD: seen = g_gps_has_data; break;
    case SIG_GPS_SATS: seen = g_sats_has_data; break;
    default:
      seen = gauge_signal_is_flag(sig) && ((g_flag_seen >> (sig - SIG_FIRST_FLAG)) & 1u);
      break;
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
    // Default: log every analog signal - user can narrow down via the Log
    // Channels web page. The on/off status flags default to NOT logged, so the
    // CSV columns people already use do not change; tick them on that page to log them.
    char key[8];
    snprintf(key, sizeof(key), "s%d", i);
    s_logEnabled[i] = logPrefs.getBool(key, !gauge_signal_is_flag(i));
  }
}

bool gauge_config_is_log_enabled(SignalId sig) {
  if (sig < 0 || sig >= SIG_COUNT) return false;
  if (gauge_signal_is_custom(sig) && !custom_slot_info(sig - SIG_C1)->name[0]) return false;   // slot not set up: no empty column
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
