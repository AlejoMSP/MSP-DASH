# Waveshare ESP32-S3-Touch-AMOLED-1.43 (round display target)

Second hardware target for this project, alongside the original
ESP32+Nextion build. Goal: display rusEFI CAN data on this round AMOLED
screen, mounted in the Miata.

## Confirmed hardware (from Waveshare's product page)
- MCU: ESP32-S3R8 (8MB PSRAM built-in), 16MB Flash
- Display: 1.43" AMOLED, 466x466, QSPI interface, 16.7M colors
- Driver IC: **SH8601 or CO5300 - varies by production batch, unconfirmed
  for this specific unit** (received new, no markings found)
- Touch: FT3168 or FT6146 (also varies)
- IMU: QMI8658 (6-axis accel+gyro) - available for future use (G-force
  display? lean angle?)
- RTC: PCF85063
- TF card slot, Li-ion battery header, USB-C

## Confirmed pinout (from Waveshare's product page)
| Function | ESP32-S3 Pin |
|---|---|
| QSPI_CS | GPIO9 |
| QSPI_CLK | GPIO10 |
| QSPI_D0 (mosi) | GPIO11 |
| QSPI_D1 (miso) | GPIO12 |
| QSPI_D2 (quadwp) | GPIO13 |
| QSPI_D3 (quadhd) | GPIO14 |
| AMOLED_RESET | GPIO21 |
| AMOLED_EN | GPIO42 (must be driven HIGH to power the panel) |
| TP_SDA (touch I2C) | GPIO47 |
| TP_SCL (touch I2C) | GPIO48 |

## Library
"GFX Library for Arduino" by moononournation
(github.com/moononournation/Arduino_GFX) - confirmed installed locally
with both Arduino_CO5300 and Arduino_SH8601 driver classes present.

Constructor signatures (copied directly from installed library source,
not guessed):
```
Arduino_ESP32QSPI(int8_t cs, int8_t sck, int8_t mosi, int8_t miso,
                   int8_t quadwp, int8_t quadhd, bool is_shared_interface = false);

Arduino_CO5300(Arduino_DataBus *bus, int8_t rst = GFX_NOT_DEFINED, uint8_t r = 0,
               int16_t w = 480, int16_t h = 480,
               uint8_t col_offset1 = 0, uint8_t row_offset1 = 0,
               uint8_t col_offset2 = 0, uint8_t row_offset2 = 0);

Arduino_SH8601(...)  // identical signature shape to Arduino_CO5300
```

## Driver chip identification - IN PROGRESS
Two test sketches created to empirically determine which driver chip
this specific unit actually has:
- `esp32s3-amoled-test-co5300/`
- `esp32s3-amoled-test-sh8601/`

Both use identical logic (color-bar fill test + text), differing only in
which Arduino_GFX display class they instantiate. Flash each in turn:
- Correct driver: clean red/green/blue fills, readable white text
- Wrong driver: blank screen, scrambled/garbled colors, or garbage pixels
- Note: col/row offset params default to 0,0,0,0 (untuned) - if the
  correct driver's pattern renders but shifted/cropped rather than
  garbled, that still confirms the right chip; offsets just need tuning
  afterward.

Status: not yet bench-tested on the actual unit as of this writing.

## Architecture plan (once display bring-up succeeds)
- CAN/GPS/EGT pin re-mapping needed: this board's GPIO9-14, 21, 42, 47,
  48 are all committed to the display/touch, so the ESP32-main sketch's
  existing pin assignments (CAN on 21/22, Nextion on 16/17, GPS on 4/5,
  EGT on 32/33/34) will need to be re-evaluated against what's actually
  free on THIS board - not yet done.
- The existing rusEFI CAN parsing logic (handleRusefiFrame et al.) should
  be largely reusable as-is; only the OUTPUT side (rendering to this
  round AMOLED instead of Nextion serial commands) is new work.
- This board also has WiFi, so it could potentially also host the same
  web dashboard code, or just render locally via Arduino_GFX/LVGL -
  approach not yet decided.

## Display bring-up: RESOLVED (2026-09-27)
- **Confirmed driver chip: CO5300** (SH8601 showed a blank screen entirely)
- **Confirmed QSPI speed: 10MHz** (40MHz badly corrupted, 20MHz cleaned
  up solid fills but text was still dithered - text is more sensitive to
  signal integrity than solid color fills since it needs pixel-exact
  accuracy)
- **Confirmed offset: 7,7,7,7** with width/height=466,466 - centers the
  visible 466x466 area within the panel's native 480x480 buffer
- **Tearing fix confirmed:** use opaque-background text
  (`setTextColor(fg, bg)`) instead of a separate clear-rect + draw-text
  pass. Two separate writes per frame let the panel's internal refresh
  land mid-update, causing visible tearing. One write per field, always
  fully overwriting old pixels (via fixed-width space-padding), fixed it
  completely.

## CAN pins (revised - see correction below)
Committed pins to avoid: GPIO0 (BOOT strap), GPIO4 (battery ADC), GPIO8
(IMU interrupt), GPIO9-14 (QSPI display), GPIO15 (RTC interrupt), GPIO19/20
(native USB), GPIO21 (AMOLED reset), GPIO38-41 (SD card), GPIO42 (AMOLED
enable), GPIO43/44 (UART), GPIO47/48 (shared I2C - touch+IMU+RTC). Also
avoid GPIO45/46 (S3 boot-strapping pins, even though the vendor table
lists them as free) and GPIO3 (strapping-adjacent).

**IMPORTANT CORRECTION:** the official pinout table shows which GPIOs
are free at the chip/silicon level - it does NOT mean every free GPIO is
physically wired to an accessible connector on this board. GPIO5/6 were
originally chosen on that basis and turned out to not be broken out
anywhere. The actual board diagram (confirmed via photo) shows two real
headers: left side has IO0, IO7, IO1, IO17, IO2, IO16 (plus GND/power
pins); right side has IO45, IO18 (plus 3V3/TXD/RXD/GND/SDA/SCL). Of
those, IO1/IO2/IO7/IO16/IO17/IO18 are both free AND physically
accessible.

**Chosen CAN pins: TX=GPIO1, RX=GPIO2** (both on the left header, right
next to each other) - wire to an SN65HVD230 transceiver (same part used
in the original ESP32 build, and conveniently 3.3V-native so no voltage
divider is needed this time): CTX->GPIO1, CRX->GPIO2, VCC->3.3V,
GND->GND, CANH/CANL->bus.

## LinkECU support - numbers-only gauge display
New sketch: `esp32s3-amoled-linkecu-numbers/`. Parses LinkECU's CAN
broadcast (base 1520 decimal/0x5F0, big-endian, most channels x0.1) -
the SAME format previously used for "MegaSquirt" in the ESP32-classic
build before that was removed; strong evidence (from PCLink CAN Setup
screenshots at the very start of this project) that Link and MegaSquirt
share this exact broadcast convention.

