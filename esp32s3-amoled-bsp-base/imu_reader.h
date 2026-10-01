#ifndef IMU_READER_H
#define IMU_READER_H

#ifdef __cplusplus
extern "C" {
#endif

// Initializes the QMI8658 IMU - call once from setup(), AFTER
// Touch_Init() (the IMU shares the same physical I2C bus as touch;
// Touch_Init() already installs that bus, so this does NOT call
// I2C_master_Init() itself - see i2c_bsp.cpp removal notes).
void imu_reader_init(void);

// Call every loop() iteration - reads fresh accelerometer data every
// 50ms and updates the global lateral/longitudinal G values.
void imu_reader_loop(void);

// Live accelerometer-derived G-force values, read by the G-meter
// widget (gauge_app.c). Raw axis-to-physical-direction mapping is NOT
// independently verified against this board's actual mounting
// orientation in the car - may need recalibration (swapping axes or
// signs) once tested with the board physically installed.
extern volatile float g_accel_lateral;      // left/right (cornering) G
extern volatile float g_accel_longitudinal; // forward/back (accel/braking) G

#ifdef __cplusplus
}
#endif

#endif
