// The staged shift light: the hub's level with hysteresis (esp32-input/shift_light.h) and each screen's colour from the
// level and the left-to-right list (link_packets.h). Build: g++ -std=c++17 -I../../esp32-input -I../../esp32s3-amoled-bsp-base test_shift_light.cpp
#include "shift_light.h"
#include "link_packets.h"
#include <stdio.h>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
int main() {
  ShiftCfg c; shift_defaults(&c);
  printf("1. settings\n");
  CHECK(shift_valid(&c) && !c.on && c.start == 5000 && c.shift == 6800 && c.over == 7200, "defaults: off, 5000 / 6800 / 7200, valid");
  ShiftCfg b = c; b.shift = 4000; CHECK(!shift_valid(&b), "shift below start: refused");
  b = c; b.over = 6000; CHECK(!shift_valid(&b), "over-rev below shift: refused");
  b = c; b.over = 0; CHECK(shift_valid(&b), "over-rev 0 = off: fine");
  printf("2. levels, 3 screens\n");
  CHECK(shift_level_raw(&c, 3, 7000) == 0, "off: always 0");
  c.on = 1;
  CHECK(shift_level_raw(&c, 3, 4000) == 0 && shift_level_raw(&c, 3, 5000) == 1, "below the start: 0; at the start: the left screen");
  CHECK(shift_level_raw(&c, 3, 5600) == 2 && shift_level_raw(&c, 3, 6200) == 3, "5600: two screens; 6200: three");
  CHECK(shift_level_raw(&c, 3, 6800) == 4 && shift_level_raw(&c, 3, 7199) == 4, "at the shift point: all green");
  CHECK(shift_level_raw(&c, 3, 7200) == 5, "over-rev: all red");
  b = c; b.over = 0; CHECK(shift_level_raw(&b, 3, 9000) == 4, "no over-rev: stays green");
  CHECK(shift_level_raw(&c, 1, 5900) == 1 && shift_level_raw(&c, 1, 6800) == 2, "one screen: yellow from the start, green at the shift point");
  CHECK(shift_level_raw(&c, 0, 7000) == 0, "no screens: nothing");
  printf("3. hysteresis\n");
  CHECK(shift_level(&c, 3, 5620, 1) == 2, "climbing: up at once");
  CHECK(shift_level(&c, 3, 5560, 2) == 2 && shift_level(&c, 3, 5490, 2) == 1, "falling: stays until 100 RPM under (5600 -> 5500)");
  CHECK(shift_level(&c, 3, 6750, 4) == 4 && shift_level(&c, 3, 6690, 4) == 3, "green holds to 6700");
  CHECK(shift_level(&c, 3, 3000, 5) == 0, "a big drop: straight off");
  printf("4. each screen's colour\n");
  int n = 0;
  CHECK(link_shift_index("Left,Center,Right", "center", &n) == 1 && n == 3, "center is second of 3 (any case)");
  CHECK(link_shift_index("Left,Center,Right", "Rear", &n) == -1, "a screen not in the list: -1");
  CHECK(link_shift_index("Left,Center", "Cent", &n) == -1, "no partial name match");
  CHECK(link_shift_color(2, 0, 3) == 1 && link_shift_color(2, 1, 3) == 1 && link_shift_color(2, 2, 3) == 0, "level 2: left and center yellow, right dark");
  CHECK(link_shift_color(4, 2, 3) == 2 && link_shift_color(5, 0, 3) == 3, "level 4: green; level 5: red");
  CHECK(link_shift_color(0, 0, 3) == 0 && link_shift_color(3, -1, 3) == 0, "off, or not in the list: dark");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
