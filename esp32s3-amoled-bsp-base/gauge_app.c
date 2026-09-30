/*
  gauge_app.c
  Eight pages total:
    0: AFR           10-20
    1: Coolant Temp  65-300 F
    2: Oil Pressure  0-100 kPa
    3: Oil Temp      65-240 F
    4: Fuel Pressure 0-100 kPa
    5: Ethanol %     0-100
    6: Numbers grid  - all 6 values above, as plain digital readouts
    7: Boost/Vacuum  - single kPa scale (see note below), rests at 0

  Style: black background, white ticks/numbers/needle, 270-degree
  sweep, caption in the bottom gap - matching a classic analog
  automotive gauge look.

  BOOST/VACUUM UNIT NOTE: true inHg (vacuum side) and PSI (boost side)
  are different units meeting at a zero point. lv_meter's built-in tick
  labels only support one continuous linear numeric axis (plain
  auto-generated integers) - not two different units on either side of
  a scale. Rather than risk something broken/misleading, this gauge
  uses kPa (the unit our CAN math already computes directly) as a single
  consistent scale, captioned clearly as "BOOST/VACUUM (kPa)". A true
  dual-unit tick face would need a custom per-tick label override, a
  larger separate task.

  Manual navigation only now (swipe or BOOT button) - the earlier
  5-second auto-cycle has been removed per request.

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

#define NUM_GAUGE_PAGES 7 // pages 0-5 are single gauges, page 6 is the numbers grid; page 7 (boost) added separately below
#define NUM_PAGES 8

typedef struct {
  lv_obj_t *page;
  lv_obj_t *meter;
  lv_meter_indicator_t *needle;
  int32_t min_val;
  int32_t max_val;
  int32_t rest_val; // shown until real data arrives - usually min_val, but 0 for boost/vacuum
} GaugePage;

static GaugePage pages[NUM_PAGES];
static int s_current_page = 0;

// Numbers-grid page (page 6) value labels
static lv_obj_t *g_afr_cell, *g_clt_cell, *g_oilt_cell, *g_oilp_cell, *g_fuelp_cell, *g_flex_cell;

static void gesture_event_cb(lv_event_t *e);

static void switch_to_page(int index) {
  s_current_page = index;
  lv_disp_load_scr(pages[index].page);
}

static void build_full_gauge_page(int index, int32_t min_val, int32_t max_val, int32_t rest_val, const char *caption) {
  lv_obj_t *page = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(page, lv_color_black(), 0);

  lv_obj_t *meter = lv_meter_create(page);
  lv_obj_set_size(meter, 460, 460); // near-full-screen on the 466x466 panel
  lv_obj_center(meter);
  lv_obj_set_style_bg_color(meter, lv_color_black(), 0);
  lv_obj_set_style_border_width(meter, 0, 0);

  lv_meter_scale_t *scale = lv_meter_add_scale(meter);
  // 26 ticks = 5 major-to-major intervals (6 major labels), 4 minor
  // ticks between each - consistent density across all gauges
  // regardless of their differing numeric ranges.
  lv_meter_set_scale_ticks(meter, scale, 26, 2, 10, lv_color_white());
  // label_gap increased (was 15) - numbers sit further out from the
  // tick marks for clearer separation.
  lv_meter_set_scale_major_ticks(meter, scale, 5, 4, 18, lv_color_white(), 25);
  lv_meter_set_scale_range(meter, scale, min_val, max_val, 270, 135);

  // Bigger, brighter tick numbers (per reference image) - LV_PART_TICKS
  // is the confirmed part lv_meter actually uses to draw its tick
  // labels (checked against lv_meter.c source directly).
  lv_obj_set_style_text_font(meter, &lv_font_montserrat_28, LV_PART_TICKS);
  lv_obj_set_style_text_color(meter, lv_color_white(), LV_PART_TICKS);

  // needle width increased (was 5) - thicker needle per request
  lv_meter_indicator_t *needle = lv_meter_add_needle_line(meter, scale, 8, lv_color_white(), -15);
  lv_meter_set_indicator_value(meter, needle, rest_val);

  lv_obj_t *capLabel = lv_label_create(page);
  lv_obj_set_style_text_color(capLabel, lv_color_white(), 0);
  lv_label_set_text(capLabel, caption);
  lv_obj_align(capLabel, LV_ALIGN_BOTTOM_MID, 0, -70);

  lv_obj_add_event_cb(page, gesture_event_cb, LV_EVENT_GESTURE, NULL);

  pages[index].page = page;
  pages[index].meter = meter;
  pages[index].needle = needle;
  pages[index].min_val = min_val;
  pages[index].max_val = max_val;
  pages[index].rest_val = rest_val;
}

static lv_obj_t *make_grid_cell(lv_obj_t *parent, int16_t x, int16_t y, const char *caption) {
  lv_obj_t *cap = lv_label_create(parent);
  lv_obj_set_style_text_color(cap, lv_color_white(), 0);
  lv_label_set_text(cap, caption);
  lv_obj_set_pos(cap, x, y);

  lv_obj_t *val = lv_label_create(parent);
  lv_obj_set_style_text_color(val, lv_color_white(), 0);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_28, 0); // bigger numbers per request
  lv_label_set_text(val, "--");
  lv_obj_set_pos(val, x, y + 24); // slightly more vertical clearance for the larger font
  return val;
}

static void build_numbers_page(int index) {
  lv_obj_t *page = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(page, lv_color_black(), 0);

  const int16_t colX[2] = {70, 260};
  const int16_t rowY[3] = {90, 210, 330};

  g_afr_cell   = make_grid_cell(page, colX[0], rowY[0], "AFR");
  g_clt_cell   = make_grid_cell(page, colX[1], rowY[0], "CLT (F)");
  g_oilt_cell  = make_grid_cell(page, colX[0], rowY[1], "OIL T (F)");
  g_oilp_cell  = make_grid_cell(page, colX[1], rowY[1], "OIL P (kPa)");
  g_fuelp_cell = make_grid_cell(page, colX[0], rowY[2], "FUEL P (kPa)");
  g_flex_cell  = make_grid_cell(page, colX[1], rowY[2], "FLEX %");

  lv_obj_add_event_cb(page, gesture_event_cb, LV_EVENT_GESTURE, NULL);
  pages[index].page = page;
  pages[index].meter = NULL; // no meter on this page
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

// Clamps a value into a gauge's own int32 range for lv_meter, or
// returns the gauge's configured rest value if no real data has
// arrived yet.
static int32_t clamped_or_rest(float value, bool has_data, int32_t min_val, int32_t max_val, int32_t rest_val) {
  if (!has_data) return rest_val;
  int32_t v = (int32_t)value;
  if (v < min_val) v = min_val;
  if (v > max_val) v = max_val;
  return v;
}

static void gauge_update_timer_cb(lv_timer_t *timer) {
  // Handle a pending page-switch request (set from Arduino's loop() on
  // BOOT button press) - safe here since we're inside LVGL's own task.
  if (g_requested_page >= 0) {
    switch_to_page(g_requested_page);
    g_requested_page = -1;
  }

  lv_meter_set_indicator_value(pages[0].meter, pages[0].needle,
    clamped_or_rest(g_afr, g_afr_has_data, pages[0].min_val, pages[0].max_val, pages[0].rest_val));
  lv_meter_set_indicator_value(pages[1].meter, pages[1].needle,
    clamped_or_rest(g_boost_kpa, g_boost_has_data, pages[1].min_val, pages[1].max_val, pages[1].rest_val));
  lv_meter_set_indicator_value(pages[2].meter, pages[2].needle,
    clamped_or_rest(g_clt_f, g_clt_has_data, pages[2].min_val, pages[2].max_val, pages[2].rest_val));
  lv_meter_set_indicator_value(pages[3].meter, pages[3].needle,
    clamped_or_rest(g_oilp_kpa, g_oilp_has_data, pages[3].min_val, pages[3].max_val, pages[3].rest_val));
  lv_meter_set_indicator_value(pages[4].meter, pages[4].needle,
    clamped_or_rest(g_oilt_f, g_oilt_has_data, pages[4].min_val, pages[4].max_val, pages[4].rest_val));
  lv_meter_set_indicator_value(pages[5].meter, pages[5].needle,
    clamped_or_rest(g_fuelp_kpa, g_fuelp_has_data, pages[5].min_val, pages[5].max_val, pages[5].rest_val));
  lv_meter_set_indicator_value(pages[6].meter, pages[6].needle,
    clamped_or_rest(g_flex_pct, g_flex_has_data, pages[6].min_val, pages[6].max_val, pages[6].rest_val));

  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", g_afr); lv_label_set_text(g_afr_cell, buf);
  snprintf(buf, sizeof(buf), "%.0f", g_clt_f); lv_label_set_text(g_clt_cell, buf);
  snprintf(buf, sizeof(buf), "%.0f", g_oilt_f); lv_label_set_text(g_oilt_cell, buf);
  snprintf(buf, sizeof(buf), "%.0f", g_oilp_kpa); lv_label_set_text(g_oilp_cell, buf);
  snprintf(buf, sizeof(buf), "%.0f", g_fuelp_kpa); lv_label_set_text(g_fuelp_cell, buf);
  snprintf(buf, sizeof(buf), "%.0f", g_flex_pct); lv_label_set_text(g_flex_cell, buf);
}

void build_gauge_app(void) {
  build_full_gauge_page(0, 10, 20, 10, "AFR");
  build_full_gauge_page(1, 0, 200, 100, "MAP / BOOST (kPa) - 100 = AMBIENT"); // 100 kPa = atmospheric/ambient pressure, not 0 - a raw MAP reading, not baro-subtracted
  build_full_gauge_page(2, 65, 300, 65, "COOLANT TEMP (F)");
  build_full_gauge_page(3, 0, 100, 0, "OIL PRESSURE (kPa)");
  build_full_gauge_page(4, 65, 240, 65, "OIL TEMP (F)");
  build_full_gauge_page(5, 0, 100, 0, "FUEL PRESSURE (kPa)");
  build_full_gauge_page(6, 0, 100, 0, "ETHANOL %");
  build_numbers_page(7);

  s_current_page = 0;
  lv_disp_load_scr(pages[0].page);

  lv_timer_create(gauge_update_timer_cb, 80, NULL);
  // Auto-cycle timer removed per request - manual navigation only now.
}
