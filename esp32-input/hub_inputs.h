// hub_inputs.h - the hub's configurable inputs (Inputs page): up to 16, each on a hub GPIO or an MCP23017 pin, set up
// as a button (an action on press and on hold), a switch (an on/off sensor) or an analog sensor (a preset or your own
// calibration). Switches and sensors fill a custom sensor slot (SIG_C1..C16), so gauges, OLED lamps and logging can use
// them. Pure C: host-tested (tests/host/test_hub_inputs.cpp).
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "hub_pins.h"

#define HUB_INPUTS 16
// Where an input is: 0..39 = hub GPIO, MCP_PIN0.. = MCP23017 board 1 PA0..PB7 (16 pins), then board 2.
#define INPUT_MCP_BASE PIN_MCP_BASE
static inline bool input_is_mcp(int pin) { return pin_is_mcp(pin); }

enum { IN_NONE = 0, IN_BUTTON = 1, IN_SWITCH = 2, IN_ANALOG = 3 };
// When an input counts as active (the page says "Active when LOW / HIGH"):
//   CONTACT_NO: LOW  - a switch to ground that closes when active, or a 12 V signal through a PC817 optocoupler board
//   CONTACT_NC: HIGH - a switch that opens when active, or a 12 V signal through a 47k / 10k divider (with a clamp)
enum { CONTACT_NO = 0, CONTACT_NC = 1 };

// Analog presets: what the sensor is, how its voltage at the pin (after the divider) becomes a reading.
enum { PRE_CUSTOM = 0, PRE_PRESS_100, PRE_PRESS_150, PRE_PRESS_200, PRE_VOLT_12, PRE_VOLT_5, PRE_NTC_BOSCH, PRE_NTC_GM, PRE_COUNT };
typedef struct {
  const char *name, *unit;
  uint8_t dec; float min, max;      // gauge template
  float mult;                       // divider: sensor volts = pin volts x mult (0 = not a voltage sensor: NTC)
  float v1, val1, v2, val2;         // two-point line in SENSOR volts (linear presets)
  float r0, beta;                   // NTC: ohms at 25 C, beta; pulled up to 3.3 V by `pullup` ohms (the input's setting)
} InputPreset;
static const InputPreset INPUT_PRESETS[PRE_COUNT] = {
  { "Custom (two-point)",                  "",    1, 0, 100, 1.0f,   0, 0, 3.3f, 100, 0, 0 },
  { "Pressure 0-100 psi (0.5-4.5 V)",      "psi", 0, 0, 100, 1.667f, 0.5f, 0, 4.5f, 100, 0, 0 },
  { "Pressure 0-150 psi (0.5-4.5 V)",      "psi", 0, 0, 150, 1.667f, 0.5f, 0, 4.5f, 150, 0, 0 },
  { "Pressure 0-200 psi (0.5-4.5 V)",      "psi", 0, 0, 200, 1.667f, 0.5f, 0, 4.5f, 200, 0, 0 },
  { "Voltage 0-16 V (47k / 10k divider)",  "V",   1, 8, 16,  5.70f,  0, 0, 1, 1, 0, 0 },
  { "Voltage 0-5 V (10k / 15k divider)",   "V",   2, 0, 5,   1.667f, 0, 0, 1, 1, 0, 0 },
  { "Temp sensor Bosch NTC (2.5k)",        "F",   0, 32, 300, 0,     0, 0, 0, 0, 2500.0f, 3450.0f },
  { "Temp sensor GM / Delphi (3k)",        "F",   0, 32, 300, 0,     0, 0, 0, 0, 3000.0f, 3970.0f },
};

typedef struct __attribute__((packed)) {
  uint8_t type;          // IN_*
  uint8_t pin;           // GPIO (0..39) or INPUT_MCP_BASE + n
  uint8_t contact;       // buttons / switches: CONTACT_NO / NC
  uint8_t slot;          // switches / sensors: custom slot 0..15 it fills
  uint8_t act_press, prm_press, act_hold, prm_hold;   // buttons: button_actions.h action + parameter
  uint8_t preset;        // sensors: PRE_*
  uint8_t spare[3];
  float mult;            // sensors: divider multiplier (custom; presets use their own)
  float v1, val1, v2, val2;   // sensors: custom two-point calibration in sensor volts
  float pullup;          // NTC: pull-up resistor ohms
  char name[16];         // what it is ("Wastegate", "Horn button")
} HubInput;
#define HUB_INPUTS_VERSION 1

// Is the pin usable for this kind of input? (Pull-up needed for buttons / switches on hub pins: 34-39 have none.)
static inline bool input_pin_fits(int type, int pin) {
  if (input_is_mcp(pin)) return type == IN_BUTTON || type == IN_SWITCH;   // the MCP23017 has no analog
  uint8_t c = gpio_caps(pin);
  if (type == IN_ANALOG) return (c & CAP_ADC) != 0;
  if (type == IN_BUTTON || type == IN_SWITCH) return (c & CAP_IN) != 0;   // 34-39 work with an outside pull-up
  return false;
}
// Active? pinLow: the pin reads LOW. NO contact: active when closed (LOW); NC: active when open (HIGH).
static inline bool input_active(const HubInput *in, bool pinLow) { return in->contact == CONTACT_NC ? !pinLow : pinLow; }

