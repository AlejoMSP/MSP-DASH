# Gauge icons

Material Design Icons (pictogrammers.com, Apache-2.0) turned into LVGL fonts for the screens, plus the same icons as
SVG paths for the gauge editor's previews.

To add an icon: append a line to `icons.txt` (`mdi-name|Label`; names at pictogrammers.com/library/mdi), never reorder,
then:

    mkdir /tmp/mdi && cd /tmp/mdi && npm pack @mdi/font @mdi/js && for f in *.tgz; do tar xzf $f && mv package ${f%.tgz}; done
    cd /tmp && npm install lv_font_conv@1.5.2
    MDI_FONT=/tmp/mdi/mdi-font-7.4.47 MDI_JS=/tmp/mdi/mdi-js-7.4.47 LV_FONT_CONV=/tmp/node_modules/.bin/lv_font_conv python3 tools/icons/make_icons.py

It rewrites `esp32s3-amoled-bsp-base/font_icons_{14,20,28,36,48}.c`, `gauge_icons.h`, `gauge_icon_count.h` and
`gauge_icons_js.h`. The script stops if a name is not in MDI. `tests/host/test_icons.c` checks every icon is drawn at
every size.
