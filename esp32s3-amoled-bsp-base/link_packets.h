// link_packets.h - the ESP-NOW packets shared by the screens and the hub.
// The hub build copies this file from here (see PREREQUISITES.md), so both sides always use the same layout.
#pragma once
#include <stdint.h>
#include <string.h>
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
       LINK_CMD_HINT = 10,       // button held: pop-up "Release: <action>"; value = action << 8 | parameter
       LINK_CMD_SHOW_IP = 11,    // each screen shows its name and IP address for 5 s (value = the hub's IP, last byte)
       LINK_CMD_POPUP = 12,      // every screen shows a pop-up; the text is in `target` (not a screen name here)
       LINK_CMD_SHIFT = 13 };    // shift light (hub 56): value = level, `target` = the screen names left to right, comma-separated
typedef struct __attribute__((packed)) { uint32_t magic; uint16_t seq; uint8_t cmd; int16_t value; char target[21]; } LinkCmdPacket;

// "MSH1": a screen in remote role says hello every 2 s (its IP, build and name); the hub uses it for the online lights
#define HELLO_MAGIC 0x4D534831u
#define HELLO_MS 2000
#define LINK_NAME_LEN 20
// uptime_s (build 195): seconds since the screen started; lets the hub's Firmware page see that an update restarted it.
// Older screens send the packet without it (the hub reads what arrived; a missing uptime reads 0 = unknown).
// stats (build 220 / hub 58): how the screen is doing, for the hub's Board status page. Older screens send the packet
// without it (stats_ver 0 = none).
typedef struct __attribute__((packed)) {
  uint8_t stats_ver;                 // 1
  uint8_t fps, draw_pct;             // frames sent to the panel last second; share of it LVGL spent drawing
  uint8_t cpu0, cpu1;                // busy % per core (cpu_load.h, an estimate)
  uint16_t heap_kb, heap_min_kb;     // internal RAM free now / lowest ever
  uint16_t heap_big_kb;              // largest free block (fragmentation)
  uint16_t psram_kb;                 // PSRAM free
  int8_t temp_c;                     // chip temperature
  int8_t rssi;                       // Wi-Fi signal, dBm (0 = not on a network)
  uint16_t rx_pps;                   // data packets from the hub per second
  uint16_t rx_age_ds;                // tenths of a second since the last one (0xFFFF = never)
  uint8_t logging;                   // SD logging running
  uint8_t cpu_mhz_4;                 // CPU clock / 4
} HelloStats;
#define HELLO_STATS_VER 1

// The hub's own Wi-Fi network (hub 61 / build 223): while it runs, the hub tells the screens its name and password
// every 10 s, so a screen that does not find the home network joins it and its pages load in the car too.
// Sent in the clear like every ESP-NOW packet: anyone in radio range could read the password.
#define HUBNET_MAGIC 0x4D535731u   // "MSW1"
#define HUBNET_MS 10000
#define HUBNET_CHANNEL 1           // the car channel: the hub's network always runs there away from home
typedef struct __attribute__((packed)) { uint32_t magic; char ssid[33]; char pass[65]; } HubNetPacket;
// The screen's address on the hub's network: 192.168.4.<the last number of its home address>, so Left / Center /
// Right keep their numbers (.201 / .202 / .203). 0 = no home address set: ask the hub (DHCP).
static inline int hubnet_host(const char *homeIp) {
  const char *dot = strrchr(homeIp, '.');
  if (!dot || !dot[1]) return 0;
  int v = 0; for (const char *q = dot + 1; *q; q++) { if (*q < '0' || *q > '9') return 0; v = v * 10 + (*q - '0'); if (v > 255) return 0; }
  return v >= 2 && v <= 254 ? v : 0;
}
typedef struct __attribute__((packed)) { uint32_t magic; uint32_t ip; uint16_t build; char name[LINK_NAME_LEN]; uint32_t uptime_s; HelloStats st; } HelloPacket;

// Shift light (LINK_CMD_SHIFT): where is this screen in the hub's left-to-right list ("Left,Center,Right")? -1 = not in it.
// The value is the level: 0 off, 1..n that many screens yellow from the left, n+1 all green, n+2 all red (flashing).
static inline int link_shift_index(const char *list, const char *name, int *count) {
  int idx = -1, n = 0; const char *p = list;
  while (*p) {
    const char *e = p; while (*e && *e != ',') e++;
    size_t len = (size_t)(e - p);
    if (len && strlen(name) == len) {   // same name, any case (the hub's name match is case-blind too)
      size_t k = 0;
      for (; k < len; k++) { char a = p[k], b = name[k]; if (a >= 'A' && a <= 'Z') a += 32; if (b >= 'A' && b <= 'Z') b += 32; if (a != b) break; }
      if (k == len) idx = n;
    }
    if (len) n++;
    p = *e ? e + 1 : e;
  }
  if (count) *count = n;
  return idx;
}
// What this screen shows: 0 off, 1 yellow, 2 green, 3 red.
static inline int link_shift_color(int level, int idx, int n) {
  if (level <= 0 || idx < 0 || n < 1) return 0;
  if (level >= n + 2) return 3;
  if (level == n + 1) return 2;
  return idx < level ? 1 : 0;
}
