#ifndef SIMULATOR_H
#define SIMULATOR_H

/*
  simulator.h
  Bench simulator: feeds FAKE sensor values into the same globals the CAN
  decoder writes, so the gauges, trend graphs, alerts and web page can be
  tested at a desk with no ECU connected.

  It runs on the HUB (build 162 on): the hub copies simulator.cpp/.h from this folder at compile time, runs it,
  and flags its data packets as simulated. The screens no longer have a simulator page; they only
  hear the flag (sim_note_remote) and treat it like their own simulator: SIMULATED on screen, no SD logging.

  Safety rules (each one is covered by the host tests):
    - Always OFF at boot. Nothing about it is saved.
    - Switched on only from the hub's /simulator web page.
    - Stops itself after 10 minutes without a change on the page.
    - Stops itself the moment ANY real CAN frame arrives, so fake and real
      data can never mix, and refuses to start while real frames are arriving.
    - SD logging is paused while it runs, so fake data never lands in a log.
    - The round screen and the web page both say SIMULATED while it runs.
*/

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// True while fake data is being fed: by this board's own simulator, or (on a screen) by the hub's, as flagged in the
// data packets heard in the last second. (C-callable: gauge_app.c and sd_logging use it.)
bool sim_is_active(void);

// A screen calls this for every hub data packet flagged LINK_MASK_SIMULATED.
void sim_note_remote(void);

#ifdef __cplusplus
}

#include "gauge_config.h"

#define SIM_TIMEOUT_MS 600000UL   // 10 minutes

// Starts feeding fake values. manual=false: an automatic demo (a rev cycle with
// warming temperatures, nothing that trips the default warnings). manual=true:
// every value is set by hand from the web page.
void sim_start(bool manual);

// Stops, clears the "has data" flags so the gauges return to "--", and records why.
void sim_stop(const char *why);

// Call from loop(): ~20 updates a second while active; also enforces the
// timeout and the "real CAN frame arrived" rule.
void sim_loop(void);

// Call from the CAN receive task for every real frame received.
void sim_notify_real_frame(void);

// Manual mode: set one signal (clamped to its range) and restart the timeout.
// Returns false if the simulator is not running in manual mode.
bool sim_set_manual(int sig, float value);

bool sim_is_manual(void);
float sim_value(int sig);              // the value last fed for this signal
int sim_seconds_left(void);            // until the automatic switch-off (0 when off)
const char *sim_last_stop_reason(void); // "" if it has never been stopped

struct SimRange { float min, max, step; };
SimRange sim_range(int sig);

// The demo profile at t seconds after start - a pure function, so it can be tested.
void sim_demo_values(float t, float out[SIG_COUNT]);

#endif // __cplusplus
#endif
