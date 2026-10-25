// rf_cc1101.h (hub) - CC1101 433 MHz receiver in "listen" mode, to find out what the tire pressure sensors send.
// Kept in a header (not the .ino) so the Arduino prototype scan leaves it alone.
//
// Wiring (module pin -> hub):  1 GND -> ground bar | 2 VCC -> 3.3 V (L6), never 5 V | 3 GDO0 -> GPIO 33 (L13)
//   4 CSN -> GPIO 27 (L16) | 5 SCK -> GPIO 14 (L17) | 6 MOSI -> GPIO 13 (L20) | 7 MISO -> GPIO 39 (L9) | 8 GDO2 open
// GDO0 is not on GPIO 39: the ESP32 puts false short pulses on GPIO 36/39 while the ADC or WiFi is busy (errata 3.11),
// which would wreck pulse timing. MISO tolerates it (only short register reads).
//
// How it listens: the CC1101 demodulates 433.92 MHz (2-FSK or OOK, chosen on the page) and puts the raw data on
// GDO0 ("asynchronous serial mode"). An interrupt timestamps every edge. A task reads the signal strength (RSSI)
// every millisecond; while it is above the threshold the edges are collected into a "burst". Each burst is turned
// into bits and Manchester bytes (rf_decode.h) and kept for the /rf page.
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include "rf_decode.h"

#define RF_SCK 14
#define RF_MOSI 13
#define RF_MISO 39
#define RF_CS 27
#define RF_GDO0 33

#define RF_BURSTS 10              // the last 10 bursts are kept
#define RF_MIN_PULSES 24          // shorter bursts are noise
#define RF_EDGE_RING 2048
#define RF_CAPTURE_MS 30          // once a signal is heard: record this long with NO SPI traffic, then check again
#define RF_CAPTURE_MAX_MS 150     // the longest burst kept
#define RF_SPI_QUIET_US 40        // edges during an SPI read and this long after it are ignored (crosstalk)

typedef struct {
  uint32_t ms;                    // when (millis)
  int8_t rssi;                    // strongest dBm during the burst
  uint16_t pulses;
  uint16_t unit;                  // estimated half-bit time T, us
  uint16_t nbits;
  bool inverted;
  char bits[257];                 // first 256 Manchester bits as '0'/'1'
  char hex[3 * 32 + 1];           // first 32 bytes
  uint16_t w[48];                 // first 48 pulse widths, us
  uint8_t firstLevel;
} RfBurst;

static SPIClass s_rfSpi(HSPI);
static bool s_rfOk = false;
static uint8_t s_rfPart = 0xFF, s_rfVersion = 0xFF;
static bool s_rfOok = false;                 // false = 2-FSK (most TPMS), true = OOK/ASK
static int s_rfThr = -85;                    // dBm
static volatile int s_rfRssi = -120;
static volatile bool s_rfReconfig = false;
static RfBurst s_rfBursts[RF_BURSTS];
static volatile int s_rfBurstHead = 0, s_rfBurstCount = 0;
static volatile uint32_t s_rfBurstTotal = 0;
static portMUX_TYPE s_rfMux = portMUX_INITIALIZER_UNLOCKED;

// Every SPI read puts a short blip on GDO0 (crosstalk on the wiring: build 23 showed a pulse exactly every 1 ms,
// the RSSI poll rate). The ISR ignores edges during an SPI transfer and RF_SPI_QUIET_US after it, and the task
// stops polling while it records a burst.
static volatile bool s_rfSpiBusy = false;
static volatile uint32_t s_rfSpiEndUs = 0;

// ---- edges from the interrupt: time of the edge and the level AFTER it ----
static volatile uint32_t s_rfEdgeUs[RF_EDGE_RING];
static volatile uint8_t s_rfEdgeLv[RF_EDGE_RING];
static volatile uint32_t s_rfEdgeHead = 0;
static void IRAM_ATTR rfEdgeIsr() {
  uint32_t now = micros();
  if (s_rfSpiBusy || now - s_rfSpiEndUs < RF_SPI_QUIET_US) return;
  uint32_t h = s_rfEdgeHead;
  s_rfEdgeUs[h % RF_EDGE_RING] = now;
  s_rfEdgeLv[h % RF_EDGE_RING] = (uint8_t)digitalRead(RF_GDO0);
  s_rfEdgeHead = h + 1;
}

