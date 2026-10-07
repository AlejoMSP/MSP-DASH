#ifndef GAUGE_FONTS_H
#define GAUGE_FONTS_H

/*
  gauge_fonts.h
  Big readout fonts, compiled into the sketch (font_digits_*.c, made by tools/gen_fonts.sh).
  Digit-only copies of Montserrat: they hold ONLY space, + - . and 0-9, which is all a readout
  ever shows. (Anything else would draw as nothing.) Sizes 14, 28 and 36 are LVGL's normal fonts.
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

#ifdef __cplusplus
}
#endif

#endif
