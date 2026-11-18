// hub_buttons.h - every button the hub reads, set up on the Buttons page (hub 53 on). Up to 24 buttons:
//   digital: one button on a hub GPIO or an MCP23017 pin, active LOW or HIGH;
//   ladder:  several buttons on ONE analog pin (GPIO 32-39 or an ADS1115 channel), each through its own resistor, so
//            each press gives its own voltage (steering wheel buttons). Each button's voltage is learned on the page.
// Each button runs one action on a press (button_actions.h), sent to the screen it names (Left, Center, Right or all).
// Also the ECU map setting for the "ECU map (next)" action. Pure C: host-tested (tests/host/test_hub_buttons.cpp).
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "hub_pins.h"
#include "button_actions.h"

#define HUB_BUTTONS 24
enum { HB_NONE = 0, HB_DIGITAL = 1, HB_LADDER = 2 };
// Which screen an action goes to. Chosen = the one picked with "Next gauge (choose screen)" (all until one is picked).
enum { HBT_ALL = 0, HBT_LEFT, HBT_CENTER, HBT_RIGHT, HBT_CHOSEN, HBT_COUNT };
static const char *const HBT_NAMES[HBT_COUNT] = { "All screens", "Left", "Center", "Right", "Chosen screen" };

typedef struct __attribute__((packed)) {
  uint8_t kind;      // HB_*
  uint8_t pin;       // GPIO, PIN_MCP_BASE + n (digital) or PIN_ADS_BASE + n (ladder)
  uint8_t active_high;   // digital: 0 = active when LOW (to ground, optocoupler), 1 = active when HIGH
  uint8_t action;    // BA_*
  uint8_t param;     // page / scheme / output number
  uint8_t target;    // HBT_*
  uint16_t mv;       // ladder: the learned voltage at the pin, millivolts
  char name[16];
} HubButton;
#define HUB_BUTTONS_VERSION 1

static inline void hb_defaults(HubButton *b) { memset(b, 0, sizeof *b); }
// Can this kind of button sit on that pin?
static inline bool hb_pin_fits(int kind, int pin) {
  if (kind == HB_DIGITAL) return pin_is_mcp(pin) || (!pin_is_ads(pin) && (gpio_caps(pin) & CAP_IN));
  if (kind == HB_LADDER) return pin_is_ads(pin) || (!pin_is_mcp(pin) && (gpio_caps(pin) & CAP_ADC));
  return false;
}

// ---- ladder: which button gives this voltage? ----
// The nearest learned voltage on that pin, if it is within LADDER_TOL_MV and within 40% of the way to its neighbour
// (so a reading near halfway, while a contact settles, presses nothing).
// -1 = none (nothing pressed, or a voltage between two buttons).
#define LADDER_TOL_MV 150
static inline int hb_ladder_match(const HubButton *b, int n, int pin, int mv) {
  int best = -1, bd = 1 << 30;
  for (int i = 0; i < n; i++) {
    if (b[i].kind != HB_LADDER || b[i].pin != pin) continue;
    int d = mv > b[i].mv ? mv - b[i].mv : b[i].mv - mv;
    if (d < bd) { bd = d; best = i; }
  }
  if (best < 0) return -1;
  int tol = LADDER_TOL_MV;
  for (int i = 0; i < n; i++) {
    if (i == best || b[i].kind != HB_LADDER || b[i].pin != pin) continue;
    int gap = (b[i].mv > b[best].mv ? b[i].mv - b[best].mv : b[best].mv - b[i].mv) * 2 / 5;   // 40% of the spacing
    if (gap < tol) tol = gap;
  }
  return bd <= tol ? best : -1;
}
// Two buttons on one ladder too close to tell apart? Returns the index of the second one, or -1.
#define LADDER_MIN_GAP_MV 120
static inline int hb_ladder_clash(const HubButton *b, int n, int pin, int mv, int skip) {
  for (int i = 0; i < n; i++) {
    if (i == skip || b[i].kind != HB_LADDER || b[i].pin != pin) continue;
    int d = mv > b[i].mv ? mv - b[i].mv : b[i].mv - mv;
    if (d < LADDER_MIN_GAP_MV) return i;
  }
  return -1;
}

// ---- debounce: a press counts once the same reading has held for `ms` ----
// raw: the button index read now (-1 = none). Returns the index that was just pressed, or -1.
typedef struct { int8_t raw, stable; uint32_t rawMs; } HbDebounce;
static inline void hb_deb_init(HbDebounce *d) { d->raw = d->stable = -1; d->rawMs = 0; }
static inline int hb_deb_step(HbDebounce *d, uint32_t now, int raw, uint32_t ms) {
  if (raw != d->raw) { d->raw = (int8_t)raw; d->rawMs = now; return -1; }
  if (now - d->rawMs < ms || raw == d->stable) return -1;
  d->stable = (int8_t)raw;
  return raw;   // -1 when released
}
#define HB_DEBOUNCE_MS 30
#define HB_LADDER_DEBOUNCE_MS 50   // the ADS1115 gives each channel a new reading about every 80 ms

