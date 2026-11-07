# MSP-DASH code guide

How the code fits together. Plain words, no history (see TIMELINE.md for that).

## 1. The system

```
 ECU --CAN--> HUB (ESP32 DevKitC, 192.168.55.204, esp32-input/)
               |  reads CAN, buttons, dimmer, lights wire
               |  ESP-NOW broadcast, channel 10
               v
   Screen .231 (Main, SD logging)   Screen .232 (Remote 1)   Screen .233 (Remote 2)
   all run esp32s3-amoled-bsp-base/ with role = remote
```

- **Hub** decodes CAN and sends one data packet 20 times a second with every signal.
- **Screens** draw gauges from that packet. If no packet arrives for 1 s, readouts show `--`.
- A screen can still be set to role **main** and read CAN itself (fallback if the hub dies).

## 2. Folders

| Folder | What it is |
|---|---|
| `esp32s3-amoled-bsp-base/` | Screen firmware (Waveshare ESP32-S3 AMOLED 1.43, 466x466, LVGL 8.4) |
| `esp32-input/` | Hub firmware (classic ESP32): CAN, buttons, brightness |
| `tests/host/` | Tests that run on a PC: C tests for gauge logic, JS tests for the web editors |
| `docs/` | Board notes, CAN map, rusEFI DBC, this guide, timeline |
| `tools/` | Helper scripts |

## 3. Screen firmware, file by file

**Startup and main loop**
- `esp32s3-amoled-bsp-base.ino` - `setup()` and `loop()`. Boot order: NVS settings, display, WiFi scan (skip join if home WiFi is absent), ESP-NOW, CAN (only if role main), web server, SD.
- `build_info.h` - build number, shown on the web pages and serial.

**Display**
- `lcd_bsp.c/.h`, `esp_lcd_sh8601.c/.h`, `lcd_config.h` - Waveshare driver for the SH8601 panel over QSPI, LVGL task, brightness (command 0x51).
- `lcd_perf.h` - fps and LVGL busy % counters.
- `FT3168.cpp/.h` - touch controller (swipe is off; I2C bus is shared).
- `read_lcd_id_bsp.c/.h` - panel ID check.

**Gauges**
- `gauge_app.c/.h` - builds each page out of cells (1/2/3/4 per page) and draws dials, half moons, numbers, trend graphs, indicator lamps. Holds the global signal values (`g_rpm`, `g_clt_f` ...) and their has-data flags.
- `gauge_config.cpp/.h` - the config model: pages, cells, signal list (`SignalId`), ranges, colors, warning zones, text sizes, decimals, peak hold, needle and text positions. Saved as JSON on the FAT partition.
- `gauge_config_json.h` - JSON read/write for that model (ArduinoJson 7).
- `gauge_style.h`, `gauge_fonts.h`, `font_*.c` - colors and fonts.
- `gauge_indicators.h` - on/off lamps (fan, check engine, rev limiter ...).
- `page_button.h` - BOOT button page switching.

**CAN**
- `can_parsing.h` - decodes rusEFI (0x200 range, little endian) and MegaSquirt/Link (0x5F0 range, big endian). Runs in its own FreeRTOS task, priority 3. Writes plain globals only, never LVGL. Also used by the hub.
- `can_flags.h` - ECU on/off flags into `g_flag_bits`.

**Link between boards**
- `espnow_link.cpp/.h` - role main/remote, data packet (`MSD1`), remote hello (`MSH1`), command packet from the hub (`MSC1`: page next/prev, brightness), timeouts.

