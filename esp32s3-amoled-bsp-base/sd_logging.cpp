/*
  sd_logging.cpp
  Continuous CSV logging to SD, session-numbered file per boot - same
  overall pattern as the WROOM board's initSdLogging()/updateSdLogging(),
  adapted to use POSIX file I/O (fopen/fprintf/fclose) instead of
  Arduino's SD.h/File class, matching this board's ESP-IDF-native VFS
  mount approach (sd_storage.cpp).

  Channel selection: logs whichever signals are enabled via the
  separate Log Channels config (gauge_config_is_log_enabled) -
  independent of what's actually displayed on any page/cell.

  RPM-gated: only writes rows once RPM exceeds 800 (engine running),
  rather than logging continuously from power-on. The file itself opens
  once per boot as before; rows are just skipped while RPM is at or
  below the threshold, rather than opening/closing many small files
  each time RPM crosses it.
*/

#include "sd_logging.h"
#include "sd_storage.h"
#include "gauge_config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>

#define SD_MOUNT_POINT "/sd_card"
#define LOGGING_RPM_THRESHOLD 800.0f

static Preferences s_prefs;
static char s_logPath[300];
static bool s_loggingReady = false;
static unsigned long s_lastLogMs = 0;
static const unsigned long LOG_INTERVAL_MS = 500;
static volatile bool s_isLoggingActive = false; // true only while rows are actually being written (RPM above threshold) - drives the on-screen blinking indicator

void sd_logging_init(void) {
  if (!sd_storage_is_mounted()) {
    Serial.println("SD logging unavailable - card not mounted");
    return;
  }

  s_prefs.begin("sdlog", false);
  int sessionNum = s_prefs.getInt("session", 0) + 1;
  s_prefs.putInt("session", sessionNum);
  snprintf(s_logPath, sizeof(s_logPath), "%s/log_%d.csv", SD_MOUNT_POINT, sessionNum);

  FILE *f = fopen(s_logPath, "w");
  if (!f) {
    Serial.println("SD logging: failed to create log file");
    return;
  }

  fprintf(f, "millis");
  for (int sig = 1; sig < SIG_COUNT; sig++) { // skip SIG_NONE
    if (!gauge_config_is_log_enabled((SignalId)sig)) continue;
    fprintf(f, ",%s", gauge_config_signal_name((SignalId)sig));
  }
  fprintf(f, "\n");
  fclose(f);

  s_loggingReady = true;
  Serial.printf("SD logging ready: %s (writes rows only once RPM > %.0f)\n", s_logPath, LOGGING_RPM_THRESHOLD);
}

bool sd_logging_is_active(void) {
  return s_isLoggingActive;
}

void sd_logging_loop(void) {
  if (!s_loggingReady) return;

  bool engineRunning = gauge_config_signal_has_data(SIG_RPM) && gauge_config_signal_value(SIG_RPM) > LOGGING_RPM_THRESHOLD;
  s_isLoggingActive = engineRunning;
  if (!engineRunning) return;

  unsigned long now = millis();
  if (now - s_lastLogMs < LOG_INTERVAL_MS) return;
  s_lastLogMs = now;

  FILE *f = fopen(s_logPath, "a");
  if (!f) return;

  fprintf(f, "%lu", now);
  for (int sig = 1; sig < SIG_COUNT; sig++) {
    if (!gauge_config_is_log_enabled((SignalId)sig)) continue;
    fprintf(f, ",%.3f", gauge_config_signal_value((SignalId)sig));
  }
  fprintf(f, "\n");
  fclose(f);
}
