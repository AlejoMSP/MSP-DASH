#ifndef GAUGE_CONFIG_JSON_H
#define GAUGE_CONFIG_JSON_H

/*
  gauge_config_json.h
  The ONE place that knows how a page/cell configuration is written to and
  read from JSON. The web endpoints (/gauges.json, /web-pages.json and their
  POSTs) and the flash files all use these two functions, so a new cell
  setting is added in gauge_config.cpp's serializeCell/deserializeCell and
  nowhere else. (C++ only - gauge_config.h itself must stay valid C.)
*/

#include <ArduinoJson.h>
#include "gauge_config.h"

// Fills `doc` with an array of the ENABLED pages, each with just the cells its
// layout uses.
void gauge_config_pages_to_json(const PageConfig *pages, JsonDocument &doc);

// Replaces ALL of `pages` (MAX_PAGES entries) from a JSON array: pages not in
// the array end up disabled and every cell not in the JSON is cleared. Values
// are sanitized (a zero divisor, a max not above min, a zero major-tick
// spacing and similar would crash or hang the display if they ever reached it).
void gauge_config_pages_from_json(JsonArray arr, PageConfig *pages);

#endif
