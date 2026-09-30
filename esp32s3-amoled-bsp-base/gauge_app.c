/*
  gauge_app.c
  Eight pages total:
    0: CLT         65-250 F   - warning if >220
    1: MAP         0-250 kPa  - no warning, rests at 100 (ambient)
    2: AFR         10-20      - warning if >15
    3: OIL PRESS   0-100 kPa  - warning if <40
    4: OIL TEMP    65-250 F   - warning if >220
    5: FUEL PRESS  0-100 kPa  - warning if <40
    6: ETH %       0-100      - no warning
    7: Numbers grid - all 6 sensor values as plain digital readouts

  Style: black background, white ticks/numbers/needle, 270-degree
  sweep, caption in the bottom gap.

  WARNING FLASH: pages with a configured threshold flash their entire
  background between black and red while that value is past threshold
  (a simple phase counter in the shared 80ms update timer, no separate
  LVGL animation needed). Only evaluated once real data has arrived for
  that signal (has_data flag) - a gauge sitting at its rest/default
  value never falsely triggers.

  Manual navigation only (swipe or BOOT button) - no auto-cycle.

  All actual LVGL API calls happen either inside our periodic update
  timer (runs in LVGL's own task context via lv_timer_handler()) or
  inside the gesture event callback (also fires from within LVGL's task
  context) - never directly from Arduino's loop(), which only touches
  simple global values.
*/

#include "gauge_app.h"
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

volatile bool g_afr_has_data = false;
volatile bool g_clt_has_data = false;
volatile bool g_oilp_has_data = false;
volatile bool g_oilt_has_data = false;
volatile bool g_fuelp_has_data = false;
volatile bool g_flex_has_data = false;
volatile bool g_boost_has_data = false;

volatile int g_requested_page = -1;

#define NUM_PAGES 8

typedef struct {
  lv_obj_t *page;
  lv_obj_t *meter;
  lv_meter_indicator_t *needle;
  int32_t min_val;
  int32_t max_val;
  int32_t rest_val; // unused now that sweep replaces the static rest state, kept for reference
  bool has_warning;
  float warning_threshold;
  bool warning_above; // true: warn when value > threshold, false: warn when value < threshold
  bool warning_active; // current flash state, tracked so we only touch bg_color on actual changes
  int32_t sweep_pos; // current needle position while no real data has arrived yet
  int32_t sweep_step; // how far the needle moves per update tick while sweeping
  int8_t sweep_dir; // +1 or -1
} GaugePage;

static GaugePage pages[NUM_PAGES];
static int s_current_page = 0;
static uint32_t s_flash_counter = 0;

// Numbers-grid page (page 7) value labels
static lv_obj_t *g_afr_cell, *g_clt_cell, *g_oilt_cell, *g_oilp_cell, *g_fuelp_cell, *g_flex_cell;

static void gesture_event_cb(lv_event_t *e);

static void switch_to_page(int index) {
  s_current_page = index;
  lv_disp_load_scr(pages[index].page);
}

