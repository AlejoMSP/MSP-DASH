/*
  esp32s3-amoled-serial-echo
  Minimal diagnostic: type on the Serial terminal (any tool - arduino-cli
  monitor, PuTTY, Arduino IDE, etc.) and whatever you type appears on the
  round AMOLED screen.

  This tests the OPPOSITE direction from what's been failing (Serial RX
  - computer to board - instead of Serial TX/print - board to computer),
  plus the display itself, all independent of any Serial.print() output
  or complex libraries (no LVGL here - raw Arduino_GFX text drawing only,
  keeping this as simple/minimal as possible to isolate variables).

  If typed characters DO appear on screen: the board is genuinely
  running application code (not crashed/stuck), USB serial works in at
  least the RX direction, and the display itself is fine - the problem
  is specifically isolated to Serial TX/print not being visible in the
  monitor tools tried so far.

  If NOTHING happens even after typing: something more fundamental is
  wrong (board not running code at all, or USB RX also broken).

  Display config (confirmed working): CO5300, 10MHz QSPI, offset 7,7,7,7.
*/

#include <Arduino_GFX_Library.h>

#define AMOLED_CS    9
#define AMOLED_SCK   10
#define AMOLED_D0    11
#define AMOLED_D1    12
#define AMOLED_D2    13
#define AMOLED_D3    14
#define AMOLED_RST   21
#define AMOLED_EN    42

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  AMOLED_CS, AMOLED_SCK, AMOLED_D0, AMOLED_D1, AMOLED_D2, AMOLED_D3);

Arduino_GFX *gfx = new Arduino_CO5300(
  bus, AMOLED_RST, 0, 466, 466,
  7, 7, 7, 7);

String typedText = "";
const int16_t TEXT_X = 40, TEXT_Y = 200;

void redrawText() {
  // Opaque background (white on black) - single write, no separate
  // clear pass needed, same tearing-avoidance technique confirmed
  // working in earlier tests.
  gfx->fillRect(0, TEXT_Y - 10, 466, 100, 0x0000); // clear a band for the text
  gfx->setTextColor(0xFFFF, 0x0000);
  gfx->setTextSize(3);
  gfx->setCursor(TEXT_X, TEXT_Y);
  gfx->print(typedText);
}

void setup() {
  pinMode(AMOLED_EN, OUTPUT);
  digitalWrite(AMOLED_EN, HIGH);

  gfx->begin(10000000);
  gfx->fillScreen(0x0000);

  gfx->setTextColor(0x4E9C, 0x0000); // cyan-ish, matches project theme
  gfx->setTextSize(2);
  gfx->setCursor(40, 100);
  gfx->print("Type on Serial terminal...");

  // Intentionally NOT calling Serial.begin() print statements as the
  // primary signal here - the test is whether typed input (RX) reaches
  // the board and displays, independent of TX/print working at all.
  Serial.begin(115200);
}

void loop() {
  bool changed = false;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      typedText = ""; // clear on Enter, ready for next line
    } else {
      typedText += c;
    }
    changed = true;
  }
  if (changed) {
    redrawText();
  }
}
