// The hub's pin map and configurable inputs (esp32-input/hub_pins.h, hub_inputs.h): what each GPIO can do, moving a
// function, switch / analog inputs, the sensor presets, outputs. Build: g++ -std=c++17 -I../../esp32-input test_hub_inputs.cpp
#include "hub_inputs.h"
#include <stdio.h>
uint8_t g_pins[PR_COUNT];
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static bool near(float a, float b, float t) { return fabsf(a - b) <= t; }
int main() {
  printf("1. what each GPIO can do\n");
  CHECK((gpio_caps(34) & CAP_ADC) && !(gpio_caps(34) & CAP_OUT) && !(gpio_caps(34) & CAP_PULLUP), "GPIO 34: analog input only, no pull-up");
  CHECK((gpio_caps(25) & CAP_OUT) && !(gpio_caps(25) & CAP_ADC), "GPIO 25: digital, no analog (ADC2 does not work with WiFi)");
  CHECK(gpio_caps(6) == 0 && gpio_caps(1) == 0 && gpio_caps(3) == 0 && gpio_caps(24) == 0, "flash pins, USB serial and missing numbers: nothing");
  printf("2. the pin map\n");
  uint8_t m[PR_COUNT]; pin_map_defaults(m);
  CHECK(pin_map_valid(m), "the defaults are a valid map");
  CHECK(m[PR_CAN_TX] == 21 && m[PR_I2C_SDA] == 5 && m[PR_FUEL] == 36 && pin_map_role_at(m, 25) == -1 && pin_map_role_at(m, 26) == -1, "defaults: CAN TX 21, SDA 5, fuel 36; 25 / 26 free (buttons page)");
  CHECK(pin_role_fits(PR_FUEL, 33) && !pin_role_fits(PR_FUEL, 25), "fuel needs an analog pin");
  CHECK(!pin_role_fits(PR_CAN_TX, 34), "CAN TX cannot go on an input-only pin");
  m[PR_DHT] = 2; CHECK(pin_map_valid(m), "moving the DHT22 to GPIO 2 is fine");
  m[PR_DHT] = 22; CHECK(!pin_map_valid(m), "onto CAN RX: refused (used twice)");
  pin_map_defaults(m); CHECK(pin_map_role_at(m, 22) == PR_CAN_RX && pin_map_role_at(m, 2) == -1, "who is on a pin");
  printf("3. inputs on pins\n");
  CHECK(input_pin_fits(IN_ANALOG, 33) && !input_pin_fits(IN_ANALOG, 2) && !input_pin_fits(IN_ANALOG, INPUT_MCP_BASE + 3), "analog only on ADC1 pins, not on the expander");
  CHECK(input_pin_fits(IN_SWITCH, 2) && input_pin_fits(IN_SWITCH, INPUT_MCP_BASE + 20) && input_pin_fits(IN_SWITCH, 36), "switches on GPIOs or expander pins (34-39 with an outside pull-up)");
  HubInput in; input_defaults(&in);
  in.contact = CONTACT_NO; CHECK(input_active(&in, true) && !input_active(&in, false), "NO: active when the pin is pulled LOW (contact closed)");
  in.contact = CONTACT_NC; CHECK(!input_active(&in, true) && input_active(&in, false), "NC: active when the contact opens (pin HIGH)");
  printf("4. sensor presets\n");
  input_defaults(&in); in.type = IN_ANALOG;
  in.preset = PRE_PRESS_100; CHECK(near(input_convert(&in, 2.5f / 1.667f), 50, 0.2f) && near(input_convert(&in, 0.5f / 1.667f), 0, 0.2f), "0-100 psi: 2.5 V sensor = 50 psi, 0.5 V = 0");
  in.preset = PRE_PRESS_150; CHECK(near(input_convert(&in, 4.5f / 1.667f), 150, 0.3f), "0-150 psi: 4.5 V = 150");
  in.preset = PRE_VOLT_12; CHECK(near(input_convert(&in, 2.526f), 14.4f, 0.02f), "12 V divider: 2.53 V at the pin = 14.4 V");
  in.preset = PRE_NTC_BOSCH; in.pullup = 2500;
  CHECK(near(input_convert(&in, 1.65f), 77, 0.5f), "Bosch NTC: equal to its pull-up = 25 C / 77 F");
  CHECK(input_convert(&in, 0.4f) > 150 && input_convert(&in, 2.9f) < 20, "lower voltage = hotter");
  CHECK(isnan(input_convert(&in, 3.3f)) && isnan(input_convert(&in, 0.0f)), "open or shorted sensor: no reading (not a number)");
  in.preset = PRE_CUSTOM; in.mult = 2; in.v1 = 1; in.val1 = 10; in.v2 = 3; in.val2 = 30;
  CHECK(near(input_convert(&in, 1.0f), 20, 0.01f), "custom: 1 V at the pin x2 = 2 V on the line from (1 V, 10) to (3 V, 30) = 20");
  in.v2 = 1; CHECK(isnan(input_convert(&in, 1.0f)), "custom with the same two voltages: no reading");
  printf("5. saved layout\n");
  CHECK(sizeof(HubInput) == 52, "saved input: 52 bytes");
  printf("6. expander pins and outputs\n");
  { uint8_t mm[PR_COUNT]; pin_map_defaults(mm);
    mm[PR_FUEL] = PIN_MCP_BASE + 5; CHECK(!pin_map_valid(mm), "the fuel sender cannot (no analog on the expander)");
    pin_map_defaults(mm); mm[PR_CAN_TX] = PIN_MCP_BASE + 6; CHECK(!pin_map_valid(mm), "nor can CAN"); }
  CHECK(output_pin_fits(13) && output_pin_fits(PIN_MCP_BASE + 9) && !output_pin_fits(34) && !output_pin_fits(6), "outputs: GPIOs that drive and expander pins, not 34-39 or the flash");
  HubOutput o; output_defaults(&o); o.type = OUT_SIGNAL; o.sig = 2; o.cond = OUTC_ABOVE; o.level = 200; o.hyst = 5;
  CHECK(!output_want(&o, 199, true, false, false, false) && output_want(&o, 201, true, false, false, false), "above 200: on at 201");
  CHECK(output_want(&o, 197, true, false, false, true) && !output_want(&o, 194, true, false, false, true), "hysteresis: stays on down to 195, off at 194");
  CHECK(!output_want(&o, 300, false, false, false, true), "no data: off (a dead sensor must not hold a fan on)");
  o.cond = OUTC_BELOW; o.level = 12; o.hyst = 0.5f;
  CHECK(output_want(&o, 11.9f, true, false, false, false) && output_want(&o, 12.3f, true, false, false, true) && !output_want(&o, 12.6f, true, false, false, true), "below 12 V, back off above 12.5");
  o.cond = OUTC_ON; CHECK(output_want(&o, 1, true, false, false, false) && !output_want(&o, 0, true, false, false, true), "a flag: on when set");
  o.type = OUT_INPUT; CHECK(output_want(&o, 0, false, true, false, false) && !output_want(&o, 0, false, false, false, true), "follows an input");
  o.type = OUT_MANUAL; CHECK(output_want(&o, 0, false, false, true, false) && !output_want(&o, 0, false, false, false, true), "toggled");
  o.active_low = 1; o.blink = 0; CHECK(!output_pin_high(&o, true, 0) && output_pin_high(&o, false, 0), "relay (LOW = on): on drives LOW, off HIGH");
  o.active_low = 0; o.blink = 1; CHECK(output_pin_high(&o, true, 100) && !output_pin_high(&o, true, 300) && !output_pin_high(&o, false, 100), "blinking: on/off every 250 ms while on, steady off");
  CHECK(sizeof(HubOutput) == 32, "saved output: 32 bytes");
  { uint8_t mm[PR_COUNT]; pin_map_defaults(mm); mm[PR_FUEL] = PIN_ADS_BASE + 2; CHECK(pin_map_valid(mm), "the fuel sender can move onto an ADS1115 channel");
    pin_map_defaults(mm); mm[PR_DHT] = PIN_ADS_BASE; CHECK(!pin_map_valid(mm), "the DHT22 cannot (analog only)"); }
  printf("7. a pin map saved before hub 53\n");
  { uint8_t old[PIN_MAP_OLD_COUNT], mm[PR_COUNT], nx = 0, pv = 0; old[0] = PIN_MCP_BASE + 2; old[1] = 26;
    uint8_t d[PR_COUNT]; pin_map_defaults(d); for (int r = 0; r < PR_COUNT; r++) old[r + 2] = d[r]; old[2 + PR_DHT] = 2;
    pin_map_upgrade(old, mm, &nx, &pv);
    CHECK(PR_COUNT + 2 == PIN_MAP_OLD_COUNT, "the old map had the two buttons on top of today's 19");
    CHECK(nx == PIN_MCP_BASE + 2 && pv == 26 && mm[PR_DHT] == 2 && mm[PR_CAN_TX] == 21 && pin_map_valid(mm), "buttons taken out (next on the expander kept), the rest in order, moves kept"); }
  CHECK(input_pin_fits(IN_ANALOG, PIN_ADS_BASE + 7) && !input_pin_fits(IN_SWITCH, PIN_ADS_BASE) && !output_pin_fits(PIN_ADS_BASE), "ADS channels: analog sensors only, not switches or outputs");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