**Web**
- `web_monitor.cpp/.h` - web server: front page (live list of the screen's own gauges), `/gauges` editor, `/settings`, `/status.json`, `/update` (OTA), net/name/diameter/role endpoints. The raw CAN monitor and the separate web dashboard editor were removed in build 160 (raw CAN lives on the hub).
- `custom_slots.h` / `custom_slots_screen.cpp` - the 16 custom sensor slots (SIG_C1..SIG_C16): shared CAN field decoder, MSN1 names packet; the screen keeps the names from the hub in NVS. Unused slots are hidden from every list and get no log column.
- `gauge_icons.h`, `font_icons_*.c`, `gauge_icon_count.h`, `gauge_icons_js.h` - caption icons (Material Design Icons as LVGL fonts, made by tools/icons/make_icons.py). A cell's `icon` / `icon_pos` (left of, above, instead of the caption); the icon takes the caption colour and flashes red/white with a warning.
- `gps_time.h` (shared) / `screen_clock.cpp` - date and time from the hub's GPS: the hub reads RMC time/date, applies the time zone (hub home page, Clock: standard offset + US daylight saving) and sends "MST1" every 10 s; screens keep it running from millis(). SD logs are named by it (`2026-10-26_14-32-05.csv`, else `log_N.csv`) and every row has a local `time` column.
- SIG_CLOCK (signal 44, after the custom slots): the local time from `screen_clock`, as minutes since midnight. A number gauge shows it as a clock, "decimals" = style (0 2:32 PM with AM/PM under it, 1 14:32, 2 14:32:05). The big digit fonts include ":" (regenerated, build 187). Not logged, not simulated.
- Hub OLED warning panel (hub 33): `oled_ssd1306.h` (own minimal I2C driver, GPIO 5 SDA / 15 SCL, 0x3C or 0x3D), `oled_panel.h` (host-tested: 8 lamps in a 4x2 grid, lit by a signal on/above/below or the seatbelt input on GPIO 2, blink, fixed or packed layout, clock + ALL OK / NO ECU DATA when nothing is lit), `oled_icons.h` (the same 79 icons as 32x32 1-bit plus an 8x8 font, made by tools/icons/make_oled_icons.py). Page /oled with a live copy of the display. Contrast follows the screens' brightness.
- `serial_log.h` - RT_LOG: runtime serial messages (heap, CAN stats, logging events, config saves) are off unless SERIAL_STREAM is 1; boot messages and answers to serial commands always print. The hub uses the same macro (can_parsing.h / esp32-input.ino).
- `web_theme.h` - the frame every page shares; its script puts the board's name next to MSP-DASH and in the tab title.
- `gauge_style.h` - `GAUGE_DEFAULT_RANGES`: the gauge template per signal (scale and warning) used when a signal is picked. Pressures are psi and MAP is boost psi, converted in `can_parsing.h` (build 161).
- `web_indicator_editor.h`, `web_theme.h` - indicator editor page and shared page style.

**Storage**
- `sd_storage.cpp/.h` - SD card mount and web file browser.
- `sd_logging.cpp/.h` - CSV logging with start conditions (key on, engine running, moving, or a sensor above/below a level) and a stop delay; a new file per session; never while simulating. Set on the Logging page (`/log-channels`, `/log-trigger`).
- `simulator.cpp/.h` - bench simulator (demo sweep or manual values). Stops when real CAN appears. RUNS ON THE HUB (copied there at compile time); on a screen only `sim_note_remote` / `sim_is_active` matter.

## 4. Hub firmware (`esp32-input/`)

- `esp32-input.ino` - everything for the hub:
  - Buttons on GPIO25 (next page) and GPIO26 (prev page), sent as command packets 3 times each.
  - Brightness: Auto (lights wire GPIO35, dimmer GPIO32, both through 47k/10k dividers) or Knob (pot on GPIO34). Settings saved in NVS `bri`.
  - CAN: TWAI on GPIO21 (TX) and GPIO22 (RX) through an SN65HVD230, 500 kbit/s.
  - Data task: builds the same `MSD1` packet the screens already understand and broadcasts it every 50 ms.
  - EGT: MAX6675 on GPIO18 (SCK), 19 (SO), 23 (CS), every 250 ms; sent on CAN as 0x7F0 for rusEFI.
  - Ambient temp + humidity: DHT22 on GPIO4, every 2.5 s; signals Ambient Temp and Humidity.
  - Fuel level: pot (mock sender) on GPIO36, 0-100 %, signal Fuel Level.
  - Web pages live in `hub_pages.h` (HOME_HTML, SENSORS_HTML, SIM_HTML), never as raw strings in the .ino: the Arduino builder mangled them ("'asyncfunction' does not name a type", hub build 18/19). A test checks the .ino has none.
  - Buttons (build 29 on): configurable on /buttons (BUTTONS_HTML): left / right / both x press, 1, 2, 3, 5, 10 s, each bound to an action in `button_actions.h` (shared). `btn_logic.h` (host-tested) gives level events while held (a "Release: ..." hint to the screens) and the release; the longest set binding at or below the reached level runs. Saved in NVS "btncfg". Race mode: 100 % brightness, all screens to the race page, logging forced on; again = back.
  - 433 MHz listen mode (build 23 on): CC1101 on GPIO 13/14/27/33/39 (`rf_cc1101.h`), edges timed by an interrupt on core 0, bursts gated by RSSI, decoded by `rf_decode.h`; page /rf. See docs/HUB_433MHZ.md.
  - Custom sensor slots (build 22 on): 16 slots in NVS (`s_slots`), set up on `/custom` (CUSTOM_HTML). `hub_custom_can_frame()` decodes them from every CAN frame (called by the tracker in web_monitor.h); `slotsSendNames()` sends MSN1 packets on change and every 10 s. Shared header `custom_slots.h` (copied in).
  - Speed (build 19 on): the hall sensor is wired to rusEFI; the hub reads speed from BASE1 byte 6 like any ECU value. GT-U7 GPS on Serial2 (GPIO 16/17) -> GPS Speed / GPS Satellites (`speed_sensors.h`, host-tested); the hub page compares the two. Frame 0x7F0 carries EGT only. See docs/HUB_SPEED_SENSORS.md and docs/RUSEFI_LUA.md.
  - Bench simulator (build 15 on): `simulator.cpp/.h` copied in from the screen folder at compile time. `/simulator` page; its data packets carry `LINK_MASK_SIMULATED` (bit 0 of `has_mask`), the screens show SIMULATED and the main screen does not log. The hub's own sensors stop writing their values while it runs.
- Web: `/` screens status (green/red) and brightness settings, `/simulator`, `/sensors` every signal with its source (hub sensor or ECU CAN ID), `/can` raw CAN monitor, `/update` OTA.
- `input_btn.h` - button struct (in a header because the Arduino builder moves prototypes above the sketch).
- `gauge_app.h`, `web_monitor.h` - small hub versions of the screen files so the shared decoder compiles here. (`simulator.h/.cpp` are the real ones, copied in.)
- `can_parsing.h`, `can_flags.h`, `gauge_config.h` - **not in the repo**; copied from the screen folder by the build command so there is only one decoder.

## 5. Packets (ESP-NOW broadcast)

| Magic | From | Contents |
|---|---|---|
| `MSD1` | hub (or a main screen) | seq, count, has-data mask, one float per signal |
| `MSH1` | remote screens | IP, build and name, every 2 s; the hub shows a green/red light per screen (red after 6 s silent) |
| `MSC1` | hub | seq, command, value, target name ("" = all) |

## 6. Saved settings (NVS)

| Namespace / key | Meaning |
|---|---|
| `link/remote` | role main or remote |
| `link/name` | board name |
| `net` | static IP or DHCP |
| `disp/diam` | screen diameter (default 520) |
| `disp/sweep`, `disp/demo` | startup sweep, demo sweep |
| `bri` (hub) | brightness mode and calibration |

## 7. How data flows, one frame

1. ECU sends a CAN frame.
2. Hub CAN task receives it, logs it for `/can`, decodes it into `g_*` values and marks the signal seen.
3. Hub data task (every 50 ms) packs all signals seen in the last 2 s into `MSD1`.
4. Each screen's ESP-NOW callback copies the values into its own `g_*` globals.
5. The screen's LVGL task redraws the gauges. The main screen also writes the SD log.

## 8. Rules learned the hard way

- Build number must equal the commit count.
- Run the host tests before every push.
- Every new web page must be listed in `tests/host/dump_pages.cpp`.
- New config fields must be set in `fillAll` in `test_gauge_config.cpp`.
- Hub flash settings: `FlashFreq=40,FlashMode=dio` or it boot-loops.
- Use `curl.exe -H "Expect:"` for OTA uploads.
- Do not name anything `F` in a sketch (Arduino macro).
