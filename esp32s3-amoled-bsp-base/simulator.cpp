/*
  simulator.cpp - see simulator.h for the safety rules.
*/

#include "simulator.h"
#include "gauge_app.h"
#include "gauge_config.h"
#include <Arduino.h>
#include <math.h>
#include <cmath>
#include <string.h>

static volatile bool s_active = false;
static volatile bool s_realFrame = false;   // set from the CAN task, handled in sim_loop()
static bool s_manual = false;
static uint32_t s_startMs = 0;
static uint32_t s_deadlineMs = 0;
static uint32_t s_lastTickMs = 0;
static float s_values[SIG_COUNT];
static char s_stopReason[64] = "";

bool sim_is_active(void) { return s_active; }
bool sim_is_manual(void) { return s_manual; }
const char *sim_last_stop_reason(void) { return s_stopReason; }
float sim_value(int sig) { return (sig > 0 && sig < SIG_COUNT) ? s_values[sig] : 0.0f; }

SimRange sim_range(int sig) {
  switch (sig) {
    case SIG_RPM:   return {0, 9000, 50};
    case SIG_CLT:   return {50, 260, 1};
    case SIG_MAP:   return {0, 250, 1};
    case SIG_AFR:   return {8, 22, 0.1f};
    case SIG_OILP:  return {0, 150, 1};
    case SIG_OILT:  return {50, 300, 1};
    case SIG_FUELP: return {0, 150, 1};
    case SIG_FLEX:  return {0, 100, 1};
    case SIG_TPS:   return {0, 100, 1};
    case SIG_BATTV: return {8, 16, 0.1f};
    default:        return gauge_signal_is_flag(sig) ? SimRange{0, 1, 1} : SimRange{0, 0, 1};   // an on/off flag: 0 or 1
  }
}

static float smooth01(float x) {          // eases in and out, so the rev cycle has no sharp corners
  if (x < 0) x = 0;
  if (x > 1) x = 1;
  return x * x * (3.0f - 2.0f * x);
}

// A 24-second rev cycle (idle, ramp up, hold, ramp down) with the temperatures warming up
// over a few minutes. Chosen to stay clear of the DEFAULT warning thresholds (coolant < 220,
// AFR < 15, oil and fuel pressure > 40), so the demo never trips an alert by itself - use
// manual mode to test alerts.
void sim_demo_values(float t, float out[SIG_COUNT]) {
  const float idle = 850.0f, top = 6200.0f;
  float ph = fmodf(t, 24.0f);
  float rpm;
  if (ph < 4.0f)       rpm = idle + 25.0f * sinf(ph * 3.0f);
  else if (ph < 12.0f) rpm = idle + (top - idle) * smooth01((ph - 4.0f) / 8.0f);
  else if (ph < 14.0f) rpm = top;
  else if (ph < 22.0f) rpm = top - (top - idle) * smooth01((ph - 14.0f) / 8.0f);
  else                 rpm = idle + 25.0f * sinf(ph * 3.0f);
  float load = (rpm - idle) / (top - idle);
  if (load < 0) load = 0;
  if (load > 1) load = 1;
  float warm = fminf(t / 120.0f, 1.0f);
  float warmOil = fminf(t / 180.0f, 1.0f);

  for (int i = 0; i < SIG_COUNT; i++) out[i] = 0;
  out[SIG_RPM]   = rpm;
  out[SIG_MAP]   = 35.0f + 155.0f * load + 3.0f * sinf(t * 5.0f);     // near vacuum at idle, boost at load
  out[SIG_TPS]   = 3.0f + 90.0f * load;
  out[SIG_AFR]   = 14.7f - 2.7f * load + 0.2f * sinf(t * 3.1f);
  out[SIG_OILP]  = 50.0f + 35.0f * load + 1.5f * sinf(t * 2.0f);
  out[SIG_CLT]   = 120.0f + 80.0f * warm + 2.0f * sinf(t * 0.7f);
  out[SIG_OILT]  = 150.0f + 55.0f * warmOil + 1.5f * sinf(t * 0.5f);
  out[SIG_FUELP] = 58.0f + 3.0f * sinf(t * 1.3f);
  out[SIG_FLEX]  = 30.0f + 1.0f * sinf(t * 0.2f);
  out[SIG_BATTV] = 13.8f + 0.15f * sinf(t * 1.7f);

  // On/off flags, so an indicators page has something to show. The check-engine light and lambda protection
  // never come on by themselves (use manual mode to test those).
  out[SIG_REVLIM]     = (rpm >= 6150.0f) ? 1.0f : 0.0f;                  // the top of the rev cycle
  out[SIG_MAINRELAY]  = 1.0f;
  out[SIG_FUELPUMP]   = 1.0f;
  out[SIG_CEL]        = 0.0f;
  out[SIG_EGOHEAT]    = (t < 20.0f) ? 1.0f : 0.0f;                       // the O2 heater, while the engine warms up
  out[SIG_LAMBDAPROT] = 0.0f;
  out[SIG_FAN]        = (out[SIG_CLT] >= 195.0f) ? 1.0f : 0.0f;
  out[SIG_FAN2]       = (out[SIG_CLT] >= 200.0f) ? 1.0f : 0.0f;
  out[SIG_BRAKE]      = (fmodf(t, 10.0f) >= 8.5f) ? 1.0f : 0.0f;         // a brake tap every 10 seconds
}

