// btn_logic.h (hub) - turns the two buttons into (input, hold time) events, for the bindings on the /buttons page.
// No Arduino calls: host-tested (tests/host/test_btn_logic.cpp).
//   input: 0 = left alone, 1 = right alone, 2 = both (both were down together at some point in the press)
//   level: 0 = short press, 1..5 = held 1, 2, 3, 5, 10 s (BTN_LEVEL_MS in button_actions.h)
// Events: BTN_EV_LEVEL while held, each time a new level is reached (for the "Release: ..." hint);
//         BTN_EV_RELEASE when every button is up again, with the input and the longest level reached.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "button_actions.h"

#define BTN_DEBOUNCE_MS 30
enum { BTN_EV_NONE = 0, BTN_EV_LEVEL, BTN_EV_RELEASE };

typedef struct {
  bool left, right;           // debounced
  bool rawL, rawR; uint32_t rawLMs, rawRMs;
  bool active;                // a press is in progress
  bool both;
  int first;                  // 0 left / 1 right: the button that started the press
  uint32_t startMs;           // when the press began (for both: when the second button went down)
  int level;                  // longest level reached in this press
} BtnState;

typedef struct { int type, input, level; } BtnEvent;

static inline void btn_init(BtnState *s) { s->first = 0; s->left = s->right = s->rawL = s->rawR = false; s->rawLMs = s->rawRMs = 0; s->active = s->both = false; s->startMs = 0; s->level = 0; }

static inline BtnEvent btn_step(BtnState *s, uint32_t now, bool rawLeft, bool rawRight) {
  BtnEvent ev = { BTN_EV_NONE, 0, 0 };
  if (rawLeft != s->rawL) { s->rawL = rawLeft; s->rawLMs = now; }
  if (rawRight != s->rawR) { s->rawR = rawRight; s->rawRMs = now; }
  bool l = (now - s->rawLMs >= BTN_DEBOUNCE_MS) ? s->rawL : s->left;
  bool r = (now - s->rawRMs >= BTN_DEBOUNCE_MS) ? s->rawR : s->right;
  s->left = l; s->right = r;
  if (!s->active && (l || r)) { s->active = true; s->both = false; s->first = l ? 0 : 1; s->startMs = now; s->level = 0; }
  if (!s->active) return ev;
  if (l && r && !s->both) { s->both = true; s->startMs = now; s->level = 0; }   // both: time counts from the second press
  int input = s->both ? 2 : s->first;
  if (l || r) {
    int lv = s->level;
    while (lv + 1 < BTN_LEVELS && now - s->startMs >= BTN_LEVEL_MS[lv + 1]) lv++;
    if (lv > s->level) { s->level = lv; ev.type = BTN_EV_LEVEL; ev.input = input; ev.level = lv; }
    return ev;
  }
  // everything released
  s->active = false;
  ev.type = BTN_EV_RELEASE; ev.input = input; ev.level = s->level;
  return ev;
}

// The binding a release at `level` runs: the longest set binding at or below that level (holding a bit too long
// still does the last thing you passed). NULL when nothing is set up to that level.
static inline const BtnBinding *btn_pick(const BtnBinding bind[BTN_INPUTS][BTN_LEVELS], int input, int level) {
  if (input < 0 || input >= BTN_INPUTS) return 0;
  for (int lv = level < BTN_LEVELS ? level : BTN_LEVELS - 1; lv >= 0; lv--)
    if (bind[input][lv].action != BA_NONE) return &bind[input][lv];
  return 0;
}

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

// The defaults (same as hub build 28): left press = previous page, right press = next page, right 1 s = next gauge,
// left 1 s = clear warning, both press = log marker.
static inline void btn_defaults(BtnBinding b[BTN_INPUTS][BTN_LEVELS]) {
  for (int i = 0; i < BTN_INPUTS; i++) for (int l = 0; l < BTN_LEVELS; l++) { b[i][l].action = BA_NONE; b[i][l].param = 0; }
  b[0][0].action = BA_PREV_PAGE; b[1][0].action = BA_NEXT_PAGE;
  b[1][1].action = BA_NEXT_GAUGE; b[0][1].action = BA_CLEAR_WARNING;
  b[2][0].action = BA_LOG_MARK;
}
