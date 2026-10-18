/*
  gauge_app.c
  Stage B of the page/cell redesign - rendering rewrite. Pages are now
  built from PageConfig (a layout template + array of CellConfig),
  supporting 1/2/3/4 cells per page in fixed arrangements, with each
  cell independently rendering as a needle meter, half-moon meter, or
  plain number readout.

  Each cell gets its own lv_obj_t container, sized/positioned to its
  slice of the 466x466 screen per the page's layout - all gauge content
  (meter, needle, labels) is built as children of that container using
  the same relative positioning (lv_obj_center, lv_obj_align) used
  throughout this project, which naturally scopes to the container's
  bounds rather than the whole screen.

  The old hardcoded "numbers grid" page is removed - the new model
  makes it redundant, since a LAYOUT_2X2 page of GAUGE_TYPE_NUMBER
  cells achieves the same thing, now with full user control over which
  signals/captions appear there.

  Manual navigation only (swipe or BOOT button) - navigates between
  PAGES, not between cells within a page.
*/

#include "gauge_app.h"
#include "gauge_config.h"
#include "sd_logging.h"
#include "simulator.h"
#include "gauge_style.h"
#include "gauge_fonts.h"
#include "gauge_indicators.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

volatile float g_afr = 0;
volatile float g_clt_f = 0;
volatile float g_oilp_kpa = 0;
volatile float g_oilt_f = 0;
volatile float g_fuelp_kpa = 0;
volatile float g_flex_pct = 0;
volatile float g_boost_kpa = 0;
volatile float g_rpm = 0;
volatile float g_tps = 0;
volatile float g_battv = 0;
volatile float g_egt_f = 0;
volatile float g_iat_f = 0;
volatile float g_ambt_f = 0;
volatile float g_hum_pct = 0;

volatile bool g_afr_has_data = false;
volatile bool g_clt_has_data = false;
volatile bool g_oilp_has_data = false;
volatile bool g_oilt_has_data = false;
volatile bool g_fuelp_has_data = false;
volatile bool g_flex_has_data = false;
volatile bool g_boost_has_data = false;
volatile bool g_rpm_has_data = false;
volatile bool g_tps_has_data = false;
volatile bool g_battv_has_data = false;
volatile bool g_egt_has_data = false;
volatile bool g_iat_has_data = false;
volatile bool g_ambt_has_data = false;
volatile bool g_hum_has_data = false;
volatile uint16_t g_flag_bits = 0;
volatile uint16_t g_flag_seen = 0;

volatile int g_requested_page = -1;
volatile int g_page_nav_delta = 0;

#define ENABLE_TOUCH_SWIPE_NAV 0 // disabled per earlier request - BOOT button only

typedef struct {
  int16_t x, y, w, h;
} CellRect;

// Returns the pixel rect for a given cell index within a given layout,
// on the 466x466 panel.
// The visible circle every gauge page is laid out in (gauge_app_set_diameter): 466 = the whole panel.
static int16_t s_diam = GAUGE_DIAMETER_DEFAULT;
static float s_glassR = GAUGE_DIAMETER_DEFAULT / 2.0f;   // its radius, centred on the panel (233, 233)

void gauge_app_set_diameter(int d) {
  if (d < GAUGE_DIAMETER_MIN || d > GAUGE_DIAMETER_MAX) d = GAUGE_DIAMETER_DEFAULT;
  s_diam = (int16_t)(d & ~1);   // even, so the halves meet on a whole pixel
  s_glassR = s_diam / 2.0f;
}

static CellRect get_cell_rect(LayoutType layout, int cellIndex) {
  const int16_t S = s_diam;
  const int16_t H = S / 2;
  const int16_t o = (466 - S) / 2;   // the square around the circle, centred on the panel

  switch (layout) {
    case LAYOUT_1CELL:
      return (CellRect){o, o, S, S};
    case LAYOUT_2UP:
      if (cellIndex == 0) return (CellRect){o, o, S, H};
      return (CellRect){o, o + H, S, H};
    case LAYOUT_3UP:
      if (cellIndex == 0) return (CellRect){o, o, S, H};
      if (cellIndex == 1) return (CellRect){o, o + H, H, H};
      return (CellRect){o + H, o + H, H, H};
    case LAYOUT_2X2:
      if (cellIndex == 0) return (CellRect){o, o, H, H};
      if (cellIndex == 1) return (CellRect){o + H, o, H, H};
      if (cellIndex == 2) return (CellRect){o, o + H, H, H};
      return (CellRect){o + H, o + H, H, H};
    default:
      return (CellRect){o, o, S, S};
  }
}

typedef struct {
  lv_obj_t *container;
  lv_obj_t *meter;         // NULL for GAUGE_TYPE_NUMBER
  lv_meter_indicator_t *needle; // NULL for GAUGE_TYPE_NUMBER
  lv_meter_indicator_t *peakNeedle; // shadow needle for peak hold; NULL when off
  int32_t peak_val; uint32_t peak_ms; uint32_t peak_hold_ms;
  lv_point_t needleStart;       // where the needle is drawn FROM when it is trimmed (needle_trim_cb points LVGL at this)
  int32_t needleHubPct;         // needle trim, % of the dial radius: gap at the centre ...
  int32_t needleTipPct;         // ... and how far short of its normal tip it stops
  lv_obj_t *numberLabel;   // used by GAUGE_TYPE_NUMBER; also doubles as the "show_value" readout for other types
  lv_obj_t *chart;         // used by GAUGE_TYPE_HISTOGRAM only
  lv_obj_t *noDataLabel;   // "NO DATA" over an empty trend graph (histogram only)
  lv_chart_series_t *chartSeries;
  bool enabled;
  GaugeType gauge_type;
  SignalId signal;
  int32_t min_val, max_val;
  float value_divisor;
  uint32_t bg_color;
  bool has_warning;
  float warning_threshold;
  int32_t decimals;        // readout digits after the point (0-2)
  bool warning_above;
  bool warning_active;
  bool alert_fullscreen;
  char caption[GAUGE_CAPTION_MAXLEN];
  int32_t hist_min;        // trend graph: the value that sits at the bottom of the chart
  int32_t hist_scale;      // trend graph: chart units per real unit (so AFR 14.2 vs 14.7 stays distinguishable)
  int32_t hist_top;        // trend graph: highest chart value (the top of the range)
  int32_t sweep_pos, sweep_step;
  int8_t sweep_dir;
  uint32_t refresh_interval_ms, last_update_ms;
} CellRuntime;

// One indicator lamp on an indicators page. Settings are copied here at build time, so editing the config
// while the board runs cannot change a lamp half way (the display picks changes up after a reboot).
typedef struct {
  lv_obj_t *lamp;                  // the rounded rectangle
  lv_obj_t *label;                 // its text
  SignalId signal;
  int32_t op;
  float threshold, hysteresis;
  bool blink;
  bool on;                         // the lamp's state, including its hysteresis memory
  int8_t shown;                    // the look drawn now: -1 nothing yet, 0 the "off" look, 1 the "on" look
  uint32_t onColor, offColor, onTextColor, offTextColor;
  char onText[IND_TEXT_MAXLEN], offText[IND_TEXT_MAXLEN];
} IndLamp;

typedef struct {
  lv_obj_t *page;
  int numCells;                    // 0 on an indicators page
  CellRuntime cells[MAX_CELLS_PER_PAGE];
  IndLamp *lamps;                  // indicators pages only: numLamps lamps (allocated when the page is built)
  int numLamps;
} GaugePageRuntime;

static GaugePageRuntime pages[MAX_PAGES];
static int s_num_active_pages = 0;
static int s_current_page = 0;
static uint32_t s_flash_counter = 0;
static lv_obj_t *s_simBanner = NULL;    // "SIMULATED" pill on lv_layer_sys(): shown on every page while the bench simulator feeds fake data
static lv_obj_t *s_logIndicator = NULL; // small dot on lv_layer_sys() - persists across every page without needing to be added to each one individually, same mechanism LVGL's own built-in perf monitor uses

// ---- Full-screen alert (per-cell opt-in via CellConfig.alert_fullscreen) ----
// A cell's flashing warning only shows on its own page. This overlay sits
// on lv_layer_sys(), so a critical condition is visible whichever page is
// up - the same idea as the Teensy gauge's full-screen warning takeover.
#define ALERT_MIN_RPM      400.0f // only alert with the engine actually running - oil pressure reads ~0 at key-on and would otherwise take over the screen on every start
#define ALERT_MIN_SHOW_MS  2000   // once shown, stay up at least this long so a value hovering on its threshold can't flicker the whole screen
#define ALERT_SNOOZE_MS    30000  // BOOT press (or a tap) hides the alert this long, so a faulty sensor can never lock the screen
#if LV_FONT_MONTSERRAT_48
#define ALERT_VALUE_FONT (&lv_font_montserrat_48)
#else
#define ALERT_VALUE_FONT (&lv_font_montserrat_36)
#endif

static lv_obj_t *s_alertOverlay = NULL;
static lv_obj_t *s_alertTitle = NULL;
static lv_obj_t *s_alertValue = NULL;
static lv_obj_t *s_alertMore = NULL;
static bool s_alertShown = false;
static bool s_alertSnoozed = false;
static uint32_t s_alertHoldUntil = 0;
static uint32_t s_alertSnoozeUntil = 0;

static bool time_reached(uint32_t now, uint32_t t) {
  return (int32_t)(now - t) >= 0; // safe across the 32-bit tick wrap
}