// Writes one tick of values exactly the way the CAN decoder would.
static void writeValues(const float *v) {
  g_rpm = v[SIG_RPM];         g_rpm_has_data = true;   gauge_config_mark_signal_active(SIG_RPM);
  g_clt_f = v[SIG_CLT];       g_clt_has_data = true;   gauge_config_mark_signal_active(SIG_CLT);
  g_boost_kpa = v[SIG_MAP];   g_boost_has_data = true; gauge_config_mark_signal_active(SIG_MAP);
  g_afr = v[SIG_AFR];         g_afr_has_data = true;   gauge_config_mark_signal_active(SIG_AFR);
  g_oilp_kpa = v[SIG_OILP];   g_oilp_has_data = true;  gauge_config_mark_signal_active(SIG_OILP);
  g_oilt_f = v[SIG_OILT];     g_oilt_has_data = true;  gauge_config_mark_signal_active(SIG_OILT);
  g_fuelp_kpa = v[SIG_FUELP]; g_fuelp_has_data = true; gauge_config_mark_signal_active(SIG_FUELP);
  g_flex_pct = v[SIG_FLEX];   g_flex_has_data = true;  gauge_config_mark_signal_active(SIG_FLEX);
  g_tps = v[SIG_TPS];         g_tps_has_data = true;   gauge_config_mark_signal_active(SIG_TPS);
  g_battv = v[SIG_BATTV];     g_battv_has_data = true; gauge_config_mark_signal_active(SIG_BATTV);

  uint16_t bits = 0;
  for (int i = 0; i < SIG_FLAG_COUNT; i++) {
    if (v[SIG_FIRST_FLAG + i] > 0.5f) bits |= (uint16_t)(1u << i);
    gauge_config_mark_signal_active((SignalId)(SIG_FIRST_FLAG + i));
  }
  g_flag_bits = bits;
  g_flag_seen = (uint16_t)((1u << SIG_FLAG_COUNT) - 1u);
}

void sim_start(bool manual) {
  uint32_t now = millis();
  s_manual = manual;
  s_startMs = now;
  s_deadlineMs = now + SIM_TIMEOUT_MS;
  s_lastTickMs = now - 1000;          // tick immediately
  s_realFrame = false;
  s_stopReason[0] = '\0';
  sim_demo_values(0.0f, s_values);    // manual mode starts from the idle values
  s_active = true;
  Serial.printf("Simulator ON (%s). Real CAN traffic, a reboot or 10 idle minutes will switch it off.\n", manual ? "manual" : "demo");
}

void sim_stop(const char *why) {
  if (!s_active) return;
  s_active = false;
  // Back to "never seen", so the gauges return to "--" instead of holding fake values.
  g_rpm_has_data = g_clt_has_data = g_boost_has_data = g_afr_has_data = g_oilp_has_data = false;
  g_oilt_has_data = g_fuelp_has_data = g_flex_has_data = g_tps_has_data = g_battv_has_data = false;
  g_flag_seen = 0;
  strncpy(s_stopReason, why ? why : "stopped", sizeof(s_stopReason) - 1);
  s_stopReason[sizeof(s_stopReason) - 1] = '\0';
  Serial.printf("Simulator OFF: %s\n", s_stopReason);
}

void sim_notify_real_frame(void) {
  if (s_active) s_realFrame = true;
}

bool sim_set_manual(int sig, float value) {
  if (!s_active || !s_manual || sig <= 0 || sig >= SIG_COUNT) return false;
  SimRange r = sim_range(sig);
  if (!std::isfinite(value)) return false;
  if (value < r.min) value = r.min;
  if (value > r.max) value = r.max;
  s_values[sig] = value;
  s_deadlineMs = millis() + SIM_TIMEOUT_MS;
  return true;
}

int sim_seconds_left(void) {
  if (!s_active) return 0;
  int32_t left = (int32_t)(s_deadlineMs - millis());
  return left > 0 ? (int)(left / 1000) : 0;
}

void sim_loop(void) {
  if (!s_active) return;
  uint32_t now = millis();
  if (s_realFrame) { sim_stop("real CAN traffic arrived"); return; }
  if ((int32_t)(now - s_deadlineMs) >= 0) { sim_stop("10 minutes passed"); return; }
  if (now - s_lastTickMs < 50) return;       // ~20 updates a second, like the real ECU's fastest frames
  s_lastTickMs = now;
  if (!s_manual) sim_demo_values((now - s_startMs) / 1000.0f, s_values);
  writeValues(s_values);
}