Blocks parsed: 0x5F0 (RPM), 0x5F2 (Baro/MAP/MAT/CLT), 0x5F3
(TPS/Batt/EGO1/EGO2). NOT yet bench-verified against a real LinkECU on
this specific wiring - confirm frames are actually arriving (Serial
debug or a bus analyzer) before trusting displayed values, same
discipline as every other protocol in this project.

Display: one large "hero" RPM readout plus a 2-column grid of six
smaller fields (MAP, CLT, TPS, AFR, Battery, MAT). Cyan labels
(approximating the web dashboard's #4dd0e1 theme color) above white
values. Numbers-only for now, per project decision - no needles/bars/
lights yet on this board.

## Future generic signal mapping (LinkECU, deferred)
Since Link's CAN layout is fully user-configurable in PCLink (unlike
rusEFI's fixed DBC), a longer-term plan is a `linkmap.conf`-style text
file (loaded from the SD card already supported on the ESP32-classic
build) defining arbitrary signals by CAN ID + bit position + scaling,
decoded generically at runtime rather than hardcoded per-channel in C++.
Modeled after the open-source M-Gauge project's canmap.conf approach.
Not yet implemented - current sketch hardcodes the 3 confirmed blocks
directly.

## Architecture change: UART bridge from WROOM, not independent CAN (2026-09-27)

Per project decision, the round display no longer reads CAN directly.
Instead, the WROOM board (esp32-main.ino) owns ALL CAN decoding - rusEFI
(auto-detected, own ID range) AND MegaSquirt/LinkECU (shared parser,
selector on /config, per project decision to support all three ECUs) -
and forwards the same field/value data it already sends to the physical
Nextion display, over a new separate UART link, to this round AMOLED
board.

**Why:** avoids duplicating a CAN transceiver + voltage divider + 5V
power sharing on the AMOLED board entirely; single source of truth for
CAN parsing; reuses the UART-based approach already proven with Nextion.

### WROOM side changes (esp32-main.ino)
- MegaSquirt/LinkECU support RE-ADDED (was removed earlier to focus on
  rusEFI-only; now restoring all three per project decision). Shares one
  parser (handleSecondaryEcuFrame) since both brands appear to use the
  identical broadcast format - selector on /config only changes
  labeling. Fixed a gap in the restored code: BATT (word1 of block 3)
  was being read from the CAN frame but never actually sent - needed for
  the round display's BATT field, now fixed.
- New UART1 link (Serial1, GPIO25=TX/GPIO26=RX, 115200 baud) added
  alongside the existing Nextion UART2 - nextionSend() now writes to
  BOTH UART2 (Nextion) and UART1 (AMOLED board) with the same command
  string and terminator convention.

### ESP32-S3 AMOLED side (esp32s3-amoled-uart-display/ - NEW, replaces
esp32s3-amoled-linkecu-numbers/ as the current approach)
- No CAN code at all anymore - just a UART receiver (GPIO2=RX from
  WROOM's GPIO25, GPIO1=TX to WROOM's GPIO26 though not used yet)
- Parses the same "field.txt=\"value\"" / "field.val=value" +
  0xFF-terminated protocol Nextion already uses
- Same 7-field numbers-only layout as before (RPM/MAP/CLT/TPS/AFR/
  BATT/MAT), same confirmed display config (CO5300, 10MHz, offset
  7,7,7,7, opaque-text tearing fix) - only the DATA SOURCE changed

### Wiring (WROOM <-> ESP32-S3 AMOLED board)
| WROOM pin | AMOLED board pin |
|---|---|
| GPIO25 (TX) | GPIO2 (RX) |
| GPIO26 (RX) | GPIO1 (TX) - not used yet |
| GND | GND (common ground required) |

NOT yet bench-tested end-to-end (WROOM decoding + forwarding + AMOLED
receiving/displaying) as of this writing.

## Custom gauge background images from SD card (2026-09-28)

New standalone test: `esp32s3-amoled-image-test/`. Loads a raw RGB565
image file from the SD card and displays it - validates the pipeline on
its own before folding into the live gauge display.

