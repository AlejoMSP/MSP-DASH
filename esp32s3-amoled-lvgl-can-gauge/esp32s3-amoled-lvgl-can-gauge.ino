/*
  esp32s3-amoled-lvgl-can-gauge
  Multi-page gauge cluster on the round AMOLED, reading rusEFI CAN data
  directly on this board (own CAN transceiver, GPIO43/44 via the UART
  connector's JST-SH cable - see docs/esp32s3-amoled-board.md).

  Three pages, cycled with the board's physical BOOT button (GPIO0):
    Page 1: AFR - needle gauge (range 10-20) + precise numeric readout
            below the needle
    Page 2: Coolant Temp + Oil Pressure as two independent half-moon
            (180 degree) gauges side by side
    Page 3: 6-value grid - AFR/CLT, Oil Temp/Oil Press, Fuel Press/Flex%

  All CAN parsing runs every loop() regardless of which page is visible -
  updating an LVGL object on a screen that isn't currently active is
  harmless (no display cost until that screen is loaded), so switching
  pages always shows already-current values with no lag.

  Data source: rusEFI (bench-confirmed elsewhere in this project),
  little-endian, base 0x200. Signals used:
    RUSEFI_BASE1 (0x201): FlexPct (byte7)
    RUSEFI_BASE3 (0x203): MAP (boost calc), CLT (byte2)
    RUSEFI_BASE4 (0x204): OilPress (bytes2-3), OilTemp (byte4)
    RUSEFI_BASE7 (0x207): Lam1->AFR (bytes0-1), FpLow (bytes4-5)

  Units: temps in Fahrenheit (project convention), pressures in kPa
  (matches the WROOM board's established convention for these signals).

  Display config (confirmed working): CO5300, 10MHz QSPI, offset 7,7,7,7.
  Font note: all text uses LVGL's default font - specific font sizes
  (e.g. LV_FONT_MONTSERRAT_36) are individually enabled/disabled in
  lv_conf.h and not confirmed which ones are active, so avoiding an
  unconfirmed font reference here. Easy follow-up if bigger text wanted.
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

// ---- CAN pins (UART connector, repurposed via ESP32-S3's flexible GPIO matrix) ----
#define CAN_TX_PIN GPIO_NUM_43
#define CAN_RX_PIN GPIO_NUM_44
#define CAN_BITRATE TWAI_TIMING_CONFIG_500KBITS()

// ---- Page-switch button (board's physical BOOT button) ----
#define BOOT_BTN_PIN 0

// ---- rusEFI CAN IDs (bench-confirmed elsewhere in this project) ----
#define RUSEFI_BASE1 0x201
#define RUSEFI_BASE3 0x203
#define RUSEFI_BASE4 0x204
#define RUSEFI_BASE7 0x207
#define ASSUMED_BARO_KPA 101.3f

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

float cToF(float c) {
  return c * 9.0f / 5.0f + 32.0f;
}

// ---- Live values, updated from CAN frames ----
volatile float v_boost_kpa = 0;
volatile float v_afr = 14.7f;
volatile float v_clt_f = 0;
volatile float v_oilp_kpa = 0;
volatile float v_oilt_f = 0;
volatile float v_fuelp_kpa = 0;
volatile float v_flex_pct = 0;

uint16_t readU16LE(uint8_t *d, int offset) {
  return d[offset] | (d[offset + 1] << 8);
}

void handleRusefiFrame(uint32_t id, uint8_t *d) {
  switch (id) {
    case RUSEFI_BASE1: {
      v_flex_pct = d[7];
      break;
    }
    case RUSEFI_BASE3: {
      uint16_t mapRaw = readU16LE(d, 0);
      float mapKpa = mapRaw * 0.0333333f;
      v_boost_kpa = mapKpa - ASSUMED_BARO_KPA;
      int8_t cltRaw = (int8_t)d[2];
      v_clt_f = cToF(cltRaw - 40);
      break;
    }
    case RUSEFI_BASE4: {
      uint16_t oilPressRaw = readU16LE(d, 2);
      v_oilp_kpa = oilPressRaw * 0.0333333f;
      int8_t oilTempRaw = (int8_t)d[4];
      v_oilt_f = cToF(oilTempRaw - 40);
      break;
    }
    case RUSEFI_BASE7: {
      uint16_t lam1Raw = readU16LE(d, 0);
      float lambda = lam1Raw * 0.0001f;
      v_afr = lambda * 14.7f;
      uint16_t fpLowRaw = readU16LE(d, 4);
      v_fuelp_kpa = fpLowRaw * 0.0333333f;
      break;
    }
  }
}

// =====================================================================
// Page 1: AFR gauge
// =====================================================================
lv_obj_t *page1;
lv_obj_t *afrScale;
lv_obj_t *afrNeedle;
lv_obj_t *afrValueLabel;

void buildPage1() {
  page1 = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(page1, lv_color_black(), 0);

  afrScale = lv_scale_create(page1);
  lv_obj_set_size(afrScale, 400, 400);
  lv_obj_center(afrScale);
  lv_scale_set_mode(afrScale, LV_SCALE_MODE_ROUND_INNER);
  lv_scale_set_total_tick_count(afrScale, 6); // 10,12,14,16,18,20
  lv_scale_set_label_show(afrScale, true);
  lv_scale_set_range(afrScale, 10, 20);
  lv_scale_set_angle_range(afrScale, 270);
  lv_scale_set_rotation(afrScale, -135);
  lv_obj_set_style_text_color(afrScale, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_line_color(afrScale, lv_color_hex(0x888888), LV_PART_ITEMS);
  lv_obj_set_style_line_color(afrScale, lv_color_white(), LV_PART_INDICATOR);

  afrNeedle = lv_line_create(afrScale);
  lv_obj_set_style_line_width(afrNeedle, 4, 0);
  lv_obj_set_style_line_color(afrNeedle, lv_color_hex(0xF44336), 0);
  lv_obj_set_style_line_rounded(afrNeedle, false, 0);
  lv_scale_set_line_needle_value(afrScale, afrNeedle, 150, 15);

  // Precise numeric readout - "numbers at bottom"
  afrValueLabel = lv_label_create(page1);
  lv_obj_set_style_text_color(afrValueLabel, lv_color_white(), 0);
  lv_label_set_text(afrValueLabel, "14.7");
  lv_obj_align(afrValueLabel, LV_ALIGN_BOTTOM_MID, 0, -30);

  lv_obj_t *caption = lv_label_create(page1);
  lv_obj_set_style_text_color(caption, lv_color_hex(0x4dd0e1), 0);
  lv_label_set_text(caption, "AFR");
  lv_obj_align_to(caption, afrValueLabel, LV_ALIGN_OUT_TOP_MID, 0, -4);
}

// =====================================================================
// Page 2: CLT + Oil Pressure half-moons
// =====================================================================
lv_obj_t *page2;
lv_obj_t *cltScale, *cltNeedle, *cltLabel;
lv_obj_t *oilpScale, *oilpNeedle, *oilpLabel;

void buildHalfMoon(lv_obj_t *parent, lv_obj_t **scaleOut, lv_obj_t **needleOut,
                    lv_obj_t **labelOut, int32_t xOffset, int32_t rangeMin, int32_t rangeMax,
                    const char *caption, uint32_t needleColor) {
  lv_obj_t *scale = lv_scale_create(parent);
  lv_obj_set_size(scale, 220, 220);
  lv_obj_align(scale, LV_ALIGN_CENTER, xOffset, -20);
  lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
  lv_scale_set_total_tick_count(scale, 5);
  lv_scale_set_label_show(scale, true);
  lv_scale_set_range(scale, rangeMin, rangeMax);
  lv_scale_set_angle_range(scale, 180); // half-moon
  lv_scale_set_rotation(scale, -90);    // opens upward
  lv_obj_set_style_text_color(scale, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_line_color(scale, lv_color_hex(0x888888), LV_PART_ITEMS);
  lv_obj_set_style_line_color(scale, lv_color_white(), LV_PART_INDICATOR);

  lv_obj_t *needle = lv_line_create(scale);
  lv_obj_set_style_line_width(needle, 3, 0);
  lv_obj_set_style_line_color(needle, lv_color_hex(needleColor), 0);
  lv_obj_set_style_line_rounded(needle, false, 0);
  lv_scale_set_line_needle_value(scale, needle, 90, rangeMin);

  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_label_set_text(label, "0");
  lv_obj_align_to(label, scale, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  lv_obj_t *capLabel = lv_label_create(parent);
  lv_obj_set_style_text_color(capLabel, lv_color_hex(0x4dd0e1), 0);
  lv_label_set_text(capLabel, caption);
  lv_obj_align_to(capLabel, label, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

  *scaleOut = scale;
  *needleOut = needle;
  *labelOut = label;
}

void buildPage2() {
  page2 = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(page2, lv_color_black(), 0);

  buildHalfMoon(page2, &cltScale, &cltNeedle, &cltLabel, -120, 0, 260, "CLT (F)", 0xF44336);
  buildHalfMoon(page2, &oilpScale, &oilpNeedle, &oilpLabel, 120, 0, 700, "OIL P (kPa)", 0x2196F3);
}

// =====================================================================
// Page 3: 6-value grid
// =====================================================================
lv_obj_t *page3;
lv_obj_t *g_afr, *g_clt, *g_oilt, *g_oilp, *g_fuelp, *g_flex;

lv_obj_t *makeGridCell(lv_obj_t *parent, int16_t x, int16_t y, const char *caption) {
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

void buildPage3() {
  page3 = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(page3, lv_color_black(), 0);

  const int16_t colX[2] = {70, 260};
  const int16_t rowY[3] = {90, 210, 330};

  g_afr   = makeGridCell(page3, colX[0], rowY[0], "AFR");
  g_clt   = makeGridCell(page3, colX[1], rowY[0], "CLT (F)");
  g_oilt  = makeGridCell(page3, colX[0], rowY[1], "OIL T (F)");
  g_oilp  = makeGridCell(page3, colX[1], rowY[1], "OIL P (kPa)");
  g_fuelp = makeGridCell(page3, colX[0], rowY[2], "FUEL P (kPa)");
  g_flex  = makeGridCell(page3, colX[1], rowY[2], "FLEX %");
}

// =====================================================================
// Page cycling (BOOT button)
// =====================================================================
int currentPage = 0;
bool lastBtnState = HIGH;
unsigned long lastBtnChange = 0;
const unsigned long DEBOUNCE_MS = 200;

void checkPageButton() {
  bool btnState = digitalRead(BOOT_BTN_PIN);
  unsigned long now = millis();
  if (btnState == LOW && lastBtnState == HIGH && (now - lastBtnChange) > DEBOUNCE_MS) {
    lastBtnChange = now;
    currentPage = (currentPage + 1) % 3;
    if (currentPage == 0) lv_screen_load(page1);
    else if (currentPage == 1) lv_screen_load(page2);
    else lv_screen_load(page3);
    Serial.printf("Switched to page %d\n", currentPage + 1);
  }
  lastBtnState = btnState;
}

// =====================================================================
// setup / loop
// =====================================================================

unsigned long lastGaugeUpdate = 0;
const unsigned long GAUGE_UPDATE_INTERVAL_MS = 80;

void updateAllGauges() {
  char buf[16];

  // Page 1
  lv_scale_set_line_needle_value(afrScale, afrNeedle, 150, (int32_t)v_afr);
  snprintf(buf, sizeof(buf), "%.1f", v_afr);
  lv_label_set_text(afrValueLabel, buf);

  // Page 2
  lv_scale_set_line_needle_value(cltScale, cltNeedle, 90, (int32_t)v_clt_f);
  snprintf(buf, sizeof(buf), "%.0f", v_clt_f);
  lv_label_set_text(cltLabel, buf);
  lv_scale_set_line_needle_value(oilpScale, oilpNeedle, 90, (int32_t)v_oilp_kpa);
  snprintf(buf, sizeof(buf), "%.0f", v_oilp_kpa);
  lv_label_set_text(oilpLabel, buf);

  // Page 3
  snprintf(buf, sizeof(buf), "%.1f", v_afr); lv_label_set_text(g_afr, buf);
  snprintf(buf, sizeof(buf), "%.0f", v_clt_f); lv_label_set_text(g_clt, buf);
  snprintf(buf, sizeof(buf), "%.0f", v_oilt_f); lv_label_set_text(g_oilt, buf);
  snprintf(buf, sizeof(buf), "%.0f", v_oilp_kpa); lv_label_set_text(g_oilp, buf);
  snprintf(buf, sizeof(buf), "%.0f", v_fuelp_kpa); lv_label_set_text(g_fuelp, buf);
  snprintf(buf, sizeof(buf), "%.0f", v_flex_pct); lv_label_set_text(g_flex, buf);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Multi-page gauge cluster starting...");

  pinMode(AMOLED_EN, OUTPUT);
  digitalWrite(AMOLED_EN, HIGH);
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);

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

  buildPage1();
  buildPage2();
  buildPage3();
  lv_screen_load(page1);

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

  Serial.println("Setup done. Press BOOT button to cycle pages.");
}

void loop() {
  twai_message_t message;
  if (twai_receive(&message, pdMS_TO_TICKS(5)) == ESP_OK) {
    handleRusefiFrame(message.identifier, message.data);
  }

  checkPageButton();

  unsigned long now = millis();
  if (now - lastGaugeUpdate >= GAUGE_UPDATE_INTERVAL_MS) {
    lastGaugeUpdate = now;
    updateAllGauges();
  }

  lv_timer_handler();
  delay(5);
}
