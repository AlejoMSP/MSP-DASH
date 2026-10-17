#ifndef GAUGE_CONFIG_H
#define GAUGE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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
  // ---- ECU on/off status flags (value 1 = on, 0 = off). Decoded in can_flags.h, from the official
  // rusEFI DBC (docs/rusefi.dbc). The ORDER matters: it is the bit order of the BASE0 status byte.
  SIG_REVLIM,      // rev limiter active
  SIG_MAINRELAY,   // main relay on
  SIG_FUELPUMP,    // fuel pump on
  SIG_CEL,         // check engine light
  SIG_EGOHEAT,     // O2 sensor heater on
  SIG_LAMBDAPROT,  // lambda (AFR) protection active
  SIG_FAN,         // fan 1 on
  SIG_FAN2,        // fan 2 on
  SIG_BRAKE,       // brake pedal pressed (BASE11)
  // Analog signals added after the flags: appended so the numbers saved in existing configs keep their meaning.
  SIG_EGT,         // exhaust gas temp, F (placeholder CAN ID until the real one is known)
  SIG_IAT,         // intake air temp, F
  SIG_COUNT
} SignalId;

#define SIG_FIRST_FLAG SIG_REVLIM
#define SIG_FLAG_END (SIG_BRAKE + 1)
#define SIG_FLAG_COUNT (SIG_FLAG_END - SIG_FIRST_FLAG)
static inline bool gauge_signal_is_flag(int sig) { return sig >= SIG_FIRST_FLAG && sig < SIG_FLAG_END; }

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
  GAUGE_TYPE_HISTOGRAM,    // scrolling strip-chart/trend line (oscilloscope-style) of recent values
  GAUGE_TYPE_COUNT
} GaugeType;

#define GAUGE_CAPTION_MAXLEN 24
#define MAX_CELLS_PER_PAGE 4   // matches the largest fixed layout (2x2)
#define MAX_INDICATORS_PER_PAGE 12
#define IND_TEXT_MAXLEN 12     // 11 characters + the end marker
#define GAUGE_UNITS_MAXLEN 8   // units text on a gauge: 7 characters + the end marker
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
  int32_t value_font_size; // readout size: 14/28/36/48/64/80/96/120 (see gauge_style.h). Big readouts shrink to the biggest size that fits the cell.
  int32_t caption_font_size; // caption (and units) text: 14/20/28/36/48; 0 = never set, the display's default font. Big text shrinks to fit.
  bool caption_on_face;    // needle/half-moon only: the caption is printed ON the dial face, above the hub, instead of at the bottom of the cell
  char units[GAUGE_UNITS_MAXLEN]; // e.g. "F", "kPa": printed under the readout (needle dials with Show value, and number cells)
  int32_t decimals;        // digits after the point in the readout: 0, 1 or 2 (configs saved before this existed: 1)
  int32_t v_align;         // where the gauge stands in its cell: 0 centre, 1 top, 2 bottom (needle, half-moon and number cells)
  int32_t tick_font_size;  // numbers around a needle/half-moon dial: 14/28/36, -1 = hidden, 0 = follow value_font_size (configs saved before this existed)
  int32_t start_deg;       // needle/half-moon dial start, compass degrees (0 = straight up, clockwise). Standard gauge 225
  int32_t end_deg;         // dial end, clockwise from the start. Standard gauge 135 (a 270 degree sweep); half moon 270 -> 90
  // ---- needle/half-moon dials: warning zones (TunerStudio style) and needle length ----
  bool low_crit_on;        // red: the scale from its minimum up to low_crit
  int32_t low_crit;        // in the dial's own units, between min and max
  bool low_warn_on;        // yellow: from the minimum up to low_warn
  int32_t low_warn;
  bool high_warn_on;       // yellow: from high_warn up to the maximum
  int32_t high_warn;
  bool high_crit_on;       // red: from high_crit up to the maximum. Red covers yellow where they overlap.
  int32_t high_crit;
  int32_t zone_style;      // 1 = coloured bands behind the ticks, 2 = coloured tick marks, 3 = both (GAUGE_ZONE_BANDS / _TICKS)
  int32_t needle_hub_pct;  // gap between the dial's centre and where the needle starts, % of the dial radius (0-60)
  int32_t needle_tip_pct;  // how far short of its normal tip the needle stops, % of the dial radius (0-60; hub + tip <= 85)
  bool has_warning;
  float warning_threshold;
  bool warning_above;
  bool alert_fullscreen;   // when this cell's warning triggers (and the engine is running), take over the whole screen on ANY page. BOOT button or a tap dismisses it for 30s. Needs has_warning.
  // ---- GAUGE_TYPE_HISTOGRAM (trend graph) only ----
  int32_t history_sec;     // seconds of history the graph shows (2-600). The graph takes 100 samples across this window, independent of refresh_hz.
  bool ref_enabled;        // draw a flat reference line, e.g. 14.7 on an AFR graph
  float ref_value;         // where that line sits, in the same units as min/max
} CellConfig;

// What a page holds: gauge cells in one of the layouts, or a grid of on/off indicator lamps.
typedef enum { PAGE_GAUGES = 0, PAGE_INDICATORS = 1, PAGE_KIND_COUNT } PageKind;