// Which screen name fits a target: Left, Center (also "centre", "middle") or Right. Compares lower case.
static inline bool hb_target_is(int target, const char *name) {
  char s[24]; int k = 0;
  for (; name[k] && k < 23; k++) s[k] = (name[k] >= 'A' && name[k] <= 'Z') ? name[k] + 32 : name[k];
  s[k] = 0;
  if (target == HBT_LEFT) return !strcmp(s, "left");
  if (target == HBT_CENTER) return !strcmp(s, "center") || !strcmp(s, "centre") || !strcmp(s, "middle");
  if (target == HBT_RIGHT) return !strcmp(s, "right");
  return false;
}

// ---- ECU map (the "ECU map (next)" action): each press moves to the next map and back to the first ----
// The hub sends CAN frame `id` 4 times a second while it hears the ECU: byte 0 = map number (1-based), byte 1 = how
// many maps. A rusEFI Lua script applies it (docs/RUSEFI_LUA.md). The map in use is saved, so it comes back after a restart.
#define ECU_MAPS 8
#define ECU_MAP_NAME 13   // 12 characters + end
typedef struct __attribute__((packed)) {
  uint8_t version;
  uint8_t count;     // 1..ECU_MAPS
  uint8_t cur;       // 0-based
  uint8_t spare;
  uint16_t id;       // 11-bit CAN id
  uint16_t spare2;
  char names[ECU_MAPS][ECU_MAP_NAME];
} EcuMapCfg;
#define ECU_MAP_VERSION 1
#define ECU_MAP_ID_DEFAULT 0x7F1
static inline void ecu_map_defaults(EcuMapCfg *c) {
  memset(c, 0, sizeof *c);
  c->version = ECU_MAP_VERSION; c->count = 2; c->id = ECU_MAP_ID_DEFAULT;
  strcpy(c->names[0], "Street"); strcpy(c->names[1], "Boost");
}
static inline void ecu_map_fix(EcuMapCfg *c) {
  if (c->version != ECU_MAP_VERSION || c->count < 1 || c->count > ECU_MAPS || c->id > 0x7FF) { ecu_map_defaults(c); return; }
  if (c->cur >= c->count) c->cur = 0;
  for (int i = 0; i < ECU_MAPS; i++) c->names[i][ECU_MAP_NAME - 1] = 0;
}
static inline void ecu_map_next(EcuMapCfg *c) { c->cur = (uint8_t)((c->cur + 1) % (c->count ? c->count : 1)); }
// "Map 2: Boost" (or "Map 2" with no name), for the screens' pop-up. out: at least 21 bytes.
static inline void ecu_map_label(const EcuMapCfg *c, char *out, int len) {
  int m = c->cur + 1, k = 0;
  const char *p = "Map ";
  while (*p && k < len - 1) out[k++] = *p++;
  if (m >= 10 && k < len - 1) out[k++] = (char)('0' + m / 10);
  if (k < len - 1) out[k++] = (char)('0' + m % 10);
  const char *nm = c->names[c->cur];
  if (nm[0]) { if (k < len - 1) out[k++] = ':'; if (k < len - 1) out[k++] = ' '; while (*nm && k < len - 1) out[k++] = *nm++; }
  out[k] = 0;
}

// ---- from before hub 53: the two page buttons (old pin map and binding table) become buttons 1 and 2 ----
// Their press bindings carry over (hold and both-button bindings are gone), going to all screens.
static inline int hb_from_old(HubButton *out, int pinPrev, int pinNext, const BtnBinding bind[BTN_INPUTS][BTN_LEVELS]) {
  hb_defaults(&out[0]); hb_defaults(&out[1]);
  out[0].kind = HB_DIGITAL; out[0].pin = (uint8_t)pinPrev; out[0].action = bind[0][0].action; out[0].param = bind[0][0].param; strcpy(out[0].name, "Left");
  out[1].kind = HB_DIGITAL; out[1].pin = (uint8_t)pinNext; out[1].action = bind[1][0].action; out[1].param = bind[1][0].param; strcpy(out[1].name, "Right");
  for (int i = 0; i < 2; i++) { out[i].target = HBT_ALL; if (out[i].action >= BA_COUNT) out[i].action = BA_NONE; }
  return 2;
}
