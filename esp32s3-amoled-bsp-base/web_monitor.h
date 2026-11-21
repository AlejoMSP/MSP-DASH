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
const char *web_monitor_home_ssid(void);   // the home network (first-boot setup, else wifi_config.h)
bool web_monitor_setup_mode(void);         // no position chosen yet: first-boot setup running
void web_monitor_factory_reset(void);      // erase every setting and restart (keeps firmware and SD logs)

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
