/*
  sd_logging.cpp
  CSV logging to the SD card, with TunerStudio-style start conditions (sd_logging.h):
  key on, engine running, moving, or a chosen signal above / below a level.

  - Logging starts when the condition becomes true and stops once it has been false for the stop delay,
    so a short stop does not split the log.
  - Every start opens a NEW file: log_1.csv, log_2.csv, ... (the number is kept in NVS). No empty files
    are left behind: a file is only made when logging actually starts.
  - Rows every 500 ms, with the signals ticked on the Log Channels page. A signal with no live data leaves
    its field empty (a 0.000 or a frozen value would look like a real reading).
  - Never logs while the simulator runs (this board's or the hub's): the current file is closed at once.
  - Uses POSIX file I/O on the ESP-IDF VFS mount (sd_storage.cpp).
*/

#include "sd_logging.h"
#include "sd_storage.h"
#include "gauge_config.h"
#include "simulator.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#ifndef SD_MOUNT_POINT
#define SD_MOUNT_POINT "/sd_card" // the host tests point this at a temp folder
#endif

static Preferences s_prefs;
static bool s_prefsOpen = false;
static SdLogTrigger s_trig = { LOG_START_RUNNING, SIG_TPS, true, 90.0f, 10 };
static char s_logPath[300] = "";
static bool s_logging = false;              // a file is open for this session
static uint32_t s_lastTrueMs = 0;           // when the start condition was last true
static uint32_t s_lastRowMs = 0;
static const uint32_t LOG_INTERVAL_MS = 500;
static volatile bool s_isLoggingActive = false;
static char s_status[96] = "Starting...";
static volatile int s_pendingMark = 0;      // marker to write in the next row (0 = none)

static bool validTrigger(const SdLogTrigger *t) {
  if (t->mode >= LOG_START_COUNT || t->stop_delay_s > LOG_STOP_DELAY_MAX) return false;
  if (t->mode == LOG_START_SIGNAL && (t->sig < 1 || t->sig >= SIG_COUNT || !isfinite(t->level))) return false;
  return true;
}

void sd_logging_init(void) {
  s_prefsOpen = s_prefs.begin("sdlog", false);
  if (s_prefsOpen) {
    SdLogTrigger t;
    t.mode = s_prefs.getUChar("tmode", LOG_START_RUNNING);
    t.sig = s_prefs.getUChar("tsig", SIG_TPS);
    t.above = s_prefs.getUChar("tabove", 1) != 0;
    t.level = s_prefs.getFloat("tlevel", 90.0f);
    t.stop_delay_s = s_prefs.getUShort("tstop", 10);
    if (validTrigger(&t)) s_trig = t;
  }
  if (!sd_storage_is_mounted()) {
    strcpy(s_status, "No SD card (not mounted) - nothing is logged");
    Serial.println("SD logging unavailable - card not mounted");
  } else {
    strcpy(s_status, "Waiting for the start condition");
    Serial.printf("SD logging ready: starts on mode %d\n", s_trig.mode);
  }
}

void sd_logging_get_trigger(SdLogTrigger *t) { *t = s_trig; }

bool sd_logging_set_trigger(const SdLogTrigger *t) {
  if (!validTrigger(t)) return false;
  s_trig = *t;
  if (s_prefsOpen) {
    s_prefs.putUChar("tmode", t->mode);
    s_prefs.putUChar("tsig", t->sig);
    s_prefs.putUChar("tabove", t->above ? 1 : 0);
    s_prefs.putFloat("tlevel", t->level);
    s_prefs.putUShort("tstop", t->stop_delay_s);
  }
  return true;
}

bool sd_logging_is_active(void) { return s_isLoggingActive; }
bool sd_logging_mark(int n) { if (!s_logging || !sd_storage_is_mounted() || n <= 0) return false; s_pendingMark = n; return true; }
const char *sd_logging_status_text(void) { return s_status; }

