/*
  esp32s3-amoled-lvgl-test
  Minimal LVGL v9 bring-up test - adapted directly from LVGL's own
  official Arduino example (examples/arduino/LVGL_Arduino/LVGL_Arduino.ino,
  bundled with the installed library), bridged to our confirmed-working
  Arduino_GFX/CO5300 display setup instead of TFT_eSPI.

  This is step 1: just get LVGL rendering ANYTHING (a label) through our
  display correctly. Once this works, step 2 builds the actual gauge
  (background image widget + rotated needle image widget).

  Confirmed APIs used here (checked against the actual installed
  library source, not guessed):
    - lv_tick_set_cb(cb) - tick source registration
    - lv_display_create(w, h)
    - lv_display_set_flush_cb(disp, cb)
    - lv_display_set_buffers(disp, buf1, buf2, size, render_mode)
    - flush_cb signature: void(lv_display_t*, const lv_area_t*, uint8_t*)
    - lv_area_get_width(area) / lv_area_get_height(area)
    - lv_display_flush_ready(disp)

  UNCONFIRMED: whether lv_conf.h needs to be manually created/placed, or
  whether the Arduino Library Manager install already provides a working
  default. This compile is the test for that - if it fails with an
  lv_conf.h-related error, that tells us definitively what's needed.

  Board compile note (same as every other S3 sketch in this project):
    arduino-cli compile --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M esp32s3-amoled-lvgl-test
*/

#include <lvgl.h>
#include <Arduino_GFX_Library.h>

// ---- Display pins (confirmed) ----
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

// LVGL's working draw buffer - NOT a full framebuffer, just a chunk it
// renders into before handing sections to our flush callback. Sized per
// LVGL's own example formula (1/10 screen size).
#define DRAW_BUF_SIZE (SCREEN_W * SCREEN_H / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

// Bridges LVGL's rendered pixels to our confirmed-working Arduino_GFX
// call - this is the actual "LVGL talks to our display" connection point.
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
  lv_display_flush_ready(disp);
}

static uint32_t my_tick(void) {
  return millis();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("LVGL test starting...");

  // --- Our confirmed-working display init (unchanged from prior tests) ---
  pinMode(AMOLED_EN, OUTPUT);
  digitalWrite(AMOLED_EN, HIGH);

  if (!gfx->begin(10000000)) {
    Serial.println("gfx->begin() FAILED");
  } else {
    Serial.println("gfx->begin() succeeded");
  }
  gfx->fillScreen(0x0000);

  // --- LVGL init, following the library's own Arduino example pattern ---
  Serial.println("Calling lv_init()...");
  lv_init();
  Serial.println("lv_init() done.");

  lv_tick_set_cb(my_tick);

  lv_display_t *disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  Serial.println("Display registered with LVGL.");

  lv_obj_t *label = lv_label_create(lv_screen_active());
  lv_label_set_text(label, "Hello, LVGL!");
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

  Serial.println("Setup done.");
}

void loop() {
  lv_timer_handler();
  delay(5);
}
