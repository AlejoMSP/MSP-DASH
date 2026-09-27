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
