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
#include "lvgl.h"
#include <stdbool.h>
#include <stdio.h>
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

volatile int g_requested_page = -1;
volatile int g_page_nav_delta = 0;

#define ENABLE_TOUCH_SWIPE_NAV 0 // disabled per earlier request - BOOT button only

typedef struct {
  int16_t x, y, w, h;
} CellRect;

// Returns the pixel rect for a given cell index within a given layout,
// on the 466x466 panel.
static CellRect get_cell_rect(LayoutType layout, int cellIndex) {
  const int16_t S = 466;
  const int16_t H = S / 2; // 233

  switch (layout) {
    case LAYOUT_1CELL:
      return (CellRect){0, 0, S, S};
    case LAYOUT_2UP:
      if (cellIndex == 0) return (CellRect){0, 0, S, H};
      return (CellRect){0, H, S, H};
    case LAYOUT_3UP:
      if (cellIndex == 0) return (CellRect){0, 0, S, H};
      if (cellIndex == 1) return (CellRect){0, H, H, H};
      return (CellRect){H, H, H, H};
    case LAYOUT_2X2:
      if (cellIndex == 0) return (CellRect){0, 0, H, H};
      if (cellIndex == 1) return (CellRect){H, 0, H, H};
      if (cellIndex == 2) return (CellRect){0, H, H, H};
      return (CellRect){H, H, H, H};
    default:
      return (CellRect){0, 0, S, S};
  }
}

