#pragma once
#include <stdint.h>
#include <string.h>
struct EEPROMClass { uint8_t b[512]; void begin(int) {} uint8_t read(int a) { return b[a]; } void write(int a, uint8_t v) { b[a] = v; } bool commit() { return true; }
  template <class T> T &get(int a, T &t) { memcpy(&t, b + a, sizeof t); return t; } template <class T> const T &put(int a, const T &t) { memcpy(b + a, &t, sizeof t); return t; } } EEPROM;
