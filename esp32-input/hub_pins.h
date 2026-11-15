// hub_pins.h - EVERY GPIO the hub uses, in one place. Each function ("role") has a default GPIO; the Inputs page can
// move a role to another GPIO (saved, applied at the next start). docs/HUB_PINS.md lists the defaults with their
// terminals, and tests/host/test_hub_pins.js fails if the two disagree. Pure C: host-tested.
#pragma once
#include <stdint.h>
#include <stdbool.h>

// ---- what each GPIO of the ESP32-WROOM can do ----
enum { CAP_IN = 1, CAP_OUT = 2, CAP_ADC = 4, CAP_PULLUP = 8 };   // ADC = ADC1 (ADC2 stops working with WiFi on)
static inline uint8_t gpio_caps(int g) {
  switch (g) {
    case 32: case 33: return CAP_IN | CAP_OUT | CAP_ADC | CAP_PULLUP;
    case 34: case 35: case 36: case 39: return CAP_IN | CAP_ADC;           // input only, no internal pull-up
    case 0: case 2: case 4: case 5: case 12: case 13: case 14: case 15: case 16: case 17: case 18: case 19:
    case 21: case 22: case 23: case 25: case 26: case 27: return CAP_IN | CAP_OUT | CAP_PULLUP;
    default: return 0;   // 1, 3 (USB serial), 6-11 (flash), and numbers the chip does not have
  }
}
// A warning for a pin with a power-up rule ("" = none).
static inline const char *gpio_note(int g) {
  switch (g) {
    case 0: return "BOOT button: must not be held LOW at power-up";
    case 2: return "must not be held HIGH while flashing over USB";
    case 12: return "must not be held HIGH at power-up";
    case 15: return "LOW at power-up silences the boot messages";
    case 34: case 35: case 36: case 39: return "input only, no internal pull-up";
    default: return "";
  }
}
#define GPIO_LIST { 0, 2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39 }

