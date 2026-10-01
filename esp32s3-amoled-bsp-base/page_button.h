#ifndef PAGE_BUTTON_H
#define PAGE_BUTTON_H

/*
  page_button.h
  BOOT button page-cycling - extracted from the main .ino, same
  reorganization/safety rationale as can_parsing.h (included only by
  the .ino, so full function bodies here are safe, no linker conflict).
*/

#include "gauge_app.h"

#define BOOT_BTN_PIN 0

static int currentPage = 0;
static bool lastBtnState = HIGH;
static unsigned long lastBtnChange = 0;
static const unsigned long DEBOUNCE_MS = 200;

static void checkPageButton() {
  bool btnState = digitalRead(BOOT_BTN_PIN);
  unsigned long now = millis();
  if (btnState == LOW && lastBtnState == HIGH && (now - lastBtnChange) > DEBOUNCE_MS) {
    lastBtnChange = now;
    currentPage = (currentPage + 1) % gauge_app_get_num_pages();
    g_requested_page = currentPage; // gauge_app.c's timer picks this up safely inside LVGL's task
    Serial.printf("Requested page %d\n", currentPage + 1);
  }
  lastBtnState = btnState;
}

#endif
