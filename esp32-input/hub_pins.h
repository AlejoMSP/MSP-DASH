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
// Hub 53 took the two page buttons out of this list (they are ordinary buttons on the Buttons page now). A map saved
// before that has 21 entries: PIN_MAP_OLD_COUNT, buttons first (next, previous), then the rest in this order.
#define PIN_MAP_OLD_COUNT 21
enum HubPinRole {
  PR_FUEL, PR_POT, PR_LIGHTS, PR_DIMMER, PR_CAN_TX, PR_CAN_RX, PR_EGT_SCK, PR_EGT_SO, PR_EGT_CS,
  PR_GPS_RX, PR_GPS_TX, PR_DHT, PR_RF_GDO0, PR_RF_CS, PR_RF_SCK, PR_RF_MOSI, PR_RF_MISO, PR_I2C_SDA, PR_I2C_SCL, PR_COUNT
};
typedef struct { const char *key; const char *name; uint8_t def; uint8_t need; const char *term; } HubPinRoleInfo;
// key: short id for the web page and NVS; def: default GPIO; need: CAP_ bits the pin must have; term: default terminal.
static const HubPinRoleInfo HUB_PIN_ROLES[PR_COUNT] = {
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
// Pins 100.. are MCP23017 expander pins: board 1 PA0..PB7 = 100..115, board 2 = 116..131 (used by buttons, inputs and
// outputs; none of the fixed functions can run through the expander).
#define PIN_MCP_BASE 100
static inline bool pin_is_mcp(int p) { return p >= PIN_MCP_BASE && p < PIN_MCP_BASE + 32; }
// Pins 140.. are ADS1115 analog channels: board 1 (0x48) A0..A3 = 140..143, 0x49 = 144..147, 0x4A, 0x4B (up to 16).
#define PIN_ADS_BASE 140
static inline bool pin_is_ads(int p) { return p >= PIN_ADS_BASE && p < PIN_ADS_BASE + 16; }
// Roles that may sit on an expander pin: none since hub 53 (the two page buttons moved to the Buttons page).
static inline bool pin_role_mcp_ok(int role) { (void)role; return false; }
// Can this role sit on that GPIO (or expander pin)?
static inline bool pin_role_fits(int role, int g) {
  if (role < 0 || role >= PR_COUNT) return false;
  if (pin_is_mcp(g)) return pin_role_mcp_ok(role);
  if (pin_is_ads(g)) return HUB_PIN_ROLES[role].need == CAP_ADC;   // analog-only roles (fuel, knob, lights, dimmer)
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
// A map saved before hub 53: the two button pins out (for the Buttons page), the rest in the new order.
static inline void pin_map_upgrade(const uint8_t old[PIN_MAP_OLD_COUNT], uint8_t map[PR_COUNT], uint8_t *btnNext, uint8_t *btnPrev) {
  *btnNext = old[0]; *btnPrev = old[1];
  for (int r = 0; r < PR_COUNT; r++) map[r] = old[r + 2];
}
#define PIN_BOOT_BTN 0       // the DevKitC's BOOT button: factory reset (first 15 s after power-up), unless GPIO 0 is given to something
#define PIN_BTN_NEXT_DEF 25   // the default page buttons (Buttons page): right / next on GPIO 25 (L14)
#define PIN_BTN_PREV_DEF 26   //                                          left / previous on GPIO 26 (L15)
// The role on a GPIO, or -1.
static inline int pin_map_role_at(const uint8_t map[PR_COUNT], int g) { for (int r = 0; r < PR_COUNT; r++) if (map[r] == g) return r; return -1; }

// The map in use (filled at the start of setup() from NVS "pins", defaults if none or invalid).
extern uint8_t g_pins[PR_COUNT];
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