### SD card pins (confirmed, separate SPI bus from the QSPI display)
SD_CS=GPIO38, SD_MOSI=GPIO39, SD_MISO=GPIO40, SD_SCLK=GPIO41 (from
Waveshare's official pinout table).

### Image format
Raw RGB565, little-endian uint16 per pixel, no header - just
width*height*2 bytes, row-major. Confirmed via the installed Arduino_GFX
library's header (`draw16bitRGBBitmap(x, y, uint16_t *bitmap, w, h)`) -
takes a pointer to pixel data already in memory, so the file is read
into a PSRAM buffer (via heap_caps_malloc + MALLOC_CAP_SPIRAM - ESP32-S3
has 8MB PSRAM, plenty for a 434KB full-screen 466x466 image) before
being blitted.

A test image (`docs/images/gauge_bg_test.raw`, 466x466, 434312 bytes)
is included, converted from a reference gauge-face photo using Python/
Pillow (RGBA flattened onto black, then R8G8B8 -> RGB565 packed as
((R>>3)<<11)|((G>>2)<<5)|(B>>3), written little-endian). Copy this file
to the SD card's root as `/gauge_bg.raw` to test.

### Unconfirmed detail worth flagging
The SD card's SPI bus setup (`SPIClass sdSPI(HSPI)`) is a best-effort
guess, not verified against ESP32-S3's actual SPI host naming in the
installed core - the display already occupies SPI2_HOST internally
(confirmed from Arduino_ESP32QSPI.h), so the SD card needs a genuinely
different SPI peripheral to avoid a bus conflict. If this doesn't
compile or the card misbehaves, try FSPI instead of HSPI.

### Next step (not yet built)
This is background-image loading only - not yet integrated with the
live gauge display (numbers still render as before, on a black
background). Combining a background image with the live number overlay
would need the numbers positioned to not overlap important parts of the
image (like tick marks), and if a rotating needle over the image is
ever wanted, that needs more complex partial-redraw logic (restoring
background pixels under the needle's old position each frame) - a
meaningfully bigger step than static-image display.

## Moving needle test (2026-09-28)

New standalone test: `esp32s3-amoled-needle-test/`. A needle sweeps
automatically via a simulated 0-100 value (no real data hookup needed
yet) - proves out rotation math and flicker-free redraw before
integrating with a real gauge background image or live UART/CAN data.

Sweep convention: -135 to +135 degrees (270 total), 0 degrees = straight
up - matches the web dashboard's established gauge convention.

Erase technique: since this test has a plain black background (no
gauge-face image yet), the old needle is erased by simply redrawing its
previous line in the background color before drawing the new one at the
new angle. This is NOT what a real background-image gauge will need -
that requires restoring the actual background pixels from a buffer
(the same PSRAM buffer already used for image loading) rather than
painting over in one flat color, since painting flat black would punch
a hole in the gauge-face image wherever the needle used to be. That's
the next step once this core motion mechanic is confirmed working.

Remember the board-option fix from the image-loading work applies here
too: `esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M`.

## LVGL v9 integration - setup findings (2026-09-28)

Switched from hand-rolled Arduino_GFX gauge drawing to LVGL v9, since it
has a confirmed working needle-rotation mechanism
(`lv_image_set_rotation()` + `lv_image_set_pivot()`) that handles
compositing a rotating needle over a background automatically via its
own render/invalidation system - solving the exact "needle over
background image without artifacts" problem this project's hand-rolled
approach would have needed much more manual work for.

### Key finding: no more all-in-one "meter" widget
LVGL v9 removed the old v8 `lv_meter_*` gauge widget entirely - confirmed
via source search (no `lv_meter.h` exists). Replaced with a compositional
approach: `lv_scale` (tick marks/arc face) as a separate widget from a
rotatable needle built from `lv_image` (or `lv_line`). A real gauge
needs building from these pieces rather than one drop-in call.

### lv_conf.h setup (confirmed via actual compile error)
LVGL expects `lv_conf.h` as a SIBLING of the `lvgl` library folder itself
(i.e. directly in `Arduino/libraries/`, not inside `Arduino/libraries/lvgl/`).
Confirmed via the actual compiler error:
`fatal error: ../../../../lv_conf.h: No such file or directory`.
Fix: copy `lv_conf_template.h` from the library root to
`Arduino/libraries/lv_conf.h`, then flip its master switch from `#if 0`
to `#if 1` near the top of the file (line ~15) to actually enable the
config's content.

### Color depth: LV_COLOR_DEPTH no longer exists in v9
Confirmed via source search - the old global compile-time
`LV_COLOR_DEPTH` macro used throughout LVGL v8 (and still referenced in
the current official Arduino example's buffer-size formula, which
appears to be stale/not fully updated for v9) doesn't exist anywhere in
v9.6.0's actual source. Replaced by a per-display RUNTIME setting:
`lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565)`. Since a
compile-time array size can't use this runtime call, hardcoded "2" bytes
per pixel directly in the buffer-size calculation instead (RGB565 is
fixed at 2 bytes/pixel by definition, no ambiguity).

### Confirmed working API surface for the display bridge
- `lv_tick_set_cb(fn)` - tick source (fn returns millis())
- `lv_display_create(w, h)`
- `lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565)`
- `lv_display_set_flush_cb(disp, fn)` - fn signature:
  `void(lv_display_t*, const lv_area_t*, uint8_t* px_map)`
- `lv_display_set_buffers(disp, buf1, buf2, size, LV_DISPLAY_RENDER_MODE_PARTIAL)`
- Inside flush_cb: `lv_area_get_width(area)`/`lv_area_get_height(area)`,
  then bridge straight to our confirmed `gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)px_map, w, h)`,
  then `lv_display_flush_ready(disp)`

New test sketch: `esp32s3-amoled-lvgl-test/` - minimal "Hello LVGL"
label, proving the full bridge (tick, buffers, color format, flush
callback, our display init) before building the actual gauge composition
(background image + rotated needle image, next step).

## First real LVGL gauge (2026-09-28)

New test: `esp32s3-amoled-lvgl-gauge-test/`. Real `lv_scale` widget with a
`lv_line` needle driven by `lv_scale_set_line_needle_value()` -
automatic simulated 0-100 sweep, before wiring to live data. Built on
the confirmed-working LVGL bridge from esp32s3-amoled-lvgl-test.

Confirmed core API (checked against installed source):
`lv_scale_create`, `lv_scale_set_mode` (LV_SCALE_MODE_ROUND_INNER),
`lv_scale_set_total_tick_count`, `lv_scale_set_label_show`,
`lv_scale_set_range`, `lv_scale_set_angle_range`, `lv_scale_set_rotation`,
`lv_line_create` (the needle object itself), `lv_scale_set_line_needle_value(scale, needle_line, needle_length, value)`.

Sweep convention -135 to +135 (270 total) matches this project's
established gauge convention elsewhere (web dashboard, hand-rolled
Arduino_GFX needle test).

NOTE - unverified detail: the part-based styling calls (LV_PART_ITEMS
for minor ticks, LV_PART_INDICATOR for major ticks/labels) are a
reasonable guess based on common LVGL scale widget convention, not
independently confirmed against source. Low risk if wrong - would just
mean colors don't apply exactly as intended cosmetically, not a
functional issue.

## Combined LVGL gauge + direct CAN (2026-09-28)

New sketch: `esp32s3-amoled-lvgl-can-gauge/`. Combines the confirmed LVGL
gauge (lv_scale + lv_line needle, from esp32s3-amoled-lvgl-gauge-test)
with direct LinkECU CAN reading on this board's own transceiver (from
esp32s3-amoled-linkecu-numbers) - per project decision, this board reads
CAN itself rather than via the UART bridge from the WROOM board.

Real RPM (0-8000 range) drives the needle instead of the earlier
simulated 0-100 sweep test. Only RPM is parsed for now (BLOCK0/word3) -
the other LinkECU values (MAP/CLT/TPS/AFR/etc, already parsed in
esp32s3-amoled-linkecu-numbers) aren't wired into this gauge yet.

Wiring: same as esp32s3-amoled-linkecu-numbers - GPIO1=CAN TX, GPIO2=CAN
RX (through 1k/2k voltage divider, MCP2551 is 5V), MCP2551 powered from
the shared automotive 5V supply (this board itself has no dedicated 5V
pin).

Needle update throttled to 80ms and only redraws when RPM actually
changed - carries over the tearing-test settings (disabled rounded line
caps, slower update rate) from esp32s3-amoled-lvgl-gauge-test, pending
confirmation those actually fixed the tearing issue seen there.

NOT yet bench-tested end-to-end (real CAN frames + gauge rendering
together) as of this writing.

## CAN pin revision: switched to the UART connector (2026-09-28)
The 1.27mm-pitch GPIO expansion header (where GPIO1/GPIO2 lived) turned
out to be physically inaccessible - no adapter or fine-pitch soldering
setup on hand. Switched to the board's UART connector instead (JST-SH
1.0mm pitch, matching cable already available): GPIO43 (labeled "TXD")
and GPIO44 (labeled "RXD"). ESP32-S3's TWAI peripheral is routable to
almost any GPIO via the chip's flexible GPIO matrix, so repurposing
these UART-labeled pins for CAN works fine - they're general-purpose
exposed pins, not consumed by any onboard peripheral. Same wiring
pattern as before otherwise (MCP2551, 5V, 1k/2k voltage divider on RX).

## Switched to boost gauge on rusEFI (2026-09-28)

esp32s3-amoled-lvgl-can-gauge repurposed from RPM/LinkECU to a BOOST
gauge on rusEFI (bench-confirmed elsewhere in this project) - matches
current test setup. Boost = MAP (RUSEFI_BASE3, little-endian) minus the
fixed 101.3 kPa assumed baro, same as the rest of this project.

