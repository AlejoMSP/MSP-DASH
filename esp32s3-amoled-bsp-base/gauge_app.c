/*
  gauge_app.c
  Data-driven gauge pages - built from gauge_config.c's persisted slot
  array (signal assignment, range, needle color, caption, warning
  threshold) instead of hardcoded literals. Up to MAX_GAUGE_SLOTS
  enabled gauge pages, plus one numbers-grid page showing all enabled
  signals as plain digital readouts.

  Style: black background, ticks/numbers/needle in each slot's
  configured color, 270-degree sweep, caption in the bottom gap.

  WARNING FLASH: pages with a configured threshold flash their entire
  background between black and red while that value is past threshold
  (a simple phase counter in the shared 80ms update timer). Only
  evaluated once real data has arrived for that signal (has_data flag).

  SWEEP: while no real data has arrived yet for a slot's signal, its
  needle sweeps back and forth between min and max instead of sitting
  static, so an unconnected/idle gauge is visually distinct from one
  genuinely reading its rest value.

  Manual navigation only (swipe or BOOT button) - no auto-cycle.

  All actual LVGL API calls happen either inside our periodic update
  timer (runs in LVGL's own task context via lv_timer_handler()) or
  inside the gesture event callback (also fires from within LVGL's task
  context) - never directly from Arduino's loop(), which only touches
  simple global values.
*/

#include "gauge_app.h"
#include "gauge_config.h"
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

#define MAX_PAGES (MAX_GAUGE_SLOTS + 1) // +1 for the numbers grid

typedef struct {
  lv_obj_t *page;
  lv_obj_t *meter;
  lv_meter_indicator_t *needle;
  SignalId signal;
  int32_t min_val;
  int32_t max_val;
  float value_divisor; // live value divided by this before display - e.g. 1000 for "RPM x1000"
  uint32_t bg_color; // the "off" state for warning flash - was previously always assumed black
  bool has_warning;
  float warning_threshold;
  bool warning_above;
  bool warning_active;
  int32_t sweep_pos;
  int32_t sweep_step;
  int8_t sweep_dir;
} GaugePage;

static GaugePage pages[MAX_PAGES];
static int s_num_active_pages = 0;
static int s_current_page = 0;
static uint32_t s_flash_counter = 0;

// Numbers-grid page value labels, one per possible slot
static lv_obj_t *s_grid_value_labels[MAX_GAUGE_SLOTS];
static SignalId s_grid_signals[MAX_GAUGE_SLOTS];
static float s_grid_divisors[MAX_GAUGE_SLOTS];
static int s_grid_count = 0;

static void gesture_event_cb(lv_event_t *e);

static void switch_to_page(int index) {
  s_current_page = index;
  lv_disp_load_scr(pages[index].page);
}

static void build_full_gauge_page(int index, GaugeSlotConfig *cfg) {
  lv_color_t bgColor = lv_color_hex(cfg->bg_color);
  lv_color_t tickColor = lv_color_hex(cfg->tick_color);
  lv_color_t needleColor = lv_color_hex(cfg->needle_color);

  lv_obj_t *page = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(page, bgColor, 0);

  lv_obj_t *meter = lv_meter_create(page);
  lv_obj_set_size(meter, 460, 460); // near-full-screen on the 466x466 panel
  lv_obj_center(meter);
  lv_obj_set_style_bg_color(meter, bgColor, 0);
  lv_obj_set_style_border_width(meter, 0, 0);

  lv_meter_scale_t *scale = lv_meter_add_scale(meter);
  lv_meter_set_scale_ticks(meter, scale, cfg->tick_count, 2, 10, tickColor);
  lv_meter_set_scale_major_ticks(meter, scale, cfg->major_nth, 4, 18, tickColor, 25);
  lv_meter_set_scale_range(meter, scale, cfg->min_val, cfg->max_val, 270, 135);

  lv_obj_set_style_text_font(meter, &lv_font_montserrat_36, LV_PART_TICKS); // bumped from 28 per request
  lv_obj_set_style_text_color(meter, tickColor, LV_PART_TICKS);

  lv_meter_indicator_t *needle = lv_meter_add_needle_line(meter, scale, 8, needleColor, -15);
  lv_meter_set_indicator_value(meter, needle, cfg->rest_val);

  lv_obj_t *capLabel = lv_label_create(page);
  lv_obj_set_style_text_color(capLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(capLabel, &lv_font_montserrat_28, 0); // caption stays at 28 - 36 was reserved for the tick numbers specifically, per request
  lv_label_set_text(capLabel, cfg->caption);
  lv_obj_align(capLabel, LV_ALIGN_BOTTOM_MID, 0, -70);

  lv_obj_add_event_cb(page, gesture_event_cb, LV_EVENT_GESTURE, NULL);

  pages[index].page = page;
  pages[index].meter = meter;
  pages[index].needle = needle;
  pages[index].signal = cfg->signal;
  pages[index].min_val = cfg->min_val;
  pages[index].max_val = cfg->max_val;
  pages[index].value_divisor = cfg->value_divisor;
  pages[index].bg_color = cfg->bg_color;
  pages[index].has_warning = cfg->has_warning;
  pages[index].warning_threshold = cfg->warning_threshold;
  pages[index].warning_above = cfg->warning_above;
  pages[index].warning_active = false;

  pages[index].sweep_pos = cfg->min_val;
  pages[index].sweep_dir = 1;
  int32_t step = (cfg->max_val - cfg->min_val) / 25;
  pages[index].sweep_step = step < 1 ? 1 : step;
}

static lv_obj_t *make_grid_cell(lv_obj_t *parent, int16_t x, int16_t y, const char *caption) {
  lv_obj_t *cap = lv_label_create(parent);
  lv_obj_set_style_text_color(cap, lv_color_white(), 0);
  lv_label_set_text(cap, caption);
  lv_obj_set_pos(cap, x, y);

  lv_obj_t *val = lv_label_create(parent);
  lv_obj_set_style_text_color(val, lv_color_white(), 0);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_28, 0);
  lv_label_set_text(val, "--");
  lv_obj_set_pos(val, x, y + 24);
  return val;
}

