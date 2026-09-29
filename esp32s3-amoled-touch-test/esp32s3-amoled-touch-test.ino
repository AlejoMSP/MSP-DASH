/*
  esp32s3-amoled-touch-test
  REWRITTEN to be diagnosable: step-by-step setup logging + continuous
  heartbeat print (every second), same fix that resolved the identical
  "nothing shows on Serial" symptom seen earlier with the needle test -
  either the boot-time prints were missed (monitor opened too late,
  since they only print once) or something is hanging silently. The
  heartbeat guarantees SOME output within ~1s of connecting the monitor,
  at any point after boot, regardless of timing.

  Library: bb_captouch by Larry Bank - supports FT6x36-family chips
  (likely covers FT6146), auto-detects sensor type/address. FT3168
  specifically isn't in its stated supported list - if this board's
  actual touch IC is FT3168 rather than FT6146, this library may not
  recognize it. This test surfaces that directly via printed
  init()/sensorType() results.

  Touch pins (confirmed): TP_SDA=GPIO47, TP_SCL=GPIO48.
*/

#include <bb_captouch.h>

#define TOUCH_SDA 47
#define TOUCH_SCL 48

BBCapTouch touch;

bool wasTouching = false;
int16_t touchStartX = -1;
int16_t lastX = -1;
const int16_t SWIPE_THRESHOLD_PX = 60;

unsigned long lastHeartbeat = 0;
unsigned long loopCount = 0;

void setup() {
  Serial.begin(115200);
  delay(1500); // extra time for USB serial to enumerate before first prints

  Serial.println();
  Serial.println("=== Touch test booting ===");

  Serial.println("Step 1: calling touch.init()...");
  int result = touch.init(TOUCH_SDA, TOUCH_SCL);
  Serial.print("Step 1 done. touch.init() returned: ");
  Serial.println(result);

  Serial.println("Step 2: calling touch.sensorType()...");
  int sType = touch.sensorType();
  Serial.print("Step 2 done. sensorType() returned: ");
  Serial.println(sType);

  Serial.println("=== Setup complete - entering loop() ===");
  Serial.println("Touch the screen and swipe left/right to test.");
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

  TOUCHINFO ti;
  int result = touch.getSamples(&ti);

  if (ti.count > 0) {
    Serial.printf("Touch detected: count=%d, x=%d, y=%d, pressure=%d\n",
                  ti.count, ti.x[0], ti.y[0], ti.pressure[0]);

    if (!wasTouching) {
      touchStartX = ti.x[0];
      wasTouching = true;
    }
    lastX = ti.x[0];
  } else {
    if (wasTouching) {
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
