// The hub's buttons (esp32-input/hub_buttons.h): which pins fit, the resistor ladder match, debounce, screen targets,
// the ECU maps, and moving the old two-button setup over. Build: g++ -std=c++17 -I../../esp32-input -I../../esp32s3-amoled-bsp-base test_hub_buttons.cpp
#include "hub_buttons.h"
#include "btn_logic.h"
#include <stdio.h>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static HubButton B[HUB_BUTTONS];
static void lad(int i, int pin, int mv) { hb_defaults(&B[i]); B[i].kind = HB_LADDER; B[i].pin = pin; B[i].mv = mv; }
int main() {
  printf("1. pins\n");
  CHECK(hb_pin_fits(HB_DIGITAL, 12) && hb_pin_fits(HB_DIGITAL, PIN_MCP_BASE + 3) && hb_pin_fits(HB_DIGITAL, 36), "single: GPIOs, expander pins, 34-39 (outside pull-up)");
  CHECK(!hb_pin_fits(HB_DIGITAL, PIN_ADS_BASE) && !hb_pin_fits(HB_DIGITAL, 6), "single: not an ADS channel, not the flash pins");
  CHECK(hb_pin_fits(HB_LADDER, 34) && hb_pin_fits(HB_LADDER, PIN_ADS_BASE + 5) && !hb_pin_fits(HB_LADDER, 12) && !hb_pin_fits(HB_LADDER, PIN_MCP_BASE), "ladder: analog GPIOs and ADS channels only");
  CHECK(sizeof(HubButton) == 24, "saved button: 24 bytes");
  printf("2. the ladder\n");
  for (int i = 0; i < HUB_BUTTONS; i++) hb_defaults(&B[i]);
  lad(0, 140, 0); lad(1, 140, 300); lad(2, 140, 600); lad(5, 140, 1050); lad(7, 140, 1650); lad(8, 34, 300);
  CHECK(hb_ladder_match(B, HUB_BUTTONS, 140, 3290) == -1, "nothing pressed (pulled up to 3.3 V): none");
  CHECK(hb_ladder_match(B, HUB_BUTTONS, 140, 20) == 0 && hb_ladder_match(B, HUB_BUTTONS, 140, 310) == 1 && hb_ladder_match(B, HUB_BUTTONS, 140, 1700) == 7, "each voltage finds its button");
  CHECK(hb_ladder_match(B, HUB_BUTTONS, 140, 445) == -1, "halfway between two (300 / 600): none");
  CHECK(hb_ladder_match(B, HUB_BUTTONS, 140, 1250) == -1 && hb_ladder_match(B, HUB_BUTTONS, 140, 1150) == 5, "more than 150 mV off: none; 100 off: the button");
  CHECK(hb_ladder_match(B, HUB_BUTTONS, 34, 300) == 8 && hb_ladder_match(B, HUB_BUTTONS, 35, 300) == -1, "each ladder pin on its own");
  CHECK(hb_ladder_clash(B, HUB_BUTTONS, 140, 380, -1) == 1 && hb_ladder_clash(B, HUB_BUTTONS, 140, 1300, -1) == -1, "a new button 80 mV from another: refused; 250 mV away: fine");
  CHECK(hb_ladder_clash(B, HUB_BUTTONS, 140, 310, 1) == -1, "re-learning a button near its own old voltage: fine");
  printf("3. debounce\n");
  { HbDebounce d; hb_deb_init(&d); uint32_t t = 0; int got = -1, n = 0;
    auto run = [&](int raw, int ms, int deb) { for (int k = 0; k < ms; k += 10) { t += 10; int r = hb_deb_step(&d, t, raw, deb); if (r >= 0) { got = r; n++; } } };
    run(-1, 100, 30); CHECK(n == 0, "idle: nothing");
    run(3, 200, 30); CHECK(got == 3 && n == 1, "held: one press, once");
    run(-1, 100, 30); run(3, 20, 30); run(-1, 100, 30); CHECK(n == 1, "a 20 ms blip: no press");
    run(5, 40, 50); CHECK(n == 1, "ladder, 40 ms: not yet");
    run(5, 40, 50); CHECK(got == 5 && n == 2, "ladder, 80 ms: pressed");
    run(2, 100, 50); CHECK(got == 2 && n == 3, "rolling straight to another ladder button: that one presses"); }
  printf("4. screens\n");
  CHECK(hb_target_is(HBT_LEFT, "Left") && hb_target_is(HBT_CENTER, "CENTRE") && hb_target_is(HBT_CENTER, "middle") && hb_target_is(HBT_RIGHT, "right"), "Left / Center / Right by name, any case");
  CHECK(!hb_target_is(HBT_LEFT, "Right") && !hb_target_is(HBT_ALL, "Left"), "no mix-ups");
  printf("5. ECU maps\n");
  { EcuMapCfg c; ecu_map_defaults(&c); char lb[21];
    CHECK(c.count == 2 && c.cur == 0 && c.id == 0x7F1 && !strcmp(c.names[1], "Boost"), "defaults: Street, Boost on 0x7F1");
    ecu_map_next(&c); ecu_map_label(&c, lb, sizeof lb); CHECK(c.cur == 1 && !strcmp(lb, "Map 2: Boost"), "next: Map 2: Boost");
    ecu_map_next(&c); CHECK(c.cur == 0, "past the last: back to the first");
    strcpy(c.names[0], "Valet-limited"); c.names[0][12] = 0; ecu_map_label(&c, lb, sizeof lb); CHECK(strlen(lb) <= 20 && !strncmp(lb, "Map 1: Valet", 12), "the label fits the 20-character pop-up");
    c.names[0][0] = 0; ecu_map_label(&c, lb, sizeof lb); CHECK(!strcmp(lb, "Map 1"), "no name: just the number");
    c.count = 3; c.cur = 7; ecu_map_fix(&c); CHECK(c.cur == 0, "a map past the count: back to 1");
    c.count = 0; ecu_map_fix(&c); CHECK(c.count == 2 && c.id == 0x7F1, "a broken setting: defaults"); }
  printf("6. from before hub 53\n");
  { BtnBinding bind[BTN_INPUTS][BTN_LEVELS]; btn_defaults(bind); HubButton o[2];
    CHECK(hb_from_old(o, 26, PIN_MCP_BASE + 4, bind) == 2, "two buttons");
    CHECK(o[0].kind == HB_DIGITAL && o[0].pin == 26 && o[0].action == BA_PREV_PAGE && !strcmp(o[0].name, "Left"), "left: GPIO 26, previous page");
    CHECK(o[1].pin == PIN_MCP_BASE + 4 && o[1].action == BA_NEXT_PAGE && o[1].target == HBT_ALL && !o[1].active_high, "right: where it was moved to, next page, all screens, active LOW");
    bind[1][0].action = 99; hb_from_old(o, 26, 25, bind); CHECK(o[1].action == BA_NONE, "an unknown action: nothing"); }
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
