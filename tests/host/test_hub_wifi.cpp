// The hub's own Wi-Fi network settings (esp32-input/hub_wifi.h). Build: g++ -std=c++17 -I../../esp32-input test_hub_wifi.cpp
#include "hub_wifi.h"
#include <stdio.h>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
int main() {
  ApCfg c; ap_defaults(&c);
  CHECK(ap_valid(&c) && c.mode == AP_WHEN_AWAY && !strcmp(c.ssid, "MSP-DASH") && !strcmp(c.pass, "mspdash123"), "defaults: on when away, MSP-DASH, valid");
  CHECK(ap_should_start(&c, 0) && !ap_should_start(&c, 1), "when away: on without the home network, off with it");
  c.mode = AP_ALWAYS; CHECK(ap_should_start(&c, 0) && ap_should_start(&c, 1), "always: on both ways");
  c.mode = AP_OFF; CHECK(!ap_should_start(&c, 0) && !ap_should_start(&c, 1), "off: never");
  ApCfg b; ap_defaults(&b); strcpy(b.pass, "short"); CHECK(!ap_valid(&b), "a 5-character password: refused");
  ap_defaults(&b); b.ssid[0] = 0; CHECK(!ap_valid(&b), "no name: refused");
  ap_defaults(&b); b.mode = 7; CHECK(!ap_valid(&b), "an unknown mode: refused");
  CHECK(ap_text_ok("Miata Dash", 1, 32) && !ap_text_ok("tab\there", 1, 32), "plain characters only");
  CHECK(sizeof(ApCfg) == 100, "saved size: 100 bytes");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
