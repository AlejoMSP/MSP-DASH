#ifndef WEB_MONITOR_H
#define WEB_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

// Call once from Arduino's setup(), after CAN init. Starts a WiFi
// access point and a web server serving a live gauge-monitoring page.
void web_monitor_init(void);

// Call every loop() iteration - handles periodic broadcast of current
// gauge values to any connected web clients over WebSocket.
void web_monitor_loop(void);

#ifdef __cplusplus
}
#endif

#endif
