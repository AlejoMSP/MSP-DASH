// btn_logic.h (hub) - the screen order for "Next gauge (choose screen)", and the old two-button bindings (before hub 53:
// read once to set up the Buttons page, hub_buttons.h). No Arduino calls: host-tested (tests/host/test_btn_logic.cpp).
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "button_actions.h"

// Screen order for "Next gauge": All, then Left, Center, Right (by name), then others alphabetically.
static inline int btn_rank(const char *nm) {
  const char *order[] = { "left", "center", "centre", "middle", "right" };
  for (int i = 0; i < 5; i++) {
    const char *a = nm, *b = order[i]; bool eq = true;
    while (*a || *b) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (x != y) { eq = false; break; } a++; b++; }
    if (eq) return i;
  }
  return 10;
}
static inline int btn_name_cmp(const char *a, const char *b) {
  int ra = btn_rank(a), rb = btn_rank(b);
  if (ra != rb) return ra - rb;
  while (*a && *b) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return x - y; a++; b++; }
  return (unsigned char)*a - (unsigned char)*b;
}
static inline int btn_next_target(const char *current, const char *const *names, int n) {
  int best = -1;
  for (int i = 0; i < n; i++) {
    if (!names[i][0]) continue;
    if (current[0] && btn_name_cmp(names[i], current) <= 0) continue;
    if (best < 0 || btn_name_cmp(names[i], names[best]) < 0) best = i;
  }
  return best;
}

// The old defaults (hub builds 28-52): left press = previous page, right press = next page, right 1 s = next gauge,
// left 1 s = clear warning, both press = log marker. Used when the old table was never saved.
static inline void btn_defaults(BtnBinding b[BTN_INPUTS][BTN_LEVELS]) {
  for (int i = 0; i < BTN_INPUTS; i++) for (int l = 0; l < BTN_LEVELS; l++) { b[i][l].action = BA_NONE; b[i][l].param = 0; }
  b[0][0].action = BA_PREV_PAGE; b[1][0].action = BA_NEXT_PAGE;
  b[1][1].action = BA_NEXT_GAUGE; b[0][1].action = BA_CLEAR_WARNING;
  b[2][0].action = BA_LOG_MARK;
}
