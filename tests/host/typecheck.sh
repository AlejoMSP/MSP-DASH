#!/bin/bash
# Type-checks the sketch's own C++ against small stand-in versions of the
# Arduino / WiFi / AsyncWebServer / TWAI APIs (tests/host/stubs). It is NOT a
# real compile - the stand-ins only approximate the real libraries - but it
# catches typos, wrong argument counts, missing includes and misspelled
# struct fields in files that otherwise only get compiled on the board.
#
# Needs: g++, and ArduinoJson v7 source (set ARDUINOJSON to its src folder).
#   ARDUINOJSON=/path/to/ArduinoJson/src ./tests/host/typecheck.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SKETCH="$HERE/../../esp32s3-amoled-bsp-base"
AJ="${ARDUINOJSON:?set ARDUINOJSON to the ArduinoJson src folder}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
cp "$SKETCH"/*.ino "$SKETCH"/*.h "$SKETCH"/*.cpp "$TMP"/
rm -f "$TMP/lcd_bsp.h" "$TMP/FT3168.h"           # pull in the ESP-IDF display stack - not needed here
echo '#pragma once
void lcd_lvgl_Init(void);' > "$TMP/lcd_bsp.h"
echo '#pragma once
void Touch_Init(void);' > "$TMP/FT3168.h"
FLAGS="-std=c++17 -fsyntax-only -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-format-truncation -Wno-maybe-uninitialized -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -I$HERE/stubs -I$AJ -I$TMP"
fail=0
for f in esp32s3-amoled-bsp-base.ino web_monitor.cpp gauge_config.cpp sd_logging.cpp ecu_config.cpp simulator.cpp; do
  x=""; [[ $f == *.ino ]] && x="-x c++"
  if out=$(g++ $FLAGS $x "$TMP/$f" 2>&1); then echo "OK    $f"; else echo "FAIL  $f"; echo "$out" | grep -E "error" | head -10; fail=1; fi
done
exit $fail
