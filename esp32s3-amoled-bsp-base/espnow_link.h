// espnow_link.h - multi-board link over ESP-NOW.
// One firmware for every board. Role "main" (default) reads CAN and broadcasts all signals 50 times a second;
// role "remote" ignores CAN and shows the main board's values. Broadcast: no pairing, no MAC list.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ESPNOW_SEND_MS     50     // main: one packet every 50 ms (20/s); gauges refresh 10/s by default
#define ESPNOW_TIMEOUT_MS  1000   // remote: no packet for this long -> readings show "--"
#define ESPNOW_CAR_CHANNEL 1      // channel used when no WiFi network joined (in the car)

void espnow_link_init(void);            // call after WiFi has connected or given up
void espnow_link_loop(void);            // call from loop()
bool espnow_link_is_remote(void);
void espnow_link_set_remote(bool remote); // saved on the board; takes effect after a reboot
uint32_t espnow_link_pps(void);          // packets per second, sent (main) or received (remote)
int32_t espnow_link_last_ms(void);       // remote: ms since the last packet, -1 = never
void espnow_link_peer_mac(char out[18]); // remote: the sender's MAC, "--" if none yet
uint8_t espnow_link_channel(void);

// Main board: the remotes it has heard. Each remote broadcasts a small "hello" (its IP and build) every 2 s.
// Commands from the input board (esp32-input): page next/previous, brightness. target = board name, "" = every screen.
// The input board keeps its own copy of this packet: keep them the same.
#define LINK_CMD_MAGIC 0x4D534331u   // "MSC1"
enum { LINK_CMD_PAGE_NEXT = 1, LINK_CMD_PAGE_PREV = 2, LINK_CMD_BRIGHTNESS = 3 };
typedef struct __attribute__((packed)) { uint32_t magic; uint16_t seq; uint8_t cmd; int16_t value; char target[21]; } LinkCmdPacket;

#define ESPNOW_MAX_BOARDS 4
#define ESPNOW_NAME_LEN 20
typedef struct { char mac[18]; char ip[16]; char name[ESPNOW_NAME_LEN + 1]; uint16_t build; int32_t age_ms; } LinkBoard;
// Board name, saved in NVS ("link"/"name"). Empty = "Main" or "Remote" from the role.
void espnow_link_name(char out[ESPNOW_NAME_LEN + 1]);
void espnow_link_set_name(const char *name);   // trimmed to ESPNOW_NAME_LEN
int espnow_link_boards(LinkBoard out[ESPNOW_MAX_BOARDS]);   // returns how many
#ifdef __cplusplus
}
#endif
