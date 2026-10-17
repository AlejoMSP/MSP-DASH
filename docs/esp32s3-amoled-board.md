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

## G-meter removed, per-gauge refresh rate, value display, bulk color apply (2026-09-30)

**G-meter removed entirely** (gmeter_widget.h/.c, imu_reader.h/.cpp,
qmi8658c.h/.cpp, i2c_bsp.h, and all wiring) - didn't work for a
vertically-mounted display per testing. Touch_Init() itself is left
untouched (still part of Waveshare's confirmed init sequence; was
already not doing anything functionally important for us beyond that
since swipe nav was already disabled).

**Per-gauge refresh rate**: new refresh_hz field per slot. Master LVGL
timer changed from 80ms to 20ms (50Hz) to support the fastest
requested tier (RPM/boost/pressure signals at 50Hz); slower gauges
(temps at 1Hz) self-throttle via a per-page last_update_ms/
refresh_interval_ms check. Only throttles once real data is flowing -
the sweep animation (while waiting for first CAN frame) always runs at
the full 20ms tick regardless of configured rate, so "waiting for
data" still looks smooth. Warning-flash blink-phase divisor adjusted
(6->24 ticks) to preserve the same real-world ~480ms blink speed after
the master rate change.

Default refresh rates applied: CLT/OilTemp=1Hz, MAP/AFR/OilPress/
FuelPress/RPM=50Hz, Ethanol%=5Hz - per request's examples (temps slow,
pressure/RPM/boost fast).

**Value display toggle + size**: new show_value (bool) and
value_font_size (14/28/36) fields per slot. When enabled, a precise
numeric label renders directly above the caption (which stays at the
bottom). Added get_font_for_size() helper that maps any requested size
to the nearest of the 3 actually-enabled fonts (14/28/36) - avoids ever
referencing an unenabled font object regardless of what gets typed into
the web config page.

**Bulk color apply**: new gauge_config_apply_colors_to_all() + POST
/gauges/apply-colors endpoint + a "apply to all" section at the top of
the /gauges web page (3 color pickers, one button) - saves configuring
every gauge's colors individually.

"Testing page" request interpreted as per-slot refresh_hz added to the
existing /gauges config UI (consistent with how every other per-gauge
setting already works) rather than a separate dedicated page - flagged
in case a genuinely separate page was wanted instead.

NOT yet bench-tested as of this writing.

## Caption positioning: 3/4 down, centered (2026-09-30)

Changed caption positioning from LV_ALIGN_BOTTOM_MID with a fixed -70px
offset to LV_ALIGN_TOP_MID with a +350px offset (350 = 75% of the
466px page height) - matches the circular gauge shape better per
request ("3/4 below top, center from left to right"). The value label
(when shown) is positioned relative to the caption, so it automatically
follows this change without needing a separate edit.

## Correction: FPS monitor position, not caption (2026-09-30)

Clarified: the earlier "3/4 down, centered" request was for the LVGL
FPS/perf monitor overlay, not the gauge caption - reverted the caption
back to its original LV_ALIGN_BOTTOM_MID/-70 position.

Confirmed from lv_refr.c source that the built-in perf monitor calls
lv_obj_align(perf_label, LV_USE_PERF_MONITOR_POS, 0, 0) - offset is
hardcoded to (0,0), meaning only LVGL's standard alignment presets are
available, not an arbitrary custom pixel position. Set
LV_USE_PERF_MONITOR_POS to LV_ALIGN_BOTTOM_MID (closest available
option to the requested "3/4 down, centered" - centered horizontally,
though anchored at the very bottom edge rather than exactly 75% down).
A fully custom-built FPS counter would be needed for exact positioning,
not pursued for now.

## OTA firmware updates enabled (2026-09-30)

Confirmed first: our current partition scheme (app3M_fat9M_16MB)
genuinely has dual OTA-capable app slots (app0/ota_0, app1/ota_1, each
3MB, plus the otadata tracking partition) - verified directly from the
actual partition CSV, not assumed from the scheme's name alone.

New /update web page + POST /update endpoint, using Arduino's built-in
Update library (no third-party OTA library needed) - same chunked
upload pattern already used successfully for SD file uploads
(ESPAsyncWebServer's upload callback), just targeting the OTA partition
via Update.begin()/write()/end() instead of a file. Uses
UPDATE_SIZE_UNKNOWN since the upload callback doesn't expose total
content-length directly - Update.h sizes itself from actual bytes
written. Automatically reboots (ESP.restart()) on successful update.

Upload page shows a progress bar (via XMLHttpRequest's upload.onprogress,
since fetch() doesn't expose upload progress) and warns not to close
the page or power off during upload.

To get the .bin file to upload: arduino-cli's compile output includes
the compiled binary - check the sketch's build cache directory (printed
near the end of a verbose compile, or findable via
"arduino-cli compile --fqbn ... --build-path <dir>" to force an
accessible output location) for a file ending in .ino.bin.

NOT yet bench-tested as of this writing.

## Page/cell redesign - Stage A: new data model (2026-09-30)

Rewrote gauge_config.h/.cpp entirely, replacing the old flat
"one-gauge-per-page" GaugeSlotConfig model with a nested
page/layout/cell model, matching the reference screenshots shared
(fixed layout templates, multi-cell pages, per-cell sensor assignment):

- LayoutType: LAYOUT_1CELL/2UP/3UP/2X2 (fixed set, not a fully
  arbitrary grid, per project decision to keep this simpler for now)
- GaugeType: NEEDLE/NUMBER/HALFMOON per cell (addresses "change gauge
  type" - previously each page was hardcoded to a full needle meter)
- CellConfig: essentially the full prior per-gauge config
  (signal/range/colors/warning/etc), now scoped to one cell instead of
  a whole page
- PageConfig: a chosen layout + up to MAX_CELLS_PER_PAGE (4) cells
- Page management: gauge_config_add_page/delete_page/empty_page/
  copy_page - matches the reference screenshot's page action menu
- Signal activity tracking: gauge_config_mark_signal_active() (called
  from CAN frame handlers whenever a signal updates) +
  gauge_config_signal_age_ms() - supports the sensor picker's
  "sort by activity" requirement from the reference screenshots (their
  ping-time column)

NVS persistence uses a NEW Preferences key ("gaugecfg2" instead of
"gaugecfg") since the old flat-slot JSON format is structurally
incompatible with this nested one - avoids misparsing old saved data
under the new schema. Defaults match the pre-redesign 7-gauge set
exactly, each as a LAYOUT_1CELL page with one cell, plus the
previously-reserved disabled RPM page.

IMPORTANT: this is Stage A of a 3-stage plan (data model only).
gauge_app.c (rendering) and web_monitor.cpp (web UI) still reference
the OLD GaugeSlotConfig-based API and have NOT been updated yet - this
will NOT compile as-is until Stage B (rendering rewrite) is done.

## Page/cell redesign - Stage B: rendering rewrite (2026-09-30)

Rewrote gauge_app.c entirely to build pages from the new PageConfig/
CellConfig model (Stage A). Key pieces:

- get_cell_rect(): returns the pixel rect for a given cell index within
  a given layout, on the 466x466 panel (1CELL=full screen, 2UP=stacked
  halves, 3UP=full-width top + split bottom, 2X2=four even quadrants)
- build_cell(): builds ONE cell's content inside its own lv_obj
  container, sized/positioned to its rect - all gauge content (meter,
  needle, labels) uses the same relative positioning (center/align)
  already used throughout this project, which naturally scopes to the
  container's bounds instead of the whole screen regardless of cell
  size
- Three gauge types per cell: NEEDLE (270-degree sweep, as before),
  HALFMOON (180-degree, rotation=180, brought back from the earlier
  full-page-only implementation), NUMBER (plain big digital readout,
  no meter widget at all)
- Per-cell warning flash now flashes just that CELL's own container
  background, not the whole page (meaningful now that multiple cells
  share one page)
- The old hardcoded "numbers grid" page is REMOVED - redundant now that
  a LAYOUT_2X2 page of GAUGE_TYPE_NUMBER cells achieves the same thing
  with full user control over which signals appear there

Added gauge_config_mark_signal_active() calls in the .ino, right after
every has_data=true assignment in both the rusEFI and MegaSquirt/LinkECU
handlers (20 call sites total) - wires up Stage A's activity-tracking
feature for the upcoming sensor-picker UI.

STILL NOT COMPILABLE: web_monitor.cpp (Stage C - the web UI) still
references the old flat GaugeSlotConfig-based API (gauge_config_get_slots,
MAX_GAUGE_SLOTS, gauge_config_swap_slots, gauge_config_apply_colors_to_all)
which no longer exists. Stage C needed before this builds.

## Page/cell redesign - Stage C: web UI rewrite (2026-09-30)

Completed the 3-stage redesign - web_monitor.cpp fully rewritten for
the new nested page/cell model, plus sd_logging.cpp updated (was still
using the old flat API, caught in a final sweep before considering this
done).

**New /gauges page**: page cards, each showing a layout dropdown
(1 Cell/2-Up/3-Up/2x2), per-page action buttons (Copy/Empty/Delete -
matching the reference screenshot's action menu), and a full editor
per cell (signal, gauge type, range, colors, divisor, tick config,
refresh rate, show-value+size, warning) - reusing the same fields from
the pre-redesign per-slot editor, now scoped per-cell. "+ Add Page"
button appends a new LAYOUT_1CELL page. All edits happen in the
browser's local copy; Save POSTs the whole structure at once (same
"edit locally, persist on Save" pattern used throughout).

**Sensor picker with activity**: new GET /signals.json returns every
signal's id/name/age_ms (via Stage A's gauge_config_signal_age_ms()).
Each cell's signal dropdown is sorted by activity (lowest age first =
most live) with alphabetical tiebreak, and each option is annotated
("(live)" / "Xs ago" / "(no data)"). Simplification: fetched once on
page load rather than continuously re-sorting, to avoid disrupting
in-progress edits - flagged as a reasonable first-pass choice.

