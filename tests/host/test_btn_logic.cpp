// The hub's two-button gestures (esp32-input/btn_logic.h). Build: g++ -std=c++17 -I../../esp32-input test_btn_logic.cpp
#include "btn_logic.h"
#include <stdio.h>
#include <string.h>
#include <vector>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static BtnState S; static uint32_t T = 1000;
static std::vector<int> run(bool n, bool p, uint32_t ms) { std::vector<int> a; for (uint32_t t = 0; t < ms; t += 5) { T += 5; int x = btn_step(&S, T, n, p); if (x) a.push_back(x); } return a; }
static std::vector<int> press(bool n, bool p, uint32_t holdMs) { auto a = run(n, p, holdMs); auto b = run(false, false, 100); a.insert(a.end(), b.begin(), b.end()); return a; }
static bool is(const std::vector<int> &v, std::vector<int> want) { return v == want; }
int main() {
  btn_init(&S); run(false, false, 100);
  printf("1. short presses\n");
  CHECK(is(press(true, false, 150), {BTN_NEXT_SHORT}), "short Next = next page, once, on release");
  CHECK(is(press(false, true, 150), {BTN_PREV_SHORT}), "short Prev = previous page");
  CHECK(is(run(true, false, 100), {}), "nothing while still held (acts on release)");
  run(false, false, 100);
  printf("2. long presses\n");
  CHECK(is(press(true, false, 1200), {BTN_NEXT_LONG}), "hold Next: choose screen, once, and no page flip on release");
  CHECK(is(press(false, true, 1200), {BTN_PREV_LONG}), "hold Prev: clear warning, once");
  CHECK(is(press(true, false, 650), {BTN_NEXT_SHORT}), "650 ms is still a short press");
  printf("3. both together\n");
  CHECK(is(press(true, true, 300), {BTN_BOTH}), "both: log marker, once, no page flips");
  CHECK(is(press(true, true, 2000), {BTN_BOTH}), "both held long: still just one marker, no long actions");
  { auto a = run(true, false, 100); auto b = run(true, true, 200); auto c = run(false, true, 100); auto d = run(false, false, 100);
    a.insert(a.end(), b.begin(), b.end()); a.insert(a.end(), c.begin(), c.end()); a.insert(a.end(), d.begin(), d.end());
    CHECK(is(a, {BTN_BOTH}), "Next then Prev joins in, released one at a time: one marker, nothing else"); }
  printf("4. bounce\n");
  { std::vector<int> a; for (int i = 0; i < 6; i++) { T += 3; int x = btn_step(&S, T, i % 2 == 0, false); if (x) a.push_back(x); }
    auto b = run(false, false, 100); a.insert(a.end(), b.begin(), b.end());
    CHECK(a.empty(), "contact chatter under 30 ms is not a press"); }
  CHECK(is(press(true, false, 40), {BTN_NEXT_SHORT}), "a quick 40 ms tap still counts");
  printf("5. screen order\n");
  const char *names[] = { "Right", "Center", "Left" };
  int i = btn_next_target("", names, 3); CHECK(i == 2, "All -> Left");
  i = btn_next_target("Left", names, 3); CHECK(i == 1, "Left -> Center");
  i = btn_next_target("Center", names, 3); CHECK(i == 0, "Center -> Right");
  i = btn_next_target("Right", names, 3); CHECK(i == -1, "Right -> All");
  const char *two[] = { "Gauge B", "", "gauge a" };
  CHECK(btn_next_target("", two, 3) == 2 && btn_next_target("gauge a", two, 3) == 0 && btn_next_target("Gauge B", two, 3) == -1, "other names: alphabetical, empty skipped");
  CHECK(btn_next_target("Left", names, 0) == -1, "no screens online: All");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
