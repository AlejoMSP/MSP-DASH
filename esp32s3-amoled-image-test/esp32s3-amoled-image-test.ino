/*
  esp32s3-amoled-image-test
  Standalone test: load a raw RGB565 image file from the SD card and
  display it on the round AMOLED - validates the image-loading pipeline
  on its own before folding it into the live gauge display.

  SD card pins (confirmed from Waveshare's official pinout table -
  DIFFERENT from the QSPI display pins, separate SPI bus):
    SD_CS    -> GPIO38
    SD_MOSI  -> GPIO39
    SD_MISO  -> GPIO40
    SD_SCLK  -> GPIO41

  Image format: raw RGB565, little-endian uint16 per pixel, no header -
  just width*height*2 bytes of pixel data, row-major. Convert any image
  to this format (see project docs for the conversion approach used).

  File expected on SD card root: /gauge_bg.raw (466x466 = 434312 bytes
  for a full-screen image, matching this display's exact resolution)

  Uses Arduino_GFX's draw16bitRGBBitmap(x, y, uint16_t *bitmap, w, h) -
  confirmed via the installed library's Arduino_GFX.h. Since ESP32-S3
  has 8MB PSRAM, the full 434KB image buffer is read into PSRAM (not the
  much smaller internal RAM) using heap_caps_malloc with MALLOC_CAP_SPIRAM.
*/

#include <Arduino_GFX_Library.h>
#include <SD.h>
#include <SPI.h>
#include "esp_heap_caps.h"

// ---- Display pins (confirmed) ----
#define AMOLED_CS    9
#define AMOLED_SCK   10
#define AMOLED_D0    11
#define AMOLED_D1    12
#define AMOLED_D2    13
#define AMOLED_D3    14
#define AMOLED_RST   21
#define AMOLED_EN    42

// ---- SD card pins (confirmed, separate SPI bus from the display) ----
#define SD_CS_PIN   38
#define SD_MOSI_PIN 39
#define SD_MISO_PIN 40
#define SD_SCLK_PIN 41

#define IMAGE_W 466
#define IMAGE_H 466
#define IMAGE_PATH "/gauge_bg.raw"

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  AMOLED_CS, AMOLED_SCK, AMOLED_D0, AMOLED_D1, AMOLED_D2, AMOLED_D3);

Arduino_GFX *gfx = new Arduino_CO5300(
  bus, AMOLED_RST, 0, 466, 466,
  7, 7, 7, 7);

// NOTE: HSPI here is a best-effort guess for ESP32-S3's SPI host naming
// (which differs somewhat from the classic ESP32). The display already
// occupies SPI2_HOST internally (confirmed from Arduino_ESP32QSPI.h's
// default), so the SD card needs a genuinely DIFFERENT SPI peripheral to
// avoid a bus conflict. If this doesn't compile or the SD card behaves
// oddly, try FSPI instead of HSPI - that's the other commonly-used
// identifier on S3-family chips, not confirmed against real source here.
SPIClass sdSPI(HSPI);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Image load test starting...");

  // --- Display init (confirmed working config) ---
  pinMode(AMOLED_EN, OUTPUT);
  digitalWrite(AMOLED_EN, HIGH);

  if (!gfx->begin(10000000)) {
    Serial.println("gfx->begin() FAILED");
  } else {
    Serial.println("gfx->begin() succeeded");
  }
  gfx->fillScreen(0x0000);

  // --- SD card init on its own SPI bus ---
  sdSPI.begin(SD_SCLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, sdSPI)) {
    Serial.println("SD card mount FAILED - check card is inserted and wiring");
    gfx->setTextColor(0xF800, 0x0000); // red - error state
    gfx->setTextSize(2);
    gfx->setCursor(60, 220);
    gfx->print("SD CARD FAILED");
    return;
  }
  Serial.println("SD card mounted");

  File f = SD.open(IMAGE_PATH);
  if (!f) {
    Serial.println("Could not open " IMAGE_PATH " - does it exist on the card?");
    gfx->setTextColor(0xF800, 0x0000);
    gfx->setTextSize(2);
    gfx->setCursor(40, 220);
    gfx->print("IMAGE FILE NOT FOUND");
    return;
  }

  size_t fileSize = f.size();
  size_t expectedSize = (size_t)IMAGE_W * IMAGE_H * 2;
  Serial.printf("Image file size: %u bytes (expected %u)\n", fileSize, expectedSize);

  // Allocate the pixel buffer in PSRAM, not the much smaller internal RAM
  uint16_t *imgBuf = (uint16_t *)heap_caps_malloc(expectedSize, MALLOC_CAP_SPIRAM);
  if (!imgBuf) {
    Serial.println("PSRAM allocation FAILED");
    f.close();
    gfx->setTextColor(0xF800, 0x0000);
    gfx->setTextSize(2);
    gfx->setCursor(40, 220);
    gfx->print("PSRAM ALLOC FAILED");
    return;
  }

  size_t bytesRead = f.read((uint8_t *)imgBuf, expectedSize);
  f.close();
  Serial.printf("Read %u bytes from SD\n", bytesRead);

  if (bytesRead != expectedSize) {
    Serial.println("WARNING: read size doesn't match expected - image may be wrong dimensions or truncated");
  }

  gfx->draw16bitRGBBitmap(0, 0, imgBuf, IMAGE_W, IMAGE_H);
  Serial.println("Image drawn to display");

  heap_caps_free(imgBuf);
}

void loop() {
  // static test - nothing to do here
}