// Pin volts -> reading, for an analog input.
static inline float input_convert(const HubInput *in, float pinV) {
  const InputPreset *p = &INPUT_PRESETS[in->preset < PRE_COUNT ? in->preset : 0];
  if (p->r0 > 0) {   // NTC to ground, pulled up to 3.3 V: R = Rpull * V / (3.3 - V); 1/T = 1/T25 + ln(R/R25)/beta
    float pull = in->pullup > 0 ? in->pullup : 2490.0f;
    if (pinV <= 0.01f || pinV >= 3.29f) return NAN;   // open or shorted sensor
    float r = pull * pinV / (3.3f - pinV);
    float tk = 1.0f / (1.0f / 298.15f + logf(r / p->r0) / p->beta);
    return (tk - 273.15f) * 9.0f / 5.0f + 32.0f;
  }
  bool custom = in->preset == PRE_CUSTOM;
  float mult = custom ? (in->mult > 0 ? in->mult : 1.0f) : p->mult;
  float v1 = custom ? in->v1 : p->v1, a1 = custom ? in->val1 : p->val1, v2 = custom ? in->v2 : p->v2, a2 = custom ? in->val2 : p->val2;
  float sv = pinV * mult;
  if (in->preset == PRE_VOLT_12 || in->preset == PRE_VOLT_5) return sv;   // voltage presets: the sensor volts themselves
  if (v2 == v1) return NAN;
  return a1 + (sv - v1) * (a2 - a1) / (v2 - v1);
}

// Button presses and holds (debounced), for button inputs. One state per input.
typedef struct { bool raw, stable, held; uint32_t rawMs, downMs; } InputBtn;
enum { IBTN_NONE = 0, IBTN_PRESS = 1, IBTN_HOLD = 2 };
#define INPUT_HOLD_MS 1000
// Press fires on release if it was shorter than a hold; hold fires once the hold time is reached (the press is then skipped).
static inline int input_btn_step(InputBtn *b, uint32_t now, bool active) {
  if (active != b->raw) { b->raw = active; b->rawMs = now; }
  if (now - b->rawMs < 30 || b->raw == b->stable) {
    if (b->stable && !b->held && now - b->downMs >= INPUT_HOLD_MS) { b->held = true; return IBTN_HOLD; }
    return IBTN_NONE;
  }
  b->stable = b->raw;
  if (b->stable) { b->downMs = now; b->held = false; return IBTN_NONE; }
  return b->held ? IBTN_NONE : IBTN_PRESS;
}

static inline void input_defaults(HubInput *in) {
  memset(in, 0, sizeof *in);
  in->mult = 1.0f; in->v1 = 0; in->val1 = 0; in->v2 = 3.3f; in->val2 = 100; in->pullup = 2490.0f;
}

// ---- outputs (Inputs and pins page): up to 16, on a hub GPIO that can drive or an MCP23017 pin ----
// An output turns on when its condition holds: a signal (on / above / below a level, with hysteresis), an input being
// active, or a button action toggling it. Drive a relay module, a transistor or an LED: a pin gives 3.3 V and a few mA.
#define HUB_OUTPUTS 16
enum { OUT_NONE = 0, OUT_SIGNAL = 1, OUT_INPUT = 2, OUT_MANUAL = 3 };
enum { OUTC_ON = 0, OUTC_ABOVE = 1, OUTC_BELOW = 2 };
typedef struct __attribute__((packed)) {
  uint8_t type;        // OUT_*
  uint8_t pin;         // GPIO or PIN_MCP_BASE + n
  uint8_t active_low;  // 1 = the pin goes LOW when on (most relay modules), 0 = HIGH when on
  uint8_t blink;       // 1 = flashes (2 Hz) while on
  uint8_t sig;         // OUT_SIGNAL: the signal
  uint8_t cond;        // OUT_SIGNAL: OUTC_*
  uint8_t input;       // OUT_INPUT: which input (0..15)
  uint8_t spare;
  float level, hyst;   // OUT_SIGNAL above / below: the level, and how far back it must come to switch off
  char name[16];
} HubOutput;
#define HUB_OUTPUTS_VERSION 1
static inline void output_defaults(HubOutput *o) { memset(o, 0, sizeof *o); }
static inline bool output_pin_fits(int pin) { return pin_is_mcp(pin) || (gpio_caps(pin) & CAP_OUT); }
// Should it be on? wasOn: its state before (for the hysteresis). manualOn: its toggled state (OUT_MANUAL).
static inline bool output_want(const HubOutput *o, float value, bool hasData, bool inputActive, bool manualOn, bool wasOn) {
  switch (o->type) {
    case OUT_INPUT: return inputActive;
    case OUT_MANUAL: return manualOn;
    case OUT_SIGNAL:
      if (!hasData) return false;   // no data: off (a dead sensor must not hold a fan or a pump on)
      if (o->cond == OUTC_ON) return value != 0.0f;
      if (o->cond == OUTC_ABOVE) return wasOn ? value > o->level - o->hyst : value > o->level;
      return wasOn ? value < o->level + o->hyst : value < o->level;
    default: return false;
  }
}
// The pin level for a state: blinking flips every 250 ms while on.
static inline bool output_pin_high(const HubOutput *o, bool on, uint32_t nowMs) {
  bool lit = on && (!o->blink || (nowMs / 250) % 2 == 0);
  return o->active_low ? !lit : lit;
}
