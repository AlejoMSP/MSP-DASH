/*
  esp32s3-amoled-test-co5300
  Display bring-up test for the Waveshare ESP32-S3-Touch-AMOLED-1.43
  board, assuming the CO5300 driver chip variant.

  Board specs (confirmed from Waveshare's product page):
    MCU: ESP32-S3R8, 16MB Flash, 8MB PSRAM
    Display: 1.43" AMOLED, 466x466, QSPI interface
    Driver IC: SH8601 *or* CO5300 (varies by production batch - this
    sketch tests the CO5300 variant specifically)

  Confirmed pinout (from Waveshare's product page):
    QSPI_CS    -> GPIO9
    QSPI_CLK   -> GPIO10
    QSPI_D0    -> GPIO11 (mosi)
    QSPI_D1    -> GPIO12 (miso)
    QSPI_D2    -> GPIO13 (quadwp)
    QSPI_D3    -> GPIO14 (quadhd)
    AMOLED_RESET -> GPIO21
    AMOLED_EN    -> GPIO42 (must be driven HIGH to power the panel)

  Library: "GFX Library for Arduino" by moononournation
    (github.com/moononournation/Arduino_GFX)
  Constructor signatures below are copied directly from the installed
  library source (Arduino_CO5300.h, Arduino_ESP32QSPI.h) - not guessed.

  Column/row offset parameters default to 0,0,0,0 here since we don't
  have Waveshare's own confirmed values for this exact board. If the
  test pattern below renders but appears SHIFTED or CROPPED (rather than
  garbled/blank), that still indicates this IS the correct driver chip -
  just needs offset tuning. A blank screen or scrambled/random colors
  instead points to this being the WRONG chip - try the SH8601 test
  sketch instead.
*/

#include <Arduino_GFX_Library.h>

#define AMOLED_CS    9
#define AMOLED_SCK   10
#define AMOLED_D0    11  // mosi
#define AMOLED_D1    12  // miso
#define AMOLED_D2    13  // quadwp
#define AMOLED_D3    14  // quadhd
#define AMOLED_RST   21
#define AMOLED_EN    42

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  AMOLED_CS, AMOLED_SCK, AMOLED_D0, AMOLED_D1, AMOLED_D2, AMOLED_D3);

Arduino_GFX *gfx = new Arduino_CO5300(
  bus, AMOLED_RST, 0 /* rotation */, 466 /* width */, 466 /* height */,
  0, 0, 0, 0 /* col/row offsets - untuned, see header note */);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("CO5300 test starting...");

  pinMode(AMOLED_EN, OUTPUT);
  digitalWrite(AMOLED_EN, HIGH); // power/enable the AMOLED panel

  // Explicit slower QSPI clock (default is 40MHz) - dithered/scrambled
  // partial rendering is a classic signal-integrity symptom at high
  // speed, especially over jumper-wire connections rather than a
  // soldered/direct connection. Testing 20MHz first.
  if (!gfx->begin(20000000)) {
    Serial.println("gfx->begin() FAILED");
  } else {
    Serial.println("gfx->begin() succeeded");
  }

  // Color bar test pattern - easy to visually confirm correct rendering.
  // Using raw RGB565 hex instead of named color constants (RED/GREEN/etc)
  // since this library version doesn't define those macros the way expected.
  // 0xF800=red, 0x07E0=green, 0x001F=blue, 0x0000=black, 0xFFFF=white
  gfx->fillScreen(0xF800);
  delay(1000);
  gfx->fillScreen(0x07E0);
  delay(1000);
  gfx->fillScreen(0x001F);
  delay(1000);
  gfx->fillScreen(0x0000);

  gfx->setCursor(80, 220);
  gfx->setTextColor(0xFFFF);
  gfx->setTextSize(3);
  gfx->println("CO5300");
  gfx->setCursor(60, 260);
  gfx->println("TEST OK");

  Serial.println("CO5300 test pattern drawn - check display");
}

void loop() {
  // static test pattern - nothing to do here
}
