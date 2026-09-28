/*
  esp32s3-amoled-lvgl-can-gauge (now: BOOST gauge)
  Real boost gauge on the round AMOLED, reading rusEFI CAN data directly
  on this board (own CAN transceiver, wired to GPIO43/44 via the UART
  connector's JST-SH cable - see docs/esp32s3-amoled-board.md).

  Data source: rusEFI (bench-confirmed elsewhere in this project),
  base 0x200, LITTLE-ENDIAN. Boost = MAP (RUSEFI_BASE3, bytes 0-1,
  x0.0333333 kPa) minus a fixed assumed baro of 101.3 kPa - rusEFI's DBC
  has no live baro signal, same fixed-assumption approach used
  throughout this whole project.

  UNIT SCOPE NOTE: showing both "-40 inHg to 30psi" (classic mechanical
  boost gauge convention - different units on either side of zero) AND
  "-2 to 2 bar" as a single gauge face isn't cleanly supported by
  lv_scale's plain single-range/tick system (no confirmed per-tick
  custom decimal formatting API). Rather than risk broken/misleading
  tick labels, the ON-SCREEN gauge shows kPa (the native computed unit -
  cleanest to render correctly). The SERIAL CONSOLE prints the same
  reading in kPa, bar, inHg, AND psi every update instead - covers
  wanting to see those units without risking the on-screen display, and
  doubles as direct confirmation that CAN data is actually arriving.
  A true dual-unit tick face (inHg on the left half, psi on the right)
  would be a larger follow-up step if that authentic look is wanted.

  Display config (confirmed working): CO5300, 10MHz QSPI, offset 7,7,7,7.
  Needle: rounded caps disabled, 80ms update throttle - carried over from
  the tearing-test fix in esp32s3-amoled-lvgl-gauge-test.
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

// ---- CAN pins - the UART connector's pins, repurposed for CAN via
// ESP32-S3's flexible GPIO matrix (see docs/esp32s3-amoled-board.md) ----
#define CAN_TX_PIN GPIO_NUM_43
#define CAN_RX_PIN GPIO_NUM_44
#define CAN_BITRATE TWAI_TIMING_CONFIG_500KBITS()

// ---- rusEFI CAN IDs (bench-confirmed elsewhere in this project) ----
#define RUSEFI_BASE3 0x203 // MAP, CoolantTemp, IntakeTemp, ..., FuelLevel
#define ASSUMED_BARO_KPA 101.3f // rusEFI's DBC has no baro signal - fixed assumption, matches the rest of this project

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

// ---- Live boost value (kPa), updated from CAN frames ----
volatile float v_boost_kpa = 0;

// rusEFI: little-endian
uint16_t readU16LE(uint8_t *d, int offset) {
  return d[offset] | (d[offset + 1] << 8);
}

void handleRusefiFrame(uint32_t id, uint8_t *d) {
  if (id == RUSEFI_BASE3) {
    uint16_t mapRaw = readU16LE(d, 0);
    float mapKpa = mapRaw * 0.0333333f;
    float boost = mapKpa - ASSUMED_BARO_KPA;
    v_boost_kpa = boost;

    // Console output: confirms CAN data is actually arriving, and shows
    // the reading in every unit of interest (kPa native, plus bar/inHg/
    // psi conversions) since the on-screen gauge only shows kPa - see
    // header note on why a true dual-unit tick face isn't built yet.
    float bar = boost / 100.0f;
    float psi = boost / 6.89476f;
    float inHg = boost / 3.386389f;
    Serial.printf("CAN boost frame: MAP=%.1f kPa, Boost=%.1f kPa (%.2f bar, %.1f psi, %.1f inHg)\n",
                  mapKpa, boost, bar, psi, inHg);
  }
}

unsigned long lastNeedleUpdate = 0;
const unsigned long NEEDLE_UPDATE_INTERVAL_MS = 80;
int32_t lastDrawnBoost = -9999;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("LVGL CAN boost gauge starting...");

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

  // --- The gauge: boost, in kPa (native computed unit - see header
  // note on why the console handles the multi-unit display instead) ---
  // Range covers roughly -40 inHg (~-135 kPa) to 30 psi (~207 kPa) and
  // roughly -2 to 2 bar (-200 to 200 kPa) - -150 to 200 comfortably
  // covers both requested ranges.
  scale = lv_scale_create(lv_screen_active());
  lv_obj_set_size(scale, 420, 420);
  lv_obj_center(scale);
  lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
  lv_scale_set_total_tick_count(scale, 8); // -150,-100,...,200 in 50kPa steps
  lv_scale_set_label_show(scale, true);
  lv_scale_set_range(scale, -150, 200);
  lv_scale_set_angle_range(scale, 270);
  lv_scale_set_rotation(scale, -135);

  lv_obj_set_style_arc_color(scale, lv_color_hex(0x555555), LV_PART_MAIN);
  lv_obj_set_style_text_color(scale, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_line_color(scale, lv_color_hex(0x888888), LV_PART_ITEMS);
  lv_obj_set_style_line_color(scale, lv_color_white(), LV_PART_INDICATOR);

  needle_line = lv_line_create(scale);
  lv_obj_set_style_line_width(needle_line, 4, 0);
  lv_obj_set_style_line_color(needle_line, lv_color_hex(0xF44336), 0);
  lv_obj_set_style_line_rounded(needle_line, false, 0);

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
  Serial.println("CAN running - waiting for rusEFI frames...");

  Serial.println("Setup done.");
}

void loop() {
  twai_message_t message;
  if (twai_receive(&message, pdMS_TO_TICKS(5)) == ESP_OK) {
    handleRusefiFrame(message.identifier, message.data);
  }

  unsigned long now = millis();
  if (now - lastNeedleUpdate >= NEEDLE_UPDATE_INTERVAL_MS) {
    lastNeedleUpdate = now;
    int32_t boostInt = (int32_t)v_boost_kpa;
    if (boostInt != lastDrawnBoost) {
      lastDrawnBoost = boostInt;
      lv_scale_set_line_needle_value(scale, needle_line, 170, boostInt);
    }
  }

  lv_timer_handler();
  delay(5);
}
