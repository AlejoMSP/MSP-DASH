#pragma once
#include "Arduino.h"
#define WIFI_STA 1
#define WL_CONNECTED 3
struct IPAddress { uint32_t v = 0; bool fromString(const String &) { v = 1; return true; } String toString() const { return "0.0.0.0"; }
  operator uint32_t() const { return v; } bool operator==(const IPAddress &o) const { return v == o.v; } };
struct WiFiClass { void mode(int) {} void begin(const char *, const char *) {} int status() { return 0; } IPAddress localIP() { return {}; } int RSSI() { return -50; } void setAutoReconnect(bool) {} int scanNetworks(bool = false, bool = false, bool = false, uint32_t = 300) { return 0; } String SSID(int) { return ""; } void scanDelete() {} bool config(IPAddress, IPAddress, IPAddress, IPAddress) { return true; } bool disconnect(bool = false, bool = false) { return true; } String macAddress() { return "00:00:00:00:00:00"; } } extern WiFi;
