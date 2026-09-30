/*
  gauge_app.c
  Multi-page gauge cluster built on LVGL v8's lv_meter widget (the
  classic all-in-one gauge widget removed in v9 - one of the reasons
  we pivoted to this confirmed-working Waveshare BSP foundation).

  Page 1: AFR meter (range 10-20) + precise numeric readout below
  Page 2: Coolant Temp + Oil Pressure as two independent half-moon
          (180 degree) meters side by side
  Page 3: 6-value grid - AFR/CLT, Oil Temp/Oil Press, Fuel Press/Flex%

  All actual LVGL API calls (needle updates, label text, page
  switching) happen from a single periodic lv_timer callback running
  INSIDE LVGL's own task context (registered here, ticking on its own -
  LVGL timers run automatically whenever lv_timer_handler() is called,
  which lcd_bsp.c's example_lvgl_port_task already does continuously).
  This avoids any cross-task LVGL API calls from Arduino's loop() -
  CAN parsing there only touches simple global values, never LVGL
  functions directly.

  NOTE on rotation angle: lv_meter_set_scale_range's rotation parameter
  convention (135 used below) is a best-effort match to the classic
  "opens at the bottom" automotive gauge look established elsewhere in
  this project - not independently verified against source for this
  exact API, may need visual adjustment.
*/

#include "gauge_app.h"
#include "lvgl.h"
#include <stdio.h>

volatile float g_afr = 14.7f;
volatile float g_clt_f = 0;
volatile float g_oilp_kpa = 0;
volatile float g_oilt_f = 0;
volatile float g_fuelp_kpa = 0;
volatile float g_flex_pct = 0;
volatile int g_requested_page = -1;

static lv_obj_t *page1, *page2, *page3;
static int s_current_page = 0; // tracked here so swipe gestures know which direction is "next"/"previous"

static lv_obj_t *afr_meter;
static lv_meter_indicator_t *afr_needle;
static lv_obj_t *afr_value_label;

static lv_obj_t *clt_meter, *oilp_meter;
static lv_meter_indicator_t *clt_needle, *oilp_needle;
static lv_obj_t *clt_value_label, *oilp_value_label;

static lv_obj_t *g_afr_cell, *g_clt_cell, *g_oilt_cell, *g_oilp_cell, *g_fuelp_cell, *g_flex_cell;

static lv_obj_t *make_grid_cell(lv_obj_t *parent, int16_t x, int16_t y, const char *caption) {
  lv_obj_t *cap = lv_label_create(parent);
  lv_obj_set_style_text_color(cap, lv_color_hex(0x4dd0e1), 0);
  lv_label_set_text(cap, caption);
  lv_obj_set_pos(cap, x, y);

  lv_obj_t *val = lv_label_create(parent);
  lv_obj_set_style_text_color(val, lv_color_white(), 0);
  lv_label_set_text(val, "--");
  lv_obj_set_pos(val, x, y + 20);
  return val;
}

static void build_page1(void) {
  page1 = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(page1, lv_color_black(), 0);

  afr_meter = lv_meter_create(page1);
  lv_obj_set_size(afr_meter, 400, 400);
  lv_obj_center(afr_meter);

  lv_meter_scale_t *scale = lv_meter_add_scale(afr_meter);
  lv_meter_set_scale_ticks(afr_meter, scale, 11, 2, 10, lv_color_hex(0x888888));
  lv_meter_set_scale_major_ticks(afr_meter, scale, 2, 4, 15, lv_color_white(), 10);
  lv_meter_set_scale_range(afr_meter, scale, 10, 20, 270, 135);

  afr_needle = lv_meter_add_needle_line(afr_meter, scale, 4, lv_color_hex(0xF44336), -10);
  lv_meter_set_indicator_value(afr_meter, afr_needle, 15);

  afr_value_label = lv_label_create(page1);
  lv_obj_set_style_text_color(afr_value_label, lv_color_white(), 0);
  lv_label_set_text(afr_value_label, "14.7");
  lv_obj_align(afr_value_label, LV_ALIGN_BOTTOM_MID, 0, -30);

  lv_obj_t *caption = lv_label_create(page1);
  lv_obj_set_style_text_color(caption, lv_color_hex(0x4dd0e1), 0);
  lv_label_set_text(caption, "AFR");
  lv_obj_align_to(caption, afr_value_label, LV_ALIGN_OUT_TOP_MID, 0, -4);
}

