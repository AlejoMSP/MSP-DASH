// custom_slots_screen.cpp - the screen side of the custom sensor slots (custom_slots.h).
// Values arrive in the hub's data packet (espnow_link.cpp). Names, units, decimals and the template scale arrive in
// the hub's "MSN1" packets; they are kept in NVS so the gauges show the right name right after a reboot, before the
// hub's next MSN1 (it sends them every 10 s).
#include "serial_log.h"   // RT_LOG: runtime messages off unless SERIAL_STREAM
#include "custom_slots.h"
#include <Arduino.h>
#include <Preferences.h>

volatile float g_custom_val[CUSTOM_SLOTS];
volatile bool g_custom_has[CUSTOM_SLOTS];

static CustomSlotInfo s_info[CUSTOM_SLOTS];
static CustomSlotInfo s_staged[CUSTOM_SLOTS];
static volatile bool s_stagedDirty = false;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

const CustomSlotInfo *custom_slot_info(int slot) {
  static CustomSlotInfo empty;
  return (slot >= 0 && slot < CUSTOM_SLOTS) ? &s_info[slot] : &empty;
}

void custom_slots_screen_init(void) {
  Preferences p;
  if (p.begin("slots", true)) {
    if (p.getBytes("info", s_info, sizeof s_info) != sizeof s_info) memset(s_info, 0, sizeof s_info);
    p.end();
  }
  for (int i = 0; i < CUSTOM_SLOTS; i++) { s_info[i].name[CUSTOM_NAME_LEN - 1] = 0; s_info[i].unit[CUSTOM_UNIT_LEN - 1] = 0; }
  memcpy(s_staged, s_info, sizeof s_info);
}

// From the ESP-NOW receive callback: only copies (no NVS writes there).
bool custom_slots_on_packet(const uint8_t *data, int len) {
  if (len < (int)offsetof(SlotsPacket, info) || ((const SlotsPacket *)data)->magic != SLOTS_MAGIC) return false;
  const SlotsPacket *p = (const SlotsPacket *)data;
  int n = p->n > SLOTS_PER_PACKET ? SLOTS_PER_PACKET : p->n;
  if ((int)(offsetof(SlotsPacket, info) + n * sizeof(CustomSlotInfo)) > len) return true;   // cut short: ignore
  portENTER_CRITICAL(&s_mux);
  for (int k = 0; k < n && p->first + k < CUSTOM_SLOTS; k++) {
    CustomSlotInfo in = p->info[k];
    in.name[CUSTOM_NAME_LEN - 1] = 0; in.unit[CUSTOM_UNIT_LEN - 1] = 0;
    if (memcmp(&s_staged[p->first + k], &in, sizeof in)) { s_staged[p->first + k] = in; s_stagedDirty = true; }
  }
  portEXIT_CRITICAL(&s_mux);
  return true;
}

// From loop(): takes new names into use and saves them (only when something changed).
void custom_slots_loop(void) {
  if (!s_stagedDirty) return;
  portENTER_CRITICAL(&s_mux);
  memcpy(s_info, s_staged, sizeof s_info);
  s_stagedDirty = false;
  portEXIT_CRITICAL(&s_mux);
  Preferences p;
  if (p.begin("slots", false)) { p.putBytes("info", s_info, sizeof s_info); p.end(); }
  RT_LOG("Custom sensor names updated from the hub\n");
}
