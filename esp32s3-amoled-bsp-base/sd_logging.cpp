/*
  sd_logging.cpp
  Continuous CSV logging to SD, session-numbered file per boot - same
  overall pattern as the WROOM board's initSdLogging()/updateSdLogging(),
  adapted to use POSIX file I/O (fopen/fprintf/fclose) instead of
  Arduino's SD.h/File class, matching this board's ESP-IDF-native VFS
  mount approach (sd_storage.cpp).

  Always-on from boot to power-off - no start/stop trigger, per project
  decision.

  Updated for the page/cell config redesign - iterates every enabled
  cell across every enabled page (instead of the old flat gauge-slot
  array) to build the CSV header and each logged row.
*/

#include "sd_logging.h"
#include "sd_storage.h"
#include "gauge_config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>

#define SD_MOUNT_POINT "/sd_card"

static Preferences s_prefs;
static char s_logPath[300];
static bool s_loggingReady = false;
static unsigned long s_lastLogMs = 0;
static const unsigned long LOG_INTERVAL_MS = 500;

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

  // Header row: millis + caption of every enabled cell on every
  // enabled page.
  PageConfig *pages = gauge_config_get_pages();
  fprintf(f, "millis");
  for (int p = 0; p < MAX_PAGES; p++) {
    if (!pages[p].enabled) continue;
    int numCells = gauge_config_cells_for_layout(pages[p].layout);
    for (int c = 0; c < numCells; c++) {
      if (!pages[p].cells[c].enabled) continue;
      fprintf(f, ",%s", pages[p].cells[c].caption);
    }
  }
  fprintf(f, "\n");
  fclose(f);

  s_loggingReady = true;
  Serial.printf("SD logging ready: %s\n", s_logPath);
}

void sd_logging_loop(void) {
  if (!s_loggingReady) return;
  unsigned long now = millis();
  if (now - s_lastLogMs < LOG_INTERVAL_MS) return;
  s_lastLogMs = now;

  FILE *f = fopen(s_logPath, "a");
  if (!f) return;

  PageConfig *pages = gauge_config_get_pages();
  fprintf(f, "%lu", now);
  for (int p = 0; p < MAX_PAGES; p++) {
    if (!pages[p].enabled) continue;
    int numCells = gauge_config_cells_for_layout(pages[p].layout);
    for (int c = 0; c < numCells; c++) {
      if (!pages[p].cells[c].enabled) continue;
      float value = gauge_config_signal_value(pages[p].cells[c].signal);
      fprintf(f, ",%.3f", value);
    }
  }
  fprintf(f, "\n");
  fclose(f);
}
