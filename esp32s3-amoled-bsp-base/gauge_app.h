#ifndef GAUGE_APP_H
#define GAUGE_APP_H

#ifdef __cplusplus
extern "C" {
#endif

// Called once from lcd_bsp.c's lcd_lvgl_Init(), in place of the
// original lv_demo_widgets() placeholder call. Builds all 3 gauge
// pages and starts the periodic update timer. Must be called while
// LVGL's mutex is already locked (matches how lv_demo_widgets() was
// originally called in that same spot).
void build_gauge_app(void);

// ---- CAN-derived values, written from Arduino's loop() (rusEFI TWAI
// parsing - completely independent of LVGL's own FreeRTOS task), read
// by our LVGL timer callback running inside LVGL's task context. Plain
// volatile floats/ints are safe here since single-word reads/writes are
// atomic in practice on this architecture - same pattern used
// throughout this project for sharing sensor values between contexts. ----
extern volatile float g_afr;
extern volatile float g_clt_f;
extern volatile float g_oilp_kpa;
extern volatile float g_oilt_f;
extern volatile float g_fuelp_kpa;
extern volatile float g_flex_pct;

// Set from Arduino's loop() when the BOOT button is pressed (debounced
// there) - our LVGL timer callback checks this and performs the actual
// lv_disp_load_scr() call safely from within LVGL's own task context.
extern volatile int g_requested_page; // -1 = no change requested, 0/1/2 = switch to that page

#ifdef __cplusplus
}
#endif

#endif