// Is the start condition true right now? A signal with no live data never counts.
static bool conditionNow(void) {
  switch (s_trig.mode) {
    case LOG_START_KEY_ON:  return gauge_config_signal_has_data(SIG_RPM);   // rusEFI sends RPM (0 too) whenever it is powered
    case LOG_START_RUNNING: return gauge_config_signal_has_data(SIG_RPM) && gauge_config_signal_value(SIG_RPM) > LOG_RUNNING_RPM;
    case LOG_START_MOVING:  return gauge_config_signal_has_data(SIG_VSS) && gauge_config_signal_value(SIG_VSS) > LOG_MOVING_MPH;
    case LOG_START_SIGNAL: {
      SignalId s = (SignalId)s_trig.sig;
      if (!gauge_config_signal_has_data(s)) return false;
      float v = gauge_config_signal_value(s);
      return s_trig.above ? v > s_trig.level : v < s_trig.level;
    }
  }
  return false;
}

static bool startSession(void) {
  int n = (s_prefsOpen ? s_prefs.getInt("session", 0) : 0) + 1;
  if (s_prefsOpen) s_prefs.putInt("session", n);
  snprintf(s_logPath, sizeof(s_logPath), "%s/log_%d.csv", SD_MOUNT_POINT, n);
  FILE *f = fopen(s_logPath, "w");
  if (!f) {
    snprintf(s_status, sizeof(s_status), "Could not create log_%d.csv on the SD card", n);
    Serial.println(s_status);
    return false;
  }
  fprintf(f, "millis");
  for (int sig = 1; sig < SIG_COUNT; sig++) {
    if (!gauge_config_is_log_enabled((SignalId)sig)) continue;
    fprintf(f, ",%s", gauge_config_signal_name((SignalId)sig));
  }
  fprintf(f, ",marker\n");   // hub: press both buttons to put a number here
  fclose(f);
  s_logging = true;
  s_lastRowMs = millis() - LOG_INTERVAL_MS;   // first row right away
  Serial.printf("SD logging started: %s\n", s_logPath);
  return true;
}

static void endSession(const char *why) {
  if (!s_logging) return;
  s_logging = false;
  Serial.printf("SD logging stopped (%s): %s\n", why, s_logPath);
}

static const char *fileName(void) { const char *p = strrchr(s_logPath, '/'); return p ? p + 1 : s_logPath; }

void sd_logging_loop(void) {
  if (!sd_storage_is_mounted()) { s_logging = false; s_isLoggingActive = false; return; }   // card gone: the session ends
  uint32_t now = millis();

  // The simulator feeds fake values: never write those to a log.
  if (sim_is_active()) {
    endSession("simulator running");
    s_isLoggingActive = false;
    strcpy(s_status, "Paused: the simulator is running");
    return;
  }

  bool cond = conditionNow();
  if (cond) s_lastTrueMs = now;
  if (cond && !s_logging && !startSession()) { s_isLoggingActive = false; return; }
  if (!cond && s_logging && now - s_lastTrueMs >= (uint32_t)s_trig.stop_delay_s * 1000u) endSession("start condition ended");

  s_isLoggingActive = s_logging;
  if (!s_logging) {
    strcpy(s_status, "Waiting for the start condition");
    return;
  }
  if (cond) snprintf(s_status, sizeof(s_status), "Logging to %s", fileName());
  else snprintf(s_status, sizeof(s_status), "Logging to %s, stops in %u s", fileName(),
                (unsigned)((s_trig.stop_delay_s * 1000u - (now - s_lastTrueMs) + 999u) / 1000u));

  if (now - s_lastRowMs < LOG_INTERVAL_MS) return;
  s_lastRowMs = now;
  FILE *f = fopen(s_logPath, "a");
  if (!f) return;
  fprintf(f, "%lu", (unsigned long)now);
  for (int sig = 1; sig < SIG_COUNT; sig++) {
    if (!gauge_config_is_log_enabled((SignalId)sig)) continue;
    if (gauge_config_signal_has_data((SignalId)sig)) fprintf(f, ",%.3f", gauge_config_signal_value((SignalId)sig));
    else fprintf(f, ",");
  }
  int mark = s_pendingMark; s_pendingMark = 0;
  if (mark) fprintf(f, ",%d\n", mark); else fprintf(f, ",\n");
  fclose(f);
}
