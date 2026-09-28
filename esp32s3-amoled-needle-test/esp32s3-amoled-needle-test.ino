/*
  esp32s3-amoled-needle-test
  Standalone test: a moving needle on the round AMOLED, sweeping
  automatically via a simulated value (no real CAN/UART data needed yet).

  REWRITTEN to be more diagnosable: prints a step at every stage of
  setup (so if it stops working, the Serial Monitor shows exactly where),
  and prints a continuous heartbeat in loop() every second - so opening
  Serial Monitor at ANY time after boot (not just the exact moment of
  reset) will show output within ~1 second, rather than only printing
  once during a boot window that's easy to miss.

  IMPORTANT - compile with the corrected board options (this board's
  chip is ESP32-S3R8 with 16MB flash - the default profile is wrong,
  PSRAM disabled and flash set to 4MB):
    arduino-cli compile --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M esp32s3-amoled-needle-test
    arduino-cli upload -p COMx --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M esp32s3-amoled-needle-test

  Display config (confirmed working via the image-loading test that DID
  work): CO5300 driver, 10MHz QSPI, offset=7,7,7,7.

  Erase technique: plain black background (no gauge-face image in this
  test), so the old needle is erased by redrawing its previous line in
  the background color before drawing the new one.

  Sweep convention: -135 to +135 degrees (270 total), 0 = straight up.
*/

#include <Arduino_GFX_Library.h>

// ---- Display pins (confirmed) ----
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

#define COLOR_BG     0x0000
#define COLOR_NEEDLE 0xF800 // red
#define COLOR_HUB    0x7BEF // gray
#define COLOR_RING   0x39C7 // faint reference ring

const int16_t CX = 233, CY = 233;
const int16_t NEEDLE_LEN = 180;
const int16_t HUB_RADIUS = 14;
const float SWEEP_START_DEG = -135.0f;
const float SWEEP_TOTAL_DEG = 270.0f;

float lastAngleDeg = SWEEP_START_DEG;

void drawNeedle(float angleDeg, uint16_t color) {
  float rad = angleDeg * PI / 180.0f;
  int16_t x1 = CX + (int16_t)(sin(rad) * NEEDLE_LEN);
  int16_t y1 = CY - (int16_t)(cos(rad) * NEEDLE_LEN);
  gfx->drawLine(CX, CY, x1, y1, color);
  gfx->drawLine(CX + 1, CY, x1 + 1, y1, color);
  gfx->drawLine(CX, CY + 1, x1, y1 + 1, color);
}

unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL_MS = 30;
float testValue = 0;
int direction = 1;

unsigned long lastHeartbeat = 0;
unsigned long loopCount = 0;

void setup() {
  Serial.begin(115200);
  delay(1500); // extra time for USB serial to enumerate before first prints

  Serial.println();
  Serial.println("=== Needle test booting ===");
  Serial.println("Step 1: setting AMOLED_EN high...");
  pinMode(AMOLED_EN, OUTPUT);
  digitalWrite(AMOLED_EN, HIGH);
  Serial.println("Step 1 done.");

  Serial.println("Step 2: calling gfx->begin(10000000)...");
  bool ok = gfx->begin(10000000);
  Serial.print("Step 2 done. begin() returned: ");
  Serial.println(ok ? "SUCCESS" : "FAILED");

  if (!ok) {
    Serial.println("HALTING - display init failed. Check wiring/power to the AMOLED panel.");
    while (true) {
      Serial.println("...still halted (display init failed)...");
      delay(2000);
    }
  }

  Serial.println("Step 3: fillScreen(BLACK)...");
  gfx->fillScreen(COLOR_BG);
  Serial.println("Step 3 done.");

  Serial.println("Step 4: drawing reference ring + hub...");
  gfx->drawCircle(CX, CY, NEEDLE_LEN + 10, COLOR_RING);
  gfx->fillCircle(CX, CY, HUB_RADIUS, COLOR_HUB);
  Serial.println("Step 4 done.");

  Serial.println("Step 5: drawing initial needle position...");
  drawNeedle(lastAngleDeg, COLOR_NEEDLE);
  Serial.println("Step 5 done.");

  Serial.println("=== Setup complete - entering loop() ===");
}

void loop() {
  unsigned long now = millis();

  // Heartbeat: prints every second regardless of anything else, so
  // connecting Serial Monitor at any point shows output within ~1s.
  if (now - lastHeartbeat >= 1000) {
    lastHeartbeat = now;
    Serial.print("heartbeat - loop() running, iterations so far: ");
    Serial.println(loopCount);
  }
  loopCount++;

  if (now - lastUpdate < UPDATE_INTERVAL_MS) return;
  lastUpdate = now;

  testValue += direction * 1.0f;
  if (testValue >= 100) { testValue = 100; direction = -1; }
  if (testValue <= 0)   { testValue = 0; direction = 1; }

  float angleDeg = SWEEP_START_DEG + (testValue / 100.0f) * SWEEP_TOTAL_DEG;

  drawNeedle(lastAngleDeg, COLOR_BG);
  drawNeedle(angleDeg, COLOR_NEEDLE);
  gfx->fillCircle(CX, CY, HUB_RADIUS, COLOR_HUB);

  lastAngleDeg = angleDeg;
}
