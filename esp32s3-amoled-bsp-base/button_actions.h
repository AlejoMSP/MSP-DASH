// button_actions.h - what the hub's buttons can do (configured on the hub's /buttons page). Shared: the hub runs the
// actions; the screens name them in the "Release: ..." pop-up while a button is held. The hub copies this file.
#pragma once
#include <stdint.h>
enum BtnActionId {
  BA_NONE = 0, BA_NEXT_PAGE, BA_PREV_PAGE, BA_NEXT_GAUGE, BA_GOTO_PAGE, BA_DIM, BA_BRIGHT, BA_COLOR,
  BA_RACE, BA_CLEAR_WARNING, BA_LOG_MARK, BA_COUNT
};
// Names for the pop-up and the page (index = BtnActionId).
static const char *const BA_NAMES[BA_COUNT] = {
  "Nothing", "Next page", "Previous page", "Next gauge (choose screen)", "Go to page", "Dim all", "Brighten all",
  "Color scheme", "Race mode", "Clear warning", "Log marker"
};
#define BTN_INPUTS 3     // 0 = left button, 1 = right button, 2 = both
#define BTN_LEVELS 6     // 0 = short press, then held 1, 2, 3, 5, 10 s
static const uint16_t BTN_LEVEL_MS[BTN_LEVELS] = { 0, 1000, 2000, 3000, 5000, 10000 };
static const char *const BTN_INPUT_NAMES[BTN_INPUTS] = { "Left", "Right", "Both" };
static const char *const BTN_LEVEL_NAMES[BTN_LEVELS] = { "Press", "1 s", "2 s", "3 s", "5 s", "10 s" };
typedef struct { uint8_t action; uint8_t param; } BtnBinding;   // param: page number (Go to page / Race mode) or colour preset (255 = cycle)
