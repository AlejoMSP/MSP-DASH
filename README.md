# MSP-DASH

Custom digital gauge cluster for a turbo Miata. Two active firmware
targets, sharing this repo:

## esp32-main/
Production firmware for a classic ESP32 (WROOM) board, driving a
Nextion 5" HMI display and a web dashboard over WiFi.
- ECUs supported: rusEFI (bench-confirmed, full DBC decoded - 74
  gauges), MegaSquirt/LinkECU (shared parser, selectable on /config)
- CAN via TWAI driver, 500kbps
- Interactive web dashboard: drag-reorder, right-click gauge type
  change, close/hide gauges, appearance theming, SD card CSV logging,
  OTA firmware updates
- See `docs/CAN-map.md` for full protocol/signal history and
  confirmation status per ECU

## esp32s3-amoled-bsp-base/
Current firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.43 round
display (466x466, SH8601), reading rusEFI CAN data directly (own
transceiver, no dependency on the WROOM board).
- LVGL v8 on Waveshare's BSP: up to 8 pages, each a 1-cell, 2-up, 3-up
  or 2x2 layout of needle, number, half-moon or trend-graph gauges, or
  a page of indicator lamps. BOOT button changes page.
- Web editor and web dashboard over WiFi, OTA updates, RPM-gated SD
  logging, bench simulator
- Host tests (no board needed) in `tests/host/`
- See `docs/esp32s3-amoled-board.md` for the full build history

Early bring-up sketches (serial echo, touch test, first LVGL CAN gauge)
were removed in build 114. They are still in git history.

## Getting started
New machine setup (arduino-cli, board packages, libraries, lv_conf.h) -
see `PREREQUISITES.md`.

## docs/
Full build history, protocol confirmation status, and hardware bring-up
notes for both boards. Worth reading before making changes - a lot of
"why this instead of the obvious approach" context lives there
(driver chip identification process, QSPI clock tuning, LVGL API
findings, pin-accessibility discoveries, etc).

## Status
Both firmwares compile and run. rusEFI on the WROOM board is
bench-confirmed. For the AMOLED board's latest state, see the newest
sections of docs/esp32s3-amoled-board.md.
