#pragma once
#include "Arduino.h"
#define WIFI_STA 1
#define WIFI_AP_STA 3
#define WL_CONNECTED 3
struct IPAddress { uint32_t v = 0; IPAddress() {} IPAddress(uint32_t x) : v(x) {} IPAddress(int a, int b, int c, int d) : v((uint32_t)a | b << 8 | c << 16 | (uint32_t)d << 24) {} uint8_t operator[](int i) const { return v >> (8 * i) & 0xFF; } bool fromString(const String &) { v = 1; return true; } String toString() const { return "0.0.0.0"; }
  operator uint32_t() const { return v; } bool operator==(const IPAddress &o) const { return v == o.v; } };
struct WiFiClass { void mode(int) {} void begin(const char *, const char * = nullptr, int = 0) {} int status() { return 0; } IPAddress localIP() { return {}; } int RSSI() { return -50; } void setAutoReconnect(bool) {} int scanNetworks(bool = false, bool = false, bool = false, uint32_t = 300) { return 0; } String SSID(int) { return ""; } String SSID() { return ""; } void scanDelete() {} bool config(IPAddress, IPAddress, IPAddress, IPAddress = IPAddress()) { return true; } bool disconnect(bool = false, bool = false) { return true; } String macAddress() { return "00:00:00:00:00:00"; } int channel() { return 1; } bool softAP(const char *, const char *, int = 1, int = 0, int = 4) { return true; } IPAddress softAPIP() { return {}; } int softAPgetStationNum() { return 0; } } extern WiFi;
