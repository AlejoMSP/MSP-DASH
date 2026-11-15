// Speed from rusEFI: BASE1 (0x201) byte 6 is VehicleSpeed in kph (docs/rusefi.dbc); the dash shows mph.
// Uses the real decoder (can_parsing.h). Build: g++ -std=c++17 -Istubs -I../../esp32-input -I../../esp32s3-amoled-bsp-base test_can_vss.cpp
#include "Arduino.h"
#include <stdio.h>
#include <math.h>
uint32_t g_fake_millis = 0; uint32_t millis() { return g_fake_millis; }
#include "gauge_config.h"
#include "gauge_app.h"
volatile float g_rpm, g_clt_f, g_boost_psi, g_afr, g_oilp_psi, g_oilt_f, g_fuelp_psi, g_flex_pct, g_tps, g_battv, g_egt_f, g_iat_f, g_ambt_f, g_hum_pct, g_fuel_pct, g_vss_mph, g_gps_mph, g_gps_sats;
volatile float g_imu_val[4]; volatile bool g_imu_has[4];
volatile bool g_rpm_has_data, g_clt_has_data, g_boost_has_data, g_afr_has_data, g_oilp_has_data, g_oilt_has_data,
              g_fuelp_has_data, g_flex_has_data, g_tps_has_data, g_battv_has_data, g_egt_has_data, g_iat_has_data, g_ambt_has_data, g_hum_has_data, g_fuel_has_data, g_vss_has_data, g_gps_has_data, g_sats_has_data;
volatile uint16_t g_flag_bits, g_flag_seen;
static int marked[SIG_COUNT];
extern "C" void gauge_config_mark_signal_active(SignalId s) { marked[s]++; }
#include "can_parsing.h"
// the rest of the CAN task is not used here
esp_err_t twai_receive(twai_message_t *, int) { return 1; }
esp_err_t twai_get_status_info(twai_status_info_t *) { return 1; }
esp_err_t twai_initiate_recovery() { return 0; }
esp_err_t twai_start() { return 0; }
int xTaskCreatePinnedToCore(void (*)(void *), const char *, unsigned int, void *, int, void **, int) { return 1; }
void sim_notify_real_frame() {}
extern "C" void web_monitor_track_can_frame(uint32_t, uint8_t *, uint8_t) {}
SerialClass Serial;
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
int main() {
  uint8_t d[8] = { 0xB8, 0x0B, 0, 0, 0, 0, 100, 10 };   // RPM 3000, speed 100 kph, ethanol 10 %
  handleRusefiFrame(0x201, d);
  CHECK(fabsf(g_vss_mph - 62.137f) < 0.01f && g_vss_has_data && marked[SIG_VSS] == 1, "100 kph from rusEFI = 62.1 mph on the dash");
  CHECK(g_rpm == 3000 && g_flex_pct == 10, "RPM and ethanol from the same frame still read");
  d[6] = 0; handleRusefiFrame(0x201, d);
  CHECK(g_vss_mph == 0 && marked[SIG_VSS] == 2, "standing still = 0 mph, still live");
  d[6] = 255; handleRusefiFrame(0x201, d);
  CHECK(fabsf(g_vss_mph - 158.45f) < 0.01f, "the top of the byte, 255 kph = 158.4 mph");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
