// gauge_icons.h - MADE BY tools/icons/make_icons.py from Material Design Icons 7.4.47 (Apache-2.0). Do not edit by hand.
// Icon n (1..GAUGE_ICON_COUNT) is character U+E000+n in font_icons_<14|20|28|36|48>; 0 = no icon.
#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
extern const lv_font_t font_icons_14;
extern const lv_font_t font_icons_20;
extern const lv_font_t font_icons_28;
extern const lv_font_t font_icons_36;
extern const lv_font_t font_icons_48;
#ifdef __cplusplus
}
#endif
#include "gauge_icon_count.h"
// UTF-8 text of each icon (index 0 unused)
static const char *const GAUGE_ICON_TEXT[GAUGE_ICON_COUNT + 1] = { "", "\xEE\x80\x81", "\xEE\x80\x82", "\xEE\x80\x83", "\xEE\x80\x84", "\xEE\x80\x85", "\xEE\x80\x86", "\xEE\x80\x87", "\xEE\x80\x88", "\xEE\x80\x89", "\xEE\x80\x8A", "\xEE\x80\x8B", "\xEE\x80\x8C", "\xEE\x80\x8D", "\xEE\x80\x8E", "\xEE\x80\x8F", "\xEE\x80\x90", "\xEE\x80\x91", "\xEE\x80\x92", "\xEE\x80\x93", "\xEE\x80\x94", "\xEE\x80\x95", "\xEE\x80\x96", "\xEE\x80\x97", "\xEE\x80\x98", "\xEE\x80\x99", "\xEE\x80\x9A", "\xEE\x80\x9B", "\xEE\x80\x9C", "\xEE\x80\x9D", "\xEE\x80\x9E", "\xEE\x80\x9F", "\xEE\x80\xA0", "\xEE\x80\xA1", "\xEE\x80\xA2", "\xEE\x80\xA3", "\xEE\x80\xA4", "\xEE\x80\xA5", "\xEE\x80\xA6", "\xEE\x80\xA7", "\xEE\x80\xA8", "\xEE\x80\xA9", "\xEE\x80\xAA", "\xEE\x80\xAB", "\xEE\x80\xAC", "\xEE\x80\xAD", "\xEE\x80\xAE", "\xEE\x80\xAF", "\xEE\x80\xB0", "\xEE\x80\xB1", "\xEE\x80\xB2", "\xEE\x80\xB3", "\xEE\x80\xB4", "\xEE\x80\xB5", "\xEE\x80\xB6", "\xEE\x80\xB7", "\xEE\x80\xB8", "\xEE\x80\xB9", "\xEE\x80\xBA", "\xEE\x80\xBB", "\xEE\x80\xBC", "\xEE\x80\xBD", "\xEE\x80\xBE", "\xEE\x80\xBF", "\xEE\x81\x80", "\xEE\x81\x81", "\xEE\x81\x82", "\xEE\x81\x83", "\xEE\x81\x84", "\xEE\x81\x85", "\xEE\x81\x86", "\xEE\x81\x87", "\xEE\x81\x88", "\xEE\x81\x89", "\xEE\x81\x8A", "\xEE\x81\x8B", "\xEE\x81\x8C", "\xEE\x81\x8D", "\xEE\x81\x8E", "\xEE\x81\x8F" };
static const char *const GAUGE_ICON_NAME[GAUGE_ICON_COUNT + 1] = { "", "engine", "engine-outline", "engine-off", "coolant-temperature", "car-coolant-level", "oil", "oil-temperature", "oil-level", "car-battery", "battery-alert", "lightning-bolt", "flash", "gas-station", "gas-station-outline", "fuel", "fuel-cell", "car-turbocharger", "turbine", "air-filter", "smoke", "molecule-co2", "thermometer", "thermometer-high", "thermometer-alert", "water-percent", "fan", "fan-alert", "air-conditioner", "gauge", "speedometer", "speedometer-medium", "speedometer-slow", "car-speed-limiter", "car-cruise-control", "car-shift-pattern", "car-clutch", "steering", "tire", "car-tire-alert", "car-brake-alert", "car-brake-parking", "car-brake-abs", "car-brake-hold", "car-traction-control", "car-esp", "car-light-high", "car-light-dimmed", "car-light-fog", "car-parking-lights", "car-light-alert", "hazard-lights", "arrow-left-bold", "arrow-right-bold", "car-door", "car-door-lock", "seatbelt", "airbag", "car-key", "car-defrost-front", "car-defrost-rear", "wiper", "car-windshield", "snowflake", "car-wrench", "wrench", "car-cog", "car-info", "car-emergency", "alert", "alert-octagon", "information", "timer-outline", "flag-checkered", "crosshairs-gps", "record-circle", "car-sports", "car-convertible", "car-side", "car-back" };
// The icon font for a caption size (14/20/28/36/48; anything else: the nearest).
static inline const lv_font_t *gauge_icon_font(int32_t size) {
  if (size <= 17) return &font_icons_14; if (size <= 24) return &font_icons_20; if (size <= 32) return &font_icons_28;
  if (size <= 42) return &font_icons_36; return &font_icons_48;
}
