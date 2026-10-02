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
#include "lvgl.h"
#include <stdbool.h>
#include <stdio.h>

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
  lv_obj_t *numberLabel;   // used by GAUGE_TYPE_NUMBER; also doubles as the "show_value" readout for other types
  lv_obj_t *chart;         // used by GAUGE_TYPE_HISTOGRAM only
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
static lv_obj_t *s_logIndicator = NULL; // small dot on lv_layer_sys() - persists across every page without needing to be added to each one individually, same mechanism LVGL's own built-in perf monitor uses

static const lv_font_t *get_font_for_size(int32_t size) {
  if (size <= 14) return &lv_font_montserrat_14;
  if (size <= 28) return &lv_font_montserrat_28;
  return &lv_font_montserrat_36;
}

static void gesture_event_cb(lv_event_t *e);

static void switch_to_page(int index) {
  s_current_page = index;
  lv_disp_load_scr(pages[index].page);
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
  rt->meter = NULL;
  rt->needle = NULL;
  rt->numberLabel = NULL;
  rt->chart = NULL;
  rt->chartSeries = NULL;

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
  const lv_font_t *tickFont = get_font_for_size(cfg->value_font_size);

  if (cfg->gauge_type == GAUGE_TYPE_NUMBER) {
    lv_obj_t *numLabel = lv_label_create(container);
    lv_obj_set_style_text_color(numLabel, lv_color_white(), 0);
    lv_obj_set_style_text_font(numLabel, tickFont, 0);
    lv_label_set_text(numLabel, "--");
    lv_obj_center(numLabel);
    rt->numberLabel = numLabel;

    lv_obj_t *capLabel = lv_label_create(container);
    lv_obj_set_style_text_color(capLabel, lv_color_hex(0x4dd0e1), 0);
    lv_label_set_text(capLabel, cfg->caption);
    lv_obj_align_to(capLabel, numLabel, LV_ALIGN_OUT_TOP_MID, 0, -4);
  } else if (cfg->gauge_type == GAUGE_TYPE_HISTOGRAM) {
    lv_obj_t *chart = lv_chart_create(container);
    lv_obj_set_size(chart, rect.w - 10, rect.h - 30);
    lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 4);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 50); // rolling window - oldest points scroll off as new ones arrive
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, cfg->min_val, cfg->max_val);
    lv_obj_set_style_bg_color(chart, lv_color_hex(cfg->bg_color), 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_border_color(chart, tickColor, 0);
    lv_chart_series_t *series = lv_chart_add_series(chart, needleColor, LV_CHART_AXIS_PRIMARY_Y);
    rt->chart = chart;
    rt->chartSeries = series;

    lv_obj_t *capLabel = lv_label_create(container);
    lv_obj_set_style_text_color(capLabel, lv_color_hex(0x4dd0e1), 0);
    lv_label_set_text(capLabel, cfg->caption);
    lv_obj_align(capLabel, LV_ALIGN_BOTTOM_MID, 0, -4);
  } else {
    // NEEDLE or HALFMOON
    int32_t angleRange = (cfg->gauge_type == GAUGE_TYPE_HALFMOON) ? 180 : 270;
    int32_t rotation = (cfg->gauge_type == GAUGE_TYPE_HALFMOON) ? 180 : 135;
    int16_t meterSize = (int16_t)(minDim * 0.88f);

    lv_obj_t *meter = lv_meter_create(container);
    lv_obj_set_size(meter, meterSize, meterSize);
    lv_obj_align(meter, LV_ALIGN_CENTER, 0, (cfg->gauge_type == GAUGE_TYPE_HALFMOON) ? (meterSize / 6) : 0);
    lv_obj_set_style_bg_color(meter, lv_color_hex(cfg->bg_color), 0);
    lv_obj_set_style_border_width(meter, 0, 0);

    lv_meter_scale_t *scale = lv_meter_add_scale(meter);
    lv_meter_set_scale_ticks(meter, scale, cfg->tick_count, 2, 10, tickColor);
    lv_meter_set_scale_major_ticks(meter, scale, cfg->major_nth, 4, 18, tickColor, 25);
    lv_meter_set_scale_range(meter, scale, cfg->min_val, cfg->max_val, angleRange, rotation);
    lv_obj_set_style_text_font(meter, tickFont, LV_PART_TICKS);
    lv_obj_set_style_text_color(meter, tickColor, LV_PART_TICKS);

    lv_meter_indicator_t *needle = lv_meter_add_needle_line(meter, scale, 6, needleColor, -12);
    lv_meter_set_indicator_value(meter, needle, cfg->rest_val);
    rt->meter = meter;
    rt->needle = needle;

    lv_obj_t *capLabel = lv_label_create(container);
    lv_obj_set_style_text_color(capLabel, lv_color_white(), 0);
    lv_label_set_text(capLabel, cfg->caption);
    lv_obj_align(capLabel, LV_ALIGN_BOTTOM_MID, 0, -8);

    if (cfg->show_value) {
      lv_obj_t *valueLabel = lv_label_create(container);
      lv_obj_set_style_text_color(valueLabel, lv_color_white(), 0);
      lv_obj_set_style_text_font(valueLabel, get_font_for_size(cfg->value_font_size), 0);
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

static void gauge_update_timer_cb(lv_timer_t *timer) {
  s_flash_counter++;

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
    int next = (s_current_page + g_page_nav_delta + s_num_active_pages) % s_num_active_pages;
    switch_to_page(next);
    g_page_nav_delta = 0;
  }

  uint32_t now = lv_tick_get();
  for (int p = 0; p < s_num_active_pages; p++) {
    for (int i = 0; i < pages[p].numCells; i++) {
      CellRuntime *c = &pages[p].cells[i];
      if (!c->enabled) continue;

      float value = gauge_config_signal_value(c->signal) / c->value_divisor;
      bool has_data = gauge_config_signal_has_data(c->signal);

      bool shouldUpdate = !has_data || (now - c->last_update_ms >= c->refresh_interval_ms);
      if (!shouldUpdate) continue;
      if (has_data) c->last_update_ms = now;

      int32_t displayValue = clamped_or_sweep(c, value, has_data);

      if (c->gauge_type == GAUGE_TYPE_NUMBER) {
        if (c->numberLabel) {
          char buf[16];
          snprintf(buf, sizeof(buf), "%.1f", value);
          lv_label_set_text(c->numberLabel, buf);
        }
      } else if (c->gauge_type == GAUGE_TYPE_HISTOGRAM) {
        if (c->chart && c->chartSeries) {
          lv_chart_set_next_value(c->chart, c->chartSeries, displayValue);
        }
      } else {
        if (c->meter) lv_meter_set_indicator_value(c->meter, c->needle, displayValue);
        update_warning_flash(c, value, has_data);
        if (c->numberLabel) { // the optional "show_value" readout for NEEDLE/HALFMOON
          char buf[16];
          snprintf(buf, sizeof(buf), "%.1f", value);
          lv_label_set_text(c->numberLabel, buf);
        }
      }
    }
  }
}

int gauge_app_get_num_pages(void) {
  return s_num_active_pages;
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

  // Logging indicator - a small circle on the system layer, so it
  // shows on top of whichever page is currently active without being
  // added to each page individually.
  s_logIndicator = lv_obj_create(lv_layer_sys());
  lv_obj_set_size(s_logIndicator, 14, 14);
  lv_obj_set_style_radius(s_logIndicator, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s_logIndicator, 0, 0);
  lv_obj_set_style_bg_color(s_logIndicator, lv_color_hex(0x00C800), 0);
  lv_obj_align(s_logIndicator, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_add_flag(s_logIndicator, LV_OBJ_FLAG_HIDDEN); // hidden until logging actually starts

  lv_timer_create(gauge_update_timer_cb, 20, NULL); // 20ms = 50Hz, fastest per-cell refresh rate supported
}
