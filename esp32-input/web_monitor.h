// web_monitor.h (hub) - raw CAN tracker for the hub's /can page.
// Keeps the latest frame per ID (up to 48 IDs), its count and the time it was last seen.
#pragma once
#include <Arduino.h>
#define CANMON_MAX 48
struct CanMonEntry { uint32_t id; uint8_t len; uint8_t d[8]; uint32_t count; uint32_t lastMs; };
static CanMonEntry s_canMon[CANMON_MAX];
static volatile int s_canMonN = 0;
static volatile uint32_t s_canTotal = 0;
static volatile uint32_t s_canLastRxMs = 0;   // last frame heard from anyone else on the bus
static volatile uint32_t s_canCurId = 0;      // ID of the frame being decoded right now (the sensors page shows it as the source)
static portMUX_TYPE s_canMux = portMUX_INITIALIZER_UNLOCKED;
void hub_custom_can_frame(uint32_t id, const uint8_t *d, uint8_t len);   // esp32-input.ino: custom CAN sensors
static inline void web_monitor_track_can_frame(uint32_t id, const uint8_t *d, uint8_t len) {
  if (len > 8) len = 8;
  portENTER_CRITICAL(&s_canMux);
  s_canTotal++;
  s_canCurId = id;
  s_canLastRxMs = millis();
  int i = 0;
  while (i < s_canMonN && s_canMon[i].id != id) i++;
  if (i == s_canMonN && s_canMonN < CANMON_MAX) { s_canMon[i].id = id; s_canMon[i].count = 0; s_canMonN++; }
  if (i < s_canMonN) {
    CanMonEntry &e = s_canMon[i];
    e.len = len; memcpy(e.d, d, len); e.count++; e.lastMs = millis();
  }
  portEXIT_CRITICAL(&s_canMux);  hub_custom_can_frame(id, d, len);   // outside the lock: decodes the custom slots that read this ID
}

// Which ECU is talking, from the CAN IDs heard in the last 2 s. rusEFI uses 0x200-0x20F; MegaSquirt and Link use the
// same frames (0x5F0-0x63F), so they cannot be told apart. Both decoders always run, so this is only a label.
// (Build 170: this replaced the ECU type setting that each screen used to have.)
static inline const char *hub_ecu_detected(void) {
  bool rus = false, ms = false; uint32_t now = millis();
  for (int i = 0; i < s_canMonN; i++) {
    if (now - s_canMon[i].lastMs > 2000) continue;
    uint32_t id = s_canMon[i].id;
    if (id >= 0x200 && id <= 0x20F) rus = true;
    if (id >= 0x5F0 && id <= 0x63F) ms = true;
  }
  return rus && ms ? "rusEFI and MegaSquirt / Link" : rus ? "rusEFI" : ms ? "MegaSquirt / Link" : "none heard";
}