// lv_label_set_text() always invalidates the label even when the text is
// identical - skip it so a held alert doesn't redraw the whole round
// screen ten times a second for nothing.
static void set_label_if_changed(lv_obj_t *label, const char *text) {
  if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static void alert_hide(void) {
  if (s_alertOverlay) lv_obj_add_flag(s_alertOverlay, LV_OBJ_FLAG_HIDDEN);
  s_alertShown = false;
}

static void alert_dismiss(uint32_t now) {
  s_alertSnoozed = true;
  s_alertSnoozeUntil = now + ALERT_SNOOZE_MS;
  alert_hide();
}

static void alert_click_cb(lv_event_t *e) {
  (void)e;
  alert_dismiss(lv_tick_get());
}

static const lv_font_t *get_font_for_size(int32_t size) {   // 14/28/36 only: the numbers around a dial
  if (size <= 14) return &lv_font_montserrat_14;
  if (size <= 28) return &lv_font_montserrat_28;
  return &lv_font_montserrat_36;
}

// ---- readout text: the biggest size that fits ----
typedef struct { int32_t size; const lv_font_t *font; } SizedFont;
static const SizedFont VALUE_FONTS[GAUGE_VALUE_SIZE_COUNT] = {   // same order as GAUGE_VALUE_SIZES in gauge_style.h
  { 14, &lv_font_montserrat_14 }, { 28, &lv_font_montserrat_28 }, { 36, &lv_font_montserrat_36 },
  { 48, &font_digits_48 }, { 64, &font_digits_64 }, { 80, &font_digits_80 }, { 96, &font_digits_96 }, { 120, &font_digits_120 },
};

#define PANEL_CENTRE 233.0f        // the AMOLED is 466x466 and round
#define PANEL_FIT_RADIUS (s_glassR - 5.0f)   // a readout's corners must stay inside this circle, or the bezel cuts the digits off

static bool box_in_panel(float cx, float cy, float w, float h) {
  for (int sx = -1; sx <= 1; sx += 2) {
    for (int sy = -1; sy <= 1; sy += 2) {
      float x = cx + sx * w / 2.0f - PANEL_CENTRE, y = cy + sy * h / 2.0f - PANEL_CENTRE;
      if (x * x + y * y > PANEL_FIT_RADIUS * PANEL_FIT_RADIUS) return false;
    }
  }
  return true;
}

// The widest text this cell's readout can need: its lowest or highest value, formatted as it will be.
static void readout_sample(const CellConfig *cfg, char *out, size_t n) {
  char lo[24], hi[24];
  int d = (int)gauge_resolve_decimals(cfg->decimals);
  snprintf(lo, sizeof(lo), "%.*f", d, (double)cfg->min_val);
  snprintf(hi, sizeof(hi), "%.*f", d, (double)cfg->max_val);
  snprintf(out, n, "%s", strlen(lo) > strlen(hi) ? lo : hi);
}

// Where a text box sits relative to the y given to pick_font_fit().
enum { ANCHOR_CENTER = 0, ANCHOR_BOTTOM = 1, ANCHOR_TOP = 2 };

// Picks the largest size in `tbl` that is <= `requested` and fits: no wider than maxW, no taller than maxH,
// and fully on the round glass. The box is centred on (cx, y), or sits with its bottom (or top) edge at y.
// Never goes below the smallest size in the table.
static const lv_font_t *pick_font_fit(const SizedFont *tbl, int count, int32_t requested, const char *sample, float cx, float y,
                                      int anchor, float maxW, float maxH) {
  int top = count - 1;
  while (top > 0 && tbl[top].size > requested) top--;
  for (int i = top; i > 0; i--) {
    lv_point_t sz;
    lv_txt_get_size(&sz, sample, tbl[i].font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    float cy = anchor == ANCHOR_BOTTOM ? y - sz.y / 2.0f : (anchor == ANCHOR_TOP ? y + sz.y / 2.0f : y);
    if (sz.x <= maxW && sz.y <= maxH && box_in_panel(cx, cy, (float)sz.x, (float)sz.y)) return tbl[i].font;
  }
  return tbl[0].font;
}

static const lv_font_t *pick_value_font(int32_t requested, const char *sample, float cx, float y, int anchor, float maxW, float maxH) {
  return pick_font_fit(VALUE_FONTS, GAUGE_VALUE_SIZE_COUNT, requested, sample, cx, y, anchor, maxW, maxH);
}

// Caption, title and units text: 14 / 20 / 28 / 36 / 48 (letters and digits). A size of 0 - never set - means the
// display's default font, exactly as captions always looked, and is returned as NULL.
static const SizedFont CAPTION_FONTS[GAUGE_CAPTION_SIZE_COUNT] = {
  { 14, &lv_font_montserrat_14 }, { 20, &font_text_20 }, { 28, &lv_font_montserrat_28 }, { 36, &lv_font_montserrat_36 }, { 48, &font_text_48 },
};

static const lv_font_t *pick_caption_font(int32_t requested, const char *text, float cx, float y, int anchor, float maxW, float maxH) {
  if (requested <= 0) return NULL;
  return pick_font_fit(CAPTION_FONTS, GAUGE_CAPTION_SIZE_COUNT, requested, text, cx, y, anchor, maxW, maxH);
}

// The caption font at exactly `size`, with no fitting - used to plan how much room to leave. NULL for 0.
static const lv_font_t *caption_font_exact(int32_t size) {
  for (int i = 0; i < GAUGE_CAPTION_SIZE_COUNT; i++) if (CAPTION_FONTS[i].size == size) return CAPTION_FONTS[i].font;
  return NULL;
}

static int font_line_h(const lv_font_t *f) { return (int)lv_font_get_line_height(f ? f : LV_FONT_DEFAULT); }

// A dial that only occupies the top (or bottom) half of its circle is moved down (or up) so it sits in
// the middle of its cell instead of leaving the other half of the cell empty. The standard 270 degree
// dial and the standard half moon come out exactly as they always did.
static int16_t dial_vertical_shift(int32_t start, int32_t sweep, int16_t meterSize) {
  float hi = -2.0f, lo = 2.0f;                       // highest and lowest point of the arc: cos() is +1 straight up
  for (int32_t a = 0; a <= sweep; a++) {
    float c = cosf((float)(start + a) * 0.01745329f);
    if (c > hi) hi = c;
    if (c < lo) lo = c;
  }
  if (lo >= -0.05f) return (int16_t)(meterSize / 6);     // never dips below the centre line
  if (hi <= 0.05f) return (int16_t)(-(meterSize / 6));   // never rises above it
  return 0;
}

// How far a dial's picture reaches above and below its centre, as multiples of its radius (cos is +1 straight up):
// the arc itself AND the hub, which the needle starts from.
static void dial_extent(int32_t start, int32_t sweep, float *hi, float *lo) {
  float h = 0.0f, l = 0.0f;
  for (int32_t a = 0; a <= sweep; a++) {
    float c = cosf((float)(start + a) * 0.01745329f);
    if (c > h) h = c;
    if (c < l) l = c;
  }
  *hi = h; *lo = l;
}

// Where the dial stands in its cell. Centre is the placement above (so every existing dial is unchanged). Top puts the
// highest point of the dial at the top of the free area, Bottom puts its lowest point at the bottom of it (just above the
// caption / readout), so a half moon - which only fills half its circle - really moves, while a nearly full dial has little
// room to. A dial taller than the free area stays centred.
static int16_t dial_aligned_shift(int valign, int32_t start, int32_t sweep, int16_t meterSize, float radius,
                                  float cellCentreY, float areaTop, float areaBottom) {
  if (valign == GAUGE_VALIGN_CENTER) return dial_vertical_shift(start, sweep, meterSize);
  float hi, lo;
  dial_extent(start, sweep, &hi, &lo);
  if ((hi - lo) * radius > areaBottom - areaTop) return dial_vertical_shift(start, sweep, meterSize);
  float shift = (valign == GAUGE_VALIGN_TOP) ? areaTop + hi * radius - cellCentreY : areaBottom + lo * radius - cellCentreY;
  return (int16_t)lroundf(shift);
}

static void gesture_event_cb(lv_event_t *e);

static void switch_to_page(int index) {
  s_current_page = index;
  lv_disp_load_scr(pages[index].page);
}

// ---- Trend graph (GAUGE_TYPE_HISTOGRAM) ----
#define HIST_POINTS   100    // samples across the history window
#define HIST_MIN_SEC  2
#define HIST_MAX_SEC  600
#define HIST_RANGE    10000  // target size of the chart's value range - comfortably inside lv_coord_t (int16) and fine enough to resolve small moves

// The panel is a circle, so a square chart's corners (and the ends of its
// trace) fall off the glass. Finds the largest rectangle inside the given
// cell (absolute screen coords) whose four corners all stay inside the
// circle, leaving a band at the bottom for the caption. Prefers wide
// shapes, since the horizontal axis is time. Runs once at build time.
static void fit_chart_rect(CellRect cell, int capBandIn, int16_t *ox, int16_t *oy, int16_t *ow, int16_t *oh) {
  const float cx = 233.0f, cy = 233.0f;
  const float R = s_glassR - 8.0f;
  const int pad = 6, step = 4, minW = 60, minH = 40;
  const int capBand = capBandIn;      // room for the caption under the chart: its line height + 18 (34 for the default 16px line)

  int topLimit = cell.y + pad;
  int botLimit = cell.y + cell.h - capBand;
  float bestScore = 0, bx = 0, bw = 0;
  int by0 = 0, by1 = 0;

  for (int y0 = topLimit; y0 + minH <= botLimit; y0 += step) {
    for (int y1 = y0 + minH; y1 <= botLimit; y1 += step) {
      float dy = fmaxf(fabsf((float)y0 - cy), fabsf((float)y1 - cy));
      if (dy >= R) continue;
      float half = sqrtf(R * R - dy * dy);
      float xl = fmaxf((float)(cell.x + pad), cx - half);
      float xr = fminf((float)(cell.x + cell.w - pad), cx + half);
      float w = xr - xl, h = (float)(y1 - y0);
      if (w < minW) continue;
      float aspect = w / h;
      float score = w * h * (aspect >= 1.4f ? 1.0f : aspect / 1.4f);
      if (score > bestScore) { bestScore = score; bx = xl; bw = w; by0 = y0; by1 = y1; }
    }
  }

  if (bestScore <= 0) { // degenerate cell - fall back to a plain inset rectangle
    *ox = cell.x + pad; *oy = cell.y + pad; *ow = cell.w - 2 * pad; *oh = cell.h - capBand - pad;
    return;
  }
  *ox = (int16_t)bx; *oy = (int16_t)by0; *ow = (int16_t)bw; *oh = (int16_t)(by1 - by0);
}

// A trend graph's caption sits under its graph, and the graph leaves room for the caption, so a bigger caption makes a shorter
// graph. Each size is therefore tried together with the graph it would leave; the biggest whose caption lies fully on the
// round glass (and inside the cell) wins. A size of 0 (never set) is the default font, returned as NULL.
static const lv_font_t *pick_hist_caption_font(CellRect rect, int32_t requested, const char *text) {
  if (requested <= 0) return NULL;
  int top = GAUGE_CAPTION_SIZE_COUNT - 1;
  while (top > 0 && CAPTION_FONTS[top].size > requested) top--;
  for (int i = top; i > 0; i--) {
    lv_point_t sz;
    lv_txt_get_size(&sz, text, CAPTION_FONTS[i].font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int16_t fx, fy, fw, fh;
    fit_chart_rect(rect, font_line_h(CAPTION_FONTS[i].font) + 18, &fx, &fy, &fw, &fh);
    float cx = fx + fw / 2.0f, cy = (float)(fy + fh) + 4.0f + sz.y / 2.0f;      // the caption is centred under the graph, 4px below it
    bool inCell = cx - sz.x / 2.0f >= rect.x && cx + sz.x / 2.0f <= rect.x + rect.w;   // centred under the graph, which is not always centred in its cell
    if (inCell && sz.x <= rect.w * 0.94f && sz.y <= rect.h * 0.25f && box_in_panel(cx, cy, (float)sz.x, (float)sz.y)) return CAPTION_FONTS[i].font;
  }
  return CAPTION_FONTS[0].font;
}

// Draws the needle shorter at either end. LVGL draws the needle from the dial's centre to a tip just inside
// the ticks and, just before drawing, lets the app move those two end points (the end points are the draw
// routine's own local variables; the event hands out const pointers to them). Only cells with a trim set
// register this, so a normal needle is drawn by LVGL untouched.
static void needle_trim_cb(lv_event_t *e) {
  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
  if (dsc->part != LV_PART_INDICATOR || dsc->type != LV_METER_DRAW_PART_NEEDLE_LINE) return;
  CellRuntime *rt = (CellRuntime *)lv_event_get_user_data(e);
  lv_point_t *tip = (lv_point_t *)dsc->p2;
  const lv_point_t *centre = dsc->p1;
  float dx = (float)(tip->x - centre->x), dy = (float)(tip->y - centre->y);
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 4.0f) return;

  lv_area_t content;
  lv_obj_get_content_coords(lv_event_get_target(e), &content);
  float radius = lv_area_get_width(&content) / 2.0f;
  float hub = rt->needleHubPct * 0.01f * radius;
  float back = rt->needleTipPct * 0.01f * radius;
  if (hub + back > len - 4.0f) {            // never trim the needle away completely
    float k = (len - 4.0f) / (hub + back);
    hub *= k; back *= k;
  }
  float ux = dx / len, uy = dy / len;
  rt->needleStart.x = (lv_coord_t)lroundf((float)centre->x + ux * hub);
  rt->needleStart.y = (lv_coord_t)lroundf((float)centre->y + uy * hub);
  tip->x = (lv_coord_t)lroundf((float)tip->x - ux * back);
  tip->y = (lv_coord_t)lroundf((float)tip->y - uy * back);
  dsc->p1 = &rt->needleStart;
}

// Builds one cell's content (meter+needle, or plain number) inside its
// own container, sized/positioned to the given rect. Content is built
// using relative positioning (center/align) within the container, so
// the same logic works regardless of the cell's actual size.
// Left / right caption: every label marked LV_OBJ_FLAG_USER_1 (only the caption; the readout and units always stay centred) gets a fixed width spanning the room its row really has (the
// cell, less the round glass at that height) and is aligned inside it, so a changing number stays put. Centre (the
// default) leaves the layout exactly as it was.
static void apply_text_halign(lv_obj_t *container, CellRect rect, int32_t halign) {
  if (halign == GAUGE_HALIGN_CENTER) return;
  lv_obj_update_layout(container);
  lv_area_t ca; lv_obj_get_coords(container, &ca);
  lv_coord_t padL = lv_obj_get_style_pad_left(container, LV_PART_MAIN) + lv_obj_get_style_border_width(container, LV_PART_MAIN);
  lv_coord_t padT = lv_obj_get_style_pad_top(container, LV_PART_MAIN) + lv_obj_get_style_border_width(container, LV_PART_MAIN);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(container); i++) {
    lv_obj_t *l = lv_obj_get_child(container, i);
    if (!lv_obj_has_flag(l, LV_OBJ_FLAG_USER_1)) continue;
    lv_area_t a; lv_obj_get_coords(l, &a);
    float dy = fmaxf(fabsf(a.y1 - 233.0f), fabsf(a.y2 + 1 - 233.0f));
    float gr = s_glassR - 6.0f, half = dy < gr ? sqrtf(gr * gr - dy * dy) : 0.0f;
    float xl = fmaxf(rect.x + 8.0f, 233.0f - half), xr = fminf(rect.x + rect.w - 8.0f, 233.0f + half);
    float w = (float)(a.x2 - a.x1 + 1);
    if (xr - xl < w) continue;   // no spare room: leave it centred
    lv_obj_set_align(l, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(l, (lv_coord_t)lroundf(xl) - ca.x1 - padL, a.y1 - ca.y1 - padT);
    lv_obj_set_width(l, (lv_coord_t)lroundf(xr - xl));
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(l, halign == GAUGE_HALIGN_LEFT ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, 0);
  }
}

// Text by coordinates: the caption (labels marked USER_1) and the readout (USER_2) are centred at the cell's settings,
// in px from the cell's centre; the units (USER_3) sit centred under the readout. Nothing is clamped: the editor's
// preview shows where it lands.
static void apply_text_xy(lv_obj_t *container, const CellConfig *cfg) {
  lv_obj_update_layout(container);
  lv_obj_t *val = NULL;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(container); i++) {
    lv_obj_t *l = lv_obj_get_child(container, i);
    if (lv_obj_has_flag(l, LV_OBJ_FLAG_USER_1)) {
      lv_obj_set_width(l, LV_SIZE_CONTENT);
      lv_obj_align(l, LV_ALIGN_CENTER, (lv_coord_t)gauge_resolve_text_xy(cfg->cap_x), (lv_coord_t)gauge_resolve_text_xy(cfg->cap_y));
    } else if (lv_obj_has_flag(l, LV_OBJ_FLAG_USER_2)) {
      val = l;
      lv_obj_align(l, LV_ALIGN_CENTER, (lv_coord_t)gauge_resolve_text_xy(cfg->val_x), (lv_coord_t)gauge_resolve_text_xy(cfg->val_y));
    }
  }
  if (!val) return;
  lv_coord_t vh = lv_obj_get_height(val);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(container); i++) {
    lv_obj_t *l = lv_obj_get_child(container, i);
    if (!lv_obj_has_flag(l, LV_OBJ_FLAG_USER_3)) continue;
    lv_obj_align(l, LV_ALIGN_CENTER, (lv_coord_t)gauge_resolve_text_xy(cfg->val_x),
                 (lv_coord_t)(gauge_resolve_text_xy(cfg->val_y) + vh / 2 + lv_obj_get_height(l) / 2 + 2));
  }
}

