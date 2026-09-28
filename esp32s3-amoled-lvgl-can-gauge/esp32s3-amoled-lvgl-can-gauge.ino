/*
  esp32s3-amoled-lvgl-can-gauge
  Real RPM needle gauge on the round AMOLED, reading LinkECU CAN data
  DIRECTLY on this board (not via the UART bridge from the WROOM board -
  per project decision, this board has its own CAN transceiver).

  Combines two previously-confirmed-working pieces:
    - The LVGL gauge (lv_scale + lv_line needle) from
      esp32s3-amoled-lvgl-gauge-test - handles needle compositing/redraw
      automatically via LVGL's own render pipeline
    - The LinkECU CAN parsing from esp32s3-amoled-linkecu-numbers (base
      1520 decimal/0x5F0, big-endian, /10 scaling) - same evidence/
      caveats as documented there: shares MegaSquirt's broadcast format,
      not yet independently bench-verified against a real Link ECU

  CAN wiring - REVISED: GPIO1/GPIO2 (the 1.27mm-pitch GPIO expansion
  header) were physically inaccessible without specialized fine-pitch
  tools/adapters. Switched to the UART connector instead (JST-SH 1.0mm
  pitch, same connector type used for Nextion-style external links
  elsewhere in this project) since a matching cable was available.
  ESP32-S3's TWAI peripheral is routable to almost any GPIO via the
  chip's flexible GPIO matrix, so repurposing these UART-labeled pins
  for CAN works fine - they're general-purpose exposed pins, not
  consumed by any onboard peripheral (unlike GPIO47/48, which are
  shared by the touch/IMU/RTC I2C bus).
    CAN TX -> GPIO43 (labeled "TXD" on the connector) -> MCP2551 CTX
    CAN RX -> GPIO44 (labeled "RXD" on the connector) -> MCP2551 CRX,
              through a 1k/2k voltage divider (MCP2551 is a 5V part,
              ESP32-S3 GPIO is not 5V-tolerant)
    MCP2551 VCC -> 5V (shared automotive supply, not from this board -
              this board itself has no dedicated 5V pin, only 3V3/VBUS)
    MCP2551 GND -> common GND with ESP32-S3
    MCP2551 CANH/CANL -> Link ECU CAN bus

  Display config (confirmed working): CO5300, 10MHz QSPI, offset 7,7,7,7.
  Currently testing whether disabling rounded line caps + slower update
  rate resolved the tearing seen in esp32s3-amoled-lvgl-gauge-test -
  same settings carried over here.
*/

#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include "driver/twai.h"

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

// ---- CAN pins (confirmed physically accessible on the real board) ----
// ---- CAN pins - using the exposed UART connector (physically
// accessible via a JST-SH 1.0mm cable, unlike the 1.27mm GPIO header
// pins originally chosen). ESP32-S3's TWAI (CAN) peripheral is routable
// to almost any GPIO via the chip's flexible GPIO matrix - it is NOT
// hardware-locked to a specific "CAN pin", so repurposing these UART-
// labeled pins for CAN works fine. ----
#define CAN_TX_PIN GPIO_NUM_43 // labeled "TXD" on the UART connector
#define CAN_RX_PIN GPIO_NUM_44 // labeled "RXD" on the UART connector
#define CAN_BITRATE TWAI_TIMING_CONFIG_500KBITS()

// ---- LinkECU CAN blocks (confirmed base address, from PCLink) ----
#define LINK_BASE   1520 // 0x5F0
#define LINK_BLOCK0 (LINK_BASE + 0) // RPM
#define LINK_BLOCK2 (LINK_BASE + 2) // Baro, MAP, MAT, CLT
#define LINK_BLOCK3 (LINK_BASE + 3) // TPS, Batt, EGO1, EGO2

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  AMOLED_CS, AMOLED_SCK, AMOLED_D0, AMOLED_D1, AMOLED_D2, AMOLED_D3);

