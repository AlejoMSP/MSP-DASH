// ads1115.h (hub) - ADS1115 16-bit 4-input ADC on the I2C bus (no library). ADDR to GND = 0x48 (VDD 0x49, SDA 0x4A,
// SCL 0x4B). Range +/-4.096 V (inputs 0 to 3.3 V: never above VDD + 0.3 V), 860 samples/s, single-shot.
// Read without waiting: adsStep() is called every 20 ms; it collects the last conversion and starts the next input,
// so each input updates about 12 times a second.
#pragma once
#include <Arduino.h>
#include <Wire.h>

static uint8_t s_adsAddr = 0;            // 0 = not found
static volatile float s_adsV[4];         // A0..A3, volts
static int s_adsCh = -1;                 // the input being converted (-1 = none started)

static bool adsWrite16(uint8_t reg, uint16_t v) {
  Wire.beginTransmission(s_adsAddr); Wire.write(reg); Wire.write((uint8_t)(v >> 8)); Wire.write((uint8_t)v);
  return Wire.endTransmission() == 0;
}
static bool adsRead16(uint8_t reg, int16_t *v) {
  Wire.beginTransmission(s_adsAddr); Wire.write(reg);
  if (Wire.endTransmission() != 0 || Wire.requestFrom(s_adsAddr, (uint8_t)2) != 2) return false;
  int hi = Wire.read(), lo = Wire.read(); *v = (int16_t)((hi << 8) | lo); return true;
}
// Start a single conversion of input ch against ground: OS=1, MUX=100+ch, PGA=001 (4.096 V), single-shot, 860 SPS, no comparator.
static bool adsStart(int ch) { return adsWrite16(0x01, (uint16_t)(0x8000 | ((4 + ch) << 12) | (1 << 9) | (1 << 8) | (7 << 5) | 0x0003)); }
// Wire must already be started. False if nothing answers at 0x48..0x4B.
static bool adsBegin() {
  for (uint8_t a = 0x48; a <= 0x4B; a++) { Wire.beginTransmission(a); if (Wire.endTransmission() == 0) { s_adsAddr = a; break; } }
  if (!s_adsAddr) return false;
  int16_t cfg;
  if (!adsRead16(0x01, &cfg)) { s_adsAddr = 0; return false; }   // answers but is not an ADS1x15
  s_adsCh = 0; adsStart(0);
  return true;
}
static void adsStep() {
  if (!s_adsAddr || s_adsCh < 0) return;
  int16_t raw;
  if (adsRead16(0x00, &raw)) s_adsV[s_adsCh] = raw * (4.096f / 32768.0f);
  s_adsCh = (s_adsCh + 1) % 4;
  adsStart(s_adsCh);
}