Requested display range was "-40 inHg to 30 psi" AND "-2 to 2 bar"
simultaneously - not cleanly supported by lv_scale's plain single-range
tick system (no confirmed per-tick custom decimal formatting API).
Scoping decision: on-screen gauge shows kPa (range -150 to 200, covering
both requested ranges), Serial console prints every CAN-derived boost
reading in kPa, bar, inHg, AND psi - covers wanting to see those units
without risking broken/misleading on-screen tick labels, and doubles as
direct confirmation CAN data is flowing. A true dual-unit tick face
(inHg on the left half, psi on the right, matching a real mechanical
boost gauge) would be a larger follow-up if that authentic look is
wanted later.

## Multi-page gauge cluster (2026-09-28)

Rewrote esp32s3-amoled-lvgl-can-gauge as a 3-page cluster, cycled with
the board's physical BOOT button (GPIO0, active-low, debounced):

- **Page 1**: AFR needle gauge (range 10-20) + precise numeric readout
  below the needle (e.g. "14.7")
- **Page 2**: Coolant Temp (0-260F) and Oil Pressure (0-700kPa) as two
  independent half-moon (180-degree) gauges side by side. Half-moon
  achieved via lv_scale_set_angle_range(180) + lv_scale_set_rotation(-90)
  (opens upward, matching a classic rainbow-arc gauge look)
- **Page 3**: 6-value grid - AFR/CLT, Oil Temp/Oil Press, Fuel Press/
  Flex% - plain numeric labels, 2 columns x 3 rows

Uses LVGL's native multi-screen support (lv_obj_create(NULL) creates each
page as an independent top-level screen, lv_screen_load() switches the
active one). All CAN parsing and gauge-value updates run every loop()
regardless of which page is currently loaded/visible - switching pages
always shows already-current data with no lag, since updating an LVGL
object on a non-active screen is harmless (no display cost until that
screen is loaded).

Expanded CAN parsing to cover: RUSEFI_BASE1 (FlexPct), RUSEFI_BASE3
(MAP/boost + CLT), RUSEFI_BASE4 (OilPress + OilTemp), RUSEFI_BASE7
(AFR via Lambda + FuelPress/FpLow) - up from boost-only.

Font note: all text uses LVGL's default font size - didn't reference a
specific larger font (e.g. LV_FONT_MONTSERRAT_36) since which font sizes
are actually enabled in lv_conf.h isn't confirmed, and guessing risked
an "undefined identifier" compile error. Follow-up if bigger text is
wanted: confirm enabled fonts in lv_conf.h first.

NOT yet bench-tested (button press cycling, half-moon rendering,
6-value grid layout, and the newly-added CAN signal parsing) as of this
writing.

## Touch controller test (2026-09-28)

New standalone test: `esp32s3-amoled-touch-test/`. Uses **bb_captouch**
library (Larry Bank) - supports FT6x36-family chips (likely covers
FT6146), auto-detects sensor type/address. FT3168 specifically isn't in
its stated supported chip list - if this board's actual touch IC is
FT3168 rather than FT6146, this library may not recognize it. This test
makes that clear directly (prints init()/sensorType() results).

Confirmed API (checked against installed library source):
`BBCapTouch` class, `init(SDA, SCL, RST=-1, INT=-1, speed=400000)`,
`getSamples(TOUCHINFO*)`. `TOUCHINFO` struct: `count` (0-5 active
touches), `x[5]`/`y[5]` coordinate arrays, `pressure[5]`/`area[5]`.

Touch pins (confirmed): TP_SDA=GPIO47, TP_SCL=GPIO48 - no dedicated
reset/interrupt pin in the official pinout table, using -1 (unused) for
both per the library's defaults.

Test prints raw touch samples and detects swipe left/right (touch-down
X vs touch-up X, threshold 60px) - validates the hardware/library
before wiring swipe-to-change-page into the real multi-page gauge
cluster (esp32s3-amoled-lvgl-can-gauge, currently uses the physical
BOOT button for page cycling).

NOT yet bench-tested as of this writing.

## Serial echo test - RX direction diagnostic (2026-09-28)

New minimal test: `esp32s3-amoled-serial-echo/`. Type on any Serial
terminal, whatever you type appears on the AMOLED screen. Tests the
OPPOSITE direction from what's been failing (Serial RX - computer to
board - instead of Serial TX/print - board to computer), plus the
display itself, independent of Serial.print() output or LVGL/any
complex library - raw Arduino_GFX text drawing only.

Built in response to persistent "nothing shows on Serial Monitor"
symptom across multiple sketches/computers - if typed characters DO
appear on screen, that proves the board is genuinely running
application code, USB serial works in at least the RX direction, and
the display is fine - isolating the problem specifically to Serial
TX/print visibility in the monitor tools tried so far, not a dead board.

## Full pivot: multi-page gauge cluster on Waveshare's ESP-IDF/LVGL v8 BSP (2026-09-30)

Major architecture change, following a long debugging saga (persistent
"nothing on Serial", display/touch issues never fully resolved on our
Arduino_GFX + LVGL v9 + bb_captouch stack). Root cause was finally
isolated by flashing Waveshare's own factory firmware directly - display
AND touch both worked perfectly, proving the hardware was always fine
and the problem was entirely in our software stack. Their factory image
uses a completely different foundation: ESP-IDF's native `esp_lcd_panel`
framework (not Arduino_GFX), LVGL v8.4.0 (not v9), LVGL running on its
own dedicated FreeRTOS task, and a small custom I2C driver for touch
(not a library). Confirmed via their own bundled example source
(07_LVGL_Test), compiled and flashed through our own arduino-cli
toolchain - both display and touch confirmed working end-to-end.

Two real hardware facts this also corrected, from the source code itself:
- **Display driver is genuinely SH8601** (not CO5300 as our own
  dithering/blank-screen testing had concluded) - their code
  auto-detects the chip ID at runtime and picks the right init sequence
  for either, explaining why "SH8601 or CO5300" wasn't actually
  ambiguous for THEM.
- **Touch controller is genuinely FT3168** (matching the official specs
  we originally had, before bb_captouch's lack of FT3168 support led us
  to suspect it might be the other listed option, FT6146).

### New sketch: esp32s3-amoled-bsp-base/
Built directly on top of Waveshare's confirmed-working 07_LVGL_Test
example (all files: lcd_bsp.c/h, esp_lcd_sh8601.c/h, FT3168.cpp/h,
read_lcd_id_bsp.c/h, lcd_config.h - unmodified except one targeted
change). Library setup: their bundled `lvgl` v8.4.0 and matching
`lv_conf.h` replace our earlier v9 install (backed up, not deleted, as
lvgl_v9_backup / lv_conf_v9_backup.h in the Arduino libraries folder).

