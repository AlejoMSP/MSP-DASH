// Extra Arduino/ESP32 declarations the hub sketch uses, for hub_typecheck.sh only (compile check, never linked).
#pragma once
#include <Arduino.h>
#include <cmath>
#include <initializer_list>
#define IRAM_ATTR
#define OUTPUT 1
#define INPUT 0
#define RISING 1
#define FALLING 2
#define CHANGE 3
#define ADC_11db 3
#define SERIAL_8N1 0
#define MSBFIRST 1
#define SPI_MODE0 0
void digitalWrite(int, int); int analogRead(int); uint32_t analogReadMilliVolts(int); void analogSetPinAttenuation(int, int);
void analogReadResolution(int); void analogSetAttenuation(int);
uint32_t micros(); void delayMicroseconds(uint32_t); void yield();
int digitalPinToInterrupt(int); void attachInterrupt(int, void (*)(), int); void detachInterrupt(int);
struct HardwareSerial { void begin(unsigned long, int = 0, int = -1, int = -1) {} int available() { return 0; } int read() { return -1; } size_t write(const uint8_t *, size_t n) { return n; } size_t write(uint8_t) { return 1; } void flush() {} };
extern HardwareSerial Serial1, Serial2;
long random(long); long random(long, long);
#define F(x) (x)
// String::replace exists on Arduino; the shared stub lacks it, so hub code calls go through this helper type.
