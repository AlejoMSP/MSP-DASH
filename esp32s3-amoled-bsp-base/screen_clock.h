// screen_clock.h - the date and time on a screen, from the hub's GPS (MST1 packets, gps_time.h). Runs on from
// millis() between packets; unknown until the first one arrives after boot.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void screen_clock_set(uint32_t utc, int16_t offsetMin);              // from the hub's packet (any task)
bool screen_clock_now(uint32_t *utc, int16_t *offsetMin);            // false = the time is not known yet
bool screen_clock_text(int fmt, char *out, int len);                 // gt_format() of now; false (and "") if unknown
#ifdef __cplusplus
}
#endif