**The one edit made to their code**: in lcd_bsp.c, added
`#include "gauge_app.h"` and replaced the placeholder
`lv_demo_widgets();` call with `build_gauge_app();` - everything else in
their confirmed-working display/touch/LVGL init is untouched.

### gauge_app.c/h - our actual gauge cluster
Rebuilt the same 3-page layout from the earlier v9 attempt, now using
v8's `lv_meter` widget (the classic all-in-one gauge widget removed in
v9 - simpler than the v9 lv_scale+lv_line manual composition):
- Page 1: AFR meter (range 10-20) + precise numeric readout
- Page 2: CLT + Oil Pressure as independent half-moon (180-degree)
  meters side by side
- Page 3: 6-value grid (AFR/CLT, Oil Temp/Oil Press, Fuel Press/Flex%)

Confirmed lv_meter API (checked against installed v8.4.0 source):
`lv_meter_create`, `lv_meter_add_scale`, `lv_meter_set_scale_ticks`,
`lv_meter_set_scale_major_ticks`, `lv_meter_set_scale_range`,
`lv_meter_add_needle_line`, `lv_meter_set_indicator_value`. Also
confirmed: `lv_obj_create(NULL)` for screens, `lv_disp_load_scr()` for
switching, `lv_label_create`/`lv_label_set_text`, and
`lv_obj_set_style_bg_color`/`lv_obj_set_style_text_color` (v8 signature
takes a `lv_style_selector_t selector` param, using `0` for default
part/state - very common v8 convention).

NOTE - unverified: the `rotation` parameter values in
`lv_meter_set_scale_range` (135 for the full gauge, 180 for the
half-moons) are a best-effort match to the classic "opens at the
bottom"/"opens upward" gauge looks established elsewhere in this
project - not independently confirmed against source for this exact
API's angle convention, may need visual adjustment once tested.

### Thread-safety approach
LVGL runs on its own FreeRTOS task (their BSP's design); Arduino's
loop() is left empty by their example and is where our CAN/TWAI polling
and BOOT-button page-switching now live. To avoid calling any LVGL
function from outside LVGL's own task (unsafe without their internal
mutex, which is file-scope static in lcd_bsp.c), loop() only ever
writes simple global values (plain volatile float/int - safe for
single-word reads/writes on this architecture, same pattern used
throughout this project). gauge_app.c registers one `lv_timer` (ticks
automatically whenever LVGL's port task calls `lv_timer_handler()`,
already properly locked) that reads those globals and performs all
actual LVGL API calls, including page-switching via a simple
"requested page" flag.

### CAN wiring
Using GPIO1/GPIO2 (originally-identified free/accessible pins on the
1.27mm GPIO header) - NOT GPIO43/44, which are this board's dedicated
UART0 pins (confirmed from the official pinout table) and were the
likely root cause of the Serial-visibility saga on the OLDER stack -
though with this new BSP foundation, Serial now uses whatever routing
Waveshare's own code establishes, which has not yet been specifically
re-verified independently.

NOT yet bench-tested (real CAN data flowing into these new pages, BOOT
button page-switching, half-moon rendering, rotation angle correctness)
as of this writing.

## Swipe gesture page switching (2026-09-30)

Added touch swipe left/right to change pages, alongside the existing
BOOT button (both work). Confirmed API used (checked against installed
v8.4.0 source, including a header comment that gave the exact call
pattern): `lv_obj_add_event_cb(obj, cb, LV_EVENT_GESTURE, user_data)`,
and inside the callback: `lv_indev_get_gesture_dir(lv_indev_get_act())`
returning `LV_DIR_LEFT`/`LV_DIR_RIGHT`.

Attached to all 3 page root objects (screens) so gestures anywhere on a
page's content bubble up and trigger the callback. Since gesture events
fire from within LVGL's own task context (detected during
lv_timer_handler()'s normal processing), the callback calls
lv_disp_load_scr() directly - unlike the BOOT button path (which crosses
from Arduino's separate loop() task and must go through the
g_requested_page flag instead). A new `s_current_page` tracker in
gauge_app.c keeps both switching mechanisms in sync.

NOT yet bench-tested as of this writing.

## Full redesign: 6 full-screen analog gauges (2026-09-30)

Complete rebuild of the gauge cluster to match a reference automotive
gauge image (classic analog style: black background, white ticks/
numbers/needle, 270-degree sweep, caption in the bottom gap). Replaced
the earlier 3-page mixed layout (AFR needle+readout, CLT/OilP
half-moons, 6-value grid) with 6 full-screen single-gauge pages:

0: AFR (10-20), 1: Coolant Temp (65-300F), 2: Oil Pressure (0-100kPa),
3: Oil Temp (65-240F), 4: Fuel Pressure (0-100kPa), 5: Ethanol % (0-100)

All 6 share one generic builder function (build_full_gauge_page) for
visual consistency - same tick density (26 ticks, major every 5th = 6
labels) and styling regardless of each gauge's differing numeric range.

**No-data fallback**: each signal has a has_data flag (g_afr_has_data
etc, set true in the .ino's handleRusefiFrame the first time a real CAN
frame updates it). Until then, that gauge's needle sits at its own
minimum rather than showing a possibly-misleading default/zero value -
implemented via a clamped_or_min() helper in the update timer.

**Auto-cycle**: advances to the next page every 5000ms via a second
lv_timer (autocycle_timer_cb). Manual navigation (swipe gesture or BOOT
button) calls lv_timer_reset() on this timer via a shared switch_to_page()
helper, so a manually-chosen page gets a full 5 seconds before
auto-cycling resumes rather than fighting with it. Confirmed
lv_timer_reset() exists in this v8.4.0 install before using it.

NOT yet bench-tested (visual density/legibility of 26-tick scale
across all 6 differently-ranged gauges, auto-cycle timing, no-data
fallback behavior) as of this writing.

## Refinements: thicker needle, numbers page, boost gauge, no auto-cycle (2026-09-30)

- Needle width increased (5 -> 8) - thicker per request
- Major tick label_gap increased (15 -> 25) - numbers sit further from
  tick marks for clearer separation
- New page 6: numbers grid - all 6 gauge values as plain digital
  readouts (reusing the same grid-label pattern from the earlier
  3-page design), no meter widget on this page
- New page 7: BOOST/VACUUM gauge, range -150 to 200 kPa, rests at 0
  (not the range minimum) until real data arrives - a genuine special
  case, since 0 (atmospheric, no boost/no vacuum) is the meaningful
  "at rest" point here, unlike the other gauges where sitting at their
  minimum makes sense. Added a per-page `rest_val` field (separate from
  `min_val`) to support this.
  UNIT NOTE: true inHg (vacuum)/PSI (boost) dual-unit tick labels aren't
  supported by lv_meter's built-in single linear numeric axis - this
  gauge uses kPa (the unit our CAN math already computes) instead,
  captioned "BOOST/VACUUM (kPa)". A true dual-unit tick face would need
  a custom per-tick label override - flagged as a larger separate task
  if wanted later.
