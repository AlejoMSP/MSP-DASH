// link_packets.h - the ESP-NOW packets shared by the screens and the hub.
// The hub build copies this file from here (see PREREQUISITES.md), so both sides always use the same layout.
#pragma once
#include <stdint.h>
#include "gauge_config.h"
#include "custom_slots.h"   // "MSN1" custom slot names (SlotsPacket)

// "MSD1": all signal values, sent 20 times a second by the hub (or a screen set to main)
#define PKT_MAGIC 0x4D534431u
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint16_t seq;
  uint16_t count;          // SIG_COUNT of the sender; a receiver only reads what both builds know
  uint64_t has_mask;       // bit s = signal s has data (64 bits since the custom slots, build 171)
  float v[SIG_COUNT];
} LinkPacket;
// has_mask has 64 bits, one per signal. Past 64 signals it needs to grow, or signals would silently drop.
static_assert(SIG_COUNT <= 64, "LinkPacket.has_mask holds 64 signals");
static_assert(sizeof(LinkPacket) <= 250, "an ESP-NOW packet is 250 bytes at most");
// Bit 0 of has_mask belongs to SIG_NONE, which never carries data, so it is used as a flag: the hub's bench simulator
// is running and these values are FAKE. The screens show SIMULATED and the main screen does not log them.
#define LINK_MASK_SIMULATED (1ull << SIG_NONE)

// "MSC1": a command from the hub (page next/prev, brightness); target "" = all screens
#define LINK_CMD_MAGIC 0x4D534331u
enum { LINK_CMD_PAGE_NEXT = 1, LINK_CMD_PAGE_PREV = 2, LINK_CMD_BRIGHTNESS = 3,
       LINK_CMD_SELECT = 4,      // the hub's buttons now control `target` ("" = all screens): that screen shows a pop-up
       LINK_CMD_ALERT_ACK = 5,   // hide the full-screen warning (snoozed 30 s, like a tap)
       LINK_CMD_LOG_MARK = 6,    // marker number `value` in the SD log (the logging screen writes it)
       LINK_CMD_GOTO_PAGE = 7,   // show page `value` (1-based) on the target screen(s)
       LINK_CMD_COLOR = 8,       // every gauge to colour preset `value` (color_presets.h), saved
       LINK_CMD_RACE = 9,        // race mode on (value 1: remember the page, force logging) / off (0: back, logging normal)
       LINK_CMD_HINT = 10 };     // button held: pop-up "Release: <action>"; value = action << 8 | parameter
typedef struct __attribute__((packed)) { uint32_t magic; uint16_t seq; uint8_t cmd; int16_t value; char target[21]; } LinkCmdPacket;

// "MSH1": a screen in remote role says hello every 2 s (its IP, build and name); the hub uses it for the online lights
#define HELLO_MAGIC 0x4D534831u
#define HELLO_MS 2000
#define LINK_NAME_LEN 20
typedef struct __attribute__((packed)) { uint32_t magic; uint32_t ip; uint16_t build; char name[LINK_NAME_LEN]; } HelloPacket;
