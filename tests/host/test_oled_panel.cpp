// The hub's OLED warning panel (esp32-input/oled_panel.h): warning / danger states, inputs, the grid, the idle screen,
// the defaults. Build: g++ -std=c++17 -I../../esp32-input test_oled_panel.cpp  (run from tests/host; writes /tmp/oled_*.pbm)
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
static OledLamp L(uint8_t icon, uint8_t op, float warn, float danger, bool hasD, uint8_t in = 0, bool low = true) { OledLamp l = {}; l.icon = icon; l.sig = 1; l.op = op; l.level = warn; l.danger = danger; l.has_danger = hasD; l.input = in; l.input_low = low; return l; }
int main() {
  OledConfig c; oled_defaults(&c); uint8_t buf[1024];
  printf("1. warning and danger\n");
  OledLamp clt = L(4, OLED_ABOVE, 220, 235, true), oil = L(6, OLED_BELOW, 15, 8, true), batt = L(9, OLED_BELOW, 12.4f, 0, false);
  CHECK(oled_lamp_state(&clt, 200, true, false) == LAMP_OFF && oled_lamp_state(&clt, 225, true, false) == LAMP_WARN && oled_lamp_state(&clt, 240, true, false) == LAMP_DANGER, "coolant: 200 off, 225 warning, 240 danger");
  CHECK(oled_lamp_state(&oil, 40, true, false) == LAMP_OFF && oled_lamp_state(&oil, 12, true, false) == LAMP_WARN && oled_lamp_state(&oil, 5, true, false) == LAMP_DANGER, "oil pressure: 40 off, 12 warning, 5 danger");
  CHECK(oled_lamp_state(&batt, 11.0f, true, false) == LAMP_WARN, "no danger level set: only ever a warning");
  CHECK(oled_lamp_state(&clt, 300, false, false) == LAMP_OFF, "no live data: off");
  OledLamp flag = L(1, OLED_ON, 0, 0, false), flagD = L(30, OLED_ON, 0, 0, true);
  CHECK(oled_lamp_state(&flag, 1, true, false) == LAMP_WARN && oled_lamp_state(&flagD, 1, true, false) == LAMP_DANGER && oled_lamp_state(&flag, 0, true, false) == LAMP_OFF, "flags: lit = warning, or danger when chosen");
  printf("2. inputs\n");
  OledLamp in = L(56, OLED_INPUT, 0, 0, false, OLED_IN_PCF0 + 3, true), inH = L(54, OLED_INPUT, 0, 0, true, OLED_IN_GPIO12, false);
  CHECK(oled_lamp_state(&in, 0, false, true) == LAMP_WARN && oled_lamp_state(&in, 0, false, false) == LAMP_OFF, "lit-when-LOW input, no ECU needed");
  CHECK(oled_lamp_state(&inH, 0, false, false) == LAMP_DANGER && oled_lamp_state(&inH, 0, false, true) == LAMP_OFF, "lit-when-HIGH input marked danger");
  CHECK(oled_input_valid(OLED_IN_GPIO2) && oled_input_valid(OLED_IN_GPIO0) && oled_input_valid(OLED_IN_PCF0) && oled_input_valid(OLED_IN_PCF1 + 7), "GPIO 2/12/0 and both expanders' P0-P7 are inputs");
  CHECK(!oled_input_valid(0) && !oled_input_valid(4) && !oled_input_valid(15) && !oled_input_valid(OLED_IN_PCF1 + 8), "anything else is not");
  printf("3. drawing\n");
  uint8_t st[8] = { LAMP_WARN, LAMP_DANGER, 0, 0, 0, 0, 0, 0 };
  CHECK(oled_render(buf, &c, st, true, "12:30", "ALL OK") == 2, "two lit");
  int warnPx = count(buf, 0, 0, 32, 32), dangerPx = count(buf, 32, 0, 32, 32);
  { uint8_t b2[1024]; uint8_t s2[8] = { 0, LAMP_WARN, 0, 0, 0, 0, 0, 0 }; oled_render(b2, &c, s2, true, "", "");
    int plain = count(b2, 32, 0, 32, 32);
    CHECK(warnPx > 80 && plain > 80 && dangerPx == 1024 - plain, "warning: the icon; danger: the same cell inverted (white box, icon cut out)"); }
  CHECK(count(buf, 64, 0, 64, 64) == 0 && count(buf, 0, 32, 64, 32) == 0, "everything else dark");
  pbm(buf, "/tmp/oled_warn_danger.pbm");
  oled_render(buf, &c, st, false, "", "");
  CHECK(count(buf, 0, 0, 32, 32) == warnPx && count(buf, 32, 0, 32, 32) == 1024 - dangerPx, "off phase: danger shows the plain icon (never dark), a steady warning stays");
  c.lamp[0].blink = 1; oled_render(buf, &c, st, false, "", ""); c.lamp[0].blink = 0;
  CHECK(count(buf, 0, 0, 32, 32) == 0, "a warning with Blink ticked goes dark in the off phase");
  uint8_t st2[8] = { 0, 0, 0, 0, 0, 0, 0, LAMP_WARN };
  oled_render(buf, &c, st2, true, "", "");
  CHECK(count(buf, 96, 32, 32, 32) > 50 && count(buf, 0, 0, 96, 64) == 0, "fixed layout: the fan in its own spot");
  c.layout = OLED_COMPACT; oled_render(buf, &c, st2, true, "", ""); c.layout = OLED_FIXED;
  CHECK(count(buf, 0, 0, 32, 32) > 50 && count(buf, 32, 0, 96, 64) == 0, "compact: packed to the first spot");
  uint8_t none[8] = {};
  CHECK(oled_render(buf, &c, none, true, "2:32 PM", "ALL OK") == 0 && count(buf, 0, 16, 128, 16) > 150 && count(buf, 0, 44, 128, 8) > 30, "nothing lit: clock and ALL OK");
  printf("4. defaults\n");
  std::vector<std::string> names; std::ifstream f("../../tools/icons/icons.txt"); std::string line;
  while (std::getline(f, line)) if (!line.empty() && line[0] != '#') names.push_back(line.substr(0, line.find('|')));
  const char *want[8] = { "engine", "car-battery", "oil", "coolant-temperature", "seatbelt", "gas-station", "speedometer", "fan" };
  bool ok = names.size() == OLED_ICON_COUNT;
  for (int i = 0; i < 8; i++) ok = ok && c.lamp[i].icon >= 1 && names[c.lamp[i].icon - 1] == want[i];
  CHECK(ok, "default icons: check engine, battery, oil, coolant, seatbelt, fuel, rev limiter, fan");
  CHECK(c.lamp[3].level == 220 && c.lamp[3].danger == 235 && c.lamp[3].has_danger && c.lamp[2].danger == 8, "default levels: coolant 220 / 235, oil 15 / 8");
  CHECK(c.lamp[4].op == OLED_INPUT && c.lamp[4].input == OLED_IN_GPIO2 && c.lamp[4].input_low, "seatbelt: GPIO 2, lit when LOW");
  CHECK(c.version == OLED_CFG_VERSION && sizeof(OledLamp) == 16 && sizeof(OledConfig) == 130, "the saved setting: 8 x 16 + 2 bytes, versioned");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