static void build_numbers_page(int index, GaugeSlotConfig *slots, int slotCount) {
  lv_obj_t *page = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(page, lv_color_black(), 0);

  const int16_t colX[2] = {70, 260};
  const int16_t rowY[4] = {70, 170, 270, 370}; // up to 4 rows (8 cells) to fit MAX_GAUGE_SLOTS

  s_grid_count = 0;
  for (int i = 0; i < slotCount && s_grid_count < MAX_GAUGE_SLOTS; i++) {
    if (!slots[i].enabled) continue;
    int col = s_grid_count % 2;
    int row = s_grid_count / 2;
    if (row >= 4) break; // grid layout only fits 8 (2x4)
    s_grid_value_labels[s_grid_count] = make_grid_cell(page, colX[col], rowY[row], slots[i].caption);
    s_grid_signals[s_grid_count] = slots[i].signal;
    s_grid_divisors[s_grid_count] = slots[i].value_divisor;
    s_grid_count++;
  }

  lv_obj_add_event_cb(page, gesture_event_cb, LV_EVENT_GESTURE, NULL);
  pages[index].page = page;
  pages[index].meter = NULL; // no meter on this page
  pages[index].has_warning = false;
}

// Fires from within LVGL's own task context - safe to call
// lv_disp_load_scr() directly here.
static void gesture_event_cb(lv_event_t *e) {
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
  if (dir == LV_DIR_LEFT) {
    switch_to_page((s_current_page + 1) % s_num_active_pages);
  } else if (dir == LV_DIR_RIGHT) {
    switch_to_page((s_current_page + s_num_active_pages - 1) % s_num_active_pages);
  }
}

// Clamps a value into a gauge's own int32 range for lv_meter, or - if
// no real data has arrived yet for that signal - advances and returns
// a back-and-forth sweep position instead.
static int32_t clamped_or_sweep(GaugePage *p, float value, bool has_data) {
  if (has_data) {
    int32_t v = (int32_t)value;
    if (v < p->min_val) v = p->min_val;
    if (v > p->max_val) v = p->max_val;
    return v;
  }

  p->sweep_pos += p->sweep_dir * p->sweep_step;
  if (p->sweep_pos >= p->max_val) {
    p->sweep_pos = p->max_val;
    p->sweep_dir = -1;
  } else if (p->sweep_pos <= p->min_val) {
    p->sweep_pos = p->min_val;
    p->sweep_dir = 1;
  }
  return p->sweep_pos;
}

// Checks a page's warning condition against the live value (only when
// real data has arrived) and updates its flashing background state.
static void update_warning_flash(GaugePage *p, float value, bool has_data) {
  if (!p->has_warning) return;

  bool triggered = false;
  if (has_data) {
    triggered = p->warning_above ? (value > p->warning_threshold) : (value < p->warning_threshold);
  }

  if (triggered) {
    bool flash_on = (s_flash_counter / 6) % 2 == 0;
    lv_color_t target = flash_on ? lv_color_hex(0xB00000) : lv_color_hex(p->bg_color);
    lv_obj_set_style_bg_color(p->page, target, 0);
    p->warning_active = true;
  } else if (p->warning_active) {
    lv_obj_set_style_bg_color(p->page, lv_color_hex(p->bg_color), 0);
    p->warning_active = false;
  }
}

static void gauge_update_timer_cb(lv_timer_t *timer) {
  s_flash_counter++;

  if (g_requested_page >= 0 && g_requested_page < s_num_active_pages) {
    switch_to_page(g_requested_page);
    g_requested_page = -1;
  }

  for (int i = 0; i < s_num_active_pages; i++) {
    GaugePage *p = &pages[i];
    if (p->meter == NULL) continue; // numbers grid page has no meter
    float value = gauge_config_signal_value(p->signal) / p->value_divisor;
    bool has_data = gauge_config_signal_has_data(p->signal);
    lv_meter_set_indicator_value(p->meter, p->needle, clamped_or_sweep(p, value, has_data));
    update_warning_flash(p, value, has_data);
  }

  char buf[16];
  for (int i = 0; i < s_grid_count; i++) {
    float value = gauge_config_signal_value(s_grid_signals[i]) / s_grid_divisors[i];
    snprintf(buf, sizeof(buf), "%.1f", value);
    lv_label_set_text(s_grid_value_labels[i], buf);
  }
}

int gauge_app_get_num_pages(void) {
  return s_num_active_pages;
}

void build_gauge_app(void) {
  GaugeSlotConfig *slots = gauge_config_get_slots();

  int pageIdx = 0;
  for (int i = 0; i < MAX_GAUGE_SLOTS; i++) {
    if (!slots[i].enabled) continue;
    build_full_gauge_page(pageIdx, &slots[i]);
    pageIdx++;
  }
  build_numbers_page(pageIdx, slots, MAX_GAUGE_SLOTS);
  pageIdx++;

  s_num_active_pages = pageIdx;
  s_current_page = 0;
  lv_disp_load_scr(pages[0].page);

  lv_timer_create(gauge_update_timer_cb, 80, NULL);
}
