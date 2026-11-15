// imu_mpu6050.h (hub) - a minimal MPU6050 driver on the OLED's I2C bus (no library to install).
// Wiring: VCC 3.3 V (L6), GND, SDA GPIO 5 (R15), SCL GPIO 15 (R21); AD0 open = address 0x68 (to 3.3 V = 0x69).
// Ranges: +/-8 g (4096 counts per g), +/-500 deg/s (65.5 counts per deg/s), its own filter at 21 Hz.
#pragma once
#include <Arduino.h>
#include <Wire.h>

static uint8_t s_imuAddr = 0;   // 0 = not found
static bool imuWrite(uint8_t reg, uint8_t v) { Wire.beginTransmission(s_imuAddr); Wire.write(reg); Wire.write(v); return Wire.endTransmission() == 0; }
// Wire must already be started (oledBegin does it). False if nothing answers at 0x68 / 0x69.
static bool imuBegin() {
  for (uint8_t a : { (uint8_t)0x68, (uint8_t)0x69 }) { Wire.beginTransmission(a); if (Wire.endTransmission() == 0) { s_imuAddr = a; break; } }
  if (!s_imuAddr) return false;
  bool ok = imuWrite(0x6B, 0x01);   // wake up, clock from the X gyro
  delay(10);
  ok = ok && imuWrite(0x1A, 0x04) && imuWrite(0x1B, 0x08) && imuWrite(0x1C, 0x10) && imuWrite(0x19, 0x13);   // 21 Hz filter, 500 dps, 8 g, 50 Hz
  if (!ok) s_imuAddr = 0;
  return ok;
}
// One reading: acceleration in g and rotation in deg/s, chip axes. False if the read failed.
static bool imuRead(float acc[3], float gyro[3]) {
  if (!s_imuAddr) return false;
  Wire.beginTransmission(s_imuAddr); Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(s_imuAddr, (uint8_t)14) != 14) return false;
  int16_t r[7];
  for (int i = 0; i < 7; i++) { int hi = Wire.read(), lo = Wire.read(); r[i] = (int16_t)((hi << 8) | lo); }
  for (int i = 0; i < 3; i++) { acc[i] = r[i] / 4096.0f; gyro[i] = r[4 + i] / 65.5f; }
  return true;
}