static void build_full_gauge_page(int index, int32_t min_val, int32_t max_val, int32_t rest_val,
                                   const char *caption, bool has_warning, float warning_threshold, bool warning_above) {
  lv_obj_t *page = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(page, lv_color_black(), 0);

  lv_obj_t *meter = lv_meter_create(page);
  lv_obj_set_size(meter, 460, 460); // near-full-screen on the 466x466 panel
  lv_obj_center(meter);
  lv_obj_set_style_bg_color(meter, lv_color_black(), 0);
  lv_obj_set_style_border_width(meter, 0, 0);

  lv_meter_scale_t *scale = lv_meter_add_scale(meter);
  lv_meter_set_scale_ticks(meter, scale, 26, 2, 10, lv_color_white());
  lv_meter_set_scale_major_ticks(meter, scale, 5, 4, 18, lv_color_white(), 25);
  lv_meter_set_scale_range(meter, scale, min_val, max_val, 270, 135);

  lv_obj_set_style_text_font(meter, &lv_font_montserrat_28, LV_PART_TICKS);
  lv_obj_set_style_text_color(meter, lv_color_white(), LV_PART_TICKS);

  lv_meter_indicator_t *needle = lv_meter_add_needle_line(meter, scale, 8, lv_color_white(), -15);
  lv_meter_set_indicator_value(meter, needle, rest_val);

  lv_obj_t *capLabel = lv_label_create(page);
  lv_obj_set_style_text_color(capLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(capLabel, &lv_font_montserrat_28, 0); // bigger caption text per request
  lv_label_set_text(capLabel, caption);
  lv_obj_align(capLabel, LV_ALIGN_BOTTOM_MID, 0, -70);

  lv_obj_add_event_cb(page, gesture_event_cb, LV_EVENT_GESTURE, NULL);

  pages[index].page = page;
  pages[index].meter = meter;
  pages[index].needle = needle;
  pages[index].min_val = min_val;
  pages[index].max_val = max_val;
  pages[index].rest_val = rest_val;
  pages[index].has_warning = has_warning;
  pages[index].warning_threshold = warning_threshold;
  pages[index].warning_above = warning_above;
  pages[index].warning_active = false;

  // Sweep animation setup - aims for roughly a 2 second one-way sweep
  // at the 80ms update rate (25 ticks), with a floor of 1 so very
  // small ranges (e.g. AFR's 10-20) still move visibly each tick.
  pages[index].sweep_pos = min_val;
  pages[index].sweep_dir = 1;
  int32_t step = (max_val - min_val) / 25;
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

static void build_numbers_page(int index) {
  lv_obj_t *page = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(page, lv_color_black(), 0);

  const int16_t colX[2] = {70, 260};
  const int16_t rowY[3] = {90, 210, 330};

  g_clt_cell   = make_grid_cell(page, colX[0], rowY[0], "CLT (F)");
  g_afr_cell   = make_grid_cell(page, colX[1], rowY[0], "AFR");
  g_oilp_cell  = make_grid_cell(page, colX[0], rowY[1], "OIL PRESS (kPa)");
  g_oilt_cell  = make_grid_cell(page, colX[1], rowY[1], "OIL TEMP (F)");
  g_fuelp_cell = make_grid_cell(page, colX[0], rowY[2], "FUEL PRESS (kPa)");
  g_flex_cell  = make_grid_cell(page, colX[1], rowY[2], "ETH %");

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
    switch_to_page((s_current_page + 1) % NUM_PAGES);
  } else if (dir == LV_DIR_RIGHT) {
    switch_to_page((s_current_page + NUM_PAGES - 1) % NUM_PAGES);
  }
}

// Clamps a value into a gauge's own int32 range for lv_meter, or - if
// no real data has arrived yet for that signal - advances and returns
// a back-and-forth sweep position instead of a static rest value. Once
// has_data goes true (a one-way latch, set on first real CAN frame),
// this always returns the clamped real value from then on.
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
static void update_warning_flash(int index, float value, bool has_data) {
  GaugePage *p = &pages[index];
  if (!p->has_warning) return;

  bool triggered = false;
  if (has_data) {
    triggered = p->warning_above ? (value > p->warning_threshold) : (value < p->warning_threshold);
  }

  if (triggered) {
    // Flip phase roughly twice a second (80ms timer * 6 ticks ~= 480ms per phase)
    bool flash_on = (s_flash_counter / 6) % 2 == 0;
    lv_color_t target = flash_on ? lv_color_hex(0xB00000) : lv_color_black();
    lv_obj_set_style_bg_color(p->page, target, 0);
    p->warning_active = true;
  } else if (p->warning_active) {
    lv_obj_set_style_bg_color(p->page, lv_color_black(), 0);
    p->warning_active = false;
  }
}

static void gauge_update_timer_cb(lv_timer_t *timer) {
  s_flash_counter++;

  // Handle a pending page-switch request (set from Arduino's loop() on
  // BOOT button press) - safe here since we're inside LVGL's own task.
  if (g_requested_page >= 0) {
    switch_to_page(g_requested_page);
    g_requested_page = -1;
  }

  lv_meter_set_indicator_value(pages[0].meter, pages[0].needle,
    clamped_or_sweep(&pages[0], g_clt_f, g_clt_has_data));
  update_warning_flash(0, g_clt_f, g_clt_has_data);

  lv_meter_set_indicator_value(pages[1].meter, pages[1].needle,
    clamped_or_sweep(&pages[1], g_boost_kpa, g_boost_has_data));

  lv_meter_set_indicator_value(pages[2].meter, pages[2].needle,
    clamped_or_sweep(&pages[2], g_afr, g_afr_has_data));
  update_warning_flash(2, g_afr, g_afr_has_data);

  lv_meter_set_indicator_value(pages[3].meter, pages[3].needle,
    clamped_or_sweep(&pages[3], g_oilp_kpa, g_oilp_has_data));
  update_warning_flash(3, g_oilp_kpa, g_oilp_has_data);

  lv_meter_set_indicator_value(pages[4].meter, pages[4].needle,
    clamped_or_sweep(&pages[4], g_oilt_f, g_oilt_has_data));
  update_warning_flash(4, g_oilt_f, g_oilt_has_data);

  lv_meter_set_indicator_value(pages[5].meter, pages[5].needle,
    clamped_or_sweep(&pages[5], g_fuelp_kpa, g_fuelp_has_data));
  update_warning_flash(5, g_fuelp_kpa, g_fuelp_has_data);

  lv_meter_set_indicator_value(pages[6].meter, pages[6].needle,
    clamped_or_sweep(&pages[6], g_flex_pct, g_flex_has_data));

  char buf[16];
  snprintf(buf, sizeof(buf), "%.0f", g_clt_f); lv_label_set_text(g_clt_cell, buf);
  snprintf(buf, sizeof(buf), "%.1f", g_afr); lv_label_set_text(g_afr_cell, buf);
  snprintf(buf, sizeof(buf), "%.0f", g_oilp_kpa); lv_label_set_text(g_oilp_cell, buf);
  snprintf(buf, sizeof(buf), "%.0f", g_oilt_f); lv_label_set_text(g_oilt_cell, buf);
  snprintf(buf, sizeof(buf), "%.0f", g_fuelp_kpa); lv_label_set_text(g_fuelp_cell, buf);
  snprintf(buf, sizeof(buf), "%.0f", g_flex_pct); lv_label_set_text(g_flex_cell, buf);
}

void build_gauge_app(void) {
  build_full_gauge_page(0, 65, 250, 65, "CLT", true, 220, true);
  build_full_gauge_page(1, 0, 250, 100, "MAP", false, 0, false);
  build_full_gauge_page(2, 10, 20, 10, "AFR", true, 15, true);
  build_full_gauge_page(3, 0, 100, 0, "OIL PRESS", true, 40, false);
  build_full_gauge_page(4, 65, 250, 65, "OIL TEMP", true, 220, true);
  build_full_gauge_page(5, 0, 100, 0, "FUEL PRESS", true, 40, false);
  build_full_gauge_page(6, 0, 100, 0, "ETH %", false, 0, false);
  build_numbers_page(7);

  s_current_page = 0;
  lv_disp_load_scr(pages[0].page);

  lv_timer_create(gauge_update_timer_cb, 80, NULL);
}
