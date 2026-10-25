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
static const char *const GAUGE_ICON_TEXT[GAUGE_ICON_COUNT + 1] = { "", "\xEE\x80\x81" };
static const char *const GAUGE_ICON_NAME[GAUGE_ICON_COUNT + 1] = { "", "engine" };
// The icon font for a caption size (14/20/28/36/48; anything else: the nearest).
static inline const lv_font_t *gauge_icon_font(int32_t size) {
  if (size <= 17) return &font_icons_14; if (size <= 24) return &font_icons_20; if (size <= 32) return &font_icons_28;
  if (size <= 42) return &font_icons_36; return &font_icons_48;
}
