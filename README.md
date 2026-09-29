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

## esp32s3-amoled-lvgl-can-gauge/
Current firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.43 round
display, reading rusEFI CAN data directly (own transceiver, no
dependency on the WROOM board).
- LVGL v9-based gauge (lv_scale + needle) - see
  `docs/esp32s3-amoled-board.md` for full display bring-up history
  (driver identification, QSPI tuning, tearing fixes, board options)
  and CAN wiring notes
- Currently configured as a boost gauge

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
bench-confirmed (RPM/MAP/TPS1/BattVolt/MCUTemp verified against real
hardware). The AMOLED board's CAN reception is still being debugged as
of this writing (display/LVGL pipeline confirmed working, CAN frame
reception not yet confirmed arriving - see docs/esp32s3-amoled-board.md
for the latest troubleshooting state).
