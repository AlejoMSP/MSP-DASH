// The screen order for "Next gauge" and the old button defaults (esp32-input/btn_logic.h). Build: g++ -std=c++17 -I../../esp32-input -I../../esp32s3-amoled-bsp-base test_btn_logic.cpp
#include "btn_logic.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
int main() {
  printf("1. the old defaults (read once on hub 53 if never saved)\n");
  BtnBinding b[BTN_INPUTS][BTN_LEVELS]; btn_defaults(b);
  CHECK(b[0][0].action == BA_PREV_PAGE && b[1][0].action == BA_NEXT_PAGE && b[1][1].action == BA_NEXT_GAUGE && b[2][0].action == BA_LOG_MARK, "left press previous, right press next, right 1 s next gauge, both = log marker");
  printf("2. screen order\n");
  const char *names[] = { "Right", "Center", "Left" };
  CHECK(btn_next_target("", names, 3) == 2 && btn_next_target("Left", names, 3) == 1 && btn_next_target("Center", names, 3) == 0 && btn_next_target("Right", names, 3) == -1, "All, Left, Center, Right, All");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
