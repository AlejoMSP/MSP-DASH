/*
  esp32s3-amoled-lvgl-gauge-test
  Real LVGL gauge: lv_scale (tick marks/arc face) + a line needle driven
  by lv_scale_set_line_needle_value() - LVGL's built-in mechanism that
  handles compositing/redraw automatically (the exact problem we were
  trying to avoid solving by hand in the Arduino_GFX needle test).

  Automatic simulated 0-100 sweep for now (same test pattern as the
  earlier hand-rolled needle test) - proves the real gauge mechanism
  before wiring to live UART/CAN data.

  Confirmed APIs used here (checked against actual installed source):
    lv_scale_create(parent)
    lv_scale_set_mode(obj, LV_SCALE_MODE_ROUND_INNER)
    lv_scale_set_total_tick_count(obj, count)
    lv_scale_set_label_show(obj, bool)
    lv_scale_set_range(obj, min, max)
    lv_scale_set_angle_range(obj, angle_range)   - degrees
    lv_scale_set_rotation(obj, rotation)         - degrees, starting angle
    lv_line_create(parent)                        - the needle object itself
    lv_scale_set_line_needle_value(scale, needle_line, needle_length, value)

  Sweep convention: -135 to +135 (270 total), matching this project's
  established gauge convention (web dashboard, hand-rolled needle test).

  Built on the confirmed-working LVGL bridge from esp32s3-amoled-lvgl-test
  (tick source, buffers, RGB565 color format, flush callback, black
  screen background).
*/

#include <lvgl.h>
#include <Arduino_GFX_Library.h>

#define AMOLED_CS    9
#define AMOLED_SCK   10
#define AMOLED_D0    11
#define AMOLED_D1    12
#define AMOLED_D2    13
#define AMOLED_D3    14
#define AMOLED_RST   21
#define AMOLED_EN    42

#define SCREEN_W 466
#define SCREEN_H 466

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  AMOLED_CS, AMOLED_SCK, AMOLED_D0, AMOLED_D1, AMOLED_D2, AMOLED_D3);

Arduino_GFX *gfx = new Arduino_CO5300(
  bus, AMOLED_RST, 0, SCREEN_W, SCREEN_H,
  7, 7, 7, 7);

#define DRAW_BUF_SIZE (SCREEN_W * SCREEN_H / 10 * 2) // 2 bytes/pixel, RGB565 - see esp32s3-amoled-lvgl-test notes
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
  lv_display_flush_ready(disp);
}

static uint32_t my_tick(void) {
  return millis();
}

lv_obj_t *scale;
lv_obj_t *needle_line;

unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL_MS = 80; // slowed from 30ms - isolates whether tearing is rate-dependent or purely rendering-complexity related
int32_t testValue = 0;
int direction = 1;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("LVGL gauge test starting...");

  pinMode(AMOLED_EN, OUTPUT);
  digitalWrite(AMOLED_EN, HIGH);

  if (!gfx->begin(10000000)) {
    Serial.println("gfx->begin() FAILED");
  } else {
    Serial.println("gfx->begin() succeeded");
  }
  gfx->fillScreen(0x0000);

  lv_init();
  lv_tick_set_cb(my_tick);

  lv_display_t *disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

  // --- The actual gauge ---
  scale = lv_scale_create(lv_screen_active());
  lv_obj_set_size(scale, 420, 420); // leaves margin within our 466x466 visible circle
  lv_obj_center(scale);
  lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
  lv_scale_set_total_tick_count(scale, 9); // 8 major intervals, matching our 0-8000-style gauges elsewhere
  lv_scale_set_label_show(scale, true);
  lv_scale_set_range(scale, 0, 100); // test range - real gauges will use their own (e.g. 0-8000 for RPM)
  lv_scale_set_angle_range(scale, 270);
  lv_scale_set_rotation(scale, -135);

  lv_obj_set_style_arc_color(scale, lv_color_hex(0x555555), LV_PART_MAIN);
  lv_obj_set_style_text_color(scale, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_line_color(scale, lv_color_hex(0x888888), LV_PART_ITEMS);
  lv_obj_set_style_line_color(scale, lv_color_white(), LV_PART_INDICATOR);

  needle_line = lv_line_create(scale);
  lv_obj_set_style_line_width(needle_line, 4, 0);
  lv_obj_set_style_line_color(needle_line, lv_color_hex(0xF44336), 0); // red
  // Rounded caps disabled - simpler single-segment line rendering,
  // testing whether the extra antialiasing/rounding pass was widening
  // the redraw region enough to cause tearing (persistent even at slow
  // needle movement, pointing at rendering complexity rather than just
  // a fast-value-change/hardware-timing coincidence).
  lv_obj_set_style_line_rounded(needle_line, false, 0);

  lv_scale_set_line_needle_value(scale, needle_line, 170, testValue);

  Serial.println("Setup done.");
}

void loop() {
  unsigned long now = millis();
  if (now - lastUpdate >= UPDATE_INTERVAL_MS) {
    lastUpdate = now;
    testValue += direction;
    if (testValue >= 100) { testValue = 100; direction = -1; }
    if (testValue <= 0)   { testValue = 0; direction = 1; }
    lv_scale_set_line_needle_value(scale, needle_line, 170, testValue);
  }
  lv_timer_handler();
  delay(5);
}