static const CellConfig *s_pairCfg = NULL;   // 2-Up: the other cell on the page while one is being built

static void build_cell(lv_obj_t *page, CellRect rect, CellConfig *cfg, CellRuntime *rt) {
  rt->enabled = cfg->enabled;
  rt->gauge_type = cfg->gauge_type;
  rt->signal = cfg->signal;
  rt->min_val = cfg->min_val;
  rt->max_val = cfg->max_val;
  rt->value_divisor = cfg->value_divisor;
  rt->bg_color = cfg->bg_color;
  rt->has_warning = cfg->has_warning;
  rt->warning_threshold = cfg->warning_threshold;
  rt->decimals = gauge_resolve_decimals(cfg->decimals);
  rt->warning_above = cfg->warning_above;
  rt->warning_active = false;
  rt->alert_fullscreen = cfg->alert_fullscreen;
  strncpy(rt->caption, cfg->caption, sizeof(rt->caption) - 1);
  rt->caption[sizeof(rt->caption) - 1] = '\0';
  rt->meter = NULL;
  rt->needle = NULL;
  rt->peakNeedle = NULL;
  rt->numberLabel = NULL;
  rt->chart = NULL;
  rt->chartSeries = NULL;
  rt->noDataLabel = NULL;

  lv_obj_t *container = lv_obj_create(page);
  lv_obj_set_pos(container, rect.x, rect.y);
  lv_obj_set_size(container, rect.w, rect.h);
  lv_obj_set_style_bg_color(container, lv_color_hex(cfg->bg_color), 0);
  lv_obj_set_style_border_width(container, 0, 0);   // no cell outline: it showed as a grey line between stacked gauges
  lv_obj_set_style_radius(container, 0, 0);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  rt->container = container;

  if (!cfg->enabled) return; // empty cell - just the bare container

  lv_color_t needleColor = lv_color_hex(cfg->needle_color);
  lv_color_t tickColor = lv_color_hex(cfg->tick_color);
  int16_t minDim = rect.w < rect.h ? rect.w : rect.h;
  // LVGL pads every container (16px on each side, plus the 1px border) and aligns children inside that padding.
  lv_coord_t cPadT = lv_obj_get_style_pad_top(container, LV_PART_MAIN) + lv_obj_get_style_border_width(container, LV_PART_MAIN);
  lv_coord_t cPadB = lv_obj_get_style_pad_bottom(container, LV_PART_MAIN) + lv_obj_get_style_border_width(container, LV_PART_MAIN);
  int valign = gauge_resolve_valign(cfg->v_align);   // where the gauge stands in its cell: centre / top / bottom
  float cellCx = rect.x + rect.w / 2.0f;
  int32_t tickSize = gauge_resolve_tick_size(cfg->tick_font_size, cfg->value_font_size);   // the numbers AROUND a dial (-1 = hidden)
  const lv_font_t *tickFont = get_font_for_size(tickSize < 0 ? 14 : tickSize);

  if (cfg->gauge_type == GAUGE_TYPE_NUMBER) {
    char sample[24];
    readout_sample(cfg, sample, sizeof(sample));
    bool hasUnits = cfg->units[0] != '\0';
    // The block is caption above, number, units below. Room for the caption and units is planned from the size asked for
    // (they may shrink to fit afterwards); the number is the biggest size that fits where it will stand.
    float capHReq = (float)font_line_h(caption_font_exact(cfg->caption_font_size));
    float unitsHReq = hasUnits ? capHReq : 0.0f;
    int numAnchor = ANCHOR_CENTER;
    float numAnchorY = rect.y + rect.h / 2.0f;
    if (valign == GAUGE_VALIGN_TOP) { numAnchor = ANCHOR_TOP; numAnchorY = rect.y + 8.0f + capHReq + 4.0f; }
    else if (valign == GAUGE_VALIGN_BOTTOM) { numAnchor = ANCHOR_BOTTOM; numAnchorY = rect.y + rect.h - 8.0f - (hasUnits ? unitsHReq + 2.0f : 0.0f); }
    const lv_font_t *numFont = pick_value_font(cfg->value_font_size, sample, cellCx, numAnchorY, numAnchor, rect.w * 0.94f, rect.h * 0.6f);
    float numH = (float)lv_font_get_line_height(numFont);
    float numTopAbs = numAnchor == ANCHOR_TOP ? numAnchorY : (numAnchor == ANCHOR_BOTTOM ? numAnchorY - numH : rect.y + rect.h / 2.0f - numH / 2.0f);
    const lv_font_t *capFont = pick_caption_font(cfg->caption_font_size, cfg->caption, cellCx, numTopAbs - 4.0f, ANCHOR_BOTTOM, rect.w * 0.94f, rect.h * 0.25f);
    const lv_font_t *unitsFont = hasUnits ? pick_caption_font(cfg->caption_font_size, cfg->units, cellCx, numTopAbs + numH + 2.0f, ANCHOR_TOP, rect.w * 0.6f, rect.h * 0.25f) : NULL;

    lv_obj_t *numLabel = lv_label_create(container);
    lv_obj_add_flag(numLabel, LV_OBJ_FLAG_USER_2);   // readout: placed by coordinates when the cell asks
    lv_obj_set_style_text_color(numLabel, lv_color_white(), 0);
    lv_obj_set_style_text_font(numLabel, numFont, 0);
    lv_label_set_text(numLabel, "--");
    if (numAnchor == ANCHOR_TOP) lv_obj_align(numLabel, LV_ALIGN_TOP_MID, 0, (lv_coord_t)lroundf(numAnchorY - (rect.y + cPadT)));
    else if (numAnchor == ANCHOR_BOTTOM) lv_obj_align(numLabel, LV_ALIGN_BOTTOM_MID, 0, -(lv_coord_t)lroundf((rect.y + rect.h - cPadB) - numAnchorY));
    else lv_obj_center(numLabel);
    rt->numberLabel = numLabel;

    lv_obj_t *capLabel = lv_label_create(container);
    if (!cfg->caption_on_face || cfg->gauge_type == GAUGE_TYPE_NUMBER || cfg->gauge_type == GAUGE_TYPE_HISTOGRAM) lv_obj_add_flag(capLabel, LV_OBJ_FLAG_USER_1);
    lv_obj_set_style_text_color(capLabel, lv_color_hex(0x4dd0e1), 0);
    if (capFont) lv_obj_set_style_text_font(capLabel, capFont, 0);
    lv_label_set_text(capLabel, cfg->caption);
    lv_obj_align_to(capLabel, numLabel, LV_ALIGN_OUT_TOP_MID, 0, -4);

    if (hasUnits) {
      lv_obj_t *unitsLabel = lv_label_create(container);
      lv_obj_add_flag(unitsLabel, LV_OBJ_FLAG_USER_3);   // units: placed by coordinates when the cell asks
      lv_obj_set_style_text_color(unitsLabel, lv_color_hex(0xA0A0A0), 0);
      if (unitsFont) lv_obj_set_style_text_font(unitsLabel, unitsFont, 0);
      lv_label_set_text(unitsLabel, cfg->units);
      lv_obj_align_to(unitsLabel, numLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    }
  } else if (cfg->gauge_type == GAUGE_TYPE_HISTOGRAM) {
    int32_t sec = cfg->history_sec;
    if (sec < HIST_MIN_SEC) sec = HIST_MIN_SEC;
    if (sec > HIST_MAX_SEC) sec = HIST_MAX_SEC;

    // Chart values are small integers, so scale real values up: a plain
    // cast would turn AFR 14.2 and 14.7 into the same point. Plot
    // (value - min) * scale, which also keeps everything well inside the
    // int16 range whatever the gauge's min/max are.
    float span = (float)(cfg->max_val - cfg->min_val);
    if (span < 1.0f) span = 1.0f;
    int32_t scale = (int32_t)(HIST_RANGE / span);
    if (scale < 1) scale = 1;
    if (scale > 1000) scale = 1000;
    int32_t top = (int32_t)(span * (float)scale);
    if (top > 30000) top = 30000;
    rt->hist_min = cfg->min_val;
    rt->hist_scale = scale;
    rt->hist_top = top;

    // The caption (with the time span, so the graph's horizontal scale is never a guess) goes under the graph; the graph leaves
    // room for it, so a bigger caption makes a slightly shorter graph. 16px line + 18 = the old fixed 34.
    char cap[GAUGE_CAPTION_MAXLEN + 12];
    if (sec >= 120) snprintf(cap, sizeof(cap), "%s  %d min", cfg->caption, (int)(sec / 60));
    else snprintf(cap, sizeof(cap), "%s  %ds", cfg->caption, (int)sec);
    const lv_font_t *capFont = pick_hist_caption_font(rect, cfg->caption_font_size, cap);
    int16_t fx, fy, fw, fh;
    fit_chart_rect(rect, font_line_h(capFont) + 18, &fx, &fy, &fw, &fh);
    // set_pos is relative to the container's CONTENT area (inside its padding and border)
    lv_coord_t padL = lv_obj_get_style_pad_left(container, LV_PART_MAIN) + lv_obj_get_style_border_width(container, LV_PART_MAIN);
    lv_coord_t padT = lv_obj_get_style_pad_top(container, LV_PART_MAIN) + lv_obj_get_style_border_width(container, LV_PART_MAIN);

    lv_obj_t *chart = lv_chart_create(container);
    lv_obj_set_size(chart, fw, fh);
    lv_obj_set_pos(chart, fx - rect.x - padL, fy - rect.y - padT);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, HIST_POINTS); // rolling window - oldest points scroll off the left as new ones arrive on the right
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, top);
    lv_obj_set_style_bg_color(chart, lv_color_hex(cfg->bg_color), 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_border_color(chart, tickColor, 0);
    lv_obj_set_style_line_color(chart, lv_color_hex(0x2A2A2A), LV_PART_MAIN); // grid lines: dim, so the trace stands out
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);                       // no dot on every sample - at 100 points they smear into a thick band
    lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);

    // Optional flat reference line (e.g. 14.7 on an AFR graph). Added
    // BEFORE the data series so the trace is drawn on top of it.
    if (cfg->ref_enabled) {
      lv_chart_series_t *ref = lv_chart_add_series(chart, lv_color_hex(0x909090), LV_CHART_AXIS_PRIMARY_Y);
      float r = (cfg->ref_value - (float)cfg->min_val) * (float)scale;
      if (r < 0) r = 0;
      if (r > (float)top) r = (float)top;
      lv_chart_set_all_value(chart, ref, (lv_coord_t)(r + 0.5f));
    }
    lv_chart_series_t *series = lv_chart_add_series(chart, needleColor, LV_CHART_AXIS_PRIMARY_Y);
    rt->chart = chart;
    rt->chartSeries = series;

    // An empty graph looks broken. Say why, until the first real data arrives
    // (and again whenever the signal is lost - the old trace stays visible behind it).
    lv_obj_t *noData = lv_label_create(chart);
    lv_label_set_text(noData, "NO DATA");
    lv_obj_set_style_text_color(noData, lv_color_hex(0x909090), 0);
    lv_obj_center(noData);
    rt->noDataLabel = noData;

    lv_obj_t *capLabel = lv_label_create(container);
    if (!cfg->caption_on_face || cfg->gauge_type == GAUGE_TYPE_NUMBER || cfg->gauge_type == GAUGE_TYPE_HISTOGRAM) lv_obj_add_flag(capLabel, LV_OBJ_FLAG_USER_1);
    lv_obj_set_style_text_color(capLabel, lv_color_hex(0x4dd0e1), 0);
    if (capFont) lv_obj_set_style_text_font(capLabel, capFont, 0);
    lv_label_set_text(capLabel, cap);
    // Directly under its own graph, not pinned to the cell's bottom edge - in the
    // bottom cells that edge is where the round panel is narrowest, and the caption's ends
    // would be cut off by the bezel.
    lv_obj_align_to(capLabel, chart, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
  } else {
    // NEEDLE or HALFMOON
    // The dial runs clockwise from start_deg to end_deg, in compass degrees (0 = straight up). LVGL
    // measures from 3 o'clock, which is 270 on our compass, hence the +270.
    int32_t dialStart, dialEnd, dialSweep;
    gauge_dial_resolve(cfg->gauge_type == GAUGE_TYPE_HALFMOON, cfg->start_deg, cfg->end_deg, &dialStart, &dialEnd, &dialSweep);
    int32_t angleRange = dialSweep;
    int32_t rotation = (dialStart + 270) % 360;
    int16_t meterSize = (int16_t)(minDim * 0.88f);

    // Plan the bottom stack (caption, units, readout) FIRST: where the dial may stand depends on where that stack starts.
    // With the caption ON THE FACE (a title above the hub, TunerStudio style) it is not part of the stack at all.
    bool onFace = cfg->caption_on_face;
    bool hasUnits = cfg->show_value && cfg->units[0] != '\0';
    float capBottomAbs = rect.y + rect.h - cPadB - 8.0f;        // where the caption's bottom edge really is
    const lv_font_t *capFont = onFace ? NULL : pick_caption_font(cfg->caption_font_size, cfg->caption, cellCx, capBottomAbs, ANCHOR_BOTTOM, rect.w * 0.94f, rect.h * 0.3f);
    const lv_font_t *unitsFont = hasUnits ? pick_caption_font(cfg->caption_font_size, cfg->units, cellCx, capBottomAbs, ANCHOR_BOTTOM, rect.w * 0.6f, rect.h * 0.25f) : NULL;
    float capH = onFace ? 0.0f : (float)font_line_h(capFont), unitsH = hasUnits ? (float)font_line_h(unitsFont) : 0.0f;
    char sample[24];
    readout_sample(cfg, sample, sizeof(sample));
    float planBottom = rect.y + rect.h - 8.0f - (onFace ? 0.0f : capH + 4.0f) - (hasUnits ? unitsH + 2.0f : 0.0f);
    const lv_font_t *valFont = cfg->show_value
        ? pick_value_font(cfg->value_font_size, sample, cellCx, planBottom, ANCHOR_BOTTOM, meterSize * 0.55f, meterSize * 0.24f) : NULL;
    float stackTop = capBottomAbs;                                // the top of everything stacked at the bottom of the cell
    if (!onFace) stackTop -= capH;
    if (hasUnits) stackTop -= (onFace ? 0.0f : 2.0f) + unitsH;
    if (cfg->show_value) stackTop -= (hasUnits ? 2.0f : (onFace ? 0.0f : 4.0f)) + (float)font_line_h(valFont);

    lv_obj_t *meter = lv_meter_create(container);
    lv_obj_set_size(meter, meterSize, meterSize);
    float meterPad = (float)lv_obj_get_style_pad_left(meter, LV_PART_MAIN);
    float meterRadius = meterSize / 2.0f - meterPad;   // the dial's real radius (the ticks end here)
    int16_t dialShift;
    if (cfg->gauge_type == GAUGE_TYPE_HALFMOON) {
      // A half moon only fills the top half of its circle, so sized like a full dial it came out small, worst in a short
      // wide cell (2-up). Grow it to the largest size whose arc and hub fit the free area of the cell, the cell's width and
      // the round glass, never smaller than before.
      float hi, lo;
      dial_extent(dialStart, dialSweep, &hi, &lo);
      // With text placed by coordinates the caption/readout stack doesn't reserve the bottom of the cell: use all of it.
      float areaTop = rect.y + 4.0f, areaBot = (cfg->text_xy ? rect.y + rect.h : stackTop) - 4.0f, cellMid = rect.y + rect.h / 2.0f;
      float span = hi - lo > 0.05f ? hi - lo : 0.05f;
      int16_t best = meterSize;
      int16_t cand = (int16_t)fminf(rect.w - 8.0f, 2.0f * ((areaBot - areaTop) / span + meterPad));
      // 2-Up: the two half moons must come out the same size, so a size counts only if it also fits the other half
      // (the same spot in the cell above or below), whose glass is narrower or wider in a different place.
      // The partner is checked with ITS OWN dial (a bowl above a dome is a mirror pair) at its own spot; a partner that
      // is not a half moon doesn't limit this one.
      bool twoUp = s_pairCfg && s_pairCfg->enabled && s_pairCfg->gauge_type == GAUGE_TYPE_HALFMOON;
      float other = (rect.y + rect.h / 2.0f) < 233.0f ? (float)rect.h : -(float)rect.h;
      int32_t pStart = dialStart, pEnd, pSweep = dialSweep; float pHi = hi, pLo = lo; int32_t pAlign = valign;
      if (twoUp) {
        gauge_dial_resolve(true, s_pairCfg->start_deg, s_pairCfg->end_deg, &pStart, &pEnd, &pSweep);
        dial_extent(pStart, pSweep, &pHi, &pLo);
        pAlign = gauge_resolve_valign(s_pairCfg->v_align);
      }
      for (; cand > meterSize; cand -= 2) {
        float r = cand / 2.0f - meterPad;
        float cy = (valign == GAUGE_VALIGN_TOP) ? areaTop + hi * r
                 : (valign == GAUGE_VALIGN_BOTTOM) ? areaBot + lo * r
                 : (areaTop + areaBot) / 2.0f + (hi + lo) / 2.0f * r;   // centre the arc + hub in the free area
        bool ok = true;
        for (int k = 0; k < (twoUp ? 2 : 1) && ok; k++) {
          float cyk = cy;
          int32_t ks = dialStart, kw = dialSweep;
          if (k) {
            float r2 = cand / 2.0f - meterPad, t2 = areaTop + other, b2 = areaBot + other;
            cyk = (pAlign == GAUGE_VALIGN_TOP) ? t2 + pHi * r2 : (pAlign == GAUGE_VALIGN_BOTTOM) ? b2 + pLo * r2 : (t2 + b2) / 2.0f + (pHi + pLo) / 2.0f * r2;
            ks = pStart; kw = pSweep;
          }
          for (int32_t a = 0; a <= kw && ok; a += 3) {
            float rad = (float)(ks + a) * 0.01745329f, R = cand / 2.0f;
            float x = cellCx + sinf(rad) * R, y = cyk - cosf(rad) * R, dx = x - 233.0f, dy = y - 233.0f;
            if (dx * dx + dy * dy > (s_glassR - 2.0f) * (s_glassR - 2.0f) || x < rect.x + 2.0f || x > rect.x + rect.w - 2.0f) ok = false;
          }
        }
        if (ok) { best = cand; break; }
      }
      if (best != meterSize) {
        meterSize = best;
        lv_obj_set_size(meter, meterSize, meterSize);
        meterRadius = meterSize / 2.0f - meterPad;
        float cy = (valign == GAUGE_VALIGN_TOP) ? areaTop + hi * meterRadius
                 : (valign == GAUGE_VALIGN_BOTTOM) ? areaBot + lo * meterRadius
                 : (areaTop + areaBot) / 2.0f + (hi + lo) / 2.0f * meterRadius;
        dialShift = (int16_t)lroundf(cy - cellMid);
      } else {
        dialShift = dial_aligned_shift(valign, dialStart, dialSweep, meterSize, meterRadius, cellMid, areaTop, areaBot);
      }
    } else {
      dialShift = dial_aligned_shift(valign, dialStart, dialSweep, meterSize, meterRadius, rect.y + rect.h / 2.0f, rect.y + 4.0f, stackTop - 4.0f);
    }
    // The user's nudge comes last, on top of the automatic placement (nothing re-checks the glass: the editor shows it)
    lv_obj_align(meter, LV_ALIGN_CENTER, (lv_coord_t)gauge_resolve_text_xy(cfg->dial_x), (lv_coord_t)(dialShift + gauge_resolve_text_xy(cfg->dial_y)));
    lv_obj_set_style_bg_color(meter, lv_color_hex(cfg->bg_color), 0);
    lv_obj_set_style_border_width(meter, 0, 0);

    lv_meter_scale_t *scale = lv_meter_add_scale(meter);
    lv_meter_set_scale_ticks(meter, scale, cfg->tick_count, 2, 10, tickColor);
    lv_meter_set_scale_major_ticks(meter, scale, cfg->major_nth, 4, 18, tickColor, 25);
    lv_meter_set_scale_range(meter, scale, cfg->min_val, cfg->max_val, angleRange, rotation);
    lv_obj_set_style_text_font(meter, tickFont, LV_PART_TICKS);
    lv_obj_set_style_text_color(meter, tickColor, LV_PART_TICKS);
    if (tickSize < 0) lv_obj_set_style_text_opa(meter, LV_OPA_TRANSP, LV_PART_TICKS);   // dial numbers hidden; the tick marks stay

    // Warning zones, TunerStudio style (gauge_style.h): up to four thresholds drawn as coloured arc BANDS behind the tick
    // marks (LVGL draws arcs before ticks) and/or as coloured TICK MARKS (it recolours the ticks whose value lies in the
    // range, both ends included). gauge_zone_ranges() hands them over in draw order, yellow first and red last, and LVGL
    // draws a later indicator over an earlier one, so red covers yellow where they overlap. The needle is added after all
    // of them so it is drawn on top.
    {
      const bool zoneOn[4] = { cfg->low_crit_on, cfg->low_warn_on, cfg->high_warn_on, cfg->high_crit_on };
      const int32_t zoneAt[4] = { cfg->low_crit, cfg->low_warn, cfg->high_warn, cfg->high_crit };
      GaugeZoneRange zr[4];
      int nz = gauge_zone_ranges(zoneOn, zoneAt, cfg->min_val, cfg->max_val, zr);
      int32_t zstyle = gauge_resolve_zone_style(cfg->zone_style);
      if (zstyle & GAUGE_ZONE_BANDS) lv_obj_set_style_arc_rounded(meter, false, LV_PART_ITEMS);   // square band ends, exactly at the threshold
      for (int k = 0; k < nz; k++) {
        lv_color_t zc = lv_color_hex(zr[k].red ? GAUGE_ZONE_RED : GAUGE_ZONE_YELLOW);
        if ((zstyle & GAUGE_ZONE_BANDS) && zr[k].start < zr[k].end) {    // a band with no width draws nothing
          lv_meter_indicator_t *band = lv_meter_add_arc(meter, scale, GAUGE_ZONE_BAND_WIDTH, zc, 0);
          lv_meter_set_indicator_start_value(meter, band, zr[k].start);
          lv_meter_set_indicator_end_value(meter, band, zr[k].end);
        }
        if (zstyle & GAUGE_ZONE_TICKS) {
          lv_meter_indicator_t *tick = lv_meter_add_scale_lines(meter, scale, zc, zc, false, 0);
          lv_meter_set_indicator_start_value(meter, tick, zr[k].start);
          lv_meter_set_indicator_end_value(meter, tick, zr[k].end);
        }
      }
    }

    // Peak hold: a dimmer shadow needle, added first so the live needle draws on top of it
    rt->peakNeedle = NULL;
    if (cfg->peak_hold) {
      rt->peakNeedle = lv_meter_add_needle_line(meter, scale, (uint16_t)gauge_resolve_needle_w(cfg->needle_w),
                                                lv_color_mix(needleColor, lv_color_hex(cfg->bg_color), 110), -12);
      lv_meter_set_indicator_value(meter, rt->peakNeedle, cfg->rest_val);
      rt->peak_val = cfg->min_val; rt->peak_ms = 0; rt->peak_hold_ms = (uint32_t)gauge_resolve_peak_s(cfg->peak_hold_s) * 1000u;
    }
    lv_meter_indicator_t *needle = lv_meter_add_needle_line(meter, scale, (uint16_t)gauge_resolve_needle_w(cfg->needle_w), needleColor, -12);
    lv_meter_set_indicator_value(meter, needle, cfg->rest_val);
    rt->meter = meter;
    rt->needle = needle;

    // Needle trim: a gap at the centre and/or a stop-short at the tip.
    gauge_resolve_needle_trim(cfg->needle_hub_pct, cfg->needle_tip_pct, &rt->needleHubPct, &rt->needleTipPct);
    if (rt->needleHubPct > 0 || rt->needleTipPct > 0) {
      lv_obj_add_event_cb(meter, needle_trim_cb, LV_EVENT_DRAW_PART_BEGIN, rt);
    }
    if (rt->needleHubPct > 0) {
      // LVGL always draws a round hub dot at the centre; with a gap it would float on its own, so hide it.
      lv_obj_set_style_bg_opa(meter, LV_OPA_TRANSP, LV_PART_INDICATOR);
      lv_obj_set_style_border_width(meter, 0, LV_PART_INDICATOR);
    }

    lv_obj_t *capLabel = lv_label_create(container);
    if (!cfg->caption_on_face || cfg->gauge_type == GAUGE_TYPE_NUMBER || cfg->gauge_type == GAUGE_TYPE_HISTOGRAM) lv_obj_add_flag(capLabel, LV_OBJ_FLAG_USER_1);
    lv_obj_set_style_text_color(capLabel, lv_color_white(), 0);
    if (onFace) {
      // The title, printed on the dial face above the hub: the biggest size that fits in the room inside the ticks.
      float faceY = rect.y + rect.h / 2.0f + dialShift - 0.30f * meterRadius;
      capFont = pick_caption_font(cfg->caption_font_size, cfg->caption, cellCx, faceY, ANCHOR_CENTER, meterRadius, meterRadius * 0.30f);
    }
    if (capFont) lv_obj_set_style_text_font(capLabel, capFont, 0);
    lv_label_set_text(capLabel, cfg->caption);
    if (onFace) lv_obj_align_to(capLabel, meter, LV_ALIGN_CENTER, 0, -(lv_coord_t)lroundf(0.30f * meterRadius));
    else lv_obj_align(capLabel, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_obj_t *unitsLabel = NULL;
    if (hasUnits) {
      unitsLabel = lv_label_create(container);
      lv_obj_add_flag(unitsLabel, LV_OBJ_FLAG_USER_3);   // units: placed by coordinates when the cell asks
      lv_obj_set_style_text_color(unitsLabel, lv_color_hex(0xA0A0A0), 0);
      if (unitsFont) lv_obj_set_style_text_font(unitsLabel, unitsFont, 0);
      lv_label_set_text(unitsLabel, cfg->units);
      if (onFace) lv_obj_align(unitsLabel, LV_ALIGN_BOTTOM_MID, 0, -8);
      else lv_obj_align_to(unitsLabel, capLabel, LV_ALIGN_OUT_TOP_MID, 0, -2);
    }

    if (cfg->show_value) {
      // The readout sits just above the units (or the caption) at the bottom of the dial.
      lv_obj_t *valueLabel = lv_label_create(container);
      lv_obj_add_flag(valueLabel, LV_OBJ_FLAG_USER_2);   // readout: placed by coordinates when the cell asks
      lv_obj_set_style_text_color(valueLabel, lv_color_white(), 0);
      lv_obj_set_style_text_font(valueLabel, valFont, 0);
      lv_label_set_text(valueLabel, "--");
      if (unitsLabel) lv_obj_align_to(valueLabel, unitsLabel, LV_ALIGN_OUT_TOP_MID, 0, -2);
      else if (onFace) lv_obj_align(valueLabel, LV_ALIGN_BOTTOM_MID, 0, -8);
      else lv_obj_align_to(valueLabel, capLabel, LV_ALIGN_OUT_TOP_MID, 0, -4);
      // align_to places it once, for "--": keep the same row but re-centre it whenever the number's width changes
      if (unitsLabel || !onFace) lv_obj_align(valueLabel, LV_ALIGN_TOP_MID, 0, lv_obj_get_y_aligned(valueLabel));
      rt->numberLabel = valueLabel; // reuse the same field - only one of these two paths is active per cell
    }
  }

  if (cfg->text_xy) apply_text_xy(container, cfg);
  else apply_text_halign(container, rect, gauge_resolve_halign(cfg->h_align));

  lv_obj_add_event_cb(page, gesture_event_cb, LV_EVENT_GESTURE, NULL);

  rt->sweep_pos = cfg->min_val;
  rt->sweep_dir = 1;
  int32_t step = (cfg->max_val - cfg->min_val) / 25;
  rt->sweep_step = step < 1 ? 1 : step;

  int32_t hz = cfg->refresh_hz < 1 ? 1 : cfg->refresh_hz;
  rt->refresh_interval_ms = 1000 / hz;
  if (cfg->gauge_type == GAUGE_TYPE_HISTOGRAM) {
    // A trend graph samples HIST_POINTS times across its history window,
    // whatever the cell's refresh rate says. (At the old behavior of one
    // point per refresh, a 50Hz signal showed only 1 second of history.)
    int32_t sec = cfg->history_sec;
    if (sec < HIST_MIN_SEC) sec = HIST_MIN_SEC;
    if (sec > HIST_MAX_SEC) sec = HIST_MAX_SEC;
    rt->refresh_interval_ms = (uint32_t)(sec * 1000) / HIST_POINTS;
  }
  rt->last_update_ms = 0;
}

