// button_actions.h - what the hub's buttons can do (configured on the hub's /buttons page). Shared: the hub runs the
// actions; the screens name them in pop-ups. The hub copies this file.
#pragma once
#include <stdint.h>
enum BtnActionId {
  BA_NONE = 0, BA_NEXT_PAGE, BA_PREV_PAGE, BA_NEXT_GAUGE, BA_GOTO_PAGE, BA_DIM, BA_BRIGHT, BA_COLOR,
  BA_RACE, BA_CLEAR_WARNING, BA_LOG_MARK, BA_SHOW_IP, BA_OLED_NEXT, BA_OUT_TOGGLE, BA_ECU_MAP, BA_COUNT   // new actions go before BA_COUNT, never in between
};
// Names for the pop-up and the page (index = BtnActionId).
static const char *const BA_NAMES[BA_COUNT] = {
  "Nothing", "Next page", "Previous page", "Next gauge (choose screen)", "Go to page", "Dim all", "Brighten all",
  "Color scheme", "Race mode", "Clear warning", "Log marker", "Show IP addresses", "OLED next page", "Toggle output", "ECU map (next)"
};
typedef struct { uint8_t action; uint8_t param; } BtnBinding;   // param: page number (Go to page / Race mode) or colour preset (255 = cycle)
// Before hub 53 the two page buttons had a 3 x 6 table of bindings (left / right / both, press / held 1-10 s), saved in
// NVS "btncfg". The hub reads it once to set up the new Buttons page (presses only), then no more.
#define BTN_INPUTS 3
#define BTN_LEVELS 6
