#ifndef WEB_MONITOR_H
#define WEB_MONITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Call once from Arduino's setup(), after CAN init. Starts a WiFi
// connection and a web server serving live gauge-monitoring and raw
// CAN-monitor pages.
void web_monitor_init(void);

// Call every loop() iteration - handles periodic broadcast of current
// gauge values to any connected web clients over WebSocket.
void web_monitor_loop(void);

// Call for EVERY received CAN frame (recognized or not) - tracks it in
// the raw CAN monitor table (ID, latest raw bytes, frequency), viewable
// at the /can web page. Separate from handleRusefiFrame()'s decoding -
// this sees every ID on the bus regardless of whether we know how to
// parse it.
void web_monitor_track_can_frame(uint32_t id, uint8_t *data, uint8_t dlc);

#ifdef __cplusplus
}
#endif

#endif
