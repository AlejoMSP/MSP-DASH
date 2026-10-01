#ifndef GAUGE_APP_H
#define GAUGE_APP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called once from lcd_bsp.c's lcd_lvgl_Init(), in place of the
// original lv_demo_widgets() placeholder call.
void build_gauge_app(void);

// The actual number of active pages - the .ino's BOOT button handler
// uses this instead of a hardcoded count, since the page count is now
// fully data-driven (page/cell config) and can change at runtime.
int gauge_app_get_num_pages(void);

// ---- CAN-derived values, written from Arduino's loop() (rusEFI TWAI
// parsing), read by our LVGL timer callback running inside LVGL's task
// context. Plain volatile floats/ints are safe here since single-word
// reads/writes are atomic in practice on this architecture. ----
extern volatile float g_afr;
extern volatile float g_clt_f;
extern volatile float g_oilp_kpa;
extern volatile float g_oilt_f;
extern volatile float g_fuelp_kpa;
extern volatile float g_flex_pct;
extern volatile float g_boost_kpa;
extern volatile float g_rpm;
extern volatile float g_tps;
extern volatile float g_battv;

// Set true the first time a real CAN frame updates each signal - until
// then, that gauge's needle sits at its rest value (min, or 0 for
// boost/vacuum) rather than showing a possibly-misleading default value.
extern volatile bool g_afr_has_data;
extern volatile bool g_clt_has_data;
extern volatile bool g_oilp_has_data;
extern volatile bool g_oilt_has_data;
extern volatile bool g_fuelp_has_data;
extern volatile bool g_flex_has_data;
extern volatile bool g_boost_has_data;
extern volatile bool g_rpm_has_data;
extern volatile bool g_tps_has_data;
extern volatile bool g_battv_has_data;

// Set from Arduino's loop() when the BOOT button is pressed (debounced
// there) - our LVGL timer callback checks this and performs the actual
// lv_disp_load_scr() call safely from within LVGL's own task context.
extern volatile int g_requested_page; // -1 = no change requested, 0-7 = switch to that page

#ifdef __cplusplus
}
#endif

#endif
