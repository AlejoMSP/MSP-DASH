// btn_logic.h (hub) - turns the two buttons into gestures. No Arduino calls: host-tested (tests/host/test_btn_logic.cpp).
//   short Next / short Prev : page next / previous (acted on RELEASE, so a long press or a both-press never also flips)
//   hold Next 0.7 s          : choose which screen the buttons control (All -> Left -> Center -> Right -> All)
//   hold Prev 0.7 s          : clear (snooze) the full-screen warning on every screen
//   press both               : drop a marker in the SD log
// Inputs are debounced levels (true = pressed). Call btn_step() every few ms; it returns at most one action.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define BTN_LONG_MS 700
#define BTN_DEBOUNCE_MS 30

enum BtnAction { BTN_NONE = 0, BTN_NEXT_SHORT, BTN_PREV_SHORT, BTN_NEXT_LONG, BTN_PREV_LONG, BTN_BOTH };

typedef struct {
  bool next, prev;            // debounced state
  bool rawNext, rawPrev;      // last raw reading
  uint32_t rawNextMs, rawPrevMs;
  uint32_t nextDownMs, prevDownMs;
  bool used;                  // this press already produced an action (long or both): nothing on release
  bool both;                  // both were down together during this press
} BtnState;

static inline void btn_init(BtnState *s) { s->next = s->prev = s->rawNext = s->rawPrev = false; s->rawNextMs = s->rawPrevMs = 0; s->used = s->both = false; s->nextDownMs = s->prevDownMs = 0; }

static inline int btn_step(BtnState *s, uint32_t now, bool rawNext, bool rawPrev) {
  // debounce: a level counts once it has held BTN_DEBOUNCE_MS
  if (rawNext != s->rawNext) { s->rawNext = rawNext; s->rawNextMs = now; }
  if (rawPrev != s->rawPrev) { s->rawPrev = rawPrev; s->rawPrevMs = now; }
  bool n = (now - s->rawNextMs >= BTN_DEBOUNCE_MS) ? s->rawNext : s->next;
  bool p = (now - s->rawPrevMs >= BTN_DEBOUNCE_MS) ? s->rawPrev : s->prev;
  int act = BTN_NONE;
  bool nDown = n && !s->next, pDown = p && !s->prev, nUp = !n && s->next, pUp = !p && s->prev;
  if (nDown) s->nextDownMs = now;
  if (pDown) s->prevDownMs = now;
  if (!s->next && !s->prev && (nDown || pDown)) { s->used = false; s->both = false; }   // a new press begins
  s->next = n; s->prev = p;
  if (n && p && !s->both) { s->both = true; if (!s->used) { s->used = true; act = BTN_BOTH; } }
  else if (n && !p && !s->used && !s->both && now - s->nextDownMs >= BTN_LONG_MS) { s->used = true; act = BTN_NEXT_LONG; }
  else if (p && !n && !s->used && !s->both && now - s->prevDownMs >= BTN_LONG_MS) { s->used = true; act = BTN_PREV_LONG; }
  else if (nUp && !p && !s->used && !s->both) act = BTN_NEXT_SHORT;
  else if (pUp && !n && !s->used && !s->both) act = BTN_PREV_SHORT;
  return act;
}

// The order the "choose screen" press steps through: All, then Left, Center, Right (by name, any case), then any
// other names alphabetically. names: the online screens. Returns the index into names to use after `current`
// (-1 = All); -1 again after the last one.
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
// Next in that order after `current` (a name, "" = All). Returns the index in names, or -1 for All.
static inline int btn_next_target(const char *current, const char *const *names, int n) {
  int best = -1;
  for (int i = 0; i < n; i++) {
    if (!names[i][0]) continue;
    if (current[0] && btn_name_cmp(names[i], current) <= 0) continue;   // not after the current one
    if (best < 0 || btn_name_cmp(names[i], names[best]) < 0) best = i;
  }
  return best;
}
