// Custom sensor slots: the shared CAN field decoder (custom_slots.h), the MSN1 names packet, and the screen side
// (custom_slots_screen.cpp: names from the hub, kept in NVS, shown by gauge_config / logging).
// Build: g++ -std=c++17 -Istubs -I<ArduinoJson>/src -I../../esp32s3-amoled-bsp-base -DGAUGE_CFG_DIR='"/tmp/cfgtest"'
//        ../../esp32s3-amoled-bsp-base/custom_slots_screen.cpp ../../esp32s3-amoled-bsp-base/gauge_config.cpp test_custom_slots.cpp
#include "Arduino.h"
#include "Preferences.h"
#include "custom_slots.h"
#include "link_packets.h"
#include "gauge_config.h"
#include <stdio.h>
#include <math.h>
uint32_t g_fake_millis = 1000; uint32_t millis() { return g_fake_millis; }
void delay(uint32_t) {}
SerialClass Serial;
#include "FFat.h"
bool g_ffat_mounts = true;
FFatClass FFat;
void *heap_caps_malloc(size_t n, int) { return malloc(n); }
volatile float g_rpm, g_clt_f, g_boost_psi, g_afr, g_oilp_psi, g_oilt_f, g_fuelp_psi, g_flex_pct, g_tps, g_battv, g_egt_f, g_iat_f, g_ambt_f, g_hum_pct, g_fuel_pct, g_vss_mph, g_gps_mph, g_gps_sats;
volatile bool g_rpm_has_data, g_clt_has_data, g_boost_has_data, g_afr_has_data, g_oilp_has_data, g_oilt_has_data,
              g_fuelp_has_data, g_flex_has_data, g_tps_has_data, g_battv_has_data, g_egt_has_data, g_iat_has_data, g_ambt_has_data, g_hum_has_data, g_fuel_has_data, g_vss_has_data, g_gps_has_data, g_sats_has_data;
volatile uint16_t g_flag_bits, g_flag_seen;
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static CustomCanDef D(uint32_t id, uint8_t start, uint8_t bits, uint8_t be, uint8_t sgn, float scale, float offset, uint8_t bit = 0) {
  CustomCanDef c; memset(&c, 0, sizeof c); c.id = id; c.start = start; c.bits = bits; c.big_endian = be; c.is_signed = sgn; c.scale = scale; c.offset = offset; c.bit = bit; return c; }
static bool near(float a, float b) { return fabsf(a - b) < 0.001f; }