Arduino_GFX *gfx = new Arduino_CO5300(
  bus, AMOLED_RST, 0, SCREEN_W, SCREEN_H,
  7, 7, 7, 7);

#define DRAW_BUF_SIZE (SCREEN_W * SCREEN_H / 10 * 2)
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

// ---- Live RPM value, updated from CAN frames ----
volatile int32_t v_rpm = 0;

int16_t wordBE(uint8_t *d, int offset) {
  return (int16_t)((d[offset] << 8) | d[offset + 1]);
}

void handleLinkFrame(uint32_t id, uint8_t *d) {
  switch (id) {
    case LINK_BLOCK0:
      v_rpm = wordBE(d, 6); // word3, raw x1
      break;
      // BLOCK2/BLOCK3 (MAP/CLT/TPS/AFR/etc) parsing omitted for this
      // single-gauge test - see esp32s3-amoled-linkecu-numbers for the
      // full 7-value parsing if/when this expands to more gauges here.
  }
}

unsigned long lastNeedleUpdate = 0;
const unsigned long NEEDLE_UPDATE_INTERVAL_MS = 80; // matches the tearing-test interval from esp32s3-amoled-lvgl-gauge-test
int32_t lastDrawnRpm = -1;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("LVGL CAN gauge starting...");

  // --- Display init (confirmed working) ---
  pinMode(AMOLED_EN, OUTPUT);
  digitalWrite(AMOLED_EN, HIGH);

  if (!gfx->begin(10000000)) {
    Serial.println("gfx->begin() FAILED");
  } else {
    Serial.println("gfx->begin() succeeded");
  }
  gfx->fillScreen(0x0000);

  // --- LVGL init ---
  lv_init();
  lv_tick_set_cb(my_tick);

  lv_display_t *disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

  // --- The gauge itself - RPM range instead of the earlier 0-100 test range ---
  scale = lv_scale_create(lv_screen_active());
  lv_obj_set_size(scale, 420, 420);
  lv_obj_center(scale);
  lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
  lv_scale_set_total_tick_count(scale, 9); // 8 major intervals -> 0,1000,...,8000
  lv_scale_set_label_show(scale, true);
  lv_scale_set_range(scale, 0, 8000);
  lv_scale_set_angle_range(scale, 270);
  lv_scale_set_rotation(scale, -135);

  lv_obj_set_style_arc_color(scale, lv_color_hex(0x555555), LV_PART_MAIN);
  lv_obj_set_style_text_color(scale, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_line_color(scale, lv_color_hex(0x888888), LV_PART_ITEMS);
  lv_obj_set_style_line_color(scale, lv_color_white(), LV_PART_INDICATOR);

  needle_line = lv_line_create(scale);
  lv_obj_set_style_line_width(needle_line, 4, 0);
  lv_obj_set_style_line_color(needle_line, lv_color_hex(0xF44336), 0);
  lv_obj_set_style_line_rounded(needle_line, false, 0); // testing this fixed tearing - see esp32s3-amoled-lvgl-gauge-test notes

  lv_scale_set_line_needle_value(scale, needle_line, 170, 0);

  // --- CAN init ---
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = CAN_BITRATE;
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
    Serial.println("TWAI driver install FAILED");
  }
  if (twai_start() != ESP_OK) {
    Serial.println("TWAI start FAILED");
  }
  Serial.println("CAN running - waiting for LinkECU frames...");

  Serial.println("Setup done.");
}

void loop() {
  twai_message_t message;
  if (twai_receive(&message, pdMS_TO_TICKS(5)) == ESP_OK) {
    handleLinkFrame(message.identifier, message.data);
  }

  unsigned long now = millis();
  if (now - lastNeedleUpdate >= NEEDLE_UPDATE_INTERVAL_MS) {
    lastNeedleUpdate = now;
    if (v_rpm != lastDrawnRpm) {
      lastDrawnRpm = v_rpm;
      lv_scale_set_line_needle_value(scale, needle_line, 170, v_rpm);
    }
  }

  lv_timer_handler();
  delay(5);
}
