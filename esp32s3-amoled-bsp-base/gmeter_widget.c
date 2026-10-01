/*
  gmeter_widget.c
  G-force meter: a cross with a dot (plus a short fading trail of
  recent positions) in the middle, showing lateral (cornering) and
  longitudinal (accel/braking) G's from the IMU.

  Dot color: blue while accelerating (positive longitudinal G), red
  while braking (negative), a neutral gray near zero. Trail dots stay
  a fixed neutral gray regardless of accel/brake state, shrinking and
  fading with age - only the current/main dot changes color.

  SCALE NOTE: PX_PER_G below is a rough estimate for typical street/
  track driving in this car (not independently calibrated against real
  measured G-forces) - may need tuning once tested with the car
  actually moving/cornering/braking.

  Pre-allocates all trail dot objects once at page-build time rather
  than creating/destroying them each update, avoiding repeated LVGL
  object churn/memory fragmentation over long runtimes.
*/

#include "gmeter_widget.h"
#include "imu_reader.h"

#define TRAIL_LENGTH 8
#define PX_PER_G 150.0f // rough scale, not independently calibrated - see header note
#define MAX_OFFSET_PX 180.0f // clamp so the dot can't travel past the cross arms

static lv_obj_t *s_page;
static lv_obj_t *s_mainDot;
static lv_obj_t *s_trailDots[TRAIL_LENGTH];
static int16_t s_trailX[TRAIL_LENGTH];
static int16_t s_trailY[TRAIL_LENGTH];
static int s_trailHead = 0;
static bool s_trailFilled = false;

static lv_obj_t *make_dot(lv_obj_t *parent, int16_t size, lv_color_t color, lv_opa_t opa) {
  lv_obj_t *dot = lv_obj_create(parent);
  lv_obj_set_size(dot, size, size);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, color, 0);
  lv_obj_set_style_bg_opa(dot, opa, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_center(dot);
  return dot;
}

lv_obj_t *build_gmeter_page(void (*gesture_cb)(lv_event_t *e)) {
  s_page = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_page, lv_color_black(), 0);

  // The cross - two lines through the center of the drawable area.
  static lv_point_t hLine[2];
  hLine[0].x = 33;  hLine[0].y = 233;
  hLine[1].x = 433; hLine[1].y = 233;
  lv_obj_t *hObj = lv_line_create(s_page);
  lv_line_set_points(hObj, hLine, 2);
  lv_obj_set_style_line_color(hObj, lv_color_hex(0x444444), 0);
  lv_obj_set_style_line_width(hObj, 2, 0);

  static lv_point_t vLine[2];
  vLine[0].x = 233; vLine[0].y = 33;
  vLine[1].x = 233; vLine[1].y = 433;
  lv_obj_t *vObj = lv_line_create(s_page);
  lv_line_set_points(vObj, vLine, 2);
  lv_obj_set_style_line_color(vObj, lv_color_hex(0x444444), 0);
  lv_obj_set_style_line_width(vObj, 2, 0);

  lv_obj_t *caption = lv_label_create(s_page);
  lv_obj_set_style_text_color(caption, lv_color_white(), 0);
  lv_obj_set_style_text_font(caption, &lv_font_montserrat_28, 0);
  lv_label_set_text(caption, "G-FORCE");
  lv_obj_align(caption, LV_ALIGN_BOTTOM_MID, 0, -20);

  // Trail dots - smallest/dimmest (oldest) to largest/brightest (most recent).
  for (int i = 0; i < TRAIL_LENGTH; i++) {
    int16_t size = 6 + (i * 2);
    lv_opa_t opa = LV_OPA_10 + (lv_opa_t)((i * (LV_OPA_60 - LV_OPA_10)) / (TRAIL_LENGTH - 1));
    s_trailDots[i] = make_dot(s_page, size, lv_color_hex(0x888888), opa);
    s_trailX[i] = 233;
    s_trailY[i] = 233;
  }

  s_mainDot = make_dot(s_page, 22, lv_color_white(), LV_OPA_COVER);

  lv_obj_add_event_cb(s_page, gesture_cb, LV_EVENT_GESTURE, NULL);

  return s_page;
}

void gmeter_update(void) {
  float lateral = g_accel_lateral;
  float longitudinal = g_accel_longitudinal;

  float offsetX = lateral * PX_PER_G;
  float offsetY = -longitudinal * PX_PER_G; // screen Y increases downward; forward accel should move the dot up

  if (offsetX > MAX_OFFSET_PX) offsetX = MAX_OFFSET_PX;
  if (offsetX < -MAX_OFFSET_PX) offsetX = -MAX_OFFSET_PX;
  if (offsetY > MAX_OFFSET_PX) offsetY = MAX_OFFSET_PX;
  if (offsetY < -MAX_OFFSET_PX) offsetY = -MAX_OFFSET_PX;

  int16_t newX = 233 + (int16_t)offsetX;
  int16_t newY = 233 + (int16_t)offsetY;

  // Push into the circular trail history.
  s_trailHead = (s_trailHead + 1) % TRAIL_LENGTH;
  s_trailX[s_trailHead] = newX;
  s_trailY[s_trailHead] = newY;
  if (s_trailHead == TRAIL_LENGTH - 1) s_trailFilled = true;

  // Reposition each trail dot - index 0 (oldest) through TRAIL_LENGTH-1
  // (most recent, right behind the main dot).
  for (int i = 0; i < TRAIL_LENGTH; i++) {
    int histIndex = (s_trailHead - (TRAIL_LENGTH - 1 - i) + TRAIL_LENGTH * 2) % TRAIL_LENGTH;
    lv_obj_set_pos(s_trailDots[i], s_trailX[histIndex] - lv_obj_get_width(s_trailDots[i]) / 2,
                                    s_trailY[histIndex] - lv_obj_get_height(s_trailDots[i]) / 2);
  }

  // Main dot - color reflects accel/brake state.
  lv_color_t dotColor;
  if (longitudinal > 0.05f) {
    dotColor = lv_color_hex(0x2196F3); // blue - accelerating
  } else if (longitudinal < -0.05f) {
    dotColor = lv_color_hex(0xF44336); // red - braking
  } else {
    dotColor = lv_color_hex(0xAAAAAA); // neutral gray - roughly steady state
  }
  lv_obj_set_style_bg_color(s_mainDot, dotColor, 0);
  lv_obj_set_pos(s_mainDot, newX - lv_obj_get_width(s_mainDot) / 2, newY - lv_obj_get_height(s_mainDot) / 2);
}