**Bulk color apply**: reworked to be fully client-side (modifies the
local page/cell array, applies on Save) instead of a separate immediate
server endpoint - more consistent with how every other edit works now.
Removed the old /gauges/apply-colors endpoint entirely.

**Hamburger menu**: nav links (Raw CAN Monitor/Firmware Update/Gauge
Config/SD Card) now collapse behind a hamburger button instead of
always being visible on the main monitor page.

**Remote page navigation**: new g_page_nav_delta global (gauge_app.h/.c)
+ POST /page-nav endpoint + Prev/Next buttons on the main page - lets
you step through the physical display's pages remotely from the
browser, handled the same safe way as the existing BOOT-button
g_requested_page mechanism (checked/cleared inside the LVGL timer
callback, never called directly from the web server's own task context).

**sd_logging.cpp fix**: was still iterating the old flat
gauge_config_get_slots()/MAX_GAUGE_SLOTS API (missed in Stages A/B since
it's not part of the rendering or web-UI code) - caught via a final
grep sweep across every file. Updated to iterate every enabled cell
across every enabled page for both the CSV header and each logged row.

This completes the 3-stage page/cell redesign. NOT yet bench-tested as
of this writing - this was the largest architectural change in the
project's history, spanning 3 stages across the data model, rendering,
and web UI.

## Fixed silent save failure: undersized POST body buffer (2026-09-30)

Root cause of "deleting pages doesn't persist after reboot": the
/gauges POST handler buffered the incoming JSON body into a fixed
bodyBuf[8192] before parsing. With the page/cell model, a fully
populated payload (up to 8 pages x 4 cells x ~18 fields/cell) can
genuinely exceed 8KB once multi-cell layouts (2x2 etc) are in use. On
overflow, the buffer silently truncated, deserializeJson() failed on
the malformed result, and the handler returned without saving anything
at all - looked exactly like "my delete did nothing" because nothing
from that save attempt took effect, not just the delete specifically.

Fixed: increased buffer to 24576 bytes (comfortable margin above the
worst-case estimate). Also added explicit Serial logging - total body
size, a warning if size reaches the buffer limit, and the specific
parse error message on failure - so any future issue like this is
visible immediately instead of silently dropped.

## Independent web-dashboard config + phone-styled main page (2026-09-30)

Built a fully SEPARATE page/cell configuration for the web browser
dashboard, independent of the physical display's pages - same
CellConfig/PageConfig/LayoutType/GaugeType types (the data model was
already generic, not display-specific), just a parallel in-memory
array, NVS key ("webcfg2"), and set of load/save/page-management
functions (gauge_config_web_init/get_web_pages/web_save/
web_add_page/web_delete_page/web_empty_page/web_copy_page).

Default web dashboard: one page, 2x2 grid, plain-number cells showing
RPM/MAP/CLT/AFR - a simple glanceable phone view, independent of
whatever the physical display's 7 default gauges show.

**New /web-config page**: a direct adaptation of the /gauges editor
(same per-cell fields, same layout picker, same page actions) pointed
at new /web-pages.json (GET) and /web-pages (POST) endpoints instead
of the display ones - kept as a genuinely separate editor/template
rather than parameterizing one shared template, to avoid any risk of
cross-contaminating the already-working display editor.

**Main page (/) completely rewritten**:
- Phone-frame styling: content constrained to iPhone 15 Pro Max's CSS
  viewport (430x932 points - the relevant number for layout, not the
  raw pixel resolution). On screens >=500px wide (clearly not an
  actual phone), wraps the content in a phone-mockup frame (rounded
  corners, border, shadow, simple dynamic-island-style notch via a
  ::before pseudo-element) - on an actual phone's narrow viewport, no
  extra framing, content fills naturally.
- Single-page-at-a-time rendering (matches the reference app's own
  paradigm, and fits a phone's limited height better than stacking all
  pages), with Prev/Next buttons cycling through the web-dashboard's
  own configured pages (fully client-side, independent of the physical
  display's current page/BOOT button state).
- Dynamic grid rendering via CSS Grid, one class per layout
  (layout-1/2up/3up/2x2), matching the same 4 fixed layouts used
  throughout.
- Hamburger menu, now includes both config editors (Display Gauge
  Config -> /gauges, Web Dashboard Config -> /web-config).

**Generalized WebSocket broadcast**: replaced the old fixed
{"clt":...,"map":...,...} shape (7 hardcoded named fields) with a
generic {"<signal_id>": value, ...} object covering all 10 signals -
necessary since the web dashboard's cells can now be configured to show
ANY signal in ANY arrangement, not a fixed predetermined set.

SCOPE NOTE: cells currently render as plain text (caption + live
number) on the web dashboard regardless of configured gauge_type
(needle/half-moon/number) - true graphical needle/half-moon rendering
for the browser is part of the separate HTML5 live-preview feature
(requested, not yet built) - flagged clearly rather than attempting
both in one pass.

NOT yet bench-tested as of this writing.

## Project review: stale data, dead code, and .ino reorganization (2026-10-01)

Full review of the esp32s3-amoled-bsp-base sketch for errors/stale data,
requested as a checkpoint after the large page/cell redesign and
web-dashboard separation work. No actual compile-breaking errors found
- everything was consistently wired. Findings and fixes:

**Stale comment** (gauge_app.h): referenced "0 for boost/vacuum" as a
special hardcoded rest-value case - that design no longer exists (rest
value is now just a normal per-cell configurable field for every
signal equally). Corrected to describe the actual current behavior
(sweep animation while waiting for data).

**Dead code removed** (8 functions): gauge_config_add_page/
delete_page/empty_page/copy_page and their gauge_config_web_* 
counterparts - declared and defined but never called anywhere, since
the actual /gauges and /web-config editors implement add/delete/
empty/copy entirely client-side in JS (the whole page/cell array is
just re-POSTed on Save). Removed from both gauge_config.h and
gauge_config.cpp.

**Dead code removed**: ecu_config_name() - never called; the web page
hardcodes its own dropdown option labels instead of fetching names
from the board.

**Wording inconsistency fixed**: the CAN monitor page said "Back to
gauges" while SD/OTA pages said "Back to monitor" - standardized to
"Back to monitor" everywhere.

**Main .ino reorganized** (269 lines -> 96 lines): extracted into two
new header files, following the same "included only by the .ino, so
full function bodies are safe" pattern already used for the BSP's own
driver files:
- can_parsing.h: all CAN ID defines, byte-reading helpers (readU16LE/
  readS16LE/readU16BE), cToF(), and both ECU frame handlers
  (handleRusefiFrame/handleSecondaryEcuFrame)
- page_button.h: BOOT_BTN_PIN, button-debounce state, checkPageButton()

The .ino itself is now just: includes, the board's CAN pin/bitrate
config (kept here since it's wiring-specific to this physical board,
not protocol-parsing logic), and setup()/loop() orchestration.

## Stage 1 of ESP-NOW plan: histogram gauge, RPM-gated logging, log channels, logging indicator (2026-10-01)

First stage of the multi-board/ESP-NOW plan - everything that doesn't
depend on ESP-NOW or the new auxiliary boards existing yet.

**GAUGE_TYPE_HISTOGRAM**: new gauge type - a scrolling strip-chart/
trend line (oscilloscope-style), assignable to any signal like the
other gauge types. Uses LVGL's lv_chart widget (confirmed LV_USE_CHART
is enabled in lv_conf.h before using it): LV_CHART_TYPE_LINE, a 50-point
rolling window, range set from the cell's configured min/max, one
series in the cell's needle_color. Updated via lv_chart_set_next_value()
on the same per-cell refresh-rate cadence as every other gauge type.

**RPM-gated logging**: sd_logging.cpp rewritten - the log file still
opens once per boot as before, but rows are only written once RPM
exceeds 800 (engine running), rather than logging continuously from
power-on. Added sd_logging_is_active() so other code (the logging
indicator) can tell whether rows are actually being written right now.

**Log Channels**: a new, fully independent "which signals get logged"
list (gauge_config_log_init/is_log_enabled/set_log_enabled/log_save),
separate from what's displayed on any page/cell - you may want to log
more or fewer channels than you're looking at. Defaults to logging
everything. New /log-channels web page (simple checkbox list per
signal) + /log-channels.json GET + /log-channels POST, added to the
hamburger menu. sd_logging.cpp's header/row writing now iterates
signals directly (checking gauge_config_is_log_enabled) instead of
iterating display cells as before.

**Blinking logging indicator**: a small green circle created once as a
child of lv_layer_sys() (the same persistent overlay layer LVGL's own
built-in FPS/perf monitor uses, confirmed from source) - shows on top
of whichever page is active without needing to be added to each page
individually. Hidden by default; blinks (same ~480ms phase as the
warning flash) only while sd_logging_is_active() is true.

Still ahead: ESP-NOW sender/protocol on this board (Stage 2), the
web UI for pairing/configuring the 3 auxiliary boards, and the
auxiliary boards' own firmware (Stage 3) - which can reuse this
board's confirmed display/touch BSP files unchanged, since the
auxiliary hardware is the identical Waveshare ESP32-S3 1.43" AMOLED
board.

NOT yet bench-tested as of this writing.

## Fixed /gauges and /web-config "hanging" with no page cards (2026-10-01)

Root cause: the page's load() function fired /gauges.json (or
/web-pages.json) and /signals.json SIMULTANEOUSLY via Promise.all, with
no error handling at all. This board juggles CAN polling, LVGL
rendering, and the web server concurrently - under that load, one of
the two simultaneous requests occasionally stalled or failed, and the
whole load() silently threw, leaving renderPages() never called - the
page just sat there with the bulk-color section visible but nothing
below it, no error shown, looking like a hang.

Fixed in /gauges, /web-config, and /log-channels (same pattern existed
in all three): fetches are now sequential instead of concurrent
(reduces simultaneous connection pressure on the board's web server),
and load() is wrapped in try/catch - any failure now shows a visible
"Failed to load" message with a Retry button instead of failing
silently.

## CAN moved to its own FreeRTOS task (2026-10-06)

Prompted by reviewing rusEFI's uaDASH firmware, which receives CAN on a
dedicated task. Our loop() was doing CAN receive AND SD writes
(fopen/fprintf/fclose every 500ms) AND web server work. An SD write can
block for tens of ms, and the TWAI driver's default RX queue is only 5
frames - on a bus carrying several hundred frames/sec (0x190/0x191 at
~99Hz plus the rusEFI base frames) that is enough to drop frames.

Changes:
- can_parsing.h: new canReceiveTask() + can_start_task(). Priority 3
  (above loop() at 1 and the LVGL task at 2, confirmed from
  lcd_config.h), pinned to core 1 (where setup() runs and the TWAI
  interrupt is installed; WiFi lives on core 0), 4KB stack. Same rule
  as before: it only writes plain global values, never calls LVGL.
- twai config: rx_queue_len raised from 5 to 64.
- Diagnostics: every 2s the task checks twai_get_status_info() and
  prints a Serial warning if rx_missed_count increased. Should stay
  silent now.
- Bus-off recovery (not previously handled): if the controller enters
  TWAI_STATE_BUS_OFF the task calls twai_initiate_recovery(); if it is
  STOPPED it calls twai_start(). Without this a wiring glitch or power
  dip froze the gauges until reboot.
- loop(): CAN removed, now just checkPageButton/web_monitor_loop/
  sd_logging_loop plus a 5ms vTaskDelay (the old 20ms twai_receive
  timeout was what paced it).
- web_monitor.cpp: the /can frame table is now written from the CAN
  task, read by the web server task, and has its Hz recomputed from
  loop(), so access is behind a portMUX spinlock. buildCanJson() copies
  the table under the lock and builds the JSON from the copy. Without
  the lock the once-a-second Hz reset could race a frame increment and
  show a doubled rate on /can. Also clamps dlc to 8.

NOT yet bench-tested as of this writing.

## /gauges blank-page investigation: likely internal RAM exhaustion (2026-10-06)

Symptom: /gauges (and /web-config) intermittently loaded with the top of
the page but no page cards, and the buttons did nothing. A hard refresh
and a confirmed fresh flash did not fix it.

What was ruled out: every page's JavaScript was extracted from
web_monitor.cpp and checked with node - all 7 parse cleanly, and the
/gauges logic run against board-style JSON renders all 7 cards and
Add/Copy/Delete/layout-change all work. So the page code is correct.

Likely cause: the POST body buffers added in the last few rounds were
static arrays - bodyBuf[24576] + webBodyBuf[24576] + logChanBuf[2048] =
51,200 bytes of internal RAM held permanently (the earlier OTA-era build
reported 104,128 bytes of globals with the old 8KB buffer; now roughly
147KB of 327KB). The LVGL draw buffers take ~86KB more of internal DMA
RAM (2 x 466 x 46 x 2 bytes). That leaves very little heap for WiFi/TCP,
JSON and String building, which fits an intermittent failure. This is
inferred from sizes, not yet measured on the board.

Changes:
- The three POST buffers are now allocated once from PSRAM (8MB
  available) via allocPostBuf(), falling back to the normal heap if
  PSRAM is unavailable. Frees ~51KB of internal RAM.
- web_monitor_loop() prints a Serial line every 10s: internal free,
  largest free block, lowest-ever free, PSRAM free. If the cause is
  right, internal free should now be ~50KB higher than before and stay
  stable while loading /gauges.
- Config pages (/gauges, /web-config, /log-channels):
  * A "Page script has not started yet..." banner sits near the top and
    is cleared by the script's first line, so a truncated/stalled page
    now says so on screen. window.onerror shows any script error there.
  * The status line moved from a sticky bottom bar (which was easy to
    cut off) to just under the back link.
  * Save is refused until the list has actually loaded - previously a
    failed load left an empty list and Save would POST [] and wipe every
    page. Saving an intentionally empty list now asks for confirmation.
  * Save checks the board's response and shows "Save FAILED" instead of
    claiming success.

Next step is to flash and watch the Heap line on the Serial monitor
while reloading /gauges repeatedly.

## Lost-signal handling + full-screen alerts (2026-10-06)

Prompted by reviewing an older Teensy MegaSquirt gauge sketch. Two real
gaps in our code, plus two small bugs found on the way.

**1. Lost signals (bug fix).** The g_*_has_data flags were set on the
first frame and never cleared, so if CAN went quiet (key off, unplugged)
every gauge froze on its last value, and logging keyed to RPM > 800 (plus
the blinking dot) could keep running on a frozen RPM.
- gauge_config_signal_has_data() now also requires a frame within
  GAUGE_SIGNAL_STALE_MS (2000ms, uses the existing signal-age tracking).
  It is the only place has_data is read, so gauges, warnings and logging
  all pick it up.
- Number readouts and show_value labels show "--" with no data (they
  used to show 0.0). Needles use the existing "waiting" sweep. The trend
  graph holds its trace instead of plotting the sweep.
- Logging writes an empty CSV field for a signal with no live data
  (previously 0.000 or a frozen value).
- The web dashboard's WebSocket broadcast omits signals with no data, so
  the page shows "--".

**2. Full-screen alert (new, per-cell opt-in).** A cell's flashing
warning only showed on its own page. New CellConfig.alert_fullscreen
(default off, saved, checkbox on /gauges only): when that cell's warning
triggers, a red overlay on lv_layer_sys() shows the caption (+ HIGH/LOW),
the value, and "+N more" if several are active, over WHATEVER page is up.
- Only fires with the engine running (RPM has data and > 400): oil
  pressure reads ~0 at key-on and would otherwise take over the screen on
  every start.
- Stays up at least 2s so a value hovering on its threshold can't flicker.
- BOOT or a tap dismisses it and snoozes alerts for 30s, so a faulty
  sensor can never lock the screen.
- A lost signal is not an alarm.
- Optional: enable LV_FONT_MONTSERRAT_48 in lv_conf.h for a bigger value;
  falls back to 36 if it is off.

**3. BOOT button desync (bug fix).** page_button.h kept its own page
counter, so after using the web Prev/Next arrows the next BOOT press
jumped to the wrong page. It now just requests "next page"
(g_page_nav_delta) and gauge_app.c advances from the page actually
showing. That same request dismisses an alert.

**Testing (new: tests/host/).** The real gauge_app.c and gauge_config.cpp
were compiled unmodified on a PC (LVGL 8.4.0, real ArduinoJson, fake clock
and flash) and driven by scripted scenarios under AddressSanitizer/UBSan:
43 checks on the display side (alert gating, hold, dismiss/snooze, lost
signals, page wrap, trend graph, 5-minute random soak) and 17 on the
config side (timeout, millis wrap, save/reload of the new field, old
configs without the field). Deliberately breaking the code 6 ways was
caught by the tests each time (one weak test was found and fixed that
way). NOT yet run on the board.

## Trend graph rework: real history, resolution, fits the round panel (2026-10-06)

Found by rendering the real gauge_app.c on a PC and looking at it (see
tests/host/screenshot.c), after the Teensy sketch comparison.

Problems with the Stage 1 trend graph (GAUGE_TYPE_HISTOGRAM):
- **Window was ~1 second.** 50 points, one pushed per cell update, and
  MAP/AFR/pressures default to 50Hz. A 60s MAP sweep looked like a flat line.
- **Integer values.** A plain cast meant AFR 14.2 and 14.7 plotted on the
  same point (and a 10-20 range had only ~10 levels).
- **Square chart on a round panel.** Corners and trace ends fell off the glass.
- **Not selectable.** The /gauges and /web-config type dropdowns only listed
  Needle/Number/Half-Moon, so the type could not be chosen from the UI at all.

Changes:
- New per-cell settings: `history_sec` (2-600, default 30), `ref_enabled`,
  `ref_value` (saved, in the web JSON, old configs load as 30s / no line).
- The graph samples HIST_POINTS (100) times across history_sec, independent of
  refresh_hz (interval = history_sec*1000/100 ms; 2s -> 20ms, 600s -> 6s).
- Values are plotted as (value - min) * scale, scale = 10000/span (1..1000),
  with the chart range 0..span*scale: sub-unit moves stay visible and
  everything stays inside int16 whatever min/max are.
- fit_chart_rect(): finds the largest rectangle inside the cell whose four
  corners stay inside the circle (radius 225), leaving a caption band, with a
  preference for wide shapes (time axis). Runs once at build time. The chart's
  position is converted from absolute screen coords to the container's content
  area (padding + border).
- Optional flat reference line (e.g. 14.7 for AFR) as a second chart series,
  added first so the trace draws over it. (LVGL lists the NEWEST series first
  and draws the list back to front.)
- Dim grid, no per-sample dots, 3px trace. Caption shows the span ("MAP  30s",
  "FLEX  10 min") and sits directly under its own graph (it used to sit on the
  cell's bottom edge, where the round panel is narrowest and clipped it).
- Editor: "Trend Graph" added to both type dropdowns (index 3 = firmware enum);
  choosing it re-renders and shows History seconds / Reference line / value on
  /gauges. /web-config only lists the type (the web page still draws plain
  numbers for every type) and passes the new fields through unchanged.

Testing: 59 display checks (16 new: sample rate vs refresh rate, window fills,
resolution, reference line, caption span + clamping, all 10 graphs across the
four layouts and their captions fully inside the circle) and 25 config checks,
under AddressSanitizer/UBSan. Six deliberate breakages (sample at refresh rate,
no scaling, no circle fit, no reference line, no history clamp, old caption
position) were each caught. NOT yet run on the board.

Seen while rendering, not fixed: in small cells (2-up, quadrants) needle-gauge
tick labels at the default 28px size overlap each other and the needle. The
tick font shares the "Value Text Size" setting - choosing Small (14px) fixes
it per cell.

## Front page: build number, honest status line, auto-reconnect (2026-10-06)

Report: the front page showed "--" on every gauge and "won't update, no
matter what I do", and there was no way to tell which firmware was running.

**Build number.** New build_info.h: FW_BUILD_NUMBER = the git commit count of
the commit that contains it (`git rev-list --count HEAD`), bumped on every
push - so the number on the page can be checked against the PC. Shown on the
front page (top bar, next to the compile date/time from __DATE__/__TIME__),
on the /update page ("Running build N"), at boot on Serial, and at the start
of the 10s "Build N | Heap:" Serial line. Build 102 is this commit.

**What was actually wrong (two things, neither a broadcast bug):**
- The page opened its WebSocket once and never retried. One dropped
  connection (WiFi hiccup, phone asleep, board busy) left it on "--" forever.
  It now reconnects with backoff (1s, 2s, 4s, then every 5s), reopens when a
  phone tab becomes visible again, and drops a connection that is "open" but
  silent for 8s (the board sends every 500ms).
- "--" meant five different things. Since the lost-signal change, "no CAN
  data" also shows "--" (it used to show 0.0). The page now has a status line:
  green "Live - N signals - CAN X frames/s"; orange "Connected, but no CAN
  frames are arriving (check CAN wiring and ECU power)"; orange "CAN frames
  arriving (X/s) but none decoded - check the ECU type"; orange "No updates
  from the board for Ns"; red "Disconnected - retrying...". On disconnect the
  values go back to "--" instead of freezing on the last numbers.
- The broadcast message is now {"v":{signal values},"fps":N,"ids":M} - fps/ids
  are total CAN frames per second and distinct IDs (from the /can table), so
  the page can tell "no traffic" from "traffic not decoded". Sent every 500ms
  even when "v" is empty, so it is also the heartbeat.
- The page's two requests (/web-pages.json, then /status.json) now run one
  after the other, with retries if either fails.

**/status.json** (new): build, compile time, uptime, internal/PSRAM heap, WiFi
RSSI, WebSocket clients, CAN driver state (running / bus-off / recovering),
rx_queue, rx_missed, rx_overrun, bus_errors, frames/s, IDs seen, and
has_data + age for every signal. Open http://<board-ip>/status.json to see it.

**Verified (on a PC):** 28 checks of the page's script with a fake WebSocket
and a controllable clock; four deliberate breakages (no reconnect, no
watchdog, frozen values kept, build never retried) each caught. The real
ArduinoJson 7 produces the expected broadcast JSON. New tests/host/
typecheck.sh type-checks the .ino, web_monitor.cpp, sd_logging.cpp and
ecu_config.cpp against stand-in libraries - the first time those files (including
the CAN task and /status.json) have been checked before reaching the board; it
is not a real compile. NOT yet run on the board.

## ROOT CAUSE of "settings don't save": config moved from NVS to flash files (2026-10-06, build 103)

Symptom: /web-config "not saving changes"; earlier, deleted pages on /gauges
reappeared after a reboot (blamed then on a POST buffer - that was NOT the cause).

**Cause.** Both configurations were stored as ONE string in NVS via
Preferences::putString(). ESP-IDF's NVS rejects any string longer than
CHUNK_MAX_SIZE = 32 x (126-1) = 4000 bytes (nvs_page.cpp: ESP_ERR_NVS_VALUE_TOO_LONG).
gauge_config_save() wrote ALL 8 pages x 4 cells whether used or not - about
12,000 bytes - so every save was rejected. Arduino's putString only logs an
error and returns 0, and our code ignored the return value, so the web page
said "Saved" while nothing was stored; every edit reverted to the defaults on
reboot. This has been broken since the page/cell redesign (Stage A). Reproduced
on a PC with a fake NVS that enforces the 4000-byte limit: both configs lost
their edit after "reboot" (12,255 and 12,149 bytes). The old host test missed it
because its fake storage had no size limit.

**Fix.**
- Each config is a JSON file on the board's FAT flash partition (/ffat/
  cfg_display.json, cfg_web.json). The partition scheme already has a ~9.9MB
  "ffat" partition that nothing used. Mounted on first use with FFat.begin(true),
  which formats a blank partition (first boot only, a few seconds). Writes go to
  "<file>.tmp" then swap into place (FAT cannot rename over a file, so the old
  one is removed first); the loader falls back to the .tmp if a save was
  interrupted, and to the defaults if both are unreadable.
- Only enabled pages and the cells their layout uses are written (default
  display config 3,013 bytes, default web config 1,600, worst case 13,795).
- gauge_config_save()/web_save() now return bool and gauge_config_last_error()
  says why. The POST endpoints answer 200 only if the data was really written,
  otherwise 500 with the reason ("NOT saved to flash: ... The change is active
  until the next reboot."), and the editors show that text. /status.json has a
  "storage" section (mounted, last_error, saved sizes, flash_free).
- The web editor's success message is now accurate: /web-config needs no reboot
  (the front page reads the in-memory config), /gauges still needs one for the
  round display.
- Small log-channel flags, the ECU choice and the SD session counter stay in NVS
  (tiny values, never near the limit).

**Also fixed in this change.**
- JSON <-> config conversion now lives in ONE place (gauge_config_json.h,
  gauge_config.cpp). The four copies of the cell field list in web_monitor.cpp
  (two GET, two POST) are gone (about 240 lines removed), so a new setting can no
  longer be added to some places and forgotten in others. Replacing a config also
  now clears everything not in the new JSON (stale cells used to survive).
- Input is sanitized on load: a zero value divisor, a max not above min, zero
  tick count or major-tick spacing, zero refresh rate, out-of-range signal /
  gauge type / layout, and colors beyond 24 bits are repaired. This matters now
  that saves really persist: a bad value that crashed LVGL would otherwise
  reload at every boot (a boot loop until the flash is erased).
- Out-of-range enum values are range-checked before being stored in an enum
  (found by UBSan; storing one is undefined behavior in C++).

**Verified (PC):** 63 + 5 + 3 config checks (normal, partition-won't-mount,
unwritable folder) under ASan/UBSan, 5 editor-save checks, front page 28; five
deliberate breakages of the storage code each caught. Board-side type-check now
includes gauge_config.cpp. FFat.begin's signature (bool, const char*, uint8_t,
const char*) and freeBytes() were checked against arduino-esp32 3.3.x source.

**Not verified:** nothing has run on the board. One assumption I could not check
from here: that the Arduino core's prebuilt sdkconfig allows 2 FAT drives
(CONFIG_FATFS_VOLUME_COUNT, ESP-IDF default 2), which FFat + the SD card need
together. Check with:
  Select-String -Path "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esp32-arduino-libs\*\esp32s3\sdkconfig" -Pattern "FATFS_VOLUME_COUNT"
If SD logging stops mounting after this build, that is the first thing to look at.

NOTE: any settings the user believed were saved before this build were never
saved. After flashing, configure once more.

## Bench simulator (build 104)

Why: with the board unplugged from the ECU nothing has data, so the gauges, trend
graphs and full-screen alert could not be tested at a desk.

**What it is.** simulator.h/.cpp feed fake values into the same globals the CAN
decoder writes (units match the decoder: RPM, CLT/OIL TEMP in F, MAP/FUEL/OIL
pressure in kPa, AFR, ETH %, TPS %, battery V), ~20 updates a second, marking each
signal active so the normal 2s staleness logic applies. Two modes: **demo** (24s
rev cycle idle to 6200, temperatures warming over a few minutes; deliberately
stays clear of the DEFAULT warning thresholds so it never alarms by itself) and
**manual** (one slider per signal; use this to test alerts, e.g. oil pressure 20
with RPM 3000).

**Safety rules (each has a test that fails without it):**
- OFF at every boot; nothing is saved.
- Switched on only from http://<board>/simulator ("Bench Simulator" in the menu).
- Stops itself after 10 minutes without a change (touching a slider restarts it).
- Stops the instant ANY real CAN frame arrives (the CAN task calls
  sim_notify_real_frame()), and refuses to start while real frames are arriving
  (HTTP 409 with the reason). Real data always wins, so fake and real never mix.
- SD logging writes nothing while it runs and the blinking logging dot stays off.
- The round screen shows an amber SIMULATED pill on every page (layered above a
  full-screen alert), and the front page shows a SIMULATED DATA bar.
- Stopping clears the "has data" flags (gauges return to "--") and WIPES every
  trend graph; starting also wipes them, so fake and real traces never share a
  graph. The stop reason is shown on the page and in /status.json ("simulator").

**Also in this build**
- Trend graphs show "NO DATA" while their signal has none (hidden within a tick of
  data arriving, not on the graph's slow sample interval; returns on signal loss).
- BUG FIX: the green "logging" dot was positioned in the top-right corner of the
  square, entirely OFF the round glass, so it was never visible. Now top-centre,
  inside the circle, with the SIMULATED pill just below it. Tests check both lie
  fully inside the panel and do not overlap.
- Repo: removed stale tests/host/Arduino.h and Preferences.h (old stub copies the
  compiler picked up before tests/host/stubs). The board-side type-check caught a
  missing #include <math.h> (NAN) in web_monitor.cpp.

**Verified (PC):** simulator 42, SD-logging pause 9 (fails if the pause is removed),
display 76 (new: banner, dot geometry, NO DATA, trace clearing), end-to-end 25 (real
simulator + config + display with the default layout), simulator page 20, front page
31, editor save 5, config storage 71, board-side type-check on 6 files; every new
safety behavior was broken on purpose and caught. Screenshots reviewed (demo graph,
simulated alert, state after a real frame). NOT yet run on the board.

## Whole site in the iPhone frame (build 105)

Request: every web page should fit an iPhone screen like the front page, look like an iPhone
when opened on a desktop, and show NO borders when opened on an actual iPhone.

**How it works.** New web_theme.h holds ONE definition of the look; every page in web_monitor.cpp
is `PH_HEAD "<title>" PH_STYLE <page css> PH_OPEN <content> PH_CLOSE <script>`, joined by the
compiler (no extra web request, nothing to drift). It gives every page: a column at most 430 CSS px
wide (iPhone 15 Pro Max), a shared header (MSP-DASH link + hamburger) with the full menu (Monitor,
Display Gauge Config, Web Dashboard Config, Log Channels, Bench Simulator, Raw CAN Monitor, SD Card,
Firmware Update), and the old per-page "Back to monitor" links are gone.

**Desktop vs phone is decided by the device, not the window width.** The bezel + notch appears only
for `(hover:hover) and (pointer:fine) and (min-width:500px)` - i.e. a mouse and a wide window. The
old front-page rule was width-only, so an iPhone turned sideways (932px wide) got the bezel. Phones
and tablets (touch only) get the plain column.

**iPhone specifics.** Safe-area insets keep content clear of the notch and home bar; form fields are
16px so Safari does not zoom in when one is tapped; viewport-fit=cover plus the apple-mobile-web-app
meta tags let "Add to Home Screen" open it full-screen like an app; `100dvh` for the page height.

**Problems found while photographing the old pages in a real browser (headless Chrome):** only the
front page had the frame (the other 7 were full-width); the SD page was 570px wide on a 430px phone
(sideways scrolling); and the front page showed the bezel on a sideways iPhone. Also fixed: the
editors' "enabled" checkboxes were stretched full width above their labels, an empty gap sat under
each editor's title (two empty status lines), SD sizes split across lines ("153092 / 2"), and the
CAN table is now compact enough for 430px (header "Last seen (ms ago)" is now "Age (ms)").

**Verified.** headless Chrome (a Chromium shipped inside an npm package) launched twice - once with a
mouse (pointer:fine), once touch-only (pointer:coarse) - over all 8 pages x desktop / iPhone portrait /
iPhone sideways = 24 page views: bezel only on desktop (430px), none on any phone view, no page wider
than the screen, no form field under 16px, menu on every page, no script errors. Five deliberate
breakages (width-based frame rule, small form fields, unwrappable SD names, missing menu, no desktop
bezel) were each caught with a clear message. The real C++ compiler and the test page-extractor
produce byte-identical pages (all 8), the board-side type-check passes, and the three page-script
test suites (31 + 5 + 20) still pass. Screenshots reviewed. NOT yet seen on a real iPhone or the board.

New tools in tests/host: extract_page.js, mock_board.js, layout_check.mjs (needs `npm install` there),
check_pages_match_compiler.sh, dump_pages.cpp, package.json.

## Colour box on the two editors tidied (build 106)

The "apply colours to ALL cells" box on /gauges and /web-config had each label and swatch side by side,
so "Needle" fit on one line while "Tick/Dial" and "Background" wrapped to three different heights, and
"+ Add Page" sat 2px off the box edge. Now: title + one-line hint, three equal columns with the label
above its swatch, a full-width Apply to All button, and a full-width dashed "+ Add Page". layout_check.mjs
gained a rule (3 pickers, same width, same row, button spans the box) and was shown to fail when the grid
rule is removed. Checked in headless Chrome at 350px and 430px; Apply to All still reaches the first and
last cell.


## Text sizes, dial angles, Save & Reboot (build 107)

Request: (1) "display text size won't change, too small", (2) set the degrees where the gauge starts and ends.

**Why the text size seemed to do nothing - three causes.**
1. The FRONT PAGE (web dashboard) never read value_font_size at all: its numbers were a fixed 2.2em. The
   "Value Text Size" dropdown in Web Dashboard Config has been inert since it was written.
2. On the round display the setting only offered 14 / 28 / 36 px (a 466px panel), and on needle gauges it also
   resized the numbers AROUND the dial, which is why it was confusing (and why tick labels overlapped in small cells).
3. The display only applies a saved config after a reboot, and the page just said so in small print.

**What changed**
- Sizes are now 14, 28, 36, 48, 64, 80, 96, 120 px. 14/28/36 are LVGL's Montserrat; 48 and up are digit-only
  copies (font_digits_*.c, made by tools/gen_fonts.sh from LVGL's own Montserrat-Medium.ttf with lv_font_conv).
  They live in the sketch, so lv_conf.h does not have to be edited. They hold only space + - . and 0-9, so
  the readout code never feeds them "nan"/"inf" (shows "--" instead). Flash cost about 74 KB.
- A readout too big for its cell shrinks to the biggest size that fits: at most 94% of the cell width and 60%
  of its height, and all four corners of the worst-case text (the lowest/highest value formatted, e.g. "250.0")
  inside a 228px radius of the panel centre (the glass is round). A 1-cell page honours every size; a 2x2
  quadrant cannot hold 120 (it gets 48). Needle-dial readouts must fit the dial's lower area.
- New cell field tick_font_size (numbers around a needle dial): -1 hidden, 14, 28, 36; 0 (never set) follows the
  readout size, so configs saved before this look exactly as before.
- Front page: --fs = 1.25 x size px, capped at 26cqw (container query units) so it never overflows its cell;
  Medium (28) = 35px = the old look. Browsers without cqw keep the old fixed size.
- Display editor: size list, dial-number size, hint text, and a "Save & Reboot Board" button (saves, POSTs
  /reboot, waits for the board to go down and come back). POST /reboot requires an X-MSP header, so a page on
  another site cannot reboot the board (a cross-origin request with a custom header needs a CORS preflight,
  which the board never allows). The board restarts 800ms after answering.

**Dial angles.** New cell fields start_deg / end_deg, COMPASS degrees (0 = straight up, clockwise): the dial
sweeps clockwise from start to end. Standard needle 225 -> 135 (270 sweep), half moon 270 -> 90, e.g. 300 -> 60
is a 120 degree arc across the top. Maps to LVGL as rotation = (start + 270) % 360, angle_range = sweep.
Equal angles (also what an all-zero cell holds) or a sweep under 10 degrees fall back to the standard dial for
the type; values wrap into 0-359 (-135 = 225). A dial that stays in the top (or bottom) half of its circle is
moved down (up) by a sixth of its size so it sits in the middle of its cell; the standard dials come out exactly
as before. The editor draws a live picture of the dial (green dot = start, red = end) using the same rules
(dialGeometry mirrors gauge_dial_resolve in gauge_style.h; test_editor_dial.js checks the two agree).
gauge_style.h holds the shared pure-C helpers so the config code, display code and tests cannot disagree.

**Config:** unknown / bad values are repaired when loaded: value_font_size snaps to the nearest allowed size.

**Verified (PC, sanitizers on).** config 88; existing display suite 76; new display scenarios (sizes in all four
layouts, needle readout, dial numbers, 16 angle pairs x 2 gauge types, nan) all pass; each of 8 deliberate
breakages caught; editor 68 (+5 breakages caught); front page 36; editor save 5; simulator page 20; browser
layout check 24 page views incl. the front page's sizes; board-side type-check; compiler vs extractor identical.
Screenshots reviewed: front page at several sizes, the editor's dial controls and live preview, and the real
display code rendered in 8 configurations. NOT run on the board: the new fonts under the Arduino build, the
Save & Reboot button and the /reboot endpoint.


## Tick colour zones and needle length (build 108)

Request: (1) set a value where the tick marks turn yellow and a value where they turn red; (2) make the needle
shorter "back from both sides" (the sentence was cut off; read as: a gap at the centre AND a stop-short at the tip,
as two independent settings, so either can stay 0).

**Tick colours.** New cell fields yellow_enabled / yellow_value, red_enabled / red_value, zones_low. Ticks at or
above the value turn that colour (the HIGH end, e.g. coolant temperature); with zones_low, ticks at or below it
(the LOW end, e.g. oil pressure). Red wins where the two overlap. Values are in the dial's own units, between min
and max. Fixed colours: GAUGE_ZONE_YELLOW 0xFFD600, GAUGE_ZONE_RED 0xFF2A2A (gauge_style.h). Implemented with LVGL's
own `lv_meter_add_scale_lines`: it recolours ticks whose value is in [start, end] (both ends included) and a LATER
indicator overrides an earlier one, so yellow is added first, red second, the needle last. The numbers around the
dial keep the dial's normal colour (only the tick marks change). A value outside min..max just colours nothing /
everything; a zone's value is kept while its box is unticked.

**Needle length.** New fields needle_hub_pct (gap at the centre) and needle_tip_pct (how far short of its normal
tip it stops), each a percentage of the dial radius, 0-60, together at most 85 (the stock needle is about 94% of
the radius, so some needle is always left; if the sum is too big the hub gap wins and the tip gives way). 0 and 0 is
the normal needle and takes the normal LVGL path untouched. Otherwise a DRAW_PART_BEGIN callback moves the needle's
two end points just before LVGL draws it (the event hands out const pointers to the draw routine's own local
variables - hence the cast; p1 is re-pointed at a per-cell point rather than writing to the shared centre point,
which LVGL also uses for the hub). With a hub gap the grey hub dot (LVGL's LV_PART_INDICATOR rectangle) is made
transparent, because it would float on its own in the gap.

**Editor (display editor only; the web dashboard has no dial).** "Tick colors": high/low end, yellow and red
(tick box + value; first tick-on offers 75%/90% of the scale for a high-end zone, 25%/10% for low-end), and the two
needle boxes. The dial picture now shows the coloured ticks and the needle with its gap and stop-short, using the
same rules as the display (zoneOf, tickValue = LVGL's integer maths, resolveTrim = gauge_resolve_needle_trim).

**Verified (PC, sanitizers on).** config 105; zones 29 + 29 (LVGL indicator lists); zonepix 132 (colour of every
tick, measured in the rendered image); needlepix 40 (where the needle really starts and stops, hub dot hidden only
with a gap); editor 123; each of 8 display and 6 editor breakages caught; plus the earlier suites. Screenshots
reviewed: the real display in 8 configurations. NOT run on the board.


## ECU on/off flags and indicator pages (build 109)

Request: "this is how TunerStudio allows the manipulation of its gauges ... we should also add ability to create
indicators page." (The TunerStudio dialog is a reference for FUTURE gauge settings, see the gap list below; this build
adds the indicators page.)

**ECU flags as signals.** docs/rusefi.dbc (the official DBC) puts the status flags in BASE0 (id 0x200) byte 4, one bit
each starting at bit 0: RevLimAct, MainRelayAct, FuelPumpAct, CELAct, EGOHeatAct, LambdaProtectAct, Fan, Fan2 (DBC bits
32-39); BrakePedal is BASE11 (0x20B) byte 0 bit 0. can_flags.h decodes them (pure C); can_apply_status_flags() is what the
CAN task runs for every frame. They are 9 new SignalIds after SIG_BATTV (SIG_COUNT is now 20; the ORDER is the DBC bit order),
value 1 or 0, stored as bits in g_flag_bits / g_flag_seen. A frame that carries a flag as 0 is real data; only a flag that
no frame has carried (or not for 2 s, the usual staleness) is "no data". The log-channel default for flags is OFF, so existing
CSV columns do not change. The simulator makes them (demo: relay and pump on, rev limiter at the top of the rev cycle, fans
follow coolant, O2 heater for 20 s, brake tap every 10 s, check engine and lambda protect only in manual mode). /signals.json
has "flag": true for them and gauge cells' signal lists hide them.

**Indicator pages.** A page has `kind` 0 (gauge cells, as before) or 1 (lamps). Kind 1 holds `cols` (1-4) and up to 12 lamps:
signal, op (above/below), threshold, hysteresis, on/off text (printable ASCII, 11 chars), on/off background and text colours,
blink. A lamp is ON when value > threshold (above) or < threshold (below); once on it stays on until the value is `hysteresis`
back past the threshold; NO DATA, NaN or a NaN threshold = OFF (a lost sensor is not an alarm). ECU flags use "above 0.5" /
"below 0.5". The rule lives in gauge_indicators.h (C, display + tests) and indicatorNext() (JS, front page); both are tested
against the same cases. Layout: gauge_indicator_layout() puts lamps in a grid, 1.6:1, 8px gap, as large as possible with every
corner inside a 223px radius of the panel centre (6 lamps in 2 columns: 156x97; 12 in 3 columns: 106x66); a part-filled last row
is centred. Text uses the biggest of 36/28/14 that fits. A lamp keeps tracking its signal while its page is hidden (so hysteresis
memory is right). An empty page says NO INDICATORS. Config JSON for kind 1: `kind`, `cols`, `indicators[]` (no layout/cells);
old pages without `kind` are gauge pages. Worst case (8 pages x 12 lamps) is ~20KB, so CONFIG_POST_MAX is now 32KB.

**Editors.** web_indicator_editor.h is ONE raw string literal #included into both editors' scripts (the display editor and the
web dashboard editor), so they cannot differ (the page extractor expands the #include like the compiler; Arduino copies .h
files, which is why it is not .inc). "+ Add Indicators Page" starts with six lamps (check engine, oil, temperature, battery,
rev limiter, fan 1); 18 presets (8 analog thresholds, 9 ECU flags and a rev-limit-by-RPM); ON/OFF preview chips; move up/down,
remove, max 12. The editor's signal-number table is checked against gauge_config.h by a test.

**Web dashboard.** Lamps are drawn on the front page from the live values; flashing uses a 480ms page timer that redraws only
while the visible page has a flashing lamp; all text is HTML-escaped.

**Gaps vs the TunerStudio gauge dialog (not done).** Four thresholds at once (low critical / low warning / high warning /
high critical) drawn as colour BANDS behind the ticks (we have one side only, as coloured ticks); gauge title and units
printed on the face; decimal places; border size.

**Verified (PC, sanitizers on).** flags decoder 69 (incl. DBC contract); lamp logic 459; config 148; simulator 51; display
indicator scenarios; editor 109; front page 77; end to end 49; browser layout check 24 page views (lamp colours, escaped text,
sizes); deliberate breakages of the display, web page and editor were each caught (two weak tests were strengthened, one
change was a redundant safeguard with no visible effect). NOT run on the board.


## Warning zones, TunerStudio style (build 110)

Request: build the TunerStudio-style four-threshold zones drawn as coloured bands (from the TunerStudio gauge properties
dialog: Low Critical / Low Warning / High Warning / High Critical).

**What replaced what.** Build 108's one-sided "Tick colors" (one yellow and one red value, at the high OR the low end) is
replaced. A needle / half-moon cell now has FOUR thresholds, each with its own on/off switch and value, in the dial's units:
low critical (red) and low warning (yellow) colour the scale from its minimum up to the value; high warning (yellow) and high
critical (red) colour it from the value up to its maximum. Fields: low_crit_on / low_crit, low_warn_on / low_warn,
high_warn_on / high_warn, high_crit_on / high_crit, and zone_style (1 = coloured arc bands behind the ticks, 2 = coloured
tick marks, 3 = both; unset or invalid = 1). Colours are fixed (GAUGE_ZONE_YELLOW 0xFFD600, GAUGE_ZONE_RED 0xFF2A2A).

**Conversion.** A config saved by build 108 (keys yellow_enabled / yellow_value / red_enabled / red_value / zones_low, no
zone_style) is converted when loaded and looks the same: yellow becomes the warning threshold and red the critical one on the
side it was on, and the style becomes 2 (coloured tick marks, as it was). Saving writes only the new keys. If the new keys are
present the old ones are ignored.

**Drawing.** gauge_zone_ranges() (gauge_style.h, pure C) returns the ranges in DRAW ORDER: yellow low, yellow high, red low, red
high, clamped to the scale, an empty range left out. Bands are `lv_meter_add_arc` indicators (12 px wide, outer edge at the
dial's edge, square ends); LVGL draws arcs BEFORE the tick marks, so ticks sit on top of the bands, and a later indicator is
drawn over an earlier one, so red covers yellow where they overlap. Coloured ticks are the existing `lv_meter_add_scale_lines`
ranges (both ends included). A band with no width (a threshold at or beyond the end of the scale) draws nothing. The needle
is added after everything so it is on top.

**Editor.** "Warning zones": four rows (tick box + value), "Show as" (bands / tick marks / both), and the dial preview draws
the bands and the coloured ticks with the same rules (zoneRanges / zoneOf mirror gauge_zone_ranges; the editor tests run them
through the same hand-written cases as the display's pixel test). Switching a threshold on for the first time offers 10%,
25%, 75% and 90% of the scale (low critical, low warning, high warning, high critical) instead of 0.

**Still not done from the TunerStudio dialog:** gauge title and units printed on the face, decimal places, border size.

**Verified (PC, sanitizers on).** zone logic 20; config 161 (incl. conversion); display: LVGL indicator lists 52 + 52, tick
colours 132, rendered bands 17, needle trim 40; editor 139; each of 14 deliberate breakages of the display and the editor was
caught (two weak tests were strengthened); earlier suites unchanged. Screenshots reviewed: eight dial configurations. NOT run on
the board.


## Caption size, units, caption on the dial face, gauge position, default scales (build 111)

Requests: the title and units on the dial face (TunerStudio style); defaults of AFR 10-20, temperature 60-250, pressure 0-100;
choose where the gauge stands in its cell (Center / Top / Bottom); and "still no ability to change the caption text size".

**New cell fields** (display and web editors; JSON keys the same): `caption_font_size` (0 = never set, the display's default font,
exactly as captions always looked; else 14/20/28/36/48, snapped to the nearest), `caption_on_face` (needle / half moon, display
only), `units` (printable ASCII, 7 characters), `v_align` (0 centre, 1 top, 2 bottom). A config saved before these existed loads
and looks exactly as before. makeCell() now zeroes the struct first (it did not, so fields added later would have started as garbage).

**Fonts.** 14 / 28 / 36 are LVGL's; 20 and 48 are new full-ASCII fonts (`font_text_20.c`, `font_text_48.c`, about 50 KB of flash,
made by tools/gen_fonts.sh). A caption bigger than its cell shrinks to the biggest size that fits: no wider than 94% of the cell,
not too tall, and fully on the round glass (`pick_font_fit`, the same picker the readout uses, now with a top / bottom / centre anchor).
If even 14 does not fit (a long caption in a bottom corner cell) it stays 14, as it always was.

**Units.** A small grey line under the readout on a dial (only when Show value is on), and under the number on a number cell, in the
caption's size. Order from the bottom of a dial cell: caption, units, readout.

**Caption on the face.** The caption moves from the cell's bottom edge to the dial face, centred above the hub, 30% of the radius up,
sized to fit inside the ticks. The bottom stack is then just units and readout. (It is drawn over the needle: a child of the cell.)

**Gauge position.** For needle, half-moon and number cells (not trend graphs). Centre is exactly the old placement (existing dials do
not move). For a dial, Top puts the highest point of its picture (arc and hub) at the top of the cell and Bottom puts its lowest point
just above the caption / units / readout stack; a dial taller than the free space stays centred. So a half moon moves a lot (about
110 px in a one-cell page) and a 270 degree dial only a little (about 40 px up, 20 down), because it fills most of its cell. For a
number cell the whole block (caption, number, units) moves, and the number size is chosen for where it will stand. On the dashboard
Top / Bottom is `justify-content: flex-start / flex-end`.

**Trend graphs.** The caption size is applied; the graph leaves room for it (band = line height + 18, which is the old fixed 34 for
the default font) and the biggest size whose caption lies on the glass AND inside its cell wins (a caption is centred under the
graph, which is not always centred in a corner cell).

**Default scales** (`GAUGE_DEFAULT_RANGES` in gauge_style.h, one list): AFR 10-20, coolant and oil temperature 60-250, oil and fuel
pressure 0-100. They are used by the firmware's default pages (coolant and oil temperature were 65) and by both editors: choosing a
signal for a cell sets Min / Max and puts Rest at Min, replacing a custom scale (the quantity changed). MAP/boost is a pressure but
keeps 0-250: under boost it reads above 100 kPa and a 0-100 scale would peg. The editors keep the same table in JavaScript; a test
reads the C list and checks they agree. Saved configs are NOT changed.

**Dashboard.** The caption size (a fixed size with a fallback that shrinks it to the cell width), the units line, and Top / Bottom are
drawn from the same fields, so the settings are not inert there. Captions on the dashboard and in the editors are now HTML-escaped.

**Known limit (unchanged).** The readout is sized to fit the glass and the dial's width, not the gap between the dial's ends, so a
very big readout on a wide-sweep dial (300 degrees, 48 px) can touch the end of a zone band; pick a smaller size.

**Verified (PC, sanitizers on).** Every existing display suite is unchanged (defaults draw exactly as before); new display scenarios 388
checks; style helpers 13; config 182; editor cells 71; dashboard 87; browser layout check 24 views; 14 deliberate breakages of the
display, 10 of the editors and 6 of the dashboard were caught (7 tests were strengthened after breakages first slipped through; a
layout-check run also caught that cell padding changed the big-number sizes, which was fixed). NOT run on the board.

## Decimals in the readout (build 112)

Request: choose how many digits follow the point in a gauge's number (it was always one: "3000.0" RPM).

**New cell field** `decimals` (display and web editors; JSON key the same): 0, 1 or 2. A config saved before this existed, or an
unknown value, loads as 1, so existing gauges look exactly as before. It applies to the big number (number cells and needle /
half-moon dials with Show value), the full-screen alert's value, and the web dashboard. The readout size is still picked for the
widest text the cell can show, now formatted with its own decimals ("250.00" with 2), so a 2-decimal readout may come out one size
smaller. Warnings compare the real reading, not the rounded one (on the web dashboard they used to compare the 1-decimal text).
The dial numbers around a needle are not affected. Tests: `test_dial_text decimals`, `test_style_defaults` (4), `test_gauge_config`,
`test_editor_cells.js`, `test_front_page.js` (12c). Host test cell builders now set `decimals = 1` like makeCell() does.

## ESP-NOW multi-board link, phase 1 (build 117)

One firmware for every board (`espnow_link.cpp`). Role is saved on the board: `http://<ip>/role?set=remote` or
`?set=main` (default main), then it reboots. Main: reads CAN as before and broadcasts all signals plus a has-data mask
every 20 ms (no pairing). Remote: CAN is not started; received values are written to the same globals the CAN decoder
uses, so pages, web dashboard and SD logging work unchanged. No packet for 1 s: readings go back to "--". Channel: with
WiFi joined, ESP-NOW uses the network's channel (all boards on the same network match); with no network after the 20 s
boot try, WiFi stops searching and parks on channel 1. `/status.json` has a `link` block (role, packets/s, ms since last
packet, sender MAC, channel, own MAC); the front page build line shows it. Each remote keeps its own pages and editor.
Later: configure remotes from the main board; EGT and IAT signals. Build 120: sending moved to its own task (core 0, every 20 ms); in loop() it only reached 12/s while the screen redrew.

## Static IP per board (build 118)

`/net?ip=192.168.55.103&gw=192.168.55.1&mask=255.255.255.0` (`&dns=` optional, default = gateway) saves a static IP in NVS
(`net`) and reboots; `/net?dhcp=1` goes back to DHCP. ip and gw must share the subnet. Applied with `WiFi.config()` before
`WiFi.begin()`. `/status.json` shows `static_ip` and `ip`. Recovery if a board is unreachable: type `dhcp` + Enter in the
USB serial monitor. Survives OTA and USB reflashes (NVS is not erased).

## USB serial commands (build 119)

In the serial monitor (115200, Enter): `info` (build, role, IP, static/DHCP, MAC), `ip <ip> <gw> <mask> [dns]`,
`dhcp`, `role main`, `role remote`. Each change is saved and the board reboots. Same checks as `/net`.

## Remote list on the main board (build 121)

Each remote broadcasts a hello (`MSH1`: its IP and build) every 2 s over ESP-NOW. The main board keeps up to 4 and lists
them under the build line on its front page (heard in the last 10 s, sorted by IP), each a link to that board's `/gauges`
editor, with a note when a remote runs a different build. `/status.json` `link.boards`: mac, ip, build, age_ms.

## Board names (build 122)

Name saved in NVS (`link`/`name`, up to 20 characters; empty = "Main" or "Remote"). Set with `/name?set=Remote%201` or the
serial command `name Remote 1` (no reboot). Shown first on the board's build line and in its tab title, and sent in the
hello so the main board's remote list uses it.

## EGT and IAT, half moons that fill their cell, 2-Up default (build 124)

New analog signals EGT (20) and IAT (21), deg F, appended after the flags so numbers saved in configs keep their meaning
(`SIG_FLAG_END` now marks the end of the flags). IAT is read from rusEFI BASE3 (0x203) byte 3, unsigned, offset -40 like CLT
(to confirm against the DBC). EGT has no source yet: PLACEHOLDER ID `CAN_ID_EGT_PLACEHOLDER` 0x7F0, bytes 0-1 unsigned LE,
deg C (can_parsing.h). Default scales EGT 0-1800, IAT 0-200; both in the simulator and the ESP-NOW packet.

Half moons grow to the largest size whose arc and hub fit the cell's free area, its width and the round glass (never smaller
than before). Center then centres the arc + hub in the free area; Top / Bottom work as before. In the display editor,
choosing 2-Up turns cells that are not set up yet into Half-Moons.

## Performance numbers, ESP-NOW at 20/s (build 125)

The LVGL task (lcd_bsp.c) counts frames sent to the panel and the share of each second spent in `lv_timer_handler()`.
`/status.json` `fps` and `lvgl_busy`; the front page build line shows them. On-screen overlay: `/perf?on=1` / `?on=0` or the
serial command `perf on` / `perf off` (not saved). The main board now sends 20 ESP-NOW packets a second (was 50): gauges
refresh 10 times a second by default, and heavy ESP-NOW traffic costs the receivers CPU.

## Text alignment (build 126)

Cell field `h_align` (display editor "Text alignment"): 0 Center (default, layout unchanged), 1 Left, 2 Right. Applies to
the caption (not a caption on the dial face), the readout and the units. Each such label gets a fixed width spanning the
room its row really has (cell less the round glass at that height) and its text is aligned inside it, so a changing number
keeps its edge. Test: `test_dial_text halign`.

Build 127: alignment applies to the caption only ("Caption alignment"); the readout and units always stay centred. Also
fixed: a dial's readout was placed once for "--" and then grew to the right as digits appeared; it now re-centres.
