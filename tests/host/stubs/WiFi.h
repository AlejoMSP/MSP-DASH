#pragma once
#include "Arduino.h"
#define WIFI_STA 1
#define WL_CONNECTED 3
struct IPAddr { String toString() const { return "0.0.0.0"; } };
struct WiFiClass { void mode(int) {} void begin(const char *, const char *) {} int status() { return 0; } IPAddr localIP() { return {}; } int RSSI() { return -50; } } extern WiFi;
