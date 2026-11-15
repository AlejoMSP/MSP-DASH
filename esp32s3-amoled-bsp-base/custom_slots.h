// custom_slots.h - configurable sensors (docs/CONFIGURABLE_SENSORS.md), shared by the screens and the hub.
// The hub build copies this file from here (see PREREQUISITES.md).
//
// 16 custom slots, SIG_C1..SIG_C16 in gauge_config.h. They are set up on the HUB's /custom page: a name, unit,
// decimals and scale, and where the value comes from (stage 1: a CAN frame). The hub decodes them and sends the
// values in the normal data packet; it sends the names/units in "MSN1" packets, so every screen can list them in
// its gauge editor with no reflash. Pure C: no Arduino calls, so the host tests can check it.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define CUSTOM_SLOTS 16
#define CUSTOM_NAME_LEN 16      // including the terminating 0
#define CUSTOM_UNIT_LEN 8

// What the screens need to show a slot (sent by the hub in MSN1 packets).
typedef struct __attribute__((packed)) {
  char name[CUSTOM_NAME_LEN];   // "" = slot not in use (hidden from the gauge editor)
  char unit[CUSTOM_UNIT_LEN];
  uint8_t decimals;             // 0..3
  float min, max;               // the gauge template: a gauge picking this slot starts with this scale
} CustomSlotInfo;

// Where a slot's value comes from: a field in a CAN frame. value = raw * scale + offset.
enum { CUSTOM_SRC_NONE = 0, CUSTOM_SRC_CAN = 1, CUSTOM_SRC_INPUT = 2 };   // INPUT: filled by a hub input (Inputs page)
typedef struct __attribute__((packed)) {
  uint32_t id;          // CAN ID (11-bit)
  uint8_t start;        // first byte, 0..7
  uint8_t bits;         // 1 (one bit, for on/off), 8, 16 or 32
  uint8_t bit;          // bits == 1: which bit of the start byte, 0..7
  uint8_t big_endian;   // 16/32 bit: 1 = most significant byte first (Motorola), 0 = little endian (Intel)
  uint8_t is_signed;    // 8/16/32 bit: two's complement
  float scale;
  float offset;
} CustomCanDef;

// The hub's full setting for one slot (stored in its NVS).
typedef struct __attribute__((packed)) {
  CustomSlotInfo info;
  uint8_t src;          // CUSTOM_SRC_*
  CustomCanDef can;
} CustomSlotConfig;

// "MSN1": up to 4 slots' info per packet (ESP-NOW packets are 250 bytes max); the hub sends all 16 every 10 s
// and right after a change.
#define SLOTS_MAGIC 0x4D534E31u
#define SLOTS_PER_PACKET 4
#define SLOTS_MS 10000
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t first;        // slot index of info[0], 0..15
  uint8_t n;            // 1..4
  CustomSlotInfo info[SLOTS_PER_PACKET];
} SlotsPacket;
#ifdef __cplusplus
static_assert(sizeof(SlotsPacket) <= 250, "an ESP-NOW packet is 250 bytes at most");
#endif

// Does the definition make sense? (The hub refuses to save one that does not.)
static inline bool custom_can_valid(const CustomCanDef *c) {
  if (c->id > 0x7FF) return false;
  if (c->bits == 1) return c->start <= 7 && c->bit <= 7;
  if (c->bits != 8 && c->bits != 16 && c->bits != 32) return false;
  if (c->start + c->bits / 8 > 8) return false;
  return c->scale == c->scale && c->offset == c->offset && c->scale != 0.0f;   // not NaN, scale not 0
}

// Decodes the field from a received frame. False if the frame is too short for it (nothing is changed then).
static inline bool custom_can_decode(const CustomCanDef *c, const uint8_t *d, uint8_t len, float *out) {
  if (!custom_can_valid(c)) return false;
  if (c->bits == 1) {
    if (c->start >= len) return false;
    *out = ((d[c->start] >> c->bit) & 1u) ? 1.0f : 0.0f;    // on/off: scale and offset do not apply
    return true;
  }
  int n = c->bits / 8;
  if (c->start + n > len) return false;
  uint32_t raw = 0;
  for (int i = 0; i < n; i++) {
    uint8_t b = c->big_endian ? d[c->start + i] : d[c->start + n - 1 - i];
    raw = (raw << 8) | b;
  }
  double v;
  if (c->is_signed) {
    if (n == 1) v = (int8_t)raw;
    else if (n == 2) v = (int16_t)raw;
    else v = (int32_t)raw;
  } else v = raw;
  *out = (float)(v * c->scale + c->offset);
  return true;
}

// Copies a C string into a fixed field, always 0-terminated; control characters become spaces.
static inline void custom_copy_text(char *dst, int dstLen, const char *src) {
  int i = 0;
  for (; src && src[i] && i < dstLen - 1; i++) dst[i] = (src[i] < 32) ? ' ' : src[i];
  dst[i] = 0;
}

#ifdef __cplusplus
extern "C" {
#endif
// Both sides keep the latest value of every slot here (screens: from the data packet; hub: from its decoder).
extern volatile float g_custom_val[CUSTOM_SLOTS];
extern volatile bool g_custom_has[CUSTOM_SLOTS];
// Names, units, decimals and template scale (screens: from MSN1 packets, kept in NVS; hub: its own settings).
const CustomSlotInfo *custom_slot_info(int slot);
#ifdef __cplusplus
}
// Screens only (custom_slots_screen.cpp):
void custom_slots_screen_init(void);
bool custom_slots_on_packet(const uint8_t *data, int len);   // true if it was an MSN1 packet
void custom_slots_loop(void);
#endif