int main() {
  printf("1. decoding a CAN field\n");
  const uint8_t f[8] = { 0x10, 0x27, 0xFF, 0xFE, 0x80, 0x01, 0x02, 0x03 };
  float v;
  CustomCanDef c = D(0x208, 0, 16, 0, 0, 0.1f, 0);
  CHECK(custom_can_decode(&c, f, 8, &v) && near(v, 1000.0f), "16 bit little endian 0x2710 x 0.1 = 1000 (rusEFI style)");
  c = D(0x5F2, 0, 16, 1, 0, 1, 0);
  CHECK(custom_can_decode(&c, f, 8, &v) && v == 0x1027, "16 bit big endian = 0x1027 (MegaSquirt style)");
  c = D(0x208, 2, 16, 1, 1, 1, 0);
  CHECK(custom_can_decode(&c, f, 8, &v) && v == -2, "signed 16 bit big endian 0xFFFE = -2");
  c = D(0x208, 4, 8, 0, 1, 1, 0);
  CHECK(custom_can_decode(&c, f, 8, &v) && v == -128, "signed 8 bit 0x80 = -128");
  c = D(0x208, 4, 8, 0, 0, 1, -40);
  CHECK(custom_can_decode(&c, f, 8, &v) && v == 88, "8 bit 0x80 with offset -40 = 88 (a temperature)");
  c = D(0x208, 4, 32, 0, 0, 1, 0);
  CHECK(custom_can_decode(&c, f, 8, &v) && v == (float)0x03020180u, "32 bit little endian");
  c = D(0x208, 5, 1, 0, 0, 1, 0, 0);
  CHECK(custom_can_decode(&c, f, 8, &v) && v == 1, "one bit: byte 5 bit 0 = ON");
  c = D(0x208, 5, 1, 0, 0, 1, 0, 1);
  CHECK(custom_can_decode(&c, f, 8, &v) && v == 0, "one bit: byte 5 bit 1 = off");

  printf("2. bad definitions and short frames\n");
  v = 123;
  c = D(0x208, 6, 16, 0, 0, 1, 0);
  CHECK(custom_can_decode(&c, f, 8, &v) && !custom_can_decode(&c, f, 7, &v) && v == 0x0302, "a frame too short for the field is skipped, the value stays");
  c = D(0x208, 7, 16, 0, 0, 1, 0); CHECK(!custom_can_valid(&c), "16 bit starting at byte 7 runs off the end: refused");
  c = D(0x208, 5, 32, 0, 0, 1, 0); CHECK(!custom_can_valid(&c), "32 bit from byte 5: refused");
  c = D(0x800, 0, 8, 0, 0, 1, 0);  CHECK(!custom_can_valid(&c), "ID above 0x7FF: refused");
  c = D(0x208, 0, 12, 0, 0, 1, 0); CHECK(!custom_can_valid(&c), "12 bit: refused (8, 16, 32 or 1)");
  c = D(0x208, 0, 8, 0, 0, 0, 0);  CHECK(!custom_can_valid(&c), "scale 0: refused");
  c = D(0x208, 0, 8, 0, 0, NAN, 0); CHECK(!custom_can_valid(&c), "scale NaN: refused");
  c = D(0x208, 0, 1, 0, 0, 1, 0, 8); CHECK(!custom_can_valid(&c), "bit 8: refused");

  printf("3. packets fit ESP-NOW\n");
  CHECK(sizeof(SlotsPacket) <= 250 && sizeof(LinkPacket) <= 250, "MSN1 and MSD1 are 250 bytes or less");
  CHECK(SIG_COUNT == 44 && SIG_C16 == 43 && SIG_C1 == 28, "16 custom slots after the 27 fixed signals (44 in all)");
  CHECK(sizeof(((LinkPacket *)0)->has_mask) == 8, "the data packet's has-data mask is 64 bits");

  printf("4. the screen keeps the names from the hub\n");
  Preferences::wipeAll();
  custom_slots_screen_init();
  gauge_config_log_init();   // log channels default on
  CHECK(!strcmp(gauge_config_signal_name(SIG_C3), "Custom 3") && custom_slot_info(2)->name[0] == 0, "before the hub names it: Custom 3");
  CHECK(!gauge_config_is_log_enabled(SIG_C3), "an unused slot gets no column in the log");
  SlotsPacket p; memset(&p, 0, sizeof p); p.magic = SLOTS_MAGIC; p.first = 0; p.n = 4;
  strcpy(p.info[2].name, "Wastegate Duty"); strcpy(p.info[2].unit, "%"); p.info[2].decimals = 1; p.info[2].min = 0; p.info[2].max = 100;
  CHECK(custom_slots_on_packet((const uint8_t *)&p, sizeof p), "an MSN1 packet is taken");
  CHECK(!strcmp(gauge_config_signal_name(SIG_C3), "Custom 3"), "not used until loop() (no NVS writes in the radio callback)");
  custom_slots_loop();
  CHECK(!strcmp(gauge_config_signal_name(SIG_C3), "Wastegate Duty") && !strcmp(custom_slot_info(2)->unit, "%"), "after loop(): the name and unit are in use");
  CHECK(gauge_config_is_log_enabled(SIG_C3), "a named slot is logged");
  g_custom_val[2] = 42.5f; g_custom_has[2] = true; gauge_config_mark_signal_active(SIG_C3);
  CHECK(gauge_config_signal_has_data(SIG_C3) && gauge_config_signal_value(SIG_C3) == 42.5f, "its value reads like any signal");
  custom_slots_screen_init();
  CHECK(!strcmp(gauge_config_signal_name(SIG_C3), "Wastegate Duty"), "after a reboot the name is still there (NVS)");
  p.magic = 0x12345678; CHECK(!custom_slots_on_packet((const uint8_t *)&p, sizeof p), "another packet type is left for the others");
  p.magic = SLOTS_MAGIC; p.n = 4; CHECK(custom_slots_on_packet((const uint8_t *)&p, 20), "a packet cut short is swallowed, nothing applied");
  p.first = 14; p.n = 4; strcpy(p.info[0].name, "S15"); strcpy(p.info[1].name, "S16"); strcpy(p.info[2].name, "OVERFLOW");
  custom_slots_on_packet((const uint8_t *)&p, sizeof p); custom_slots_loop();
  CHECK(!strcmp(gauge_config_signal_name(SIG_C15), "S15") && !strcmp(gauge_config_signal_name(SIG_C16), "S16"), "the last packet fills slots 15 and 16 and nothing past them");
  memset(p.info[0].name, 'X', CUSTOM_NAME_LEN); p.first = 0; p.n = 1;
  custom_slots_on_packet((const uint8_t *)&p, sizeof p); custom_slots_loop();
  CHECK(strlen(gauge_config_signal_name(SIG_C1)) == CUSTOM_NAME_LEN - 1, "a name with no end is cut at 15 characters");

  printf("5. text copy\n");
  char b[8]; custom_copy_text(b, sizeof b, "a\tbcdefghij");
  CHECK(!strcmp(b, "a bcdef"), "control characters become spaces, cut to fit");

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
