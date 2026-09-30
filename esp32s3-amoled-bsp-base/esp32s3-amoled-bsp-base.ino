/*
  esp32s3-amoled-bsp-base
  Multi-page gauge cluster on the round AMOLED, built on Waveshare's own
  confirmed-working BSP (ESP-IDF esp_lcd + LVGL v8 + FT3168 touch) -
  the pivot away from our earlier Arduino_GFX/LVGL v9 stack, which had
  persistent display/touch/serial issues never fully resolved. This
  foundation is CONFIRMED working end-to-end (display + touch both
  verified) via Waveshare's own factory firmware and our own from-source
  rebuild of their 07_LVGL_Test example.

  Display/touch/LVGL init (lcd_bsp.c, esp_lcd_sh8601.c, FT3168.cpp,
  read_lcd_id_bsp.c) is UNCHANGED from their confirmed-working example,
  except one targeted edit: lv_demo_widgets() was replaced with our own
  build_gauge_app() call - see gauge_app.c for the actual gauge cluster.

  CAN reading (rusEFI, little-endian, base 0x200 - bench-confirmed
  elsewhere in this project) happens entirely in Arduino's loop(), which
  their BSP leaves empty (LVGL runs on its own dedicated FreeRTOS task).
  This keeps CAN parsing completely independent of LVGL's task - we only
  ever touch simple global values here, never call any LVGL function
  directly from this context. gauge_app.c's own periodic timer (running
  inside LVGL's task, registered via lv_timer_create) reads those values
  and performs all actual LVGL API calls safely.

  CAN wiring: GPIO43/44 (the UART connector, JST-SH cable) - the only
  physically accessible connector available (the 1.27mm GPIO header
  where GPIO1/GPIO2 live needs a cable not currently on hand). These are
  this board's dedicated UART0 pins, which Serial defaults to unless
  CDCOnBoot=cdc is set - compile with that option to avoid Serial/CAN
  conflicting over the same physical UART.
*/

#include "lcd_bsp.h"
#include "FT3168.h"
#include "gauge_app.h"
#include "driver/twai.h"

// ---- CAN pins - using the UART connector (JST-SH cable available),
// same as our earlier v9-based sketch. These are this board's dedicated
// UART0 pins, which Serial defaults to unless CDCOnBoot=cdc is set -
// MUST compile with that option to avoid Serial/CAN fighting over the
// same physical UART. ----
#define CAN_TX_PIN GPIO_NUM_43
#define CAN_RX_PIN GPIO_NUM_44
#define CAN_BITRATE TWAI_TIMING_CONFIG_500KBITS()

// ---- rusEFI CAN IDs (bench-confirmed elsewhere in this project) ----
#define RUSEFI_BASE1 0x201
#define RUSEFI_BASE3 0x203
#define RUSEFI_BASE4 0x204
#define RUSEFI_BASE7 0x207
#define ASSUMED_BARO_KPA 101.3f

// ---- Page-switch button (board's physical BOOT button) ----
#define BOOT_BTN_PIN 0

float cToF(float c) {
  return c * 9.0f / 5.0f + 32.0f;
}

uint16_t readU16LE(uint8_t *d, int offset) {
  return d[offset] | (d[offset + 1] << 8);
}

void handleRusefiFrame(uint32_t id, uint8_t *d) {
  switch (id) {
    case RUSEFI_BASE1: {
      g_flex_pct = d[7];
      break;
    }
    case RUSEFI_BASE3: {
      int8_t cltRaw = (int8_t)d[2];
      g_clt_f = cToF(cltRaw - 40);
      break;
    }
    case RUSEFI_BASE4: {
      uint16_t oilPressRaw = readU16LE(d, 2);
      g_oilp_kpa = oilPressRaw * 0.0333333f;
      int8_t oilTempRaw = (int8_t)d[4];
      g_oilt_f = cToF(oilTempRaw - 40);
      break;
    }
    case RUSEFI_BASE7: {
      uint16_t lam1Raw = readU16LE(d, 0);
      float lambda = lam1Raw * 0.0001f;
      g_afr = lambda * 14.7f;
      uint16_t fpLowRaw = readU16LE(d, 4);
      g_fuelp_kpa = fpLowRaw * 0.0333333f;
      break;
    }
  }
}

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
    g_requested_page = currentPage; // gauge_app.c's timer picks this up safely inside LVGL's task
    Serial.printf("Requested page %d\n", currentPage + 1);
  }
  lastBtnState = btnState;
}

void setup()
{
  Serial.begin(115200);
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);

  Touch_Init();
  lcd_lvgl_Init();

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
}

void loop()
{
  twai_message_t message;
  if (twai_receive(&message, pdMS_TO_TICKS(20)) == ESP_OK) {
    handleRusefiFrame(message.identifier, message.data);
  }
  checkPageButton();
}
