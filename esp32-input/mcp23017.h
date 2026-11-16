// mcp23017.h (hub) - MCP23017 16-input expanders on the I2C bus (no library), for OLED lamp inputs.
// Up to two boards. Board 1 = the highest address found (the SG-IO-E017's DIP switch default is 0x27), board 2 = the
// next one down (set its switch to 0x26). VCC 3.3 V (its pull-ups go to VCC: never 5 V on the hub's bus).
// Every pin: input with the chip's 100k pull-up, so a switch to ground reads LOW. INTA / INTB are not used.
#pragma once
#include <Arduino.h>
#include <Wire.h>

static uint8_t s_mcpAddr[2] = { 0, 0 };           // 0 = not found
// Outputs on the expander: the pins set as outputs (1 = output) and the levels wanted (1 = HIGH), filled by the outputs
// code; mcpApplyOutputs() writes them from the I2C task when they change.
static volatile uint16_t s_mcpOutMask[2] = { 0, 0 }, s_mcpOutBits[2] = { 0, 0 };
static uint16_t s_mcpDirSent[2] = { 0, 0 }, s_mcpLatSent[2] = { 0, 0 };
static bool s_mcpSent[2] = { false, false };
static volatile uint16_t s_mcpBits[2] = { 0xFFFF, 0xFFFF };   // bit 0-7 = PA0-PA7, bit 8-15 = PB0-PB7; 1 = HIGH

static bool mcpWrite(uint8_t a, uint8_t reg, uint8_t v) { Wire.beginTransmission(a); Wire.write(reg); Wire.write(v); return Wire.endTransmission() == 0; }
// Wire must already be started. Looks at 0x27 down to 0x20; an answering address must also read back like an MCP23017
// (IODIRA = 0xFF after power-up). Returns how many were found.
static int mcpBegin() {
  int n = 0;
  for (int a = 0x27; a >= 0x20 && n < 2; a--) {
    Wire.beginTransmission(a); Wire.write(0x00);   // IODIRA
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint8_t)a, (uint8_t)1) != 1) continue;
    if (Wire.read() != 0xFF) continue;
    bool ok = mcpWrite(a, 0x00, 0xFF) && mcpWrite(a, 0x01, 0xFF)    // IODIRA / IODIRB: all inputs
           && mcpWrite(a, 0x0C, 0xFF) && mcpWrite(a, 0x0D, 0xFF);   // GPPUA / GPPUB: pull-ups on
    if (ok) s_mcpAddr[n++] = (uint8_t)a;
  }
  return n;
}
// Sets output pins (IODIR 0) and their levels (OLAT) when they change. Input pins stay inputs with pull-ups.
static void mcpApplyOutputs() {
  for (int e = 0; e < 2; e++) {
    if (!s_mcpAddr[e]) continue;
    uint16_t mask = s_mcpOutMask[e], lat = s_mcpOutBits[e] & mask;
    if (!s_mcpSent[e] || lat != s_mcpLatSent[e]) {   // levels first, so a new output does not glitch
      if (mcpWrite(s_mcpAddr[e], 0x14, lat & 0xFF) && mcpWrite(s_mcpAddr[e], 0x15, lat >> 8)) s_mcpLatSent[e] = lat;
    }
    if (!s_mcpSent[e] || mask != s_mcpDirSent[e]) {
      uint16_t dir = (uint16_t)~mask;   // IODIR: 1 = input
      if (mcpWrite(s_mcpAddr[e], 0x00, dir & 0xFF) && mcpWrite(s_mcpAddr[e], 0x01, dir >> 8)) s_mcpDirSent[e] = mask;
    }
    s_mcpSent[e] = true;
  }
}
// Reads both ports of each board (GPIOA, then GPIOB).
static void mcpRead() {
  for (int e = 0; e < 2; e++) {
    if (!s_mcpAddr[e]) continue;
    Wire.beginTransmission(s_mcpAddr[e]); Wire.write(0x12);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(s_mcpAddr[e], (uint8_t)2) != 2) continue;
    uint8_t a = Wire.read(), b = Wire.read();
    s_mcpBits[e] = (uint16_t)(a | (b << 8));
  }
}
