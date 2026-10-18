// link_packets.h - the ESP-NOW packets shared by the screens and the hub.
// The hub build copies this file from here (see PREREQUISITES.md), so both sides always use the same layout.
#pragma once
#include <stdint.h>
#include "gauge_config.h"

// "MSD1": all signal values, sent 20 times a second by the hub (or a screen set to main)
#define PKT_MAGIC 0x4D534431u
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint16_t seq;
  uint16_t count;          // SIG_COUNT of the sender; a receiver only reads what both builds know
  uint32_t has_mask;       // bit s = signal s has data
  float v[SIG_COUNT];
} LinkPacket;
// has_mask has 32 bits, one per signal. Past 32 signals it needs to grow, or signals would silently drop.
static_assert(SIG_COUNT <= 32, "LinkPacket.has_mask holds 32 signals: make it uint64_t before adding more");

// "MSC1": a command from the hub (page next/prev, brightness); target "" = all screens
#define LINK_CMD_MAGIC 0x4D534331u
enum { LINK_CMD_PAGE_NEXT = 1, LINK_CMD_PAGE_PREV = 2, LINK_CMD_BRIGHTNESS = 3 };
typedef struct __attribute__((packed)) { uint32_t magic; uint16_t seq; uint8_t cmd; int16_t value; char target[21]; } LinkCmdPacket;
