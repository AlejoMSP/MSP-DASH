/*
  gauge_app.c
  Six full-screen analog gauges (one per page), matching the classic
  automotive gauge look: black background, white ticks/numbers/needle,
  270-degree sweep with a caption in the bottom gap - same general
  layout as a typical aftermarket single gauge (e.g. Speedhut-style).

  Pages (swipe left/right, BOOT button, or auto-cycle every 5s to move
  between them):
    0: AFR          10-20
    1: Coolant Temp 65-300 F
    2: Oil Pressure 0-100 kPa
    3: Oil Temp     65-240 F
    4: Fuel Pressure 0-100 kPa
    5: Ethanol %    0-100

  "No data yet" handling: each signal has a has_data flag (set true the
  first time a real CAN frame updates it, from the .ino's CAN parsing).
  Until then, that gauge's needle sits at its own minimum rather than
  showing a possibly-misleading default value.

  Auto-cycle: advances to the next page every 5000ms. Manual navigation
  (swipe or BOOT button) resets this timer, so a manually-chosen page
  gets a full 5 seconds before auto-cycling resumes.

  All actual LVGL API calls happen either inside our periodic update
  timer (runs in LVGL's own task context via lv_timer_handler(), which
  lcd_bsp.c's port task already calls continuously) or inside the
  gesture event callback (also fires from within LVGL's task context) -
  never directly from Arduino's loop(), which only touches simple
  global values.
*/

#include "gauge_app.h"
#include "lvgl.h"
#include <stdbool.h>

volatile float g_afr = 0;
volatile float g_clt_f = 0;
volatile float g_oilp_kpa = 0;
volatile float g_oilt_f = 0;
volatile float g_fuelp_kpa = 0;
volatile float g_flex_pct = 0;

volatile bool g_afr_has_data = false;
volatile bool g_clt_has_data = false;
volatile bool g_oilp_has_data = false;
volatile bool g_oilt_has_data = false;
volatile bool g_fuelp_has_data = false;
volatile bool g_flex_has_data = false;

volatile int g_requested_page = -1;

#define NUM_PAGES 6

typedef struct {
  lv_obj_t *page;
  lv_obj_t *meter;
  lv_meter_indicator_t *needle;
  int32_t min_val;
  int32_t max_val;
} GaugePage;

static GaugePage pages[NUM_PAGES];
static int s_current_page = 0;
static lv_timer_t *s_autocycle_timer = NULL;

static void gesture_event_cb(lv_event_t *e);

static void switch_to_page(int index) {
  s_current_page = index;
  lv_disp_load_scr(pages[index].page);
  if (s_autocycle_timer) {
    lv_timer_reset(s_autocycle_timer); // manual navigation gets a fresh 5s before auto-cycle resumes
  }
}

static void build_full_gauge_page(int index, int32_t min_val, int32_t max_val, const char *caption) {
  lv_obj_t *page = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(page, lv_color_black(), 0);

  lv_obj_t *meter = lv_meter_create(page);
  lv_obj_set_size(meter, 460, 460); // near-full-screen on the 466x466 panel
  lv_obj_center(meter);
  lv_obj_set_style_bg_color(meter, lv_color_black(), 0);
  lv_obj_set_style_border_width(meter, 0, 0);

  lv_meter_scale_t *scale = lv_meter_add_scale(meter);
  // 26 ticks = 5 major-to-major intervals (6 major labels), 4 minor
  // ticks between each - consistent density across all 6 gauges
  // regardless of their differing numeric ranges.
  lv_meter_set_scale_ticks(meter, scale, 26, 2, 10, lv_color_white());
  lv_meter_set_scale_major_ticks(meter, scale, 5, 4, 18, lv_color_white(), 15);
  lv_meter_set_scale_range(meter, scale, min_val, max_val, 270, 135);

  lv_meter_indicator_t *needle = lv_meter_add_needle_line(meter, scale, 5, lv_color_white(), -15);
  lv_meter_set_indicator_value(meter, needle, min_val); // starts at minimum until real data arrives

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

static void autocycle_timer_cb(lv_timer_t *timer) {
  switch_to_page((s_current_page + 1) % NUM_PAGES);
}

// Clamps a value into a gauge's own int32 range for lv_meter, or
// returns the gauge's minimum if no real data has arrived yet.
static int32_t clamped_or_min(float value, bool has_data, int32_t min_val, int32_t max_val) {
  if (!has_data) return min_val;
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
    clamped_or_min(g_afr, g_afr_has_data, pages[0].min_val, pages[0].max_val));
  lv_meter_set_indicator_value(pages[1].meter, pages[1].needle,
    clamped_or_min(g_clt_f, g_clt_has_data, pages[1].min_val, pages[1].max_val));
  lv_meter_set_indicator_value(pages[2].meter, pages[2].needle,
    clamped_or_min(g_oilp_kpa, g_oilp_has_data, pages[2].min_val, pages[2].max_val));
  lv_meter_set_indicator_value(pages[3].meter, pages[3].needle,
    clamped_or_min(g_oilt_f, g_oilt_has_data, pages[3].min_val, pages[3].max_val));
  lv_meter_set_indicator_value(pages[4].meter, pages[4].needle,
    clamped_or_min(g_fuelp_kpa, g_fuelp_has_data, pages[4].min_val, pages[4].max_val));
  lv_meter_set_indicator_value(pages[5].meter, pages[5].needle,
    clamped_or_min(g_flex_pct, g_flex_has_data, pages[5].min_val, pages[5].max_val));
}

void build_gauge_app(void) {
  build_full_gauge_page(0, 10, 20, "AFR");
  build_full_gauge_page(1, 65, 300, "COOLANT TEMP (F)");
  build_full_gauge_page(2, 0, 100, "OIL PRESSURE (kPa)");
  build_full_gauge_page(3, 65, 240, "OIL TEMP (F)");
  build_full_gauge_page(4, 0, 100, "FUEL PRESSURE (kPa)");
  build_full_gauge_page(5, 0, 100, "ETHANOL %");

  s_current_page = 0;
  lv_disp_load_scr(pages[0].page);

  lv_timer_create(gauge_update_timer_cb, 80, NULL);
  s_autocycle_timer = lv_timer_create(autocycle_timer_cb, 5000, NULL);
}
