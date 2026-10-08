#!/bin/bash
# Regenerates the extra fonts: the big readout digits (font_digits_*.c) and the caption text fonts (font_text_*.c).
# They are digit-only copies of LVGL's own Montserrat, so lv_conf.h does not have to enable bigger fonts.
# Characters: space + - . 0-9  (a readout only ever shows a number, a minus sign, a decimal point or "--").
# Needs node/npm. To add a size, add it to the list here AND to GAUGE_VALUE_SIZES in gauge_style.h
# AND to the font table in gauge_app.c AND to the editors' size lists in web_monitor.cpp.
set -e
cd "$(dirname "$0")/.."
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
curl -fsSL -o "$WORK/Montserrat-Medium.ttf" https://raw.githubusercontent.com/lvgl/lvgl/v8.4.0/scripts/built_in_font/Montserrat-Medium.ttf
(cd "$WORK" && npm install --silent lv_font_conv@1.5.3)
for sz in 48 64 80 96 120; do
  "$WORK/node_modules/.bin/lv_font_conv" --font "$WORK/Montserrat-Medium.ttf" -r 0x20,0x2B,0x2D,0x2E,0x30-0x39 \
    --size $sz --bpp 4 --no-compress --format lvgl --lv-include lvgl.h --lv-font-name font_digits_$sz \
    -o esp32s3-amoled-bsp-base/font_digits_$sz.c
done
# Full-ASCII text fonts for captions / titles / units (letters included): 20 and 48 px, to go with LVGL's own 14 / 28 / 36.
for sz in 20 48; do
  "$WORK/node_modules/.bin/lv_font_conv" --font "$WORK/Montserrat-Medium.ttf" -r 0x20-0x7E \
    --size $sz --bpp 4 --no-compress --format lvgl --lv-include lvgl.h --lv-font-name font_text_$sz \
    -o esp32s3-amoled-bsp-base/font_text_$sz.c
done
echo "done"
