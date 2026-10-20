#ifndef SD_LOGGING_H
#define SD_LOGGING_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// When logging starts (chosen on the Log Channels page, saved in NVS). Like TunerStudio's log triggers:
// logging starts when the condition becomes true and stops once it has been false for stop_delay_s seconds,
// so a stop at a red light does not split the log. Each start opens a NEW file: log_1.csv, log_2.csv, ...
enum {
  LOG_START_KEY_ON = 0,   // the ECU is sending data (key on)
  LOG_START_RUNNING = 1,  // RPM above 400 (engine running)
  LOG_START_MOVING = 2,   // speed above 2 mph
  LOG_START_SIGNAL = 3,   // a chosen signal goes above (or below) a level, e.g. throttle above 90 %
  LOG_START_COUNT
};
#define LOG_RUNNING_RPM 400.0f
#define LOG_MOVING_MPH 2.0f
#define LOG_STOP_DELAY_MAX 600

typedef struct {
  uint8_t mode;          // LOG_START_*
  uint8_t sig;           // LOG_START_SIGNAL: which signal
  bool above;            // LOG_START_SIGNAL: true = above the level, false = below
  float level;           // LOG_START_SIGNAL: the level, in the signal's units
  uint16_t stop_delay_s; // keep logging this long after the condition goes false (0..600)
} SdLogTrigger;

// Loads the trigger settings. Logging needs the SD card; without it nothing is written. Call once from setup().
void sd_logging_init(void);

// Call every loop(): checks the trigger, opens/closes log files and appends a row every 500 ms while logging.
// Never logs while the simulator runs (this board's or the hub's).
void sd_logging_loop(void);

// True only while rows are being written - drives the on-screen blinking logging indicator in gauge_app.c.
bool sd_logging_is_active(void);

void sd_logging_get_trigger(SdLogTrigger *t);
// Checks and saves new settings. Returns false (and changes nothing) if they make no sense.
bool sd_logging_set_trigger(const SdLogTrigger *t);

// One line for the web page: what logging is doing right now.
const char *sd_logging_status_text(void);

#ifdef __cplusplus
}
#endif

#endif