// ---- CC1101 SPI ----
static bool rfSelect() {
  digitalWrite(RF_CS, LOW);
  uint32_t t = micros();
  while (digitalRead(RF_MISO)) if (micros() - t > 2000) { digitalWrite(RF_CS, HIGH); return false; }   // chip ready
  return true;
}
static void rfWrite(uint8_t a, uint8_t v) {
  s_rfSpiBusy = true;
  s_rfSpi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  if (rfSelect()) { s_rfSpi.transfer(a); s_rfSpi.transfer(v); }
  digitalWrite(RF_CS, HIGH); s_rfSpi.endTransaction();
  s_rfSpiEndUs = micros(); s_rfSpiBusy = false;
}
static uint8_t rfRead(uint8_t a, bool status) {
  s_rfSpiBusy = true;
  uint8_t v = 0xFF;
  s_rfSpi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  if (rfSelect()) { s_rfSpi.transfer(a | (status ? 0xC0 : 0x80)); v = s_rfSpi.transfer(0); }
  digitalWrite(RF_CS, HIGH); s_rfSpi.endTransaction();
  s_rfSpiEndUs = micros(); s_rfSpiBusy = false;
  return v;
}
static void rfStrobe(uint8_t c) {
  s_rfSpiBusy = true;
  s_rfSpi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  if (rfSelect()) s_rfSpi.transfer(c);
  digitalWrite(RF_CS, HIGH); s_rfSpi.endTransaction();
  s_rfSpiEndUs = micros(); s_rfSpiBusy = false;
}
static int rfRssiDbm() {
  uint8_t r = rfRead(0x34, true);
  int v = r >= 128 ? (int)r - 256 : r;
  return v / 2 - 74;
}

// 433.92 MHz, asynchronous serial output on GDO0, 232 kHz receive filter, 19.2 kBaud, 47.6 kHz FSK deviation.
static void rfConfigure() {
  static const uint8_t regs[][2] = {
    {0x00, 0x0E}, {0x02, 0x0D}, {0x03, 0x47}, {0x07, 0x00}, {0x08, 0x32}, {0x0A, 0x00}, {0x0B, 0x06}, {0x0C, 0x00},
    {0x0D, 0x10}, {0x0E, 0xB0}, {0x0F, 0x71},                       // FREQ = 433.92 MHz (26 MHz crystal)
    {0x10, 0x79}, {0x11, 0x83}, {0x13, 0x02}, {0x14, 0xF8}, {0x15, 0x47},
    {0x16, 0x07}, {0x17, 0x30}, {0x18, 0x18}, {0x19, 0x16}, {0x1A, 0x6C}, {0x1B, 0x43}, {0x1C, 0x40}, {0x1D, 0x91},
    {0x21, 0x56}, {0x23, 0xE9}, {0x24, 0x2A}, {0x25, 0x00}, {0x26, 0x1F}, {0x2C, 0x81}, {0x2D, 0x35}, {0x2E, 0x09} };
  rfStrobe(0x36);                                                    // SIDLE
  for (auto &r : regs) rfWrite(r[0], r[1]);
  rfWrite(0x12, s_rfOok ? 0x30 : 0x00);                              // MDMCFG2: OOK or 2-FSK, no sync word
  rfWrite(0x22, s_rfOok ? 0x11 : 0x10);                              // FREND0
  rfStrobe(0x3A);                                                    // SFRX
  rfStrobe(0x34);                                                    // SRX
}

static void rfLoadSettings() { Preferences p; p.begin("rf", true); s_rfOok = p.getUChar("ook", 0); s_rfThr = p.getChar("thr", -85); p.end(); }
static void rfSaveSettings() { Preferences p; p.begin("rf", false); p.putUChar("ook", s_rfOok); p.putChar("thr", (int8_t)s_rfThr); p.end(); }

// Turns the collected pulses into a burst for the page.
static void rfFinishBurst(const uint16_t *w, int n, uint8_t firstLevel, int peak) {
  if (n < RF_MIN_PULSES) return;
  static uint8_t lv[RF_MAX_BITS * 2], bits[RF_MAX_BITS], bytes[RF_MAX_BYTES];
  RfBurst b; memset(&b, 0, sizeof b);
  b.ms = millis(); b.rssi = (int8_t)peak; b.pulses = n; b.firstLevel = firstLevel;
  b.unit = (uint16_t)rf_estimate_unit(w, n);
  int m = rf_pulses_to_levels(w, n, firstLevel, b.unit, lv, sizeof lv);
  int nbits = 0; bool inv = false;
  int nbytes = rf_best_manchester(lv, m, bytes, 32, bits, &nbits, &inv);
  b.nbits = nbits; b.inverted = inv;
  for (int i = 0; i < nbits && i < 256; i++) b.bits[i] = bits[i] ? '1' : '0';
  rf_hex(bytes, nbytes, b.hex);
  for (int i = 0; i < n && i < 48; i++) b.w[i] = w[i];
  portENTER_CRITICAL(&s_rfMux);
  s_rfBursts[s_rfBurstHead] = b;
  s_rfBurstHead = (s_rfBurstHead + 1) % RF_BURSTS;
  if (s_rfBurstCount < RF_BURSTS) s_rfBurstCount++;
  s_rfBurstTotal++;
  portEXIT_CRITICAL(&s_rfMux);
}

