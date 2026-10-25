// The gauge icon fonts (gauge_icons.h, made by tools/icons/make_icons.py): every icon is really in every size, so the
// screen draws a symbol and not an empty box. Build: see tests/host/README.md (needs LVGL's font code).
#include "gauge_icons.h"
#include <stdio.h>
#include <string.h>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static uint32_t utf8_cp(const char *s) {   // the one character in an icon's text
  const unsigned char *u = (const unsigned char *)s;
  if (u[0] < 0x80) return u[0];
  if ((u[0] & 0xE0) == 0xC0) return ((u[0] & 0x1F) << 6) | (u[1] & 0x3F);
  if ((u[0] & 0xF0) == 0xE0) return ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
  return 0;
}
int main(void) {
  const lv_font_t *fonts[] = { &font_icons_14, &font_icons_20, &font_icons_28, &font_icons_36, &font_icons_48 };
  const int sizes[] = { 14, 20, 28, 36, 48 };
  printf("1. every icon in every size\n");
  for (int i = 1; i <= GAUGE_ICON_COUNT; i++) {
    uint32_t cp = utf8_cp(GAUGE_ICON_TEXT[i]);
    CHECK(cp == 0xE000u + (uint32_t)i, "icon text is U+E000 + its number");
    int prevW = 0;
    for (int f = 0; f < 5; f++) {
      lv_font_glyph_dsc_t g;
      bool ok = lv_font_get_glyph_dsc(fonts[f], &g, cp, 0);
      char m[96]; snprintf(m, sizeof m, "%s at %d px: drawn, %d x %d px", GAUGE_ICON_NAME[i], sizes[f], g.box_w, g.box_h);
      CHECK(ok && g.box_w > 0 && g.box_h > 0 && g.box_w >= prevW, m);
      if (f == 4) printf("    %s\n", m);
      prevW = g.box_w;
    }
  }
  printf("2. picking the font for a caption size\n");
  CHECK(gauge_icon_font(14) == &font_icons_14 && gauge_icon_font(20) == &font_icons_20 && gauge_icon_font(28) == &font_icons_28 &&
        gauge_icon_font(36) == &font_icons_36 && gauge_icon_font(48) == &font_icons_48, "each caption size gets its own icon size");
  CHECK(gauge_icon_font(0) == &font_icons_14 && gauge_icon_font(200) == &font_icons_48, "out of range: the nearest end");
  CHECK(!strcmp(GAUGE_ICON_NAME[1], "engine"), "icon 1 is MDI 'engine' (check engine)");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
