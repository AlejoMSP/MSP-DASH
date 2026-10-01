#ifndef GAUGE_CONFIG_H
#define GAUGE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Signal pool - every value we currently know how to decode from
// either rusEFI or MegaSquirt/LinkECU. ----
typedef enum {
  SIG_NONE = 0,
  SIG_RPM,
  SIG_CLT,
  SIG_MAP,
  SIG_AFR,
  SIG_OILP,
  SIG_OILT,
  SIG_FUELP,
  SIG_FLEX,
  SIG_TPS,
  SIG_BATTV,
  SIG_COUNT
} SignalId;

// ---- Page layout templates - a fixed set of common arrangements
// rather than a fully arbitrary grid, per project decision. ----
typedef enum {
  LAYOUT_1CELL = 0, // one cell, full screen
  LAYOUT_2UP,       // two cells, stacked top/bottom
  LAYOUT_3UP,       // one cell on top (full width), two below (split)
  LAYOUT_2X2,       // four even cells
  LAYOUT_COUNT
} LayoutType;

// ---- How a cell displays its assigned signal. ----
typedef enum {
  GAUGE_TYPE_NEEDLE = 0,   // full needle meter (what every gauge was before this redesign)
  GAUGE_TYPE_NUMBER,       // plain big digital readout, no needle
  GAUGE_TYPE_HALFMOON,     // 180-degree meter (previously only available full-page)
  GAUGE_TYPE_COUNT
} GaugeType;

#define GAUGE_CAPTION_MAXLEN 24
#define MAX_CELLS_PER_PAGE 4   // matches the largest fixed layout (2x2)
#define MAX_PAGES 8

// Everything a single cell needs to render and behave - essentially
// the full per-gauge config from before this redesign, now scoped to
// one cell within a page's layout instead of an entire page.
typedef struct {
  bool enabled;            // false = this cell is empty (shows nothing)
  SignalId signal;
  GaugeType gauge_type;
  int32_t min_val;
  int32_t max_val;
  int32_t rest_val;
  char caption[GAUGE_CAPTION_MAXLEN];
  uint32_t needle_color;   // 0xRRGGBB
  uint32_t tick_color;     // 0xRRGGBB
  uint32_t bg_color;       // 0xRRGGBB - also the warning-flash "off" state
  float value_divisor;     // e.g. 1000 for "RPM x1000"
  int32_t tick_count;
  int32_t major_nth;
  int32_t refresh_hz;
  bool show_value;         // precise numeric readout alongside the needle (GAUGE_TYPE_NEEDLE/HALFMOON only - NUMBER type always shows a value by definition)
  int32_t value_font_size; // 14/28/36 - must match a font actually enabled in lv_conf.h
  bool has_warning;
  float warning_threshold;
  bool warning_above;
} CellConfig;

// A page: a chosen layout template plus however many cells that
// layout needs (unused cells beyond the layout's count are ignored).
typedef struct {
  bool enabled;    // false = this page is skipped in the page cycle
  LayoutType layout;
  CellConfig cells[MAX_CELLS_PER_PAGE];
} PageConfig;

// Loads the persisted page/cell configuration from NVS (falls back to
// sensible defaults matching the pre-redesign gauge set, one signal
// per page via LAYOUT_1CELL, if nothing has been saved yet) - call
// once from setup(), before build_gauge_app().
void gauge_config_init(void);

// Returns a pointer to the live in-memory page array (MAX_PAGES
// entries) - gauge_app.c reads this to build pages.
PageConfig *gauge_config_get_pages(void);

// How many cells a given layout actually uses (1/2/3/4).
int gauge_config_cells_for_layout(LayoutType layout);

// Saves the current in-memory page/cell array to NVS.
void gauge_config_save(void);

// ---- Page management (matches the add/delete/empty/copy workflow) ----
// Appends a new page (LAYOUT_1CELL, single empty cell) if room remains
// (MAX_PAGES not yet reached). Returns the new page's index, or -1 if
// full.
int gauge_config_add_page(void);

// Removes a page and shifts subsequent pages up to fill the gap.
void gauge_config_delete_page(int index);

// Clears a page's cells (all disabled) but keeps its chosen layout.
void gauge_config_empty_page(int index);

// Duplicates a page, appending the copy (if room remains). Returns the
// new page's index, or -1 if full.
int gauge_config_copy_page(int srcIndex);

// Human-readable signal name, for the web UI's sensor picker.
const char *gauge_config_signal_name(SignalId sig);

// Reads the live current value and has_data flag for a given signal.
float gauge_config_signal_value(SignalId sig);
bool gauge_config_signal_has_data(SignalId sig);

// ---- Signal activity tracking (for the sensor picker's "how live is
// this signal" sort/display, matching the ping-time-style column in
// the reference screenshots) - call from the CAN frame handlers
// whenever a signal's value actually gets updated. ----
void gauge_config_mark_signal_active(SignalId sig);

// Milliseconds since this signal was last updated (very large if never
// seen) - lower means "more active/live" for sorting purposes.
uint32_t gauge_config_signal_age_ms(SignalId sig);

#ifdef __cplusplus
}
#endif

#endif
