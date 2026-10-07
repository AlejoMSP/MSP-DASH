#!/bin/bash
# The browser/page tests use extract_page.js to assemble each page from web_monitor.cpp + web_theme.h. This proves that
# assembly is the same as what the REAL C++ compiler produces: it compiles web_monitor.cpp (against the stand-in
# libraries), makes it print every page, and compares byte for byte.
#   ARDUINOJSON=/path/to/ArduinoJson/src ./tests/host/check_pages_match_compiler.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SKETCH="$HERE/../../esp32s3-amoled-bsp-base"
AJ="${ARDUINOJSON:?set ARDUINOJSON to the ArduinoJson src folder}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
cd "$TMP"
g++ -std=c++17 -O0 -w -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -I"$HERE/stubs" -I"$AJ" -I"$SKETCH" "$HERE/dump_pages.cpp" -o dump_pages -Wl,--unresolved-symbols=ignore-all
./dump_pages
fail=0
for f in compiler_*.html; do
  name="${f#compiler_}"; name="${name%.html}"
  node "$HERE/extract_page.js" "$name" > "js_$name.html"
  if cmp -s "$f" "js_$name.html"; then echo "IDENTICAL  $name ($(wc -c < "$f") bytes)"; else echo "DIFFERENT  $name"; fail=1; fi
done
exit $fail