// Turns the edges recorded since readTail (up to now) into pulse widths.
static int rfCollect(uint32_t &readTail, uint16_t *w, int n, uint32_t &prevUs, uint8_t &firstLevel, uint8_t &lastLevel) {
  uint32_t head = s_rfEdgeHead;
  if (head - readTail > RF_EDGE_RING) readTail = head - RF_EDGE_RING;   // fell behind: skip what was overwritten
  for (; readTail != head; readTail++) {
    uint32_t t = s_rfEdgeUs[readTail % RF_EDGE_RING];
    uint8_t lvAfter = s_rfEdgeLv[readTail % RF_EDGE_RING];
    if (prevUs && lvAfter == lastLevel) continue;                       // an edge was masked: keep the levels alternating
    if (prevUs) {
      uint32_t d = t - prevUs;
      if (n == 0) firstLevel = lastLevel;                               // the level of the pulse that just ended
      if (n < RF_MAX_PULSES) w[n++] = d > 65535 ? 65535 : (uint16_t)d;
    }
    prevUs = t; lastLevel = lvAfter;
  }
  return n;
}

// Idle: read the signal strength every 1 ms. Above the threshold: stop all SPI traffic and record for
// RF_CAPTURE_MS, check the strength once, and keep recording (up to RF_CAPTURE_MAX_MS) while it is still there.
static void rfTask(void *) {
  static uint16_t w[RF_MAX_PULSES];
  // Attached here so the interrupt runs on core 0: the DHT22 read switches interrupts off on core 1 for ~5 ms.
  attachInterrupt(digitalPinToInterrupt(RF_GDO0), rfEdgeIsr, CHANGE);
  for (;;) {
    vTaskDelay(1);
    if (s_rfReconfig) { s_rfReconfig = false; rfConfigure(); }
    int rssi = rfRssiDbm(); s_rfRssi = rssi;
    if (rssi < s_rfThr) continue;
    // a signal: record it with the SPI bus quiet
    uint32_t readTail = s_rfEdgeHead > 64 ? s_rfEdgeHead - 64 : 0;      // keep the last few edges: the start of the preamble
    uint32_t prevUs = 0; uint8_t firstLevel = 0, lastLevel = 0;
    int n = 0, peak = rssi;
    uint32_t start = millis();
    for (;;) {
      vTaskDelay(pdMS_TO_TICKS(RF_CAPTURE_MS));
      n = rfCollect(readTail, w, n, prevUs, firstLevel, lastLevel);
      if (millis() - start >= RF_CAPTURE_MAX_MS || n >= RF_MAX_PULSES) break;
      int r = rfRssiDbm(); s_rfRssi = r;
      if (r < s_rfThr) break;
      if (r > peak) peak = r;
    }
    rfFinishBurst(w, n, firstLevel, peak);
  }
}

// Starts the receiver. False if the CC1101 does not answer (not wired, or wired wrong): the hub carries on without it.
static bool rfBegin() {
  rfLoadSettings();
  pinMode(RF_CS, OUTPUT); digitalWrite(RF_CS, HIGH);
  pinMode(RF_GDO0, INPUT);
  s_rfSpi.begin(RF_SCK, RF_MISO, RF_MOSI, -1);
  rfStrobe(0x30); delay(5);                                          // SRES
  s_rfPart = rfRead(0x30, true); s_rfVersion = rfRead(0x31, true);
  s_rfOk = s_rfPart == 0x00 && (s_rfVersion == 0x14 || s_rfVersion == 0x04 || s_rfVersion == 0x07);
  Serial.printf("CC1101: partnum 0x%02X version 0x%02X -> %s\n", s_rfPart, s_rfVersion, s_rfOk ? "found" : "NOT found (check wiring)");
  if (!s_rfOk) return false;
  rfConfigure();
  xTaskCreatePinnedToCore(rfTask, "rf", 6144, NULL, 3, NULL, 0);
  return true;
}
