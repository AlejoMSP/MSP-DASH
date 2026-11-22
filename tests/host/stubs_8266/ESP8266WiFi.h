#pragma once
#include <Arduino.h>
inline void noInterrupts() {} inline void interrupts() {}
#ifndef OUTPUT
#define OUTPUT 3
#endif
#define WIFI_STA 1
struct ESP8266WiFiClass { void mode(int) {} bool disconnect(bool = false) { return true; } } WiFi;
inline void analogWriteFreq(uint32_t) {} inline void analogWriteRange(uint32_t) {} inline void analogWrite(uint8_t, int) {}