static void build_page(int pageIndex, PageConfig *cfg) {
  lv_obj_t *page = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(page, lv_color_black(), 0);
  lv_obj_set_style_pad_all(page, 0, 0);
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

  int numCells = gauge_config_cells_for_layout(cfg->layout);
  pages[pageIndex].page = page;
  pages[pageIndex].numCells = numCells;

  for (int i = 0; i < numCells; i++) {
    CellRect rect = get_cell_rect(cfg->layout, i);
    s_pairCfg = (cfg->layout == LAYOUT_2UP) ? &cfg->cells[1 - i] : NULL;   // the stacked partner, for matching half moons
    build_cell(page, rect, &cfg->cells[i], &pages[pageIndex].cells[i]);
    s_pairCfg = NULL;
  }
}

// Biggest of 36 / 28 / 14 at which the wider of a lamp's two texts fits inside it.
static const lv_font_t *pick_lamp_font(const char *a, const char *b, int w, int h) {
  const lv_font_t *const fonts[3] = { &lv_font_montserrat_36, &lv_font_montserrat_28, &lv_font_montserrat_14 };
  for (int i = 0; i < 2; i++) {
    lv_point_t sa, sb;
    lv_txt_get_size(&sa, a, fonts[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_txt_get_size(&sb, b, fonts[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int tw = sa.x > sb.x ? sa.x : sb.x, th = sa.y > sb.y ? sa.y : sb.y;
    if (tw <= w * 0.88f && th <= h * 0.8f) return fonts[i];
  }
  return fonts[2];
}

// Draws a lamp in its on (look 1) or off (look 0) colours and text.
static void apply_lamp_look(IndLamp *l, int look) {
  uint32_t bg = look ? l->onColor : l->offColor;
  lv_obj_set_style_bg_color(l->lamp, lv_color_hex(bg), 0);
  lv_obj_set_style_border_color(l->lamp, lv_color_mix(lv_color_white(), lv_color_hex(bg), LV_OPA_30), 0);   // a faint lighter edge, so a near-black lamp still has an outline
  lv_obj_set_style_text_color(l->label, lv_color_hex(look ? l->onTextColor : l->offTextColor), 0);
  lv_label_set_text_static(l->label, look ? l->onText : l->offText);
  l->shown = (int8_t)look;
}

// A page of on/off lamps. The grid is laid out by gauge_indicator_layout() so every lamp lies fully on the round glass.
static void build_indicator_page(int pageIndex, PageConfig *cfg) {
  lv_obj_t *page = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(page, lv_color_black(), 0);
  lv_obj_set_style_pad_all(page, 0, 0);
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  pages[pageIndex].page = page;
  pages[pageIndex].numCells = 0;
  pages[pageIndex].lamps = NULL;
  pages[pageIndex].numLamps = 0;

  int n = cfg->ind_count;
  if (n < 0) n = 0;
  if (n > MAX_INDICATORS_PER_PAGE) n = MAX_INDICATORS_PER_PAGE;
  if (n == 0) {                          // an empty page says so, rather than looking broken
    lv_obj_t *none = lv_label_create(page);
    lv_label_set_text(none, "NO INDICATORS");
    lv_obj_set_style_text_color(none, lv_color_hex(0x707070), 0);
    lv_obj_set_style_text_font(none, &lv_font_montserrat_28, 0);
    lv_obj_center(none);
    lv_obj_add_event_cb(page, gesture_event_cb, LV_EVENT_GESTURE, NULL);
    return;
  }

  GaugeLampRect rects[MAX_INDICATORS_PER_PAGE];
  gauge_indicator_layout(n, cfg->ind_cols, rects);
  IndLamp *lamps = (IndLamp *)calloc((size_t)n, sizeof(IndLamp));
  if (!lamps) return;                    // out of memory: leave the page empty rather than crash
  pages[pageIndex].lamps = lamps;
  pages[pageIndex].numLamps = n;

  for (int i = 0; i < n; i++) {
    const IndicatorConfig *c = &cfg->indicators[i];
    IndLamp *l = &lamps[i];
    l->signal = c->signal; l->op = c->op; l->threshold = c->threshold; l->hysteresis = c->hysteresis; l->blink = c->blink;
    l->onColor = c->on_color; l->offColor = c->off_color; l->onTextColor = c->on_text_color; l->offTextColor = c->off_text_color;
    strncpy(l->onText, c->on_text, IND_TEXT_MAXLEN - 1);
    strncpy(l->offText, c->off_text, IND_TEXT_MAXLEN - 1);
    l->on = false;

    l->lamp = lv_obj_create(page);
    lv_obj_set_pos(l->lamp, rects[i].x, rects[i].y);
    lv_obj_set_size(l->lamp, rects[i].w, rects[i].h);
    lv_obj_clear_flag(l->lamp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(l->lamp, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_radius(l->lamp, rects[i].h / 5 > 6 ? rects[i].h / 5 : 6, 0);
    lv_obj_set_style_border_width(l->lamp, 2, 0);
    lv_obj_set_style_pad_all(l->lamp, 0, 0);

    l->label = lv_label_create(l->lamp);
    lv_obj_set_style_text_font(l->label, pick_lamp_font(l->onText, l->offText, rects[i].w, rects[i].h), 0);
    lv_obj_center(l->label);
    apply_lamp_look(l, 0);               // every lamp starts in its off look, until its signal says otherwise
  }
  lv_obj_add_event_cb(page, gesture_event_cb, LV_EVENT_GESTURE, NULL);
}

// Re-evaluates every lamp of an indicators page and redraws only the ones that changed.
static void update_indicator_page(GaugePageRuntime *pr) {
  bool blinkPhase = (s_flash_counter / 24) % 2 == 0;     // ~480ms per phase, the same rate as the warning flash
  for (int i = 0; i < pr->numLamps; i++) {
    IndLamp *l = &pr->lamps[i];
    l->on = gauge_indicator_next(l->on, gauge_config_signal_has_data(l->signal), gauge_config_signal_value(l->signal),
                                 (int)l->op, l->threshold, l->hysteresis);
    int look = (l->on && (!l->blink || blinkPhase)) ? 1 : 0;
    if (look != l->shown) apply_lamp_look(l, look);
  }
}

#if ENABLE_TOUCH_SWIPE_NAV
static void gesture_event_cb(lv_event_t *e) {
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
  if (dir == LV_DIR_LEFT) {
    switch_to_page((s_current_page + 1) % s_num_active_pages);
  } else if (dir == LV_DIR_RIGHT) {
    switch_to_page((s_current_page + s_num_active_pages - 1) % s_num_active_pages);
  }
}
#else
static void gesture_event_cb(lv_event_t *e) {
  (void)e; // swipe navigation disabled - BOOT button only, see ENABLE_TOUCH_SWIPE_NAV
}
#endif

static bool s_startupSweep = false, s_demo = true;   // the board turns the sweep on from its saved setting (default on); host tests start without it
static uint32_t s_sweepStartMs = 0;   // first update tick; 0 = not started yet
#define STARTUP_SWEEP_MS 1600
void gauge_app_set_options(bool startupSweep, bool demo) { s_startupSweep = startupSweep; s_demo = demo; }

static int32_t clamped_or_sweep(CellRuntime *c, float value, bool has_data) {
  if (!has_data && !s_demo) return c->min_val;   // demo off: rest at the bottom of the scale
  if (has_data) {
    int32_t v = (int32_t)value;
    if (v < c->min_val) v = c->min_val;
    if (v > c->max_val) v = c->max_val;
    return v;
  }
  c->sweep_pos += c->sweep_dir * c->sweep_step;
  if (c->sweep_pos >= c->max_val) { c->sweep_pos = c->max_val; c->sweep_dir = -1; }
  else if (c->sweep_pos <= c->min_val) { c->sweep_pos = c->min_val; c->sweep_dir = 1; }
  return c->sweep_pos;
}

static void update_warning_flash(CellRuntime *c, float value, bool has_data) {
  if (!c->has_warning) return;
  bool triggered = has_data && (c->warning_above ? (value > c->warning_threshold) : (value < c->warning_threshold));

  if (triggered) {
    bool flash_on = (s_flash_counter / 24) % 2 == 0;
    lv_color_t target = flash_on ? lv_color_hex(0xB00000) : lv_color_hex(c->bg_color);
    lv_obj_set_style_bg_color(c->container, target, 0);
    c->warning_active = true;
  } else if (c->warning_active) {
    lv_obj_set_style_bg_color(c->container, lv_color_hex(c->bg_color), 0);
    c->warning_active = false;
  }
}

// Runs every ~100ms from the timer. Looks at every alert-enabled cell on
// EVERY page (not just the one on screen), independent of each cell's own
// refresh throttle.
static void update_alert_overlay(uint32_t now) {
  if (!s_alertOverlay) return;

  if (s_alertSnoozed && time_reached(now, s_alertSnoozeUntil)) s_alertSnoozed = false;

  int count = 0;
  const CellRuntime *first = NULL;
  float firstValue = 0;

  bool engineRunning = gauge_config_signal_has_data(SIG_RPM) &&
                       gauge_config_signal_value(SIG_RPM) > ALERT_MIN_RPM;
  if (engineRunning) {
    for (int p = 0; p < s_num_active_pages; p++) {
      for (int i = 0; i < pages[p].numCells; i++) {
        const CellRuntime *c = &pages[p].cells[i];
        if (!c->enabled || !c->has_warning || !c->alert_fullscreen) continue;
        if (!gauge_config_signal_has_data(c->signal)) continue; // a lost signal is not an alarm value
        float v = gauge_config_signal_value(c->signal) / c->value_divisor;
        bool bad = c->warning_above ? (v > c->warning_threshold) : (v < c->warning_threshold);
        if (!bad) continue;
        count++;
        if (!first) { first = c; firstValue = v; }
      }
    }
  }

  if (s_alertSnoozed) { alert_hide(); return; }

  if (count > 0) {
    s_alertHoldUntil = now + ALERT_MIN_SHOW_MS;
    char buf[48];
    const char *name = first->caption[0] ? first->caption : gauge_config_signal_name(first->signal);
    snprintf(buf, sizeof(buf), "%s %s", name, first->warning_above ? "HIGH" : "LOW");
    set_label_if_changed(s_alertTitle, buf);
    snprintf(buf, sizeof(buf), "%.*f", (int)first->decimals, firstValue);
    set_label_if_changed(s_alertValue, buf);
    if (count > 1) snprintf(buf, sizeof(buf), "+%d more", count - 1);
    else buf[0] = '\0';
    set_label_if_changed(s_alertMore, buf);
    lv_obj_clear_flag(s_alertOverlay, LV_OBJ_FLAG_HIDDEN);
    s_alertShown = true;
  } else if (s_alertShown && !time_reached(now, s_alertHoldUntil)) {
    // condition just cleared - keep the alert up for the minimum time
  } else {
    alert_hide();
  }
}

// Wipes every trend graph's trace. Done whenever the bench simulator starts or
// stops, so fake data and real data are never drawn on the same graph.
static void clear_all_traces(void) {
  for (int p = 0; p < s_num_active_pages; p++) {
    for (int i = 0; i < pages[p].numCells; i++) {
      CellRuntime *c = &pages[p].cells[i];
      if (c->chart && c->chartSeries) lv_chart_set_all_value(c->chart, c->chartSeries, LV_CHART_POINT_NONE);
    }
  }
}

static void gauge_update_timer_cb(lv_timer_t *timer) {
  s_flash_counter++;
  uint32_t now = lv_tick_get();

  static bool s_simWasActive = false;
  bool simNow = sim_is_active();
  if (simNow != s_simWasActive) { clear_all_traces(); s_simWasActive = simNow; }

  if (s_simBanner) {
    bool want = sim_is_active();
    bool hidden = lv_obj_has_flag(s_simBanner, LV_OBJ_FLAG_HIDDEN);
    if (want && hidden) lv_obj_clear_flag(s_simBanner, LV_OBJ_FLAG_HIDDEN);
    else if (!want && !hidden) lv_obj_add_flag(s_simBanner, LV_OBJ_FLAG_HIDDEN);
  }

  if (s_logIndicator) {
    if (sd_logging_is_active()) {
      bool blinkOn = (s_flash_counter / 24) % 2 == 0; // ~480ms per phase, same blink rate as the warning flash
      if (blinkOn) lv_obj_clear_flag(s_logIndicator, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(s_logIndicator, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_logIndicator, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (g_requested_page >= 0 && g_requested_page < s_num_active_pages) {
    switch_to_page(g_requested_page);
    g_requested_page = -1;
  }

  if (g_page_nav_delta != 0 && s_num_active_pages > 0) {
    if (s_alertShown) {
      alert_dismiss(now); // BOOT (or the web arrows) while an alert is up just dismisses it
    } else {
      int next = (s_current_page + g_page_nav_delta + s_num_active_pages) % s_num_active_pages;
      switch_to_page(next);
    }
    g_page_nav_delta = 0;
  }

  if (s_flash_counter % 5 == 0) update_alert_overlay(now); // every ~100ms

  if (s_sweepStartMs == 0) s_sweepStartMs = now ? now : 1;
  for (int p = 0; p < s_num_active_pages; p++) {
    if (pages[p].numLamps > 0) update_indicator_page(&pages[p]);
    for (int i = 0; i < pages[p].numCells; i++) {
      CellRuntime *c = &pages[p].cells[i];
      if (!c->enabled) continue;

      float value = gauge_config_signal_value(c->signal) / c->value_divisor;
      bool has_data = gauge_config_signal_has_data(c->signal);

      if (c->noDataLabel) {                       // every tick, so it never lingers for a long history's slow sample interval
        bool hidden = lv_obj_has_flag(c->noDataLabel, LV_OBJ_FLAG_HIDDEN);
        if (has_data && !hidden) lv_obj_add_flag(c->noDataLabel, LV_OBJ_FLAG_HIDDEN);
        else if (!has_data && hidden) lv_obj_clear_flag(c->noDataLabel, LV_OBJ_FLAG_HIDDEN);
      }

      // Startup sweep: every needle on every page, min -> max -> min together, then normal
      if (s_startupSweep && c->meter && (uint32_t)(now - s_sweepStartMs) < STARTUP_SWEEP_MS) {
        uint32_t t = now - s_sweepStartMs, half = STARTUP_SWEEP_MS / 2;
        float f = t < half ? (float)t / half : (float)(STARTUP_SWEEP_MS - t) / half;
        lv_meter_set_indicator_value(c->meter, c->needle, c->min_val + (int32_t)lroundf(f * (c->max_val - c->min_val)));
        continue;
      }

      bool shouldUpdate = !has_data || (now - c->last_update_ms >= c->refresh_interval_ms);
      if (!shouldUpdate) continue;
      if (has_data) c->last_update_ms = now;

      int32_t displayValue = clamped_or_sweep(c, value, has_data);

      // No data (before the first frame, or the bus went quiet): show
      // dashes rather than 0.0 or a frozen last reading.
      char buf[16];
      if (has_data && isfinite(value)) snprintf(buf, sizeof(buf), "%.*f", (int)c->decimals, value);   // the big fonts only have digits: never feed them "nan"/"inf"
      else snprintf(buf, sizeof(buf), "--");

      if (c->gauge_type == GAUGE_TYPE_NUMBER) {
        if (c->numberLabel) set_label_if_changed(c->numberLabel, buf);
      } else if (c->gauge_type == GAUGE_TYPE_HISTOGRAM) {
        // Hold the trace while there's no data - don't plot the "waiting" sweep.
        if (has_data && c->chart && c->chartSeries) {
          float y = (value - (float)c->hist_min) * (float)c->hist_scale;
          if (y < 0) y = 0;
          if (y > (float)c->hist_top) y = (float)c->hist_top;
          lv_chart_set_next_value(c->chart, c->chartSeries, (lv_coord_t)(y + 0.5f));
        }
      } else {
        if (c->meter) lv_meter_set_indicator_value(c->meter, c->needle, displayValue);
        if (c->meter && c->peakNeedle) {   // peak hold: rises with the reading, holds the top for the set time, then drops back
          if (!has_data || displayValue >= c->peak_val || (uint32_t)(now - c->peak_ms) >= c->peak_hold_ms) { c->peak_val = displayValue; c->peak_ms = now; }
          lv_meter_set_indicator_value(c->meter, c->peakNeedle, c->peak_val);
        }
        update_warning_flash(c, value, has_data);
        if (c->numberLabel) set_label_if_changed(c->numberLabel, buf); // the optional "show_value" readout for NEEDLE/HALFMOON
      }
    }
  }
}

int gauge_app_get_num_pages(void) {
  return s_num_active_pages;
}

static void build_alert_overlay(void) {
  lv_coord_t w = lv_disp_get_hor_res(NULL);
  lv_coord_t h = lv_disp_get_ver_res(NULL);

  s_alertOverlay = lv_obj_create(lv_layer_sys());
  lv_obj_set_size(s_alertOverlay, w, h);
  lv_obj_set_pos(s_alertOverlay, 0, 0);
  lv_obj_set_style_radius(s_alertOverlay, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(s_alertOverlay, lv_color_hex(0x500000), 0);
  lv_obj_set_style_bg_opa(s_alertOverlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_alertOverlay, 10, 0);
  lv_obj_set_style_border_color(s_alertOverlay, lv_color_hex(0xFF2020), 0);
  lv_obj_set_style_pad_all(s_alertOverlay, 0, 0);
  lv_obj_clear_flag(s_alertOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_alertOverlay, alert_click_cb, LV_EVENT_CLICKED, NULL);

  s_alertTitle = lv_label_create(s_alertOverlay);
  lv_obj_set_width(s_alertTitle, 330);
  lv_label_set_long_mode(s_alertTitle, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(s_alertTitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(s_alertTitle, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_alertTitle, &lv_font_montserrat_36, 0);
  lv_label_set_text(s_alertTitle, "");
  lv_obj_align(s_alertTitle, LV_ALIGN_CENTER, 0, -90);

  s_alertValue = lv_label_create(s_alertOverlay);
  lv_obj_set_style_text_color(s_alertValue, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_alertValue, ALERT_VALUE_FONT, 0);
  lv_label_set_text(s_alertValue, "");
  lv_obj_align(s_alertValue, LV_ALIGN_CENTER, 0, 0);

  s_alertMore = lv_label_create(s_alertOverlay);
  lv_obj_set_style_text_color(s_alertMore, lv_color_hex(0xFFB0B0), 0);
  lv_obj_set_style_text_font(s_alertMore, &lv_font_montserrat_28, 0);
  lv_label_set_text(s_alertMore, "");
  lv_obj_align(s_alertMore, LV_ALIGN_CENTER, 0, 75);

  lv_obj_t *hint = lv_label_create(s_alertOverlay);
  lv_obj_set_style_text_color(hint, lv_color_hex(0xFFB0B0), 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
  lv_label_set_text(hint, "BOOT or tap to dismiss");
  lv_obj_align(hint, LV_ALIGN_CENTER, 0, 130);

  lv_obj_add_flag(s_alertOverlay, LV_OBJ_FLAG_HIDDEN);
}

void build_gauge_app(void) {
  PageConfig *pageConfigs = gauge_config_get_pages();

  int pageIdx = 0;
  for (int i = 0; i < MAX_PAGES; i++) {
    if (!pageConfigs[i].enabled) continue;
    if (pageConfigs[i].kind == PAGE_INDICATORS) build_indicator_page(pageIdx, &pageConfigs[i]);
    else build_page(pageIdx, &pageConfigs[i]);
    pageIdx++;
  }

  s_num_active_pages = pageIdx > 0 ? pageIdx : 0;
  if (s_num_active_pages > 0) {
    s_current_page = 0;
    lv_disp_load_scr(pages[0].page);
  }

  build_alert_overlay(); // created before the logging dot so the dot stays on top of it

  // Logging indicator - a small circle on the system layer, so it
  // shows on top of whichever page is currently active without being
  // added to each page individually.
  s_logIndicator = lv_obj_create(lv_layer_sys());
  lv_obj_set_size(s_logIndicator, 14, 14);
  lv_obj_set_style_radius(s_logIndicator, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s_logIndicator, 0, 0);
  lv_obj_set_style_bg_color(s_logIndicator, lv_color_hex(0x00C800), 0);
  // Top centre, NOT the corner: the panel is a circle, and the old top-right
  // corner position was entirely off the glass, so the dot was never visible.
  lv_obj_align(s_logIndicator, LV_ALIGN_TOP_MID, 0, 14);
  lv_obj_add_flag(s_logIndicator, LV_OBJ_FLAG_HIDDEN); // hidden until logging actually starts

  // Bench-simulator banner, just under the logging dot. Created after the alert
  // overlay so it stays visible on top of a full-screen alert.
  s_simBanner = lv_label_create(lv_layer_sys());
  lv_label_set_text(s_simBanner, "SIMULATED");
  lv_obj_set_style_text_color(s_simBanner, lv_color_black(), 0);
  lv_obj_set_style_bg_color(s_simBanner, lv_color_hex(0xF5A623), 0);
  lv_obj_set_style_bg_opa(s_simBanner, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_simBanner, 10, 0);
  lv_obj_set_style_pad_hor(s_simBanner, 10, 0);
  lv_obj_set_style_pad_ver(s_simBanner, 2, 0);
  lv_obj_align(s_simBanner, LV_ALIGN_TOP_MID, 0, 36);
  lv_obj_add_flag(s_simBanner, LV_OBJ_FLAG_HIDDEN); // only while fake data is being fed

  lv_timer_create(gauge_update_timer_cb, 20, NULL); // 20ms = 50Hz, fastest per-cell refresh rate supported
}
