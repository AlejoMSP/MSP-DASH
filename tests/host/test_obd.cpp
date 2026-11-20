// OBD-II (Speeduino): the answers through the real decoder (can_parsing.h) and the hub's question order (obd_poll.h).
// Build: g++ -std=c++17 -Istubs -I../../esp32-input -I../../esp32s3-amoled-bsp-base test_obd.cpp
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
#include "obd_poll.h"
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static void ans(uint8_t pid, uint8_t a, uint8_t b = 0, uint8_t c = 0, uint8_t d = 0, uint32_t id = 0x7E8) {
  uint8_t f[8] = { 0, 0x41, pid, a, b, c, d, 0x55 };
  f[0] = (pid == 0x0C || pid == 0x42 || pid == 0x34 || pid == 0x24) ? 4 : (pid % 32 == 0 ? 6 : 3);
  handleObdFrame(id, f, 8);
}
static bool near(float a, float b, float t) { return fabsf(a - b) <= t; }
static bool supAll(uint8_t) { return true; }
static bool noLambda(uint8_t p) { return p != 0x34 && p != 0x24 && p != 0x5C; }
int main() {
  printf("1. answers into the dash's values\n");
  ans(0x0C, 0x2E, 0xE0); CHECK(near(g_rpm, 3000, 0.1) && g_rpm_has_data, "RPM: (256A+B)/4 = 3000");
  ans(0x05, 130); CHECK(near(g_clt_f, 194, 0.1), "coolant 90 C = 194 F");
  ans(0x0B, 201); CHECK(near(g_boost_psi, 14.45f, 0.1), "MAP 201 kPa = about 14.5 psi boost");
  ans(0x0B, 60); CHECK(g_boost_psi < -5, "MAP 60 kPa = vacuum");
  ans(0x0F, 70); CHECK(near(g_iat_f, 86, 0.1), "intake air 30 C");
  ans(0x11, 255); CHECK(near(g_tps, 100, 0.1), "TPS 255 = 100 %");
  ans(0x0D, 100); CHECK(near(g_vss_mph, 62.1f, 0.1), "speed 100 km/h = 62 mph");
  ans(0x42, 0x36, 0xB0); CHECK(near(g_battv, 14.0f, 0.01), "battery 14000 mV");
  ans(0x34, 0x80, 0x00); CHECK(near(g_afr, 14.7f, 0.01), "lambda 1.00 = 14.7 AFR");
  ans(0x5C, 140); CHECK(near(g_oilt_f, 212, 0.1), "oil 100 C");
  ans(0x0A, 100); CHECK(near(g_fuelp_psi, 43.5f, 0.1), "fuel pressure 300 kPa = 43.5 psi");
  ans(0x52, 217); CHECK(near(g_flex_pct, 85.1f, 0.1), "ethanol 85 %");
  { float r = g_rpm; uint8_t f[8] = { 4, 0x41, 0x0C, 0, 0, 0, 0, 0 }; handleObdFrame(0x7E0, f, 8); CHECK(g_rpm == r, "a frame on 0x7E0 (a question): ignored"); }
  { float r = g_rpm; uint8_t f[8] = { 4, 0x7F, 0x01, 0x12, 0, 0, 0, 0 }; handleObdFrame(0x7E8, f, 8); CHECK(g_rpm == r, "a refusal (0x7F): ignored"); }
  CHECK(marked[SIG_RPM] > 0 && marked[SIG_AFR] > 0, "signals marked active");
  printf("2. which PIDs it has\n");
  CHECK(obd_pid_supported(0x0C), "list not known yet: assume yes");
  ans(0x00, 0xBE, 0x1F, 0xA8, 0x13);   // the example from the Speeduino page: 01 03-07 0C-11 13 15 1C 1F 20
  CHECK(obd_pid_supported(0x0C) && obd_pid_supported(0x05) && obd_pid_supported(0x11) && obd_pid_supported(0x0D) && obd_pid_supported(0x20), "has 05, 0C, 0D, 11, and the next list");
  CHECK(!obd_pid_supported(0x0A) && !obd_pid_supported(0x02), "does not have 02 or 0A");
  CHECK(obd_pid_supported(0x42), "0x41-0x60 list not answered: assume yes");
  printf("3. the hub's questions\n");
  ObdPoll p; obd_init(&p); uint32_t t = 1000, a = 0;
  CHECK(obd_should_poll(OBD_AUTO, false) && !obd_should_poll(OBD_AUTO, true) && obd_should_poll(OBD_ALWAYS, true) && !obd_should_poll(OBD_OFF, false), "auto: only with no broadcasting ECU; always; off");
  CHECK(obd_next(&p, t, false, a, supAll) == 0x00, "nobody answering: asks which PIDs it has");
  obd_sent(&p, t, a);
  CHECK(obd_next(&p, t + 500, false, a, supAll) == -1 && obd_next(&p, t + 1000, false, a, supAll) == 0x00, "then once a second");
  obd_sent(&p, t + 1000, a);
  t += 1010; a++;   // it answered
  int seq[12]; for (int i = 0; i < 12; i++) { seq[i] = obd_next(&p, t, true, a, supAll); obd_sent(&p, t, a); a++; t += 5; }
  CHECK(seq[0] == 0x00 && seq[1] == 0x20 && seq[2] == 0x40, "answering: the supported lists first");
  CHECK(seq[3] == 0x0C && seq[4] == 0x0B && seq[5] == 0x11 && seq[6] == 0x0D && seq[7] == 0x05 && seq[8] == 0x0C, "then RPM, MAP, TPS, a mid one, a slow one, RPM again");
  obd_sent(&p, t, a); a++; CHECK(obd_next(&p, t + 1, true, a, supAll) == -1, "at most one question every 4 ms, even when answered");
  t += 10; obd_sent(&p, t, a); CHECK(obd_next(&p, t + 30, true, a, supAll) == -1 && obd_next(&p, t + 61, true, a, supAll) > 0, "no answer: waits 60 ms, then asks the next");
  ObdPoll q; obd_init(&q); uint32_t tq = 5000, aq = 1; int asked[40]; int n = 0;
  for (int i = 0; i < 40; i++) { int pid = obd_next(&q, tq, true, aq, noLambda); if (pid >= 0) { asked[n++] = pid; obd_sent(&q, tq, aq); aq++; } tq += 5; }
  bool none = true; for (int i = 0; i < n; i++) if (asked[i] == 0x34 || asked[i] == 0x24 || asked[i] == 0x5C) none = false;
  CHECK(n > 30 && none, "PIDs it does not have are never asked");
  { uint8_t r[8]; obd_request(0x0C, r); CHECK(r[0] == 2 && r[1] == 1 && r[2] == 0x0C && r[7] == 0x55, "request: 02 01 0C 55..."); }
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