- Auto-cycle timer removed entirely - manual navigation (swipe/BOOT
  button) only now, per request.

NOT yet bench-tested as of this writing.

## Bigger, brighter tick numbers (2026-09-30)

Enabled LV_FONT_MONTSERRAT_28 in lv_conf.h (was only 14px available
before) and applied it to LV_PART_TICKS - confirmed via lv_meter.c
source that this is the actual part lv_meter uses to draw tick number
labels (lv_obj_init_draw_label_dsc(obj, LV_PART_TICKS, &label_dsc)).
Matches a reference image showing prominently large tick numbers on a
classic tachometer-style gauge face.

NOTE: with a much larger font at the same label_gap (25) used for the
smaller 14px font, numbers may visually crowd against tick marks or
each other - not yet visually confirmed, may need the gap or meter size
adjusted once tested.

## Page reorder, boost/MAP recentered, bigger grid numbers (2026-09-30)

- Page order changed: Boost/MAP now page 1 (right after AFR), pushing
  CLT/OilP/OilT/FuelP/Flex%/NumbersGrid each down one slot (now 2-7)
- Boost gauge changed from a baro-subtracted signed value (-150 to 200,
  rest 0) to raw MAP (0-200 kPa, rest 100) - 100 kPa represents
  ambient/atmospheric pressure directly (no ECU baro sensor needed,
  matches rusEFI's DBC which has no live baro signal - same
  fixed-assumption rationale used throughout this project, just applied
  differently: instead of subtracting a fixed baro constant to get a
  signed "boost" value, the raw MAP number itself is shown, with the
  gauge face's "100" tick serving as the visual ambient reference)
- Numbers-grid page (now page 7): value labels enlarged to 28px
  (lv_font_montserrat_28, already enabled), row vertical offset bumped
  slightly (20->24) for clearance under the larger font

## Renamed/reordered pages, range updates, warning flash system (2026-09-30)

New page order and specs, per request:
0: CLT (65-250F, warning >220), 1: MAP (0-250kPa, rests at 100/ambient,
no warning), 2: AFR (10-20, warning >15), 3: OIL PRESS (0-100kPa,
warning <40), 4: OIL TEMP (65-250F, warning >220), 5: FUEL PRESS
(0-100kPa, warning <40), 6: ETH % (0-100, no warning), 7: numbers grid.

Caption font enlarged to 28px (lv_font_montserrat_28, already enabled)
on all individual gauge pages and the numbers-grid values - was
previously only applied to the meter's own tick numbers.

**Warning flash**: pages with a configured threshold (has_warning=true)
flash their entire page background between black and a dark red
(0xB00000) while the live value is past threshold - a phase counter in
the shared 80ms update timer (roughly 480ms per on/off phase), no
separate LVGL animation subsystem needed. Only evaluated once real data
has arrived for that signal (has_data flag), so a gauge sitting at its
rest/default value never falsely triggers. Added has_warning/
warning_threshold/warning_above/warning_active fields to the GaugePage
struct and a new update_warning_flash() helper, called after each
gauge's needle update in the main timer callback.

## Sweeping needle animation while no data present (2026-09-30)

Replaced the static "sit at rest value" behavior with a back-and-forth
sweep animation: while no real CAN frame has arrived yet for a signal
(has_data still false), its needle sweeps between min and max at a
speed aimed for roughly a 2-second one-way pass (step = range/25 per
80ms tick, floored at 1 so small ranges like AFR's 10-20 still move
visibly). Once the first real frame arrives, has_data latches true
permanently and the gauge shows clamped real values from then on -
sweeping only ever happens during the "waiting for CAN" state, not as
an ongoing idle animation once live data is flowing.

New clamped_or_sweep() function replaces the old clamped_or_rest();
added sweep_pos/sweep_step/sweep_dir fields to GaugePage, initialized
in build_full_gauge_page().

## Stage 1: WiFi + web monitor (2026-09-30)

New files: web_monitor.h/.cpp. WiFi AP mode ("MSP-DASH" network),
AsyncWebServer + AsyncWebSocket serving a simple live-updating HTML
page showing all 7 sensor values, broadcasting JSON every 500ms to
connected clients. Uses the same "ESP Async WebServer" + "Async TCP"
libraries already proven working on the WROOM board (esp32-main)
elsewhere in this project - same general pattern, adapted for this
board's 7 values instead of the WROOM board's 74 gauges.

Read-only monitoring only for now (Stage 1 of the larger wireless
configuration plan) - no control/configuration yet. Broadcast skips
building/sending JSON entirely when no clients are connected
(ws.count() == 0), avoiding unnecessary work.

Connect a phone to the "MSP-DASH" WiFi network, then browse to
192.168.4.1 (the default AP IP) to view the live monitor page.

Planned next stages: raw CAN broadcast monitor (any ID, not just
recognized ones), ECU selection (port WROOM board's MegaSquirt/LinkECU
parser, persisted via NVS), full gauge configuration (color/range/
add/remove/rename, requires a data-driven gauge-config refactor).

NOT yet bench-tested as of this writing.

## Fixed boot crash after adding WiFi: shrunk LVGL's DMA buffer (2026-09-30)

Adding web_monitor.cpp (WiFi AP + AsyncWebServer) caused an immediate
boot crash-loop: `assert failed: lcd_lvgl_Init lcd_bsp.c:87 (buf2)`.
Root cause: LVGL's display draw buffers are allocated with
MALLOC_CAP_DMA (line 87 of lcd_bsp.c, part of Waveshare's original BSP
code) - a much smaller, more limited memory pool on the ESP32-S3 than
general heap. WiFi's networking stack also needs DMA-capable memory for
its own radio/buffer operations. At the original buffer size
(EXAMPLE_LVGL_BUF_HEIGHT = screen_height/4, ~216KB total for double
buffering), the two subsystems were competing for the same limited
pool and LVGL's second buffer allocation failed outright.

Fix: reduced EXAMPLE_LVGL_BUF_HEIGHT from /4 to /10 in lcd_config.h
(the one other targeted edit made to Waveshare's originally-untouched
BSP files, alongside the earlier lv_demo_widgets() -> build_gauge_app()
swap in lcd_bsp.c) - frees up enough DMA RAM for WiFi to coexist.
Trade-off: slightly more frequent partial-screen flushes, matches the
buffer fraction our earlier v9-based sketches used successfully with
no visible downside.

## Switched web monitor to STA mode, joins home network (2026-09-30)

Changed from WiFi AP mode to STA (joins an existing WiFi network)
so the board and a phone/laptop can be on the same network for
testing, rather than needing to connect directly to the board's own
AP. Credentials handled the same way as the WROOM board: a git-ignored
wifi_config.h (local only) with a committed wifi_config.h.example
template. Connection progress and the assigned IP address print to
Serial on boot (20 second connection timeout, prints a clear failure
message if it doesn't connect in that window).

## Stage 2: Raw CAN broadcast monitor (2026-09-30)

New /can web page + /can.json endpoint - shows every CAN ID actually
arriving on the bus (up to 32 tracked at once), regardless of whether
we know how to decode it. Table shows ID (hex), DLC, raw data bytes
(hex), frequency (Hz, recalculated once per second), and how many ms
ago each was last seen. Polls /can.json every 1 second via plain
fetch() rather than a second WebSocket connection - simpler, and this
data doesn't need sub-second responsiveness.

New web_monitor_track_can_frame() function, called from the .ino's
loop() for every successfully received CAN frame (before
handleRusefiFrame() - runs independently of whether that frame's ID is
one we recognize/decode).

Confirmed twai_message_t's actual field name (data_length_code) from
ESP-IDF's hal/twai_types_deprecated.h header before using it - matched
the initial guess, no correction needed.

## Stage 3: ECU selection (2026-09-30)

Ported the MegaSquirt/LinkECU shared parser directly from the WROOM
board's confirmed handleSecondaryEcuFrame (esp32-main.ino) - same CAN
IDs (base 1520/0x5F0), same big-endian word extraction, same /10
scaling, same block layout (BLOCK2: baro/MAP/MAT/CLT, BLOCK3: TPS/BATT/
EGO1, BLOCK13: FUELP/OILP/OILT, BLOCK47: Ethanol%). Feeds the same
global g_* values used by the gauge cluster and web monitor.

New files: ecu_config.h/.cpp - persisted ECU selection (rusEFI/
MegaSquirt/LinkECU) via Preferences/NVS, same pattern as the WROOM
board's sysPrefs. Per the WROOM board's own design, this selection is
purely a DISPLAY LABEL, not a parsing gate - MegaSquirt/LinkECU's ID
range (1520+) never overlaps rusEFI's (0x200s), so both parsers stay
active simultaneously regardless of which is "selected" - matches the
WROOM board's exact approach.

Web page: added an ECU dropdown (rusEFI/MegaSquirt/LinkECU) with a
Save button, GET/POST /ecu endpoint for reading/writing the current
selection.

NOT yet bench-tested (MS/Link parsing specifically, since testing so
far has used rusEFI) as of this writing.

## Stage 4: Full gauge configuration (2026-09-30)

Major rewrite - gauge pages are now entirely data-driven instead of
hardcoded, enabling runtime configuration of signal assignment, range,
needle color, caption, and warning threshold per gauge slot, editable
from a new /gauges web page and persisted to NVS.

**Expanded signal pool**: added RPM, TPS (throttle position), and
BattV (battery voltage) decoding to both rusEFI and MegaSquirt/LinkECU
parsers - ported directly from the WROOM board's confirmed byte offsets
(rusEFI: RPM at BASE1 offset 0 no scaling, TPS at BASE2 offset 2 signed
x0.01, BattV at BASE4 offset 6 x0.001; MS/Link: RPM at BLOCK0 offset 6,
TPS/BattV at BLOCK3 offsets 0/2 x0.1). Full pool is now 10 signals
(RPM, CLT, MAP, AFR, OilP, OilT, FuelP, Flex%, TPS, BattV).

**New files**: gauge_config.h/.cpp - SignalId enum, GaugeSlotConfig
struct (signal/enabled/min/max/rest/caption/color/warning fields),
NVS persistence via ArduinoJson (whole MAX_GAUGE_SLOTS=8 array
serialized as one JSON string under a single Preferences key, simpler
than dozens of individual keys), default config matching the previous
hardcoded 7-gauge setup plus one extra disabled slot (RPM) ready to
enable. Also provides the generic gauge_config_signal_value()/
has_data() dispatch gauge_app.c's update timer now uses instead of
hardcoded per-gauge references.

**gauge_app.c rewrite**: build_gauge_app() now loops over enabled slots
from gauge_config_get_slots() instead of fixed calls - page count is
genuinely dynamic based on how many slots are enabled. Needle color is
now per-slot (was hardcoded white everywhere). The numbers-grid page is
also data-driven, showing whatever signals are currently enabled (up
to 8, in a 2x4 layout).

**Bug fix found and fixed along the way**: checkPageButton() in the
.ino had hardcoded `% 3` for page cycling - a stale leftover from when
there were only 3 pages, meaning the BOOT button has only ever cycled
among the first 3 of our now-8 pages this whole time. Added
gauge_app_get_num_pages() so this uses the real dynamic count.

**Web UI**: new /gauges page - one editable block per slot (signal
dropdown, enabled checkbox, caption, min/max/rest, color picker,
warning enable/threshold/direction). Saves via POST of the whole array
as raw JSON (accumulated across chunks via ESPAsyncWebServer's body
callback, parsed once complete) to /gauges; GET /gauges.json returns
current config. Changes are saved to NVS immediately but require a
reboot to actually apply (pages are built once at boot inside
lcd_lvgl_Init() - runtime page teardown/rebuild was judged out of scope
for this pass).

NOT yet bench-tested as of this writing - this is the largest single
rewrite in this project's history, worth extra care testing.

## Customization: tick/background color, bigger numbers (2026-09-30)

Added tick_color and bg_color fields to GaugeSlotConfig (alongside the
existing needle_color) - both editable from the /gauges web page via
color pickers, persisted the same way as everything else.

Tick color required no new LVGL style API - lv_meter_set_scale_ticks()
and lv_meter_set_scale_major_ticks() already take a color parameter
directly (confirmed via lv_meter.c source: scale->tick_color /
scale->tick_major_color), we were just hardcoding lv_color_white() for
both calls before.

Background color applies to both the page and the meter's own
background style. Also fixed a related correctness issue: the warning
flash previously assumed black as its "off" state unconditionally -
now stores each page's configured bg_color and flashes between THAT
and red, so a custom background color doesn't break the warning
indicator.

Tick-label font bumped from 28px to 36px (LV_FONT_MONTSERRAT_36, needs
enabling in lv_conf.h) per request for bigger numbers - caption text
stays at 28px. Flagged flash-storage usage (93% at last compile) as a
real constraint going forward, before adding SD card support next.

## SD card: mount, browse, upload, download (2026-09-30)

New files: sd_storage.h/.cpp - mount code ported directly from
Waveshare's confirmed 04_SD_Card example for this exact board. Uses
ESP-IDF's native VFS/FAT + SD SPI driver (esp_vfs_fat_sdspi_mount),
NOT Arduino's SD.h - matches their architecture pattern (native
ESP-IDF components) used throughout this whole BSP. Confirmed pins:
MISO=40, MOSI=39, CLK=41, CS=38, mounted at /sd_card. Once mounted,
files are accessed via standard POSIX calls (fopen/fread/fwrite/fclose/
opendir/readdir/stat), not any Arduino-specific filesystem API.

New web page /sd: lists files (name + size) with download links, plus
an upload form. New endpoints:
- GET /sd/list - JSON directory listing (opendir/readdir, regular
  files only, with stat() for size)
- GET /sd/download?file=X - streams a file's contents via
  ESPAsyncWebServer's chunked response callback (fopen/fread per chunk,
  avoids loading the whole file into RAM - important for potentially
  large log files)
