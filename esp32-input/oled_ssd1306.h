// oled_ssd1306.h (hub) - a minimal SSD1306 128x64 I2C driver (no library to install): init, contrast, send a buffer.
// Wiring: VCC 3.3 V (L6), GND, SDA GPIO 5 (R15), SCL GPIO 15 (R21). Address 0x3C (0x3D on some modules).
#pragma once
#include <Arduino.h>
#include <Wire.h>

#define OLED_SDA 5
#define OLED_SCL 15
static uint8_t s_oledAddr = 0;   // 0 = not found

static bool oledCmds(const uint8_t *c, int n) {
  Wire.beginTransmission(s_oledAddr); Wire.write(0x00);   // control byte: commands follow
  for (int i = 0; i < n; i++) Wire.write(c[i]);
  return Wire.endTransmission() == 0;
}
// Finds the display and switches it on. False if nothing answers at 0x3C or 0x3D.
static bool oledBegin() {
  Wire.begin(OLED_SDA, OLED_SCL, 400000);
  for (uint8_t a : { (uint8_t)0x3C, (uint8_t)0x3D }) { Wire.beginTransmission(a); if (Wire.endTransmission() == 0) { s_oledAddr = a; break; } }
  if (!s_oledAddr) return false;
  static const uint8_t init[] = { 0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8,
                                  0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0x2E, 0xAF };
  return oledCmds(init, sizeof init);
}
static void oledContrast(uint8_t v) { const uint8_t c[] = { 0x81, v }; if (s_oledAddr) oledCmds(c, 2); }
// Sends the whole 1024-byte buffer (8 pages x 128 columns), 16 bytes per I2C write (the Wire buffer is 128).
static void oledShow(const uint8_t *buf) {
  if (!s_oledAddr) return;
  static const uint8_t win[] = { 0x21, 0, 127, 0x22, 0, 7 };
  oledCmds(win, sizeof win);
  for (int i = 0; i < 1024; i += 16) {
    Wire.beginTransmission(s_oledAddr); Wire.write(0x40);   // control byte: data follows
    Wire.write(buf + i, 16);
    Wire.endTransmission();
  }
}
