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

  CAN reading (rusEFI + MegaSquirt/LinkECU, bench-confirmed elsewhere in
  this project) happens on its own dedicated FreeRTOS task (started by
  can_start_task(), priority above both loop() and LVGL's task), so SD
  writes and web server work in loop() can never cause dropped frames.
  This also keeps CAN parsing completely independent of LVGL's task - we
  only ever touch simple global values from there, never call any LVGL
  function directly. gauge_app.c's own periodic timer (running
  inside LVGL's task, registered via lv_timer_create) reads those values
  and performs all actual LVGL API calls safely. The actual frame
  decoding lives in can_parsing.h (extracted here to keep this file as
  just setup()/loop() orchestration); BOOT button page-cycling lives in
  page_button.h, same rationale.

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
#include "gauge_config.h"
#include "driver/twai.h"
#include "web_monitor.h"
#include "ecu_config.h"
#include "sd_storage.h"
#include "sd_logging.h"
#include "can_parsing.h"
#include "page_button.h"
#include "build_info.h"

// ---- CAN pins - using the UART connector (JST-SH cable available).
// These are this board's dedicated UART0 pins, which Serial defaults
// to unless CDCOnBoot=cdc is set - MUST compile with that option to
// avoid Serial/CAN fighting over the same physical UART. ----
#define CAN_TX_PIN GPIO_NUM_43
#define CAN_RX_PIN GPIO_NUM_44
#define CAN_BITRATE TWAI_TIMING_CONFIG_500KBITS()

void setup()
{
  Serial.begin(115200);
  Serial.printf("\nMSP-DASH build %d, compiled %s %s\n", FW_BUILD_NUMBER, __DATE__, __TIME__);
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);

  ecu_config_init();
  gauge_config_init(); // must load before lcd_lvgl_Init(), since build_gauge_app() (called from within it) reads this config
  gauge_config_web_init(); // the separate, independent web-dashboard config
  gauge_config_log_init(); // which signals get logged to SD - independent of what's displayed

  Touch_Init();
  lcd_lvgl_Init();

  // --- CAN init ---
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  g_config.rx_queue_len = 64; // default is 5 - this bus runs several hundred frames/sec, so give the driver room to buffer
  twai_timing_config_t t_config = CAN_BITRATE;
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
    Serial.println("TWAI driver install FAILED");
  }
  if (twai_start() != ESP_OK) {
    Serial.println("TWAI start FAILED");
  }
  can_start_task(); // frames are received and decoded on their own task from here on, see can_parsing.h
  Serial.println("CAN running - waiting for rusEFI/MegaSquirt/LinkECU frames...");

  sd_storage_init();
  sd_logging_init();
  web_monitor_init();
}

void loop()
{
  // CAN no longer lives here - see can_parsing.h. This loop only does
  // slow, non-time-critical work, so it can safely block on SD writes
  // without ever delaying a CAN frame.
  checkPageButton();
  web_monitor_loop();
  sd_logging_loop();
  vTaskDelay(pdMS_TO_TICKS(5)); // previously paced by twai_receive()'s timeout - keeps this from spinning flat out
}
