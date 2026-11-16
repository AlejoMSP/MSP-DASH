// ads1115.h (hub) - ADS1115 16-bit 4-channel ADCs on the I2C bus (no library), as analog "pins" 140..155 (hub_pins.h).
// Up to 4 boards: ADDR to GND = 0x48 (board 1), VDD = 0x49, SDA = 0x4A, SCL = 0x4B. VDD 3.3 V; inputs 0 to 3.3 V (never
// above VDD + 0.3 V: 5 V and 12 V signals through a divider). Range +/-4.096 V, 860 samples/s, single-shot, against GND.
// adsStep() runs every 20 ms: each board hands over its last conversion and starts its next channel, so every channel
// updates about 12 times a second however many boards there are.
#pragma once
#include <Arduino.h>
#include <Wire.h>

#define ADS_BOARDS 4
static uint8_t s_adsAddr[ADS_BOARDS] = { 0, 0, 0, 0 };   // board n is at 0x48 + n; 0 = not found
static volatile float s_adsV[ADS_BOARDS * 4];             // volts per channel (pin PIN_ADS_BASE + board * 4 + ch)
static int8_t s_adsCh[ADS_BOARDS] = { -1, -1, -1, -1 };    // the channel each board is converting

static bool adsW16(uint8_t a, uint8_t reg, uint16_t v) {
  Wire.beginTransmission(a); Wire.write(reg); Wire.write((uint8_t)(v >> 8)); Wire.write((uint8_t)v);
  return Wire.endTransmission() == 0;
}
static bool adsR16(uint8_t a, uint8_t reg, int16_t *v) {
  Wire.beginTransmission(a); Wire.write(reg);
  if (Wire.endTransmission() != 0 || Wire.requestFrom(a, (uint8_t)2) != 2) return false;
  int hi = Wire.read(), lo = Wire.read(); *v = (int16_t)((hi << 8) | lo); return true;
}
// One conversion of channel ch against GND: OS=1, MUX=100+ch, PGA=001 (4.096 V), single-shot, 860 SPS, no comparator.
static bool adsStart(uint8_t a, int ch) { return adsW16(a, 0x01, (uint16_t)(0x8000 | ((4 + ch) << 12) | (1 << 9) | (1 << 8) | (7 << 5) | 0x0003)); }
// Wire must already be started. Returns how many boards answered (and read back like an ADS1x15).
static int adsBegin() {
  int n = 0;
  for (int b = 0; b < ADS_BOARDS; b++) {
    uint8_t a = 0x48 + b; int16_t cfg;
    Wire.beginTransmission(a);
    if (Wire.endTransmission() != 0 || !adsR16(a, 0x01, &cfg)) continue;
    s_adsAddr[b] = a; s_adsCh[b] = 0; adsStart(a, 0); n++;
  }
  return n;
}
static void adsStep() {
  for (int b = 0; b < ADS_BOARDS; b++) {
    if (!s_adsAddr[b] || s_adsCh[b] < 0) continue;
    int16_t raw;
    if (adsR16(s_adsAddr[b], 0x00, &raw)) s_adsV[b * 4 + s_adsCh[b]] = raw * (4.096f / 32768.0f);
    s_adsCh[b] = (s_adsCh[b] + 1) % 4;
    adsStart(s_adsAddr[b], s_adsCh[b]);
  }
}
static bool adsFound(int pin) { return pin >= PIN_ADS_BASE && pin < PIN_ADS_BASE + 16 && s_adsAddr[(pin - PIN_ADS_BASE) / 4]; }