static void build_half_moon(lv_obj_t *parent, lv_obj_t **meterOut, lv_meter_indicator_t **needleOut,
                            lv_obj_t **labelOut, int32_t xOffset, int32_t rangeMin, int32_t rangeMax,
                            const char *caption, uint32_t needleColor) {
  lv_obj_t *meter = lv_meter_create(parent);
  lv_obj_set_size(meter, 220, 220);
  lv_obj_align(meter, LV_ALIGN_CENTER, xOffset, -20);

  lv_meter_scale_t *scale = lv_meter_add_scale(meter);
  lv_meter_set_scale_ticks(meter, scale, 6, 2, 8, lv_color_hex(0x888888));
  lv_meter_set_scale_major_ticks(meter, scale, 1, 3, 12, lv_color_white(), 8);
  // Half-moon: 180 degree sweep, rotation 180 to open upward (best-effort,
  // same caveat as the full gauge's rotation value - visual verification needed)
  lv_meter_set_scale_range(meter, scale, rangeMin, rangeMax, 180, 180);

  lv_meter_indicator_t *needle = lv_meter_add_needle_line(meter, scale, 3, lv_color_hex(needleColor), -8);
  lv_meter_set_indicator_value(meter, needle, rangeMin);

  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_label_set_text(label, "0");
  lv_obj_align_to(label, meter, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  lv_obj_t *capLabel = lv_label_create(parent);
  lv_obj_set_style_text_color(capLabel, lv_color_hex(0x4dd0e1), 0);
  lv_label_set_text(capLabel, caption);
  lv_obj_align_to(capLabel, label, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

  *meterOut = meter;
  *needleOut = needle;
  *labelOut = label;
}

static void build_page2(void) {
  page2 = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(page2, lv_color_black(), 0);

  build_half_moon(page2, &clt_meter, &clt_needle, &clt_value_label, -120, 0, 260, "CLT (F)", 0xF44336);
  build_half_moon(page2, &oilp_meter, &oilp_needle, &oilp_value_label, 120, 0, 700, "OIL P (kPa)", 0x2196F3);
}

static void build_page3(void) {
  page3 = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(page3, lv_color_black(), 0);

  const int16_t colX[2] = {70, 260};
  const int16_t rowY[3] = {90, 210, 330};

  g_afr_cell   = make_grid_cell(page3, colX[0], rowY[0], "AFR");
  g_clt_cell   = make_grid_cell(page3, colX[1], rowY[0], "CLT (F)");
  g_oilt_cell  = make_grid_cell(page3, colX[0], rowY[1], "OIL T (F)");
  g_oilp_cell  = make_grid_cell(page3, colX[1], rowY[1], "OIL P (kPa)");
  g_fuelp_cell = make_grid_cell(page3, colX[0], rowY[2], "FUEL P (kPa)");
  g_flex_cell  = make_grid_cell(page3, colX[1], rowY[2], "FLEX %");
}

// Fires from WITHIN LVGL's own task context (gesture detection happens
// during lv_timer_handler()'s normal processing) - safe to call
// lv_disp_load_scr() directly here, unlike Arduino's loop() which must
// go through the g_requested_page flag instead.
static void gesture_event_cb(lv_event_t *e) {
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
  int next_page = s_current_page;

  if (dir == LV_DIR_LEFT) {
    next_page = (s_current_page + 1) % 3;
  } else if (dir == LV_DIR_RIGHT) {
    next_page = (s_current_page + 2) % 3; // +2 mod 3 == -1 mod 3, avoids negative modulo
  } else {
    return; // vertical or no clear gesture - ignore
  }

  s_current_page = next_page;
  if (next_page == 0) lv_disp_load_scr(page1);
  else if (next_page == 1) lv_disp_load_scr(page2);
  else lv_disp_load_scr(page3);
}

static void gauge_update_timer_cb(lv_timer_t *timer) {
  char buf[16];

  // Handle a pending page-switch request (set from Arduino's loop() on
  // BOOT button press) - safe here since we're inside LVGL's own task.
  if (g_requested_page >= 0) {
    if (g_requested_page == 0) lv_disp_load_scr(page1);
    else if (g_requested_page == 1) lv_disp_load_scr(page2);
    else lv_disp_load_scr(page3);
    s_current_page = g_requested_page;
    g_requested_page = -1;
  }

  // Page 1
  lv_meter_set_indicator_value(afr_meter, afr_needle, (int32_t)g_afr);
  snprintf(buf, sizeof(buf), "%.1f", g_afr);
  lv_label_set_text(afr_value_label, buf);

  // Page 2
  lv_meter_set_indicator_value(clt_meter, clt_needle, (int32_t)g_clt_f);
  snprintf(buf, sizeof(buf), "%.0f", g_clt_f);
  lv_label_set_text(clt_value_label, buf);
  lv_meter_set_indicator_value(oilp_meter, oilp_needle, (int32_t)g_oilp_kpa);
  snprintf(buf, sizeof(buf), "%.0f", g_oilp_kpa);
  lv_label_set_text(oilp_value_label, buf);

  // Page 3
  snprintf(buf, sizeof(buf), "%.1f", g_afr); lv_label_set_text(g_afr_cell, buf);
  snprintf(buf, sizeof(buf), "%.0f", g_clt_f); lv_label_set_text(g_clt_cell, buf);
  snprintf(buf, sizeof(buf), "%.0f", g_oilt_f); lv_label_set_text(g_oilt_cell, buf);
  snprintf(buf, sizeof(buf), "%.0f", g_oilp_kpa); lv_label_set_text(g_oilp_cell, buf);
  snprintf(buf, sizeof(buf), "%.0f", g_fuelp_kpa); lv_label_set_text(g_fuelp_cell, buf);
  snprintf(buf, sizeof(buf), "%.0f", g_flex_pct); lv_label_set_text(g_flex_cell, buf);
}

void build_gauge_app(void) {
  build_page1();
  build_page2();
  build_page3();

  // Swipe left/right to change pages - attached to each page (a
  // screen's root object) so gestures anywhere on that page's content
  // bubble up and trigger this callback.
  lv_obj_add_event_cb(page1, gesture_event_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(page2, gesture_event_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(page3, gesture_event_cb, LV_EVENT_GESTURE, NULL);

  lv_disp_load_scr(page1);

  lv_timer_create(gauge_update_timer_cb, 80, NULL); // 80ms, matches the update rate used throughout this project's other gauge sketches
}
