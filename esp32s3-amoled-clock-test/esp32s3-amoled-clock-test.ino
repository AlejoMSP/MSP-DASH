/*
  esp32s3-amoled-clock-test
  Simple digital clock (seconds.milliseconds since boot) in big text on
  the AMOLED display - no color test pattern, boots straight into this.
  Confirms clean, CONTINUOUSLY-updating text rendering (not just a
  static one-time draw) ahead of building the real gauge cluster.

  Confirmed working config from prior bring-up testing:
    - Driver: CO5300
    - QSPI clock: 10MHz (40MHz/20MHz both showed signal-integrity issues
      - dithered/corrupted rendering - at higher speeds)
    - Visible area: 466x466, centered in the panel's 480x480 native
      buffer via offset=7,7,7,7 - alignment still being visually
      confirmed as of this writing (see esp32s3-amoled-test-co5300's
      border+crosshair test)

  Performance note: a full 466x466 16-bit-color frame is ~434KB - at our
  confirmed 10MHz QSPI (quad-lane) speed, a full-screen redraw takes
  longer than a smooth update interval would allow. Instead of clearing
  the whole screen every frame, this only clears a fixed-size rectangle
  behind the text (~36KB) - fast enough for smooth continuous updates.
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
  7, 7, 7, 7 /* centering offset - see header note */);

const uint8_t TEXT_SIZE = 6;
const int16_t CLEAR_X = 60, CLEAR_Y = 190, CLEAR_W = 350, CLEAR_H = 70;

unsigned long lastDraw = 0;
const unsigned long DRAW_INTERVAL_MS = 50; // ~20 updates/sec

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Clock test starting...");

  pinMode(AMOLED_EN, OUTPUT);
  digitalWrite(AMOLED_EN, HIGH); // power/enable the AMOLED panel

  if (!gfx->begin(10000000)) {
    Serial.println("gfx->begin() FAILED");
  } else {
    Serial.println("gfx->begin() succeeded");
  }

  gfx->fillScreen(0x0000); // one-time full clear at boot only
}

void loop() {
  unsigned long now = millis();
  if (now - lastDraw < DRAW_INTERVAL_MS) return;
  lastDraw = now;

  unsigned long secs = now / 1000;
  unsigned int millisPart = now % 1000;

  char buf[16];
  snprintf(buf, sizeof(buf), "%lu.%03u", secs, millisPart);

  // Clear only the text region, not the whole screen - keeps this fast
  // enough for smooth updates at our confirmed 10MHz QSPI speed.
  gfx->fillRect(CLEAR_X, CLEAR_Y, CLEAR_W, CLEAR_H, 0x0000);

  gfx->setTextColor(0xFFFF);
  gfx->setTextSize(TEXT_SIZE);
  gfx->setCursor(CLEAR_X + 10, CLEAR_Y + 15);
  gfx->print(buf);
}