- POST /sd/upload - multipart file upload, writes directly to
  /sd_card/<filename> via fopen/fwrite as chunks arrive

sd_storage_init() called from setup(), before web_monitor_init() -
safe to call even with no card inserted (logs a failure message,
sd_storage_is_mounted() then returns false and /sd/list simply shows
an empty listing rather than crashing).

NOT yet bench-tested (no physical card tested yet) as of this writing.
This adds meaningful new code (ESP-IDF SD/VFS components, directory
listing, chunked upload/download handlers) on top of an already
93%-flash-used build - watch the next compile's storage percentage
closely.

## SD logging, gauge reordering, RPM special display (2026-09-30)

**SD logging**: new sd_logging.h/.cpp - always-on CSV logging from boot
(session-numbered file, same pattern as the WROOM board's initSdLogging/
updateSdLogging, adapted to POSIX file I/O matching this board's
ESP-IDF-native VFS mount instead of Arduino's SD.h/File class). Logs
every enabled gauge's live value every 500ms, header row uses each
slot's caption as the column name.

**Gauge reordering**: added gauge_config_swap_slots() and reorder
Up/Down buttons on the /gauges web page - since pages are built in
array order, swapping slot positions changes which page a gauge
appears on. Reordering happens client-side in the browser's local copy
and only takes effect on the board once Save is pressed (consistent
with the existing "reboot to apply" behavior for all config changes).

**RPM special display / general tick customization**: added
value_divisor, tick_count, and major_nth fields to GaugeSlotConfig -
value_divisor divides the live signal value before display (RPM
defaults to 1000, captioned "RPM x1000", with tick_count=9/major_nth=1
giving exactly 9 evenly-labeled ticks 1 through 9 on a 1-9 range,
avoiding the same custom-per-tick-label limitation we hit with the
boost/vacuum gauge earlier - since the underlying tracked VALUE is
pre-divided, the tick labels lv_meter auto-generates are already
correct without needing any override). tick_count/major_nth were
previously hardcoded (26/5) for every gauge; now configurable per slot,
applied consistently to both the dedicated gauge page and the
numbers-grid page.

## Confirmed bug fix via actual rusEFI DBC (2026-09-30)

User shared the real rusEFI DBC file, letting us cross-check every
currently-decoded signal's byte offset/scale/sign against the
authoritative source rather than our earlier best-effort assumptions.
Everything matched exactly (RPM, TPS, MAP, OilPress, BattVolt, Lam1/
AFR, FpLow, FlexPct) EXCEPT two fields with a real bug:

CoolantTemp and OilTemp were being read as `(int8_t)` (signed) when
the DBC explicitly specifies them as unsigned (`8@1+`, offset -40).
This produces correct results only below ~87C/189F raw value - above
that, the signed interpretation wraps negative, giving wildly wrong
readings right at normal/hot engine operating temperatures. Fixed by
reading as plain uint8_t (no cast needed, d[] is already uint8_t).

Confirmed DBC base IDs now also documented precisely: BASE0=0x200
(warnings/flags/gear/distance), BASE1=0x201 (RPM/ignition timing/inj
duty/ign duty/VSS/flex%), BASE2=0x202 (PPS/TPS1/TPS2/wastegate),
BASE3=0x203 (MAP/CLT/IAT/AUX1/AUX2/MCU temp/fuel level), BASE4=0x204
(OilPress/OilTemp/FuelTemp/BattVolt), BASE5=0x205 (cyl airmass/MAF/
InjPW/knock count), BASE6=0x206 (fuel used/flow/trims), BASE7=0x207
(Lam1/Lam2/FpLow/FpHigh), BASE8=0x208 (cam angles), BASE9=0x209 (EGT
x8), BASE10=0x20A (knock x8), BASE11=0x20B (brake pedal).

Confirmed via live /can monitor capture: 0x200-0x20A all present at
20Hz exactly as expected - wiring/reception/ID-range all validated
working correctly end to end.

## IMU/G-meter integration complete (2026-09-30)

Wired the full G-force meter into the gauge cluster:
- imu_reader.h/.cpp: calls qmi8658_init() once in setup() (AFTER
  Touch_Init(), since the IMU shares the same physical I2C bus as touch
  - Touch_Init() already installs that bus; calling I2C_master_Init()
  separately would conflict). Reads fresh data every 50ms via
  qmi8658_read_xyz(), exposing g_accel_lateral/g_accel_longitudinal.
- gmeter_widget.h/.c: the actual custom widget (cross + main dot + an
  8-step fading/shrinking trail of recent positions, pre-allocated
  once rather than created per-frame). Dot color: blue while
  accelerating, red while braking, gray near steady-state.
- gauge_app.c: G-meter added as one more page (after the numbers grid),
  using the same shared 80ms update timer and gesture-navigation
  wiring as every other page.

Resolved a real conflict discovered along the way: i2c_bsp.cpp
(Waveshare's confirmed IMU example's I2C bus layer) duplicated both
the I2C bus install (already done by Touch_Init()/FT3168.cpp) and the
exact function names FT3168.cpp already defines (I2C_writr_buff etc) -
would have caused both a driver-conflict crash and a linker "multiple
definition" error. Removed i2c_bsp.cpp entirely; kept i2c_bsp.h for the
declarations qmi8658c.cpp needs, confirmed its internal I2C calls use
the exact same function names/signatures FT3168.cpp already provides.

KNOWN UNVERIFIED ASSUMPTIONS (flagged for testing once the board is
physically mounted and the car is moving):
- Axis mapping (acc[0]=lateral, acc[1]=longitudinal) - depends on the
  board's actual mounting orientation, not independently confirmed
- PX_PER_G scale factor (150px/g) - a rough estimate, not calibrated
  against real measured G-forces
- Longitudinal sign convention (positive=accelerating=blue,
  negative=braking=red) - may need flipping if backward in practice

NOT yet bench-tested as of this writing - this is a substantial new
subsystem (IMU + custom widget), expect some iteration once tested.

## Touch swipe navigation disabled (2026-09-30)

Added ENABLE_TOUCH_SWIPE_NAV (0/1) as a single master toggle in
gauge_app.c - set to 0 per request, since this gauge cluster doesn't
need a touchscreen. BOOT button is now the only page-navigation
method.

Chose "disable" over "remove entirely": Touch_Init() is intentionally
still called in setup() - it shares the same physical I2C bus the IMU
depends on for the G-meter page (confirmed dependency from the earlier
IMU integration work), so removing touch initialization outright would
have broken the G-meter too. Disabling at the gesture-callback level
(a single early-return, rather than touching the 3 call sites that
attach it) avoids that risk entirely while fully satisfying "swipe does
nothing now" - easily reversible by flipping the define back to 1.
