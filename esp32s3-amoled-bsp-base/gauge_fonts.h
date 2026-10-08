#ifndef GAUGE_FONTS_H
#define GAUGE_FONTS_H

/*
  gauge_fonts.h
  Extra fonts compiled into the sketch (made by tools/gen_fonts.sh):
    - font_digits_*.c: big READOUT fonts. Digit-only copies of Montserrat: they hold ONLY space, + - . and 0-9, which is all a
      readout ever shows (anything else would draw as nothing). Sizes 14, 28 and 36 are LVGL's normal fonts.
    - font_text_20.c / font_text_48.c: full-ASCII (letters too) copies for captions, titles and units: 20 and 48 px, between and
      above LVGL's own 14 / 28 / 36.
*/

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t font_digits_48;
extern const lv_font_t font_digits_64;
extern const lv_font_t font_digits_80;
extern const lv_font_t font_digits_96;
extern const lv_font_t font_digits_120;

// Full-ASCII (letters too) copies of Montserrat for captions, titles and units: 20 and 48 px, between and above LVGL's own
// 14 / 28 / 36. (Digit-only fonts above could not draw letters.)
extern const lv_font_t font_text_20;
extern const lv_font_t font_text_48;

#ifdef __cplusplus
}
#endif

#endif
