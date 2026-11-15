// The hub's pin map and configurable inputs (esp32-input/hub_pins.h, hub_inputs.h): what each GPIO can do, moving a
// function, button / switch / analog inputs, the sensor presets. Build: g++ -std=c++17 -I../../esp32-input test_hub_inputs.cpp
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
  CHECK(m[PR_BTN_NEXT] == 25 && m[PR_CAN_TX] == 21 && m[PR_I2C_SDA] == 5 && m[PR_FUEL] == 36, "defaults: buttons 25, CAN TX 21, SDA 5, fuel 36");
  CHECK(pin_role_fits(PR_FUEL, 33) && !pin_role_fits(PR_FUEL, 25), "fuel needs an analog pin");
  CHECK(!pin_role_fits(PR_CAN_TX, 34) && !pin_role_fits(PR_BTN_NEXT, 39), "CAN TX cannot go on an input-only pin; a button needs a pull-up");
  m[PR_BTN_NEXT] = 2; CHECK(pin_map_valid(m), "moving the right button to GPIO 2 is fine");
  m[PR_BTN_NEXT] = 26; CHECK(!pin_map_valid(m), "onto the left button's pin: refused (used twice)");
  pin_map_defaults(m); CHECK(pin_map_role_at(m, 22) == PR_CAN_RX && pin_map_role_at(m, 2) == -1, "who is on a pin");
  printf("3. inputs on pins\n");
  CHECK(input_pin_fits(IN_ANALOG, 33) && !input_pin_fits(IN_ANALOG, 2) && !input_pin_fits(IN_ANALOG, INPUT_MCP_BASE + 3), "analog only on ADC1 pins, not on the expander");
  CHECK(input_pin_fits(IN_BUTTON, 2) && input_pin_fits(IN_SWITCH, INPUT_MCP_BASE + 20) && input_pin_fits(IN_BUTTON, 36), "buttons / switches on GPIOs or expander pins (34-39 with an outside pull-up)");
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
  printf("5. button presses and holds\n");
  InputBtn b = {}; uint32_t t = 1000; int ev = 0;
  auto run = [&](bool on, int ms) { int got = 0; for (int k = 0; k < ms; k += 10) { t += 10; int e = input_btn_step(&b, t, on); if (e) got = e; } return got; };
  run(false, 100); CHECK(run(true, 200) == 0 && run(false, 100) == IBTN_PRESS, "a short press fires on release");
  CHECK(run(true, 1200) == IBTN_HOLD && run(false, 100) == 0, "a 1 s hold fires once while held, and no press on release");
  { InputBtn c = {}; uint32_t tt = 0; int got = 0; for (int k = 0; k < 6; k++) { tt += 5; if (input_btn_step(&c, tt, k % 2 == 0)) got = 1; } CHECK(!got, "chatter under 30 ms is ignored"); }
  (void)ev;
  CHECK(sizeof(HubInput) == 52, "saved input: 52 bytes");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
