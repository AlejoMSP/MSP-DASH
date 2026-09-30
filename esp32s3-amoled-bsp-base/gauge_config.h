#ifndef GAUGE_CONFIG_H
#define GAUGE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// The pool of signals available to assign to any gauge slot - every
// value we currently know how to decode from either rusEFI or
// MegaSquirt/LinkECU.
typedef enum {
  SIG_NONE = 0,
  SIG_RPM,
  SIG_CLT,
  SIG_MAP,
  SIG_AFR,
  SIG_OILP,
  SIG_OILT,
  SIG_FUELP,
  SIG_FLEX,
  SIG_TPS,
  SIG_BATTV,
  SIG_COUNT
} SignalId;

#define MAX_GAUGE_SLOTS 8
#define GAUGE_CAPTION_MAXLEN 24

typedef struct {
  SignalId signal;
  bool enabled;
  int32_t min_val;
  int32_t max_val;
  int32_t rest_val;
  char caption[GAUGE_CAPTION_MAXLEN];
  uint32_t needle_color; // 0xRRGGBB
  bool has_warning;
  float warning_threshold;
  bool warning_above;
} GaugeSlotConfig;

// Loads the persisted slot configuration from NVS (falls back to the
// current 7-gauge defaults if nothing has been saved yet) - call once
// from setup(), before build_gauge_app().
void gauge_config_init(void);

// Returns a pointer to the live in-memory config array (MAX_GAUGE_SLOTS
// entries) - gauge_app.c reads this to build pages.
GaugeSlotConfig *gauge_config_get_slots(void);

// Saves the current in-memory config array to NVS - call after
// applying changes received from the web config page.
void gauge_config_save(void);

// Human-readable signal name, for the web UI's dropdown and captions.
const char *gauge_config_signal_name(SignalId sig);

// Reads the live current value and has_data flag for a given signal -
// the generic dispatch gauge_app.c's update timer uses instead of
// hardcoded per-gauge references, now that pages are data-driven.
float gauge_config_signal_value(SignalId sig);
bool gauge_config_signal_has_data(SignalId sig);

#ifdef __cplusplus
}
#endif

#endif
