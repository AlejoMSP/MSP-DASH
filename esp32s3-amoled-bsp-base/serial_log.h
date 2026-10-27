// serial_log.h - what goes to the serial port. Boot messages and answers to typed serial commands always print.
// Everything printed while running (heap readouts, CAN statistics, logging start/stop, config saves, ...) goes through
// RT_LOG, which prints nothing unless SERIAL_STREAM is 1 (set it here, or with -DSERIAL_STREAM=1, when debugging).
#pragma once
#ifndef SERIAL_STREAM
#define SERIAL_STREAM 0
#endif
#ifndef RT_LOG
#if SERIAL_STREAM
#define RT_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define RT_LOG(...) do { } while (0)
#endif
#endif
