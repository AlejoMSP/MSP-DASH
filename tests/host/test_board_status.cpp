// Board status: the CPU busy estimate (cpu_load.h), the stats in the screens' hello, and that the hub's copy of
// cpu_load.h matches the screens'. Build: g++ -std=c++17 -I../../esp32s3-amoled-bsp-base test_board_status.cpp
#include "cpu_load.h"
#include "link_packets.h"
#include <stdio.h>
#include <stddef.h>
#include <fstream>
#include <sstream>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static std::string slurp(const char *p) { std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }
int main() {
  printf("1. CPU busy estimate\n");
  CHECK(cpu_load_from(1000, 1000, 1000) == 0, "idle every tick of the second: 0 %");
  CHECK(cpu_load_from(250, 1000, 1000) == 75, "idle a quarter of the ticks: 75 %");
  CHECK(cpu_load_from(0, 1000, 1000) == 100, "never idle: 100 %");
  CHECK(cpu_load_from(1800, 1000, 1000) == 0, "extra wake-ups from other interrupts: still 0 %, never negative");
  CHECK(cpu_load_from(600, 2000, 1000) == 70, "over 2 s");
  CHECK(cpu_load_from(10, 0, 1000) == 0 && cpu_load_from(10, 1000, 0) == 0, "no time or no tick rate: 0, no divide by zero");
  printf("2. the hello\n");
  CHECK(sizeof(HelloStats) == 21, "stats: 21 bytes");
  CHECK(offsetof(HelloPacket, uptime_s) == 4 + 4 + 2 + LINK_NAME_LEN && offsetof(HelloPacket, st) == offsetof(HelloPacket, uptime_s) + 4, "stats come after everything older screens send");
  CHECK(sizeof(HelloPacket) <= 250, "fits one ESP-NOW packet");
  { HelloPacket h; memset(&h, 0, sizeof h); size_t oldLen = offsetof(HelloPacket, st); HelloPacket r; memset(&r, 0, sizeof r); memcpy(&r, &h, oldLen);
    CHECK(r.st.stats_ver == 0, "an older screen's hello: no stats (stats_ver 0)"); }
  printf("3. one cpu_load.h\n");
  std::string a = slurp("../../esp32s3-amoled-bsp-base/cpu_load.h"), b = slurp("../../esp32-input/cpu_load.h");
  CHECK(a.size() > 100 && a == b, "the hub's copy matches the screens'");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
