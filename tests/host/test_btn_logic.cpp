// The hub's button events and bindings (esp32-input/btn_logic.h). Build: g++ -std=c++17 -I../../esp32-input -I../../esp32s3-amoled-bsp-base test_btn_logic.cpp
#include "btn_logic.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static BtnState S; static uint32_t T = 1000;
static std::string evs;
static void run(bool l, bool r, uint32_t ms) { for (uint32_t t = 0; t < ms; t += 5) { T += 5; BtnEvent e = btn_step(&S, T, l, r);
  if (e.type) { char b[24]; snprintf(b, sizeof b, "%s%d.%d ", e.type == BTN_EV_LEVEL ? "L" : "R", e.input, e.level); evs += b; } } }
static std::string press(bool l, bool r, uint32_t ms) { evs.clear(); run(l, r, ms); run(false, false, 100); return evs; }
int main() {
  btn_init(&S); run(false, false, 100);
  printf("1. presses and holds\n");
  CHECK(press(true, false, 200) == "R0.0 ", "left tap: release, left, level 0");
  CHECK(press(false, true, 200) == "R1.0 ", "right tap: release, right, level 0");
  CHECK(press(false, true, 1100) == "L1.1 R1.1 ", "right held 1.1 s: a level-1 hint, then release at level 1");
  CHECK(press(true, false, 3200) == "L0.1 L0.2 L0.3 R0.3 ", "left 3.2 s: hints at 1, 2, 3 s, release at level 3");
  CHECK(press(true, false, 10500) == "L0.1 L0.2 L0.3 L0.4 L0.5 R0.5 ", "left 10.5 s: through 5 and 10 s");
  CHECK(press(true, false, 990) == "R0.0 ", "0.99 s is still a press");
  printf("2. both buttons\n");
  CHECK(press(true, true, 300) == "R2.0 ", "both tapped: input both");
  { evs.clear(); run(true, false, 600); run(true, true, 2100); run(false, true, 50); run(false, false, 100);
    CHECK(evs == "L2.1 L2.2 R2.2 ", "left, then right joins: both, timed from the second press"); }
  { evs.clear(); run(true, false, 1500); run(true, true, 200); run(false, false, 100);
    CHECK(evs == "L0.1 R2.0 ", "left held 1.5 s then right joins briefly: both, short (the left hint came first)"); }
  printf("3. bounce\n");
  { evs.clear(); for (int i = 0; i < 6; i++) { T += 3; BtnEvent e = btn_step(&S, T, i % 2 == 0, false); if (e.type) evs += "x"; } run(false, false, 100);
    CHECK(evs.empty(), "chatter under 30 ms is not a press"); }
  printf("4. picking the binding\n");
  BtnBinding b[BTN_INPUTS][BTN_LEVELS]; btn_defaults(b);
  CHECK(btn_pick(b, 0, 0)->action == BA_PREV_PAGE && btn_pick(b, 1, 0)->action == BA_NEXT_PAGE, "defaults: left = previous page, right = next page");
  CHECK(btn_pick(b, 1, 1)->action == BA_NEXT_GAUGE && btn_pick(b, 0, 1)->action == BA_CLEAR_WARNING && btn_pick(b, 2, 0)->action == BA_LOG_MARK, "defaults: right 1 s next gauge, left 1 s clear warning, both = log marker");
  CHECK(btn_pick(b, 1, 3)->action == BA_NEXT_GAUGE, "right held 3 s with nothing at 2 or 3 s: the 1 s action (longest set one passed)");
  b[1][4].action = BA_RACE; b[1][4].param = 2;
  CHECK(btn_pick(b, 1, 5)->action == BA_RACE && btn_pick(b, 1, 5)->param == 2, "held 10 s with race mode at 5 s: race mode, page 2");
  for (int l = 0; l < BTN_LEVELS; l++) b[2][l].action = BA_NONE;
  CHECK(btn_pick(b, 2, 3) == 0, "nothing set for both: nothing happens");
  CHECK(btn_pick(b, 5, 0) == 0 && btn_pick(b, -1, 0) == 0, "bad input: nothing, no crash");
  printf("5. screen order\n");
  const char *names[] = { "Right", "Center", "Left" };
  CHECK(btn_next_target("", names, 3) == 2 && btn_next_target("Left", names, 3) == 1 && btn_next_target("Center", names, 3) == 0 && btn_next_target("Right", names, 3) == -1, "All, Left, Center, Right, All");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