typedef struct {
  lv_obj_t *container;
  lv_obj_t *meter;         // NULL for GAUGE_TYPE_NUMBER
  lv_meter_indicator_t *needle; // NULL for GAUGE_TYPE_NUMBER
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

typedef struct {
  lv_obj_t *page;
  int numCells;
  CellRuntime cells[MAX_CELLS_PER_PAGE];
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
#define PANEL_FIT_RADIUS 228.0f    // a readout's corners must stay inside this circle, or the bezel cuts the digits off

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
  snprintf(lo, sizeof(lo), "%.1f", (double)cfg->min_val);
  snprintf(hi, sizeof(hi), "%.1f", (double)cfg->max_val);
  snprintf(out, n, "%s", strlen(lo) > strlen(hi) ? lo : hi);
}

// Picks the largest size that is <= `requested` and fits: no wider than maxW, no taller than maxH,
// and fully on the round glass. The box is centred on (cx, y), or - when anchorBottom - sits with its
// bottom edge at y. Never goes below 14.
static const lv_font_t *pick_value_font(int32_t requested, const char *sample, float cx, float y,
                                        bool anchorBottom, float maxW, float maxH) {
  int top = GAUGE_VALUE_SIZE_COUNT - 1;
  while (top > 0 && VALUE_FONTS[top].size > requested) top--;
  for (int i = top; i > 0; i--) {
    lv_point_t sz;
    lv_txt_get_size(&sz, sample, VALUE_FONTS[i].font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    float cy = anchorBottom ? y - sz.y / 2.0f : y;
    if (sz.x <= maxW && sz.y <= maxH && box_in_panel(cx, cy, (float)sz.x, (float)sz.y)) return VALUE_FONTS[i].font;
  }
  return VALUE_FONTS[0].font;
}

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
static void fit_chart_rect(CellRect cell, int16_t *ox, int16_t *oy, int16_t *ow, int16_t *oh) {
  const float S = 466.0f;
  const float cx = S / 2.0f, cy = S / 2.0f;
  const float R = S / 2.0f - 8.0f;
  const int pad = 6, capBand = 34, step = 4, minW = 60, minH = 40;

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
  rt->warning_above = cfg->warning_above;
  rt->warning_active = false;
  rt->alert_fullscreen = cfg->alert_fullscreen;
  strncpy(rt->caption, cfg->caption, sizeof(rt->caption) - 1);
  rt->caption[sizeof(rt->caption) - 1] = '\0';
  rt->meter = NULL;
  rt->needle = NULL;
  rt->numberLabel = NULL;
  rt->chart = NULL;
  rt->chartSeries = NULL;
  rt->noDataLabel = NULL;

  lv_obj_t *container = lv_obj_create(page);
  lv_obj_set_pos(container, rect.x, rect.y);
  lv_obj_set_size(container, rect.w, rect.h);
  lv_obj_set_style_bg_color(container, lv_color_hex(cfg->bg_color), 0);
  lv_obj_set_style_border_width(container, 1, 0);
  lv_obj_set_style_border_color(container, lv_color_hex(0x333333), 0);
  lv_obj_set_style_radius(container, 0, 0);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  rt->container = container;

  if (!cfg->enabled) return; // empty cell - just the bare container

  lv_color_t needleColor = lv_color_hex(cfg->needle_color);
  lv_color_t tickColor = lv_color_hex(cfg->tick_color);
  int16_t minDim = rect.w < rect.h ? rect.w : rect.h;
  int32_t tickSize = gauge_resolve_tick_size(cfg->tick_font_size, cfg->value_font_size);   // the numbers AROUND a dial (-1 = hidden)
  const lv_font_t *tickFont = get_font_for_size(tickSize < 0 ? 14 : tickSize);

  if (cfg->gauge_type == GAUGE_TYPE_NUMBER) {
    char sample[24];
    readout_sample(cfg, sample, sizeof(sample));
    const lv_font_t *numFont = pick_value_font(cfg->value_font_size, sample, rect.x + rect.w / 2.0f, rect.y + rect.h / 2.0f,
                                               false, rect.w * 0.94f, rect.h * 0.6f);
    lv_obj_t *numLabel = lv_label_create(container);
    lv_obj_set_style_text_color(numLabel, lv_color_white(), 0);
    lv_obj_set_style_text_font(numLabel, numFont, 0);
    lv_label_set_text(numLabel, "--");
    lv_obj_center(numLabel);
    rt->numberLabel = numLabel;

    lv_obj_t *capLabel = lv_label_create(container);
    lv_obj_set_style_text_color(capLabel, lv_color_hex(0x4dd0e1), 0);
    lv_label_set_text(capLabel, cfg->caption);
    lv_obj_align_to(capLabel, numLabel, LV_ALIGN_OUT_TOP_MID, 0, -4);
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

    int16_t fx, fy, fw, fh;
    fit_chart_rect(rect, &fx, &fy, &fw, &fh);
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

    // Caption with the time span, so the graph's horizontal scale is never a guess
    char cap[GAUGE_CAPTION_MAXLEN + 12];
    if (sec >= 120) snprintf(cap, sizeof(cap), "%s  %d min", cfg->caption, (int)(sec / 60));
    else snprintf(cap, sizeof(cap), "%s  %ds", cfg->caption, (int)sec);
    lv_obj_t *capLabel = lv_label_create(container);
    lv_obj_set_style_text_color(capLabel, lv_color_hex(0x4dd0e1), 0);
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

    lv_obj_t *meter = lv_meter_create(container);
    lv_obj_set_size(meter, meterSize, meterSize);
    lv_obj_align(meter, LV_ALIGN_CENTER, 0, dial_vertical_shift(dialStart, dialSweep, meterSize));
    lv_obj_set_style_bg_color(meter, lv_color_hex(cfg->bg_color), 0);
    lv_obj_set_style_border_width(meter, 0, 0);

    lv_meter_scale_t *scale = lv_meter_add_scale(meter);
    lv_meter_set_scale_ticks(meter, scale, cfg->tick_count, 2, 10, tickColor);
    lv_meter_set_scale_major_ticks(meter, scale, cfg->major_nth, 4, 18, tickColor, 25);
    lv_meter_set_scale_range(meter, scale, cfg->min_val, cfg->max_val, angleRange, rotation);
    lv_obj_set_style_text_font(meter, tickFont, LV_PART_TICKS);
    lv_obj_set_style_text_color(meter, tickColor, LV_PART_TICKS);
    if (tickSize < 0) lv_obj_set_style_text_opa(meter, LV_OPA_TRANSP, LV_PART_TICKS);   // dial numbers hidden; the tick marks stay

    // Coloured tick zones. LVGL recolours the ticks whose value lies in [start, end] (both ends included) and
    // lets a LATER indicator win over an earlier one, so yellow goes in first and red second: red wins where
    // they overlap. High-end zones run from their value up to max; low-end zones from min up to their value.
    for (int zone = 0; zone < 2; zone++) {
      bool red = (zone == 1);
      if (!(red ? cfg->red_enabled : cfg->yellow_enabled)) continue;
      int32_t at = red ? cfg->red_value : cfg->yellow_value;
      lv_color_t zc = lv_color_hex(red ? GAUGE_ZONE_RED : GAUGE_ZONE_YELLOW);
      lv_meter_indicator_t *z = lv_meter_add_scale_lines(meter, scale, zc, zc, false, 0);
      lv_meter_set_indicator_start_value(meter, z, cfg->zones_low ? cfg->min_val : at);
      lv_meter_set_indicator_end_value(meter, z, cfg->zones_low ? at : cfg->max_val);
    }

    lv_meter_indicator_t *needle = lv_meter_add_needle_line(meter, scale, 6, needleColor, -12);
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
    lv_obj_set_style_text_color(capLabel, lv_color_white(), 0);
    lv_label_set_text(capLabel, cfg->caption);
    lv_obj_align(capLabel, LV_ALIGN_BOTTOM_MID, 0, -8);

    if (cfg->show_value) {
      // The readout sits just above the caption at the bottom of the dial: the biggest size that fits there.
      char sample[24];
      readout_sample(cfg, sample, sizeof(sample));
      float capH = (float)lv_font_get_line_height(lv_obj_get_style_text_font(capLabel, LV_PART_MAIN));
      float readoutBottom = rect.y + rect.h - 8.0f - capH - 4.0f;
      const lv_font_t *valFont = pick_value_font(cfg->value_font_size, sample, rect.x + rect.w / 2.0f, readoutBottom,
                                                 true, meterSize * 0.55f, meterSize * 0.24f);
      lv_obj_t *valueLabel = lv_label_create(container);
      lv_obj_set_style_text_color(valueLabel, lv_color_white(), 0);
      lv_obj_set_style_text_font(valueLabel, valFont, 0);
      lv_label_set_text(valueLabel, "--");
      lv_obj_align_to(valueLabel, capLabel, LV_ALIGN_OUT_TOP_MID, 0, -4);
      rt->numberLabel = valueLabel; // reuse the same field - only one of these two paths is active per cell
    }
  }

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
    build_cell(page, rect, &cfg->cells[i], &pages[pageIndex].cells[i]);
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

static int32_t clamped_or_sweep(CellRuntime *c, float value, bool has_data) {
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
    snprintf(buf, sizeof(buf), "%.1f", firstValue);
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

  for (int p = 0; p < s_num_active_pages; p++) {
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

      bool shouldUpdate = !has_data || (now - c->last_update_ms >= c->refresh_interval_ms);
      if (!shouldUpdate) continue;
      if (has_data) c->last_update_ms = now;

      int32_t displayValue = clamped_or_sweep(c, value, has_data);

      // No data (before the first frame, or the bus went quiet): show
      // dashes rather than 0.0 or a frozen last reading.
      char buf[16];
      if (has_data && isfinite(value)) snprintf(buf, sizeof(buf), "%.1f", value);   // the big fonts only have digits: never feed them "nan"/"inf"
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
    build_page(pageIdx, &pageConfigs[i]);
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
