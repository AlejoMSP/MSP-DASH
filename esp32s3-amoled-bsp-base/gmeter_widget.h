#ifndef GMETER_WIDGET_H
#define GMETER_WIDGET_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds the G-force meter page: a cross with a dot (plus a short
// fading trail of recent positions) showing lateral (cornering) and
// longitudinal (accel/braking) G's. Returns the page object so the
// caller can add it to the page-cycle list and attach gesture
// handling, same as every other page.
lv_obj_t *build_gmeter_page(void (*gesture_cb)(lv_event_t *e));

// Called from the shared gauge update timer (same 80ms cadence as
// everything else) - repositions the dot and trail based on the
// current g_accel_lateral/g_accel_longitudinal values, and recolors
// the dot (blue = accelerating, red = braking).
void gmeter_update(void);

#ifdef __cplusplus
}
#endif

#endif
