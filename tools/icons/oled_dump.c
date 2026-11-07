// Helper for make_oled_icons.py: prints every gauge icon from font_icons_36 as a 32x32 1-bit grid ('#'/'.').
#include "gauge_icons.h"
#include <stdio.h>
int main(void) {
  for (int i = 1; i <= GAUGE_ICON_COUNT; i++) {
    lv_font_glyph_dsc_t g; uint32_t cp = 0xE000 + i;
    lv_font_get_glyph_dsc(&font_icons_36, &g, cp, 0);
    const uint8_t *b = lv_font_get_glyph_bitmap(&font_icons_36, cp);
    int ox = (32 - g.box_w) / 2, oy = (32 - g.box_h) / 2;
    printf("ICON %d\n", i);
    for (int y = 0; y < 32; y++) {
      for (int x = 0; x < 32; x++) {
        int gx = x - ox, gy = y - oy, on = 0;
        if (gx >= 0 && gy >= 0 && gx < g.box_w && gy < g.box_h) { int k = gy * g.box_w + gx; on = ((b[k / 2] >> ((k % 2) ? 0 : 4)) & 15) >= 7; }
        putchar(on ? '#' : '.');
      }
      putchar('\n');
    }
  }
  return 0;
}
