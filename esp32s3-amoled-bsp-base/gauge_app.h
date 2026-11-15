#ifndef GAUGE_APP_H
#define GAUGE_APP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called once from lcd_bsp.c's lcd_lvgl_Init(), in place of the
// original lv_demo_widgets() placeholder call.
void build_gauge_app(void);
// Visible screen diameter in pixels (the panel is 466; less if a bezel covers the edge, more to push gauges past it).
// Every gauge page is laid out in a circle of this size, centred on the panel. Set before build_gauge_app().
#define GAUGE_DIAMETER_DEFAULT 466        // the layout before any setting is applied (and in the host tests)
#define GAUGE_DIAMETER_BOARD_DEFAULT 520  // what a board with no saved setting uses (chosen on the real panels)
#define GAUGE_DIAMETER_MIN 300
#define GAUGE_DIAMETER_MAX 560
void gauge_app_set_diameter(int d);
// Startup sweep: every needle sweeps min -> max -> min once at boot (about 1.6 s), on every page.
// Demo: while a signal has no data its needle sweeps back and forth; off = it rests at its minimum.
void gauge_app_set_options(bool startupSweep, bool demo);

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
extern volatile float g_oilp_psi;
extern volatile float g_oilt_f;
extern volatile float g_fuelp_psi;
extern volatile float g_flex_pct;
extern volatile float g_boost_psi;
extern volatile float g_rpm;
extern volatile float g_tps;
extern volatile float g_battv;
extern volatile float g_egt_f;
extern volatile float g_iat_f;
extern volatile float g_ambt_f;
extern volatile float g_hum_pct;
extern volatile float g_fuel_pct;
extern volatile float g_vss_mph;
extern volatile float g_gps_mph;
extern volatile float g_gps_sats;
extern volatile float g_imu_val[4];   // SIG_GLAT .. SIG_YAW, from the hub
extern volatile bool g_imu_has[4];

// Set true the first time a real CAN frame updates each signal - until
// then, that cell's needle sweeps back and forth within its range
// rather than showing a possibly-misleading default value (see
// clamped_or_sweep() in gauge_app.c).
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
extern volatile bool g_egt_has_data;
extern volatile bool g_iat_has_data;
extern volatile bool g_ambt_has_data;
extern volatile bool g_hum_has_data;
extern volatile bool g_fuel_has_data;
extern volatile bool g_vss_has_data;
extern volatile bool g_gps_has_data;
extern volatile bool g_sats_has_data;
// ECU on/off status flags: one bit per signal from SIG_FIRST_FLAG up (bit 0 = SIG_REVLIM ...). Written by the CAN task and the simulator.
extern volatile uint16_t g_flag_bits;   // 1 = on
extern volatile uint16_t g_flag_seen;   // 1 = a frame has carried this flag at least once

// Set from Arduino's loop() when the BOOT button is pressed (debounced
// there) - our LVGL timer callback checks this and performs the actual
// lv_disp_load_scr() call safely from within LVGL's own task context.
extern volatile int g_requested_page; // -1 = no change requested, 0-7 = switch to that page

// Set from the web server (remote next/prev page arrows) - 0 = no
// action, 1 = advance one page, -1 = go back one page. Handled the
// same safe way as g_requested_page (checked/cleared inside the LVGL
// timer callback).
extern volatile int g_page_nav_delta;

// Hub buttons (build 172): hold Prev = hide the full-screen warning (like a tap), and short messages
// ("Buttons: this screen", "Marker 3") shown for 1.5 s. Both are handled inside the LVGL timer like the above.
extern volatile int g_alert_ack_req;
extern volatile int g_color_preset_req;   // recolour every gauge to this preset (color_presets.h)
extern volatile int g_race_req;           // race mode: 1 on (remember the page), 0 off (go back)
void gauge_app_popup(const char *text);   // safe to call from any task
void gauge_app_popup_ms(const char *text, uint32_t ms);   // same, shown for ms instead of 1.5 s

#ifdef __cplusplus
}
#endif

#endif
