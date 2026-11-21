// The screens joining the hub's own network: their address on it, the packet, and that every board agrees on the
// car channel. Build: g++ -std=c++17 -I../../esp32s3-amoled-bsp-base test_hubnet.cpp
#include "link_packets.h"
#include <stdio.h>
#include <fstream>
#include <sstream>
#include <regex>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static std::string slurp(const char *p) { std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }
static int defined(const std::string &src, const char *name) { std::smatch m; std::regex r(std::string("#define ") + name + "\\s+(\\d+)"); return std::regex_search(src, m, r) ? std::stoi(m[1]) : -1; }
int main() {
  CHECK(hubnet_host("192.168.55.201") == 201 && hubnet_host("192.168.55.203") == 203, "keeps the last number: .201 / .203");
  CHECK(hubnet_host("") == 0 && hubnet_host("garbage") == 0 && hubnet_host("10.0.0.") == 0, "no home address: 0 (DHCP from the hub)");
  CHECK(hubnet_host("192.168.55.1") == 0 && hubnet_host("192.168.55.255") == 0 && hubnet_host("192.168.55.300") == 0, ".1 (the hub), .255, out of range: 0");
  CHECK(sizeof(HubNetPacket) == 102 && sizeof(HubNetPacket) <= 250, "packet fits one ESP-NOW frame");
  int hub = defined(slurp("../../esp32-input/esp32-input.ino"), "CAR_CHANNEL"), scr = defined(slurp("../../esp32s3-amoled-bsp-base/espnow_link.h"), "ESPNOW_CAR_CHANNEL");
  CHECK(hub == HUBNET_CHANNEL && scr == HUBNET_CHANNEL, "hub, screens and the hub's network all on the same car channel");
  CHECK((SIG_COUNT & LINK_COUNT_FROM_HUB) == 0 && ((SIG_COUNT | LINK_COUNT_FROM_HUB) & LINK_COUNT_MASK) == SIG_COUNT, "the hub flag sits above any signal count and masks off cleanly");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
