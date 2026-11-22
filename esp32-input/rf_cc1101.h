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
#include "esp_timer.h"
#include "soc/gpio_reg.h"
#include "hub_pins.h"
#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include "rf_decode.h"


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
  uint16_t nw;                    // pulse widths stored (all of them, up to RF_MAX_PULSES)
  uint16_t w[RF_MAX_PULSES];      // pulse widths, us
  uint8_t firstLevel;
} RfBurst;

static SPIClass s_rfSpi(HSPI);
static bool s_rfOk = false;
static uint8_t s_rfPart = 0xFF, s_rfVersion = 0xFF;
static bool s_rfOok = false;                 // false = 2-FSK (most TPMS), true = OOK/ASK
static int s_rfThr = -85;                    // dBm (manual threshold)
static bool s_rfAuto = true;                 // automatic threshold: background + margin
static int s_rfMargin = 10;                  // dB above the background (automatic mode)
static volatile int s_rfFloor = -100;        // background: median signal of the last second, dBm
static volatile int s_rfActiveThr = -85;     // the threshold in use right now
#define RF_HIST 120                          // seconds of signal history for the page
static int8_t s_rfHistPeak[RF_HIST], s_rfHistFloor[RF_HIST];
static volatile int s_rfHistHead = 0, s_rfHistCount = 0;
static volatile int s_rfRssi = -120;
static volatile bool s_rfReconfig = false;
static RfBurst *s_rfBursts = nullptr;   // RF_BURSTS of them, on the heap (rfBegin): static RAM is full
#define RF_TOP 5                              // the strongest bursts are also kept here, so noise cannot push them out
static RfBurst *s_rfTop = nullptr;      // RF_TOP of them, on the heap too
static volatile int s_rfTopCount = 0;
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
// IRAM-safe throughout (no digitalRead / micros, which live in flash): esp_timer and the GPIO input register.
static volatile uint8_t s_rfIsrPin = 255;
static void IRAM_ATTR rfEdgeIsr() {
  uint32_t now = (uint32_t)esp_timer_get_time();
  if (s_rfSpiBusy || now - s_rfSpiEndUs < RF_SPI_QUIET_US) return;
  uint32_t h = s_rfEdgeHead;
  uint8_t pin = s_rfIsrPin;
  uint32_t lv = pin < 32 ? (REG_READ(GPIO_IN_REG) >> pin) & 1 : (REG_READ(GPIO_IN1_REG) >> (pin - 32)) & 1;
  s_rfEdgeUs[h % RF_EDGE_RING] = now;
  s_rfEdgeLv[h % RF_EDGE_RING] = (uint8_t)lv;
  s_rfEdgeHead = h + 1;
}
// GDO0 carries the raw demodulated signal, so plain noise toggles it thousands of times a second. Hub 63: the
// interrupt is only switched on while a burst is being recorded (it used to run all the time on core 0, next to
// Wi-Fi, and on a noisy wire starved it: the task watchdog crashes in the network task, settings not saving).
// A capture that brings more than RF_STORM_EDGES_PER_MS edges per millisecond is cut short as noise.
#define RF_STORM_EDGES_PER_MS 60
static volatile uint32_t s_rfStorms = 0;
static void rfIrqOn() { s_rfIsrPin = RF_GDO0; attachInterrupt(digitalPinToInterrupt(RF_GDO0), rfEdgeIsr, CHANGE); }
static void rfIrqOff() { detachInterrupt(digitalPinToInterrupt(RF_GDO0)); }

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

static void rfLoadSettings() {
  Preferences p; p.begin("rf", true);
  s_rfOok = p.getUChar("ook", 0); s_rfThr = p.getChar("thr", -85); s_rfAuto = p.getUChar("auto", 1); s_rfMargin = p.getUChar("margin", 10);
  p.end();
}
static void rfSaveSettings() {
  Preferences p; p.begin("rf", false);
  p.putUChar("ook", s_rfOok); p.putChar("thr", (int8_t)s_rfThr); p.putUChar("auto", s_rfAuto); p.putUChar("margin", (uint8_t)s_rfMargin);
  p.end();
}

// Background tracking: a histogram of the idle readings of the last second; its median is the background.
static uint16_t s_rfBins[128];   // -128..-1 dBm
static int s_rfBinN = 0, s_rfSecPeak = -128;
static uint32_t s_rfSecStart = 0;
static void rfTrack(int rssi) {
  int b = rssi + 128; if (b < 0) b = 0; if (b > 127) b = 127;
  s_rfBins[b]++; s_rfBinN++; if (rssi > s_rfSecPeak) s_rfSecPeak = rssi;
  uint32_t now = millis();
  if (now - s_rfSecStart < 1000) return;
  if (s_rfBinN) {
    int half = s_rfBinN / 2, acc = 0, med = -100;
    for (int i = 0; i < 128; i++) { acc += s_rfBins[i]; if (acc > half) { med = i - 128; break; } }
    s_rfFloor = med;
  }
  s_rfHistPeak[s_rfHistHead] = (int8_t)s_rfSecPeak; s_rfHistFloor[s_rfHistHead] = (int8_t)s_rfFloor;
  s_rfHistHead = (s_rfHistHead + 1) % RF_HIST; if (s_rfHistCount < RF_HIST) s_rfHistCount++;
  memset(s_rfBins, 0, sizeof s_rfBins); s_rfBinN = 0; s_rfSecPeak = -128; s_rfSecStart = now;
}
static inline int rfThreshold() { return s_rfAuto ? s_rfFloor + s_rfMargin : s_rfThr; }

