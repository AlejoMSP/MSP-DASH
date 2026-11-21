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
#include <Preferences.h>
#include "gauge_config.h"
#include "driver/twai.h"
#include "web_monitor.h"
#include "sd_storage.h"
#include "sd_logging.h"
#include "can_parsing.h"
#include "page_button.h"
#include "build_info.h"
#include "espnow_link.h"
#include "custom_slots.h"

// ---- CAN pins - using the UART connector (JST-SH cable available).
// These are this board's dedicated UART0 pins, which Serial defaults
// to unless CDCOnBoot=cdc is set - MUST compile with that option to
// avoid Serial/CAN fighting over the same physical UART. ----
#define CAN_TX_PIN GPIO_NUM_43
#define CAN_RX_PIN GPIO_NUM_44
#define CAN_BITRATE TWAI_TIMING_CONFIG_500KBITS()

// ---- automatic role (factory setup): a board with no saved role listens to its own CAN bus (listen-only, sends
// nothing) and to ESP-NOW. Data from a hub or another screen: stays remote and stops listening to CAN. CAN traffic and
// nobody else: becomes main. Re-checked all the time until one of the two happens (the ECU may power up later).
static bool s_canListening = false, s_autoDone = false;
static void canListen() {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY);
  twai_timing_config_t t = CAN_BITRATE;
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  s_canListening = twai_driver_install(&g, &t, &f) == ESP_OK && twai_start() == ESP_OK;
}
static void autoRoleStep() {
  if (s_autoDone || !espnow_link_is_auto()) return;
  if (espnow_link_any_rx()) {   // a hub or a main screen is sending: remote it is
    if (s_canListening) { twai_stop(); twai_driver_uninstall(); s_canListening = false; }
    s_autoDone = true; Serial.println("Automatic role: data from another board: this screen is REMOTE");
    return;
  }
  twai_message_t m;
  if (s_canListening && twai_receive(&m, 0) == ESP_OK) {   // CAN here and nobody sending: main
    twai_stop(); twai_driver_uninstall(); s_canListening = false;
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    g.rx_queue_len = 64;
    twai_timing_config_t t = CAN_BITRATE;
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&g, &t, &f) == ESP_OK && twai_start() == ESP_OK) can_start_task();
    espnow_link_become_main();
    s_autoDone = true;
  }
}

// ---- factory reset from the BOOT button (factory setup): in the first 15 s after power-up, hold BOOT for 5 s; the
// screen asks; let go and press BOOT again within 10 s. (BOOT cannot be held WHILE powering on: the chip would start
// its USB loader instead of this firmware.) Any other time BOOT is the page button as before.
static void factoryButtonStep() {
  static int state = 0; static uint32_t t = 0; static int shown = -1;
  if (state == 9) return;
  uint32_t now = millis(); bool down = digitalRead(BOOT_BTN_PIN) == LOW;
  switch (state) {
    case 0: if (now > 15000) { state = 9; break; } if (down) { state = 1; t = now; } break;
    case 1: if (!down) { state = 0; break; }
            if (now - t >= 5000) { state = 2; gauge_app_popup_ms("Factory reset? Let go, then press BOOT again", 10000); } break;
    case 2: if (!down) { state = 3; t = now; } break;
    case 3: {
      int left = 10 - (int)((now - t) / 1000);
      if (left != shown) { shown = left; char m[48]; snprintf(m, sizeof m, "Factory reset? Press BOOT (%d s)", left); gauge_app_popup_ms(m, 1500); }
      if (now - t > 10000) { gauge_app_popup("Factory reset cancelled"); state = 9; }
      else if (down && now - t > 300) { gauge_app_popup("Factory reset..."); delay(600); web_monitor_factory_reset(); }
      break;
    }
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.printf("\nMSP-DASH build %d, compiled %s %s\n", FW_BUILD_NUMBER, __DATE__, __TIME__);
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);

  { Preferences dp; dp.begin("disp", true); gauge_app_set_diameter(dp.getInt("diam", GAUGE_DIAMETER_BOARD_DEFAULT));
    gauge_app_set_options(dp.getBool("sweep", true), dp.getBool("demo", true)); dp.end(); }   // visible screen size
  custom_slots_screen_init(); // custom sensor names, saved from the hub (before gauge_config: gauges show the names)
  gauge_config_init(); // must load before lcd_lvgl_Init(), since build_gauge_app() (called from within it) reads this config
  gauge_config_web_cleanup(); // deletes the old web-dashboard config file, if any
  gauge_config_log_init(); // which signals get logged to SD - independent of what's displayed

  Touch_Init();
  lcd_lvgl_Init();

  // --- CAN init (main board only; a remote board takes its values over ESP-NOW) ---
  if (espnow_link_is_auto()) canListen();   // automatic role: only listen for now (autoRoleStep decides)
  if (!espnow_link_is_remote()) {
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
  }

  sd_storage_init();
  sd_logging_init();
  web_monitor_init();
  espnow_link_init();   // after WiFi has joined (shares its channel) or given up (parks on channel 1)
}

void loop()
{
  // CAN no longer lives here - see can_parsing.h. This loop only does
  // slow, non-time-critical work, so it can safely block on SD writes
  // without ever delaying a CAN frame.
  checkPageButton();
  factoryButtonStep();
  autoRoleStep();
  web_monitor_loop();
  sim_loop();
  espnow_link_loop();
  sd_logging_loop();
  vTaskDelay(pdMS_TO_TICKS(5)); // previously paced by twai_receive()'s timeout - keeps this from spinning flat out
}