// One indicator lamp (TunerStudio style): shows its on text / colours while its condition holds and its off
// text / colours otherwise. The condition is "signal is above (or below) a threshold"; an ECU on/off flag is
// 1 or 0, so for those the editor uses "above 0.5" (flag on) or "below 0.5" (flag off). See gauge_indicators.h.
typedef struct {
  SignalId signal;
  int32_t op;                       // GAUGE_IND_ABOVE (0) or GAUGE_IND_BELOW (1)
  float threshold;                  // in the signal's own units (rpm, degrees F, kPa, AFR, %, volts)
  float hysteresis;                 // >= 0: once on, stays on until the value is this far back past the threshold
  char on_text[IND_TEXT_MAXLEN];    // printable ASCII
  char off_text[IND_TEXT_MAXLEN];
  uint32_t on_color, off_color;     // lamp background, 0xRRGGBB
  uint32_t on_text_color, off_text_color;
  bool blink;                       // flash while on
} IndicatorConfig;

// A page: a chosen layout template plus however many cells that
// layout needs (unused cells beyond the layout's count are ignored) - or, for an indicators page, a grid of lamps.
typedef struct {
  bool enabled;    // false = this page is skipped in the page cycle
  LayoutType layout;
  CellConfig cells[MAX_CELLS_PER_PAGE];
  // ---- indicator pages (kind == PAGE_INDICATORS): layout and cells are unused ----
  PageKind kind;
  int32_t ind_cols;                 // lamps per row, 1-4
  int32_t ind_count;                // lamps in use, 0-12: indicators[0 .. ind_count-1]
  IndicatorConfig indicators[MAX_INDICATORS_PER_PAGE];
} PageConfig;

// Loads the persisted page/cell configuration from NVS (falls back to
// sensible defaults matching the pre-redesign gauge set, one signal
// per page via LAYOUT_1CELL, if nothing has been saved yet) - call
// once from setup(), before build_gauge_app().
void gauge_config_init(void);

// Returns a pointer to the live in-memory page array (MAX_PAGES
// entries) - gauge_app.c reads this to build pages.
PageConfig *gauge_config_get_pages(void);

// ---- A fully SEPARATE page/cell configuration, specifically for the
// web browser dashboard - independent of the physical display's pages
// above. Same CellConfig/PageConfig/LayoutType/GaugeType types (the
// data model is generic, not display-specific), just a different
// in-memory array and NVS key, with its own defaults and load/save.
// This lets you configure what shows on your phone's browser
// completely differently from what shows on the physical gauge
// cluster. ----
void gauge_config_web_init(void);
PageConfig *gauge_config_get_web_pages(void);
bool gauge_config_web_save(void);

// How many cells a given layout actually uses (1/2/3/4).
int gauge_config_cells_for_layout(LayoutType layout);

// Saves the current in-memory page/cell array to flash. Returns true only
// if the data was really written; on failure gauge_config_last_error()
// says why. Note: page add/delete/empty/copy for BOTH configs is handled
// entirely client-side in the /gauges and /web-config editors (the whole
// array is just re-POSTed on Save) - there's no server-side per-action API
// for this.
//
// Where it is stored: JSON files on the board's FAT flash partition
// ("ffat", ~9.9MB), NOT in NVS. NVS refuses any stored string over 4000
// bytes, and a full config is several times that - an earlier version
// saved everything as one NVS string, every save failed silently, and
// every edit was lost on reboot.
bool gauge_config_save(void);

// True once the flash filesystem has mounted (it is mounted on first use).
bool gauge_config_storage_ok(void);

// Why the last load/save failed ("" if the last operation succeeded).
const char *gauge_config_last_error(void);

// Size in bytes of the last successful save: 0 = display config, 1 = web config.
size_t gauge_config_saved_bytes(int which);

// Human-readable signal name, for the web UI's sensor picker.
const char *gauge_config_signal_name(SignalId sig);

// A signal counts as "lost" after this long without a new frame. Covers
// key-off, an unplugged CAN cable, or an ECU that stopped broadcasting -
// every ECU frame we decode arrives many times a second, so 2s is far
// beyond normal gaps.
#define GAUGE_SIGNAL_STALE_MS 2000

// Reads the live current value for a given signal.
float gauge_config_signal_value(SignalId sig);

// True only while the signal has been seen within GAUGE_SIGNAL_STALE_MS.
// False before the first frame AND again if frames stop - so gauges,
// warnings and logging never act on a frozen last value.
bool gauge_config_signal_has_data(SignalId sig);

// ---- Signal activity tracking (for the sensor picker's "how live is
// this signal" sort/display, matching the ping-time-style column in
// the reference screenshots) - call from the CAN frame handlers
// whenever a signal's value actually gets updated. ----
void gauge_config_mark_signal_active(SignalId sig);

// Milliseconds since this signal was last updated (very large if never
// seen) - lower means "more active/live" for sorting purposes.
uint32_t gauge_config_signal_age_ms(SignalId sig);

// ---- Which signals get logged to SD - independent of what's actually
// displayed on any page/cell, since you may want to log more (or
// fewer) channels than you're looking at. A separate persisted
// bool-per-signal list, with its own small web config page. ----
void gauge_config_log_init(void);
bool gauge_config_is_log_enabled(SignalId sig);
void gauge_config_set_log_enabled(SignalId sig, bool enabled);
void gauge_config_log_save(void);

#ifdef __cplusplus
}
#endif

#endif