// Turns the collected pulses into a burst for the page.
static void rfFinishBurst(const uint16_t *w, int n, uint8_t firstLevel, int peak) {
  if (n < RF_MIN_PULSES) return;
  static uint8_t bytes[RF_MAX_BYTES];
  static RfBurst b;   // 1.3 kB: static, not on the task stack
  memset(&b, 0, sizeof b);
  b.ms = millis(); b.rssi = (int8_t)peak; b.pulses = n; b.firstLevel = firstLevel;
  // pulse pairs -> half-bit time, glitches merged, receiver stretch corrected, Manchester (rf_decode.h)
  static char bitText[RF_MAX_BITS + 1];
  float unit = 0;
  int nbits = rf_decode_pairs(w, n, firstLevel, bitText, RF_MAX_BITS, &unit);
  b.unit = (uint16_t)(unit + 0.5f); b.nbits = nbits; b.inverted = false;
  memcpy(b.bits, bitText, nbits < 256 ? nbits : 256); b.bits[nbits < 256 ? nbits : 256] = 0;
  int nbytes = rf_bits_to_bytes(bitText, nbits, bytes, 32);
  rf_hex(bytes, nbytes, b.hex);
  b.nw = n > RF_MAX_PULSES ? RF_MAX_PULSES : n;
  memcpy(b.w, w, b.nw * sizeof(uint16_t));
  portENTER_CRITICAL(&s_rfMux);
  // strongest list, kept in order: replace the weakest if this one is stronger
  if (s_rfTopCount < RF_TOP) s_rfTop[s_rfTopCount++] = b;
  else {
    int weakest = 0;
    for (int i = 1; i < RF_TOP; i++) if (s_rfTop[i].rssi < s_rfTop[weakest].rssi) weakest = i;
    if (b.rssi > s_rfTop[weakest].rssi) s_rfTop[weakest] = b;
  }
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
  // The interrupt is attached from this task (core 0: the DHT22 read switches interrupts off on core 1 for ~5 ms),
  // and only while recording (rfIrqOn / rfIrqOff).
  for (;;) {
    vTaskDelay(1);
    if (s_rfReconfig) { s_rfReconfig = false; rfConfigure(); }
    int rssi = rfRssiDbm(); s_rfRssi = rssi;
    rfTrack(rssi);
    int thr = rfThreshold(); s_rfActiveThr = thr;
    if (rssi < thr) continue;
    // a signal: record it with the SPI bus quiet (the interrupt only runs from here to the end of the burst)
    uint32_t readTail = s_rfEdgeHead;
    rfIrqOn();
    uint32_t prevUs = 0; uint8_t firstLevel = 0, lastLevel = 0;
    int n = 0, peak = rssi;
    bool storm = false;
    uint32_t start = millis();
    for (;;) {
      uint32_t headBefore = s_rfEdgeHead;
      vTaskDelay(pdMS_TO_TICKS(RF_CAPTURE_MS));
      if (s_rfEdgeHead - headBefore > (uint32_t)RF_STORM_EDGES_PER_MS * RF_CAPTURE_MS) { s_rfStorms++; storm = true; break; }   // noise, not a sensor
      n = rfCollect(readTail, w, n, prevUs, firstLevel, lastLevel);
      if (millis() - start >= RF_CAPTURE_MAX_MS || n >= RF_MAX_PULSES) break;
      int r = rfRssiDbm(); s_rfRssi = r;
      if (r > s_rfSecPeak) s_rfSecPeak = r;     // the history shows the burst (not counted in the background)
      if (r < thr) break;
      if (r > peak) peak = r;
    }
    rfIrqOff();
    if (!storm) rfFinishBurst(w, n, firstLevel, peak);
  }
}

// Starts the receiver. False if the CC1101 does not answer (not wired, or wired wrong): the hub carries on without it.
static bool rfBegin() {
  s_rfBursts = (RfBurst *)calloc(RF_BURSTS, sizeof(RfBurst));
  s_rfTop = (RfBurst *)calloc(RF_TOP, sizeof(RfBurst));
  if (!s_rfBursts || !s_rfTop) { Serial.println("CC1101: no memory for the burst list"); return false; }
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
