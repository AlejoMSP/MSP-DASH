// The hub's ECU detection (esp32-input/web_monitor.h): which ECU is talking, from the CAN IDs heard in the last 2 s.
// Build: g++ -std=c++17 -Istubs -I../../esp32-input test_hub_ecu.cpp
#include "Arduino.h"
#include <stdio.h>
#include <string.h>
uint32_t g_fake_millis = 1000; uint32_t millis() { return g_fake_millis; }
#include "web_monitor.h"
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static void hear(uint32_t id) { uint8_t d[8] = {0}; web_monitor_track_can_frame(id, d, 8); }
int main() {
  CHECK(!strcmp(hub_ecu_detected(), "none heard"), "nothing on the bus: none heard");
  hear(0x7E8); CHECK(!strcmp(hub_ecu_detected(), "none heard"), "a frame outside both ranges is not an ECU");
  hear(0x201); CHECK(!strcmp(hub_ecu_detected(), "rusEFI"), "0x201: rusEFI");
  hear(0x5F2); CHECK(!strcmp(hub_ecu_detected(), "rusEFI and MegaSquirt / Link"), "both ranges at once: both named");
  g_fake_millis += 2500; hear(0x5F3);
  CHECK(!strcmp(hub_ecu_detected(), "MegaSquirt / Link"), "rusEFI quiet for 2.5 s drops off");
  hear(0x20F); hear(0x210);
  CHECK(!strcmp(hub_ecu_detected(), "rusEFI and MegaSquirt / Link"), "0x20F is the last rusEFI ID");
  g_fake_millis += 2500; hear(0x210); hear(0x640);
  CHECK(!strcmp(hub_ecu_detected(), "none heard"), "0x210 and 0x640 are just outside the ranges");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
