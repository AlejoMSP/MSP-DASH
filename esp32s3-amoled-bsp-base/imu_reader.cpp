/*
  imu_reader.cpp
  QMI8658 IMU reading - initialization sequence matches Waveshare's own
  confirmed qmi8658c_example() exactly (qmi8658_init() alone, no
  separate enableSensors call - their example shows this is sufficient).

  AXIS MAPPING NOTE: acc[0]/acc[1] are mapped here as lateral/
  longitudinal G directly, assuming units are already in 'g' (the
  driver's apparent default). This has NOT been verified against the
  board's actual physical mounting orientation in the car - axes may
  need to be swapped or sign-flipped once tested with the board
  installed and the car actually moving/turning.
*/

#include "imu_reader.h"
#include "qmi8658c.h"
#include <Arduino.h>

volatile float g_accel_lateral = 0;
volatile float g_accel_longitudinal = 0;

static unsigned long s_lastReadMs = 0;
static const unsigned long READ_INTERVAL_MS = 50;

void imu_reader_init(void) {
  qmi8658_init();
  delay(1000); // matches the delay in Waveshare's own confirmed example before first read
  Serial.println("QMI8658 IMU initialized");
}

void imu_reader_loop(void) {
  unsigned long now = millis();
  if (now - s_lastReadMs < READ_INTERVAL_MS) return;
  s_lastReadMs = now;

  float acc[3], gyro[3];
  qmi8658_read_xyz(acc, gyro);
  g_accel_lateral = acc[0];
  g_accel_longitudinal = acc[1];
}
