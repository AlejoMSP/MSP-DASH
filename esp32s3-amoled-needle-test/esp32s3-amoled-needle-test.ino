/*
  esp32s3-amoled-needle-test
  Standalone test: a moving needle on the round AMOLED, sweeping
  automatically via a simulated value (no real CAN/UART data needed yet)
  to prove out rotation math + flicker-free erase/redraw before
  integrating with a real gauge background image or live data.

  Technique: since this test has NO background image (plain black), the
  needle is "erased" by redrawing its OLD line in the background color
  before drawing the NEW line at the new angle. This is much simpler
  than what a real gauge-face image background would need (restoring
  the actual background pixels from a buffer instead of just painting
  over in one flat color) - that's a separate, later step once this
  core rotation/redraw mechanic is proven here.

  Sweep convention: -135 to +135 degrees (270 total), 0 degrees = straight
  up - matches the web dashboard's gauge convention used throughout this
  project (see buildGaugeFace()/updateNeedle() in esp32-main.ino).

  Display config (confirmed working via prior bring-up testing):
    - Driver: CO5300, QSPI clock: 10MHz, offset=7,7,7,7

  Board compile note: this board needs PSRAM=opi and FlashSize=16M board
  options (confirmed from `arduino-cli board details` - the default
  profile has PSRAM disabled and flash set to 4MB, both wrong for this
  ESP32-S3R8/16MB-flash board):
    arduino-cli compile --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M ...
  (This particular sketch doesn't actually need PSRAM itself, but using
  the same corrected FQBN as the other S3 sketches avoids confusion.)
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
#define COLOR_RING   0x39C7 // faint reference ring, just to visualize the sweep range

const int16_t CX = 233, CY = 233; // screen center
const int16_t NEEDLE_LEN = 180;
const int16_t HUB_RADIUS = 14;
const float SWEEP_START_DEG = -135.0f;
const float SWEEP_TOTAL_DEG = 270.0f;

float lastAngleDeg = SWEEP_START_DEG;

// 0 degrees = straight up (matches this project's established gauge
// convention). Draws a slightly-thickened line (3 parallel 1px lines)
// since Arduino_GFX's basic drawLine is 1px only.
void drawNeedle(float angleDeg, uint16_t color) {
  float rad = angleDeg * PI / 180.0f;
  int16_t x1 = CX + (int16_t)(sin(rad) * NEEDLE_LEN);
  int16_t y1 = CY - (int16_t)(cos(rad) * NEEDLE_LEN);
  gfx->drawLine(CX, CY, x1, y1, color);
  gfx->drawLine(CX + 1, CY, x1 + 1, y1, color);
  gfx->drawLine(CX, CY + 1, x1, y1 + 1, color);
}

unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL_MS = 30; // ~33 updates/sec

float testValue = 0; // simulated 0-100 value driving the needle
int direction = 1;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Needle test starting...");

  pinMode(AMOLED_EN, OUTPUT);
  digitalWrite(AMOLED_EN, HIGH);

  if (!gfx->begin(10000000)) {
    Serial.println("gfx->begin() FAILED");
  } else {
    Serial.println("gfx->begin() succeeded");
  }

  gfx->fillScreen(COLOR_BG);
  gfx->drawCircle(CX, CY, NEEDLE_LEN + 10, COLOR_RING); // faint reference ring
  gfx->fillCircle(CX, CY, HUB_RADIUS, COLOR_HUB);

  drawNeedle(lastAngleDeg, COLOR_NEEDLE);
  Serial.println("Needle test running - sweeping automatically");
}

void loop() {
  unsigned long now = millis();
  if (now - lastUpdate < UPDATE_INTERVAL_MS) return;
  lastUpdate = now;

  testValue += direction * 1.0f;
  if (testValue >= 100) { testValue = 100; direction = -1; }
  if (testValue <= 0)   { testValue = 0; direction = 1; }

  float angleDeg = SWEEP_START_DEG + (testValue / 100.0f) * SWEEP_TOTAL_DEG;

  drawNeedle(lastAngleDeg, COLOR_BG);   // erase old position
  drawNeedle(angleDeg, COLOR_NEEDLE);   // draw new position
  gfx->fillCircle(CX, CY, HUB_RADIUS, COLOR_HUB); // redraw hub in case the erase clipped it

  lastAngleDeg = angleDeg;
}
