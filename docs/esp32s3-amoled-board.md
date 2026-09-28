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