// ---- the hub's fixed functions ----
enum HubPinRole {
  PR_BTN_NEXT, PR_BTN_PREV, PR_FUEL, PR_POT, PR_LIGHTS, PR_DIMMER, PR_CAN_TX, PR_CAN_RX, PR_EGT_SCK, PR_EGT_SO, PR_EGT_CS,
  PR_GPS_RX, PR_GPS_TX, PR_DHT, PR_RF_GDO0, PR_RF_CS, PR_RF_SCK, PR_RF_MOSI, PR_RF_MISO, PR_I2C_SDA, PR_I2C_SCL, PR_COUNT
};
typedef struct { const char *key; const char *name; uint8_t def; uint8_t need; const char *term; } HubPinRoleInfo;
// key: short id for the web page and NVS; def: default GPIO; need: CAP_ bits the pin must have; term: default terminal.
static const HubPinRoleInfo HUB_PIN_ROLES[PR_COUNT] = {
  { "btn_next", "Right (next) button",          25, CAP_IN | CAP_PULLUP, "L14" },
  { "btn_prev", "Left (previous) button",       26, CAP_IN | CAP_PULLUP, "L15" },
  { "fuel",     "Fuel level sender",            36, CAP_ADC,             "L8"  },
  { "pot",      "Brightness knob",              34, CAP_ADC,             "L10" },
  { "lights",   "Lights-on wire (divider)",     35, CAP_ADC,             "L11" },
  { "dimmer",   "Dash dimmer (divider)",        32, CAP_ADC,             "L12" },
  { "can_tx",   "CAN TX (SN65HVD230 CTX)",      21, CAP_OUT,             "R11" },
  { "can_rx",   "CAN RX (SN65HVD230 CRX)",      22, CAP_IN,              "R8"  },
  { "egt_sck",  "EGT MAX6675 SCK",              18, CAP_OUT,             "R14" },
  { "egt_so",   "EGT MAX6675 SO",               19, CAP_IN,              "R13" },
  { "egt_cs",   "EGT MAX6675 CS",               23, CAP_OUT,             "R7"  },
  { "gps_rx",   "GPS: GPS TX to hub RX",        16, CAP_IN,              "R17" },
  { "gps_tx",   "GPS: hub TX to GPS RX",        17, CAP_OUT,             "R16" },
  { "dht",      "DHT22 cabin temp/humidity",     4, CAP_IN | CAP_OUT,    "R18" },
  { "rf_gdo0",  "CC1101 GDO0 (data)",           33, CAP_IN,              "L13" },
  { "rf_cs",    "CC1101 CSN",                   27, CAP_OUT,             "L16" },
  { "rf_sck",   "CC1101 SCK",                   14, CAP_OUT,             "L17" },
  { "rf_mosi",  "CC1101 MOSI",                  13, CAP_OUT,             "L20" },
  { "rf_miso",  "CC1101 MISO",                  39, CAP_IN,              "L9"  },
  { "i2c_sda",  "I2C SDA (OLED, MPU6050, MCP23017)", 5, CAP_IN | CAP_OUT, "R15" },
  { "i2c_scl",  "I2C SCL (OLED, MPU6050, MCP23017)", 15, CAP_IN | CAP_OUT, "R21" },
};
// Can this role sit on that GPIO?
static inline bool pin_role_fits(int role, int g) {
  if (role < 0 || role >= PR_COUNT) return false;
  uint8_t c = gpio_caps(g), need = HUB_PIN_ROLES[role].need;
  return c && (c & need) == need;
}
// A whole map: every role on a GPIO that fits it, and no GPIO used twice.
static inline bool pin_map_valid(const uint8_t map[PR_COUNT]) {
  for (int r = 0; r < PR_COUNT; r++) {
    if (!pin_role_fits(r, map[r])) return false;
    for (int q = 0; q < r; q++) if (map[q] == map[r]) return false;
  }
  return true;
}
static inline void pin_map_defaults(uint8_t map[PR_COUNT]) { for (int r = 0; r < PR_COUNT; r++) map[r] = HUB_PIN_ROLES[r].def; }
// The role on a GPIO, or -1.
static inline int pin_map_role_at(const uint8_t map[PR_COUNT], int g) { for (int r = 0; r < PR_COUNT; r++) if (map[r] == g) return r; return -1; }

// The map in use (filled at the start of setup() from NVS "pins", defaults if none or invalid).
extern uint8_t g_pins[PR_COUNT];
#define BTN_NEXT   (g_pins[PR_BTN_NEXT])
#define BTN_PREV   (g_pins[PR_BTN_PREV])
#define FUEL_PIN   (g_pins[PR_FUEL])
#define POT_PIN    (g_pins[PR_POT])
#define LIGHTS_PIN (g_pins[PR_LIGHTS])
#define DIMMER_PIN (g_pins[PR_DIMMER])
#define CAN_TX_PIN ((gpio_num_t)g_pins[PR_CAN_TX])
#define CAN_RX_PIN ((gpio_num_t)g_pins[PR_CAN_RX])
#define EGT_SCK    (g_pins[PR_EGT_SCK])
#define EGT_SO     (g_pins[PR_EGT_SO])
#define EGT_CS     (g_pins[PR_EGT_CS])
#define GPS_RX_PIN (g_pins[PR_GPS_RX])
#define GPS_TX_PIN (g_pins[PR_GPS_TX])
#define DHT_PIN    (g_pins[PR_DHT])
#define RF_GDO0    (g_pins[PR_RF_GDO0])
#define RF_CS      (g_pins[PR_RF_CS])
#define RF_SCK     (g_pins[PR_RF_SCK])
#define RF_MOSI    (g_pins[PR_RF_MOSI])
#define RF_MISO    (g_pins[PR_RF_MISO])
#define I2C_SDA    (g_pins[PR_I2C_SDA])
#define I2C_SCL    (g_pins[PR_I2C_SCL])
