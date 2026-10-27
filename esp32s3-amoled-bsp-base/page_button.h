#ifndef PAGE_BUTTON_H
#define PAGE_BUTTON_H

/*
  page_button.h
  BOOT button page-cycling - extracted from the main .ino, same
  reorganization/safety rationale as can_parsing.h (included only by
  the .ino, so full function bodies here are safe, no linker conflict).

  The button asks for "next page" (g_page_nav_delta) rather than a
  specific page number. It used to keep its own page counter, which
  drifted out of sync with the display whenever the page was changed any
  other way (the web Prev/Next arrows) - the next press then jumped to
  the wrong page. gauge_app.c's timer advances from whatever page is
  actually showing, and also uses the same request to dismiss a
  full-screen alert.
*/

#include "serial_log.h"   // RT_LOG: runtime messages off unless SERIAL_STREAM
#include "gauge_app.h"

#define BOOT_BTN_PIN 0

static bool lastBtnState = HIGH;
static unsigned long lastBtnChange = 0;
static const unsigned long DEBOUNCE_MS = 200;

static void checkPageButton() {
  bool btnState = digitalRead(BOOT_BTN_PIN);
  unsigned long now = millis();
  if (btnState == LOW && lastBtnState == HIGH && (now - lastBtnChange) > DEBOUNCE_MS) {
    lastBtnChange = now;
    g_page_nav_delta = 1;
    RT_LOG("BOOT pressed: next page / dismiss alert\n");
  }
  lastBtnState = btnState;
}

#endif
