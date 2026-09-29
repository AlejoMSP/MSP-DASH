/*
  esp32s3-amoled-touch-test
  Standalone test for the board's capacitive touch controller (FT3168 or
  FT6146, varies by batch - same "varies" caveat as the display driver).
  Prints raw touch samples and detected swipe direction to Serial -
  validates the hardware/library work before wiring swipe-to-change-page
  into the real multi-page gauge cluster.

  Library: bb_captouch by Larry Bank - supports FT6x36-family chips
  (likely covers FT6146) plus several others, with auto-detection of
  sensor type/address. FT3168 specifically isn't in its stated supported
  list - if this board's actual chip is FT3168, auto-detect may not
  recognize it. This test will make that clear either way (prints
  init() result and whatever getSamples() reports).

  Touch pins (confirmed): TP_SDA=GPIO47, TP_SCL=GPIO48 (I2C). No
  dedicated reset/interrupt pin listed in the official pinout table, so
  using -1 (unused) for both, matching the library's defaults.

  Swipe detection: tracks the touch position from first contact to
  release (or largest movement across samples while touching), reports
  LEFT/RIGHT if horizontal movement exceeds a threshold.
*/

#include <bb_captouch.h>

#define TOUCH_SDA 47
#define TOUCH_SCL 48

BBCapTouch touch;

bool wasTouching = false;
int16_t touchStartX = -1;
int16_t lastX = -1;
const int16_t SWIPE_THRESHOLD_PX = 60;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Touch test starting...");

  int result = touch.init(TOUCH_SDA, TOUCH_SCL);
  Serial.printf("touch.init() returned: %d\n", result);
  Serial.printf("sensorType() returned: %d\n", touch.sensorType());

  Serial.println("Touch the screen and swipe left/right to test.");
}

void loop() {
  TOUCHINFO ti;
  int result = touch.getSamples(&ti);

  if (ti.count > 0) {
    Serial.printf("Touch detected: count=%d, x=%d, y=%d, pressure=%d\n",
                  ti.count, ti.x[0], ti.y[0], ti.pressure[0]);

    if (!wasTouching) {
      // New touch-down
      touchStartX = ti.x[0];
      wasTouching = true;
    }
    lastX = ti.x[0];
  } else {
    if (wasTouching) {
      // Touch just released - evaluate swipe
      int16_t deltaX = lastX - touchStartX;
      if (deltaX > SWIPE_THRESHOLD_PX) {
        Serial.println(">>> SWIPE RIGHT detected <<<");
      } else if (deltaX < -SWIPE_THRESHOLD_PX) {
        Serial.println(">>> SWIPE LEFT detected <<<");
      } else {
        Serial.println("(touch released - no significant swipe)");
      }
    }
    wasTouching = false;
    touchStartX = -1;
    lastX = -1;
  }

  delay(20);
}
