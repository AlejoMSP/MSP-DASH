// gauge_app.h (hub) - stand-in for the screen's gauge_app.h: only the signal values the CAN decoder writes.
// The decoder itself (can_parsing.h, can_flags.h, gauge_config.h) is copied in from esp32s3-amoled-bsp-base at
// compile time (see PREREQUISITES.md) so there is one copy of it in the repo.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
extern volatile float g_rpm, g_clt_f, g_boost_kpa, g_afr, g_oilp_kpa, g_oilt_f, g_fuelp_kpa, g_flex_pct, g_tps, g_battv, g_egt_f, g_iat_f, g_ambt_f, g_hum_pct, g_fuel_pct;
extern volatile bool g_rpm_has_data, g_clt_has_data, g_boost_has_data, g_afr_has_data, g_oilp_has_data, g_oilt_has_data,
                     g_fuelp_has_data, g_flex_has_data, g_tps_has_data, g_battv_has_data, g_egt_has_data, g_iat_has_data, g_ambt_has_data, g_hum_has_data, g_fuel_has_data;
extern volatile uint16_t g_flag_bits, g_flag_seen;
#ifdef __cplusplus
}
#endif
