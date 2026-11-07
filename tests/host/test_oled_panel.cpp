// The hub's OLED warning panel (esp32-input/oled_panel.h): when lamps light, the grid, the idle screen, the defaults.
// Build: g++ -std=c++17 -I../../esp32-input test_oled_panel.cpp   (run from tests/host; writes /tmp/oled_*.pbm previews)
#include "oled_panel.h"
#include <stdio.h>
#include <fstream>
#include <string>
#include <vector>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static bool px(const uint8_t *b, int x, int y) { return b[(y / 8) * 128 + x] >> (y % 8) & 1; }
static int count(const uint8_t *b, int x0, int y0, int w, int h) { int n = 0; for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) n += px(b, x, y); return n; }
static void pbm(const uint8_t *b, const char *fn) { FILE *f = fopen(fn, "w"); fprintf(f, "P1\n128 64\n"); for (int y = 0; y < 64; y++) { for (int x = 0; x < 128; x++) fprintf(f, "%d ", px(b, x, y)); fprintf(f, "\n"); } fclose(f); }
int main() {
  OledConfig c; oled_defaults(&c); uint8_t buf[1024];
  printf("1. when a lamp lights\n");
  OledLamp on = { 1, 14, OLED_ON, 0, 0 }, below = { 9, 10, OLED_BELOW, 0, 12.0f }, above = { 4, 2, OLED_ABOVE, 0, 230 }, belt = { 56, 0, OLED_SEATBELT, 0, 0 };
  CHECK(oled_lamp_lit(&on, 1, true, false, true) && !oled_lamp_lit(&on, 0, true, false, true), "flag lamp: on when the flag is 1");
  CHECK(oled_lamp_lit(&below, 11.8f, true, false, true) && !oled_lamp_lit(&below, 13.9f, true, false, true), "battery: lit under 12.0 V");
  CHECK(oled_lamp_lit(&above, 235, true, false, true) && !oled_lamp_lit(&above, 190, true, false, true), "coolant: lit over 230");
  CHECK(!oled_lamp_lit(&below, 0, false, false, true) && !oled_lamp_lit(&on, 1, false, false, true), "no live data: not lit (0 V from a silent ECU is not a warning)");
  CHECK(oled_lamp_lit(&belt, 0, false, true, true) && !oled_lamp_lit(&belt, 0, false, false, true), "seatbelt, lit-when-LOW: follows the pin, needs no ECU data");
  CHECK(oled_lamp_lit(&belt, 0, false, false, false) && !oled_lamp_lit(&belt, 0, false, true, false), "seatbelt, lit-when-HIGH: the other way");
  OledLamp none = { 0, 14, OLED_ON, 0, 0 }; CHECK(!oled_lamp_lit(&none, 1, true, true, true), "an unused slot never lights");
  printf("2. the grid\n");
  bool lit[8] = { true, true, false, false, false, false, false, false };
  CHECK(oled_render(buf, &c, lit, true, "12:30", "ALL OK") == 2, "two lit");
  CHECK(count(buf, 0, 0, 32, 32) > 100 && count(buf, 32, 0, 32, 32) > 100 && count(buf, 64, 0, 64, 64) == 0 && count(buf, 0, 32, 128, 32) == 0, "fixed layout: check engine in cell 1, battery in cell 2, the rest dark");
  pbm(buf, "/tmp/oled_cel_battery.pbm");
  bool lit2[8] = { false, false, false, false, false, false, false, true };
  oled_render(buf, &c, lit2, true, "12:30", "");
  CHECK(count(buf, 96, 32, 32, 32) > 50 && count(buf, 0, 0, 96, 64) == 0, "fixed: the fan stays in its own spot (bottom right)");
  c.layout = OLED_COMPACT; oled_render(buf, &c, lit2, true, "12:30", "");
  CHECK(count(buf, 0, 0, 32, 32) > 50 && count(buf, 32, 0, 96, 64) == 0, "compact: the only lit lamp moves to the first spot");
  c.layout = OLED_FIXED;
  printf("3. blinking\n");
  oled_render(buf, &c, lit, false, "", "");
  CHECK(count(buf, 0, 0, 32, 32) == 0 && count(buf, 32, 0, 32, 32) > 100, "blink off phase: the blinking check engine is dark, the steady battery stays");
  CHECK(oled_render(buf, &c, lit, false, "12:30", "ALL OK") == 2 && count(buf, 0, 16, 128, 16) < 300, "a blinking lamp still counts as lit (no clock in its off phase)");
  printf("4. nothing lit\n");
  bool off[8] = {};
  CHECK(oled_render(buf, &c, off, true, "2:32 PM", "ALL OK") == 0, "none lit");
  CHECK(count(buf, 0, 16, 128, 16) > 150 && count(buf, 0, 44, 128, 8) > 30, "the clock big in the middle, ALL OK under it");
  pbm(buf, "/tmp/oled_idle.pbm");
  printf("5. defaults match the icon list\n");
  std::vector<std::string> names; std::ifstream f("../../tools/icons/icons.txt"); std::string line;
  while (std::getline(f, line)) if (!line.empty() && line[0] != '#') names.push_back(line.substr(0, line.find('|')));
  const char *want[8] = { "engine", "car-battery", "oil", "coolant-temperature", "seatbelt", "gas-station", "speedometer", "fan" };
  bool ok = names.size() == OLED_ICON_COUNT;
  for (int i = 0; i < 8; i++) ok = ok && c.lamp[i].icon >= 1 && names[c.lamp[i].icon - 1] == want[i];
  CHECK(ok, "default lamps: check engine, battery, oil, coolant, seatbelt, fuel, rev limiter, fan (right icons)");
  CHECK(sizeof(OledConfig) == 8 * 8 + 2, "the saved setting is 66 bytes");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
