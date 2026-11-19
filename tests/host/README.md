# Host tests (run on a PC, no board needed)

These compile the REAL `gauge_app.c` and `gauge_config.cpp` from the sketch,
unmodified, on a normal Linux/WSL machine and drive them with fake data. They
caught real mistakes while the full-screen alert was being written.

- `test_gauge_app.c`: full-screen alert, "--" for no data, BOOT/page navigation,
  trend graphs (sample rate vs history window, value resolution, reference line,
  history limits, every layout fits inside the round panel). Needs LVGL v8.4.0 built for the host (an `lv_conf.h`
  with LV_COLOR_16_SWAP=1, LV_MEM_CUSTOM=1, Montserrat 14/28/36/48 enabled) and
  stubs for the 6 functions gauge_app.c expects from other files (they are
  defined at the top of the test).
- `test_gauge_config.cpp`: the real `gauge_config.cpp` with a fake clock, a temp
  folder standing in for the flash filesystem, and a fake NVS (`stubs/Preferences.h`)
  that enforces ESP-IDF's real rule that a stored string may not exceed 4000 bytes.
  Covers saving/reloading (default and worst-case 8x4 configs), crash recovery between
  the steps of a save, corrupt files, hostile values, and storage that will not mount.
  It exists because an earlier version stored the config as one NVS string, every save
  failed silently on the board, and an unlimited fake in the old test let it pass.

Build lines used (AddressSanitizer + UBSan on):

    gcc -O1 -g -fsanitize=address,undefined -I<lvgl> -I<sketch> \
        <sketch>/gauge_app.c <sketch>/font_digits_*.c test_gauge_app.c <lvgl objects> -o t -lm
    mkdir /tmp/cfgtest
    g++ -std=c++17 -O1 -g -fsanitize=address,undefined -Istubs -I<ArduinoJson>/src \
        -I<sketch> -DGAUGE_CFG_DIR='"/tmp/cfgtest"' <sketch>/gauge_config.cpp \
        test_gauge_config.cpp -o t2
    # failure paths: build again with -DGAUGE_CFG_DIR='"/nonexistent/dir"' and run
    #   ./t2_bad mountfail   and   ./t2_bad baddir

## Seeing what LVGL draws

`screenshot.c` renders the real `gauge_app.c` into a framebuffer and saves raw
RGB files (466x466x3). Convert with Pillow:

    from PIL import Image
    Image.frombytes('RGB', (466, 466), open('shot_map_page.rgb', 'rb').read()).save('x.png')

Build it like the gauge_app test, adding `-DHAVE_HISTORY`. It was used to look at
the alert overlay and the trend graph layouts, which found a caption clipped by
the round bezel that the numeric tests alone would not have shown.

## Type-checking the board-only code

`typecheck.sh` runs `g++ -fsyntax-only` over the `.ino`, `web_monitor.cpp`,
`sd_logging.cpp` against small stand-ins for the Arduino /
WiFi / AsyncWebServer / TWAI APIs (`stubs/`). It is not a real compile, but it
catches typos, wrong argument counts and misspelled fields in code that is
otherwise only compiled on the board. Deliberate typos in the build macro, a
TWAI field name, a helper's arguments and the CAN task were each caught.

    ARDUINOJSON=/path/to/ArduinoJson/src ./tests/host/typecheck.sh

`test_front_page.js` (run with `node`) pulls the front page's script straight out
of `web_monitor.cpp` and drives it with a fake WebSocket and a controllable clock:
status messages, reconnect backoff, the silence watchdog, recovery.

`test_editor_save.js` checks the /gauges Save button: success wording
for each page, and that a failed save shows the board's own explanation.

## Bench simulator tests

- `test_simulator.cpp`: the real `simulator.cpp` with a fake clock. Every safety rule has a
  test that fails when the rule is removed: off at boot, 10-minute timeout (restarted by
  touching a value), switches itself off on any real CAN frame (and a stray frame while off
  cannot kill the next run), manual values clamped, stopping clears the "has data" flags,
  and the demo never crosses the DEFAULT warning thresholds.
- `test_sim_logging.cpp`: the real `sd_logging.cpp` writing to a temp folder: each start condition (key on,
  running, moving, a sensor above/below a level), the stop delay, a new file per session and no empty files,
  nothing while the simulator runs, bad settings refused, settings kept across a reboot, no SD card.
  Build: `g++ -std=c++17 -Istubs -I<sketch> -DSD_MOUNT_POINT='"/tmp/sdtest"' <sketch>/sd_logging.cpp test_sim_logging.cpp`
- `test_logging_page.js`: the Logging page: start condition form, save, refusals, the channel ticks, alphabetical lists.
- `test_speed_sensors.cpp`: the hub GPS reader (checksums, RMC/GGA, junk, the 5 Hz command). Build: `g++ -std=c++17 -I../../esp32-input test_speed_sensors.cpp`
- `test_custom_slots.cpp`: custom slots: the CAN field decoder (byte orders, signed, 1 bit, short frames, bad definitions), packet sizes, and the screen keeping the hub's names (NVS, cut names, slots past 16). Build: `g++ -std=c++17 -Istubs -I<ArduinoJson>/src -I<sketch> -DGAUGE_CFG_DIR='"/tmp/cfgtest"' <sketch>/custom_slots_screen.cpp <sketch>/gauge_config.cpp <sketch>/screen_clock.cpp test_custom_slots.cpp`
- `test_hub_custom_page.js`: the hub's /custom page: list, editor, live preview (its decoder matches the firmware's), Save, refusals, "Make a sensor".
- `test_icons.c`: every gauge icon is really drawn at 14/20/28/36/48 px. Build: `gcc -DLV_CONF_INCLUDE_SIMPLE -I<lv_conf dir> -I<lvgl> -I<sketch> test_icons.c <sketch>/font_icons_*.c <lvgl>/src/font/lv_font.c <lvgl>/src/font/lv_font_fmt_txt.c <lvgl>/src/misc/lv_utils.c <lvgl>/src/misc/lv_mem.c <lvgl>/src/misc/lv_log.c <lvgl>/src/misc/lv_tlsf.c <lvgl>/src/misc/lv_gc.c`
- `test_gps_time.cpp`: GPS dates, US daylight saving (Eastern, Pacific), log-name text, RMC time fields, the hub keeping the time. Build: `g++ -std=c++17 -I../../esp32s3-amoled-bsp-base -I../../esp32-input test_gps_time.cpp`
- `test_hub_inputs.cpp`: what each GPIO can do, the pin map (defaults valid, moves, duplicates), inputs on pins, NO/NC, sensor presets (pressure, voltage, NTC, custom), expander rules, upgrading a pin map saved before hub 53, outputs (levels, hysteresis, relays, blink). Build: `g++ -std=c++17 -I../../esp32-input test_hub_inputs.cpp`
- `test_hub_inputs_page.js`: the Inputs and pins page: pin table, moving a function, inputs list, editor for buttons / switches / sensors.
- `test_hub_pins.js`: hub_pins.h vs docs/HUB_PINS.md (same pins and terminals), no GPIO or terminal twice, no raw pin numbers in hub files.
- `hub_typecheck.sh`: compile check of the whole hub sketch against stubs (stubs_hub/ + stubs/), with the shared headers copied in as the flash command does.
- `test_oled_panel.cpp`: the OLED warning panel: lit rules, grid, blink, idle clock, defaults vs icons.txt. Build: `g++ -std=c++17 -I../../esp32-input test_oled_panel.cpp`
- `test_hub_dashboard.js`: the Dashboard: tiles and colours, lit lamps, readings, screens, a failing request.
- `test_hub_home_page.js`: the Setup page (the old home page) and the side menu on every page.
- `test_hub_screen_page.js`: the hub /screen page (screen picker, tabs, frame, address) and /firmware (every board's build).
- `test_hub_oled_page.js`: the hub /oled page.
- `test_oled_pages.cpp`: OLED pages drawn (big number, values, G-meter, clock), lamp takeover, defaults. Build: `g++ -std=c++17 -I../../esp32-input test_oled_pages.cpp`
- `test_imu_math.cpp`: accelerometer mounting axes, signs, zero, smoothing. Build: `g++ -std=c++17 -I../../esp32-input test_imu_math.cpp`
- `test_shift_light.cpp`: the staged shift light: settings, levels for 1 and 3 screens, over-rev, hysteresis, each screen's colour from the level and the left-to-right list. Build: `g++ -std=c++17 -I../../esp32-input -I../../esp32s3-amoled-bsp-base test_shift_light.cpp`
- `test_hub_buttons.cpp`: hub buttons (hub_buttons.h): which pins fit, the resistor ladder match and too-close check, debounce, screen targets, the ECU maps and their pop-up label, moving the old two-button setup over. Build: `g++ -std=c++17 -I../../esp32-input -I../../esp32s3-amoled-bsp-base test_hub_buttons.cpp`
- `test_btn_logic.cpp`: the old button defaults (read once on hub 53) and the "Next gauge" screen order. Build: `g++ -std=c++17 -I../../esp32-input -I../../esp32s3-amoled-bsp-base test_btn_logic.cpp`
- `hub_page.js`: helper that rebuilds a hub page from its raw-string pieces and the HUB_NAV macros.
- `test_hub_home_page.js`: the hub home page: screens in Left/Center/Right order with links to their pages, status grid, nav bar on every hub page.
- `test_site_design.js`: one design for the whole site: every hub page and screen page has the side menu, menu button, shared base look and iPhone-safe viewport, no iPhone bezel; screen pages hide their menu inside the hub; the Dashboard's quick controls and Add to Home Screen.
- `test_hub_buttons_page.js`: the hub's /buttons page: the list, single and ladder editors (used pins greyed out, Learn and its too-close warning), screen choice for page actions, action boxes, refusals, the ECU maps card.
- `test_rf_decode.cpp`: the hub's 433 MHz decoding: half-bit time, Manchester bits and bytes, jitter, both polarities, junk, and a real U912 tire sensor capture (fixtures/u912_burst1.txt; run from tests/host). Build: `g++ -std=c++17 -I../../esp32-input test_rf_decode.cpp`
- `test_hub_ecu.cpp`: the hub names the ECU from the CAN IDs it hears (rusEFI, MegaSquirt / Link, both, none). Build: `g++ -std=c++17 -Istubs -I../../esp32-input test_hub_ecu.cpp`
- `test_can_vss.cpp`: speed from rusEFI BASE1 byte 6, kph to mph, through the real `can_parsing.h`. Build: `g++ -std=c++17 -Istubs -I../../esp32-input -I../../esp32s3-amoled-bsp-base test_can_vss.cpp`
- `test_hub_sensors_page.js`: the hub's /sensors page: alphabetical, and CAN frames no sensor reads listed by themselves.
- `test_sim_page.js`: the /simulator page's script - modes, countdown, one slider request at
  a time (newest value wins), the board's refusal text shown. Set `WEB_MONITOR_CPP` to test a
  modified copy of web_monitor.cpp.
- `test_integration.cpp`: end to end. The real simulator feeds the real config code (defaults,
  2-second staleness) which feeds the real display code, for simulated minutes, with the
  default page layout: demo shows a rev cycle and never alarms, manual oil pressure 20 raises
  the full-screen alert under the SIMULATED banner, and a real CAN frame returns everything to
  "no data". Link `gauge_app.o`, `gauge_config.o`, `simulator.o`, the font objects (`font_digits_*.c`), the test and the LVGL objects.

## Looking at the web pages in a real browser

Every page is built from `web_theme.h` (the iPhone-shaped column, header and menu, shared by all pages)
plus its own content in `web_monitor.cpp`. The compiler joins the pieces, so nothing extra is requested
over WiFi.

- `extract_page.js` assembles a page exactly as the board serves it
  (`node extract_page.js SD_HTML > sd.html`). The page tests use it. `SKETCH_DIR=<copy>` points it at a
  modified copy of the sketch folder.
- `check_pages_match_compiler.sh` compiles `web_monitor.cpp` with the stand-in libraries, makes the real
  compiler print each page, and checks `extract_page.js` produced the same bytes.
- `mock_board.js` is a stand-in for the board's web server with believable data, including the live
  WebSocket. `node mock_board.js 8080`, then open http://localhost:8080/ (needs `npm install` here first).
- `layout_check.mjs` opens every page in headless Chrome as a DESKTOP (mouse: `pointer:fine`) and as an
  iPHONE (touch only: `pointer:coarse`, portrait and sideways), saves screenshots, and fails if: desktop does
  not show the 430px bezel; a phone shows a bezel at all; anything is wider than the screen; a form field is
  under 16px (Safari zooms in on it); the shared header/menu is missing; or a script errors. Needs
  `npm install` in this folder (puppeteer-core, ws, and @sparticuz/chromium, which ships its own headless
  Chromium; or set CHROME_PATH). Run it with `node layout_check.mjs [screenshot-folder]`.

## Text sizes, dial angles and Save & Reboot

All of these need the big digit fonts linked in: add `<sketch>/font_digits_*.c` to the gcc line (the display
code uses them for readouts of 48 px and up).

- `test_dial_text.c`: the real `gauge_app.c` against LVGL, one scenario per run
  (`./t num1 | num2up | num3up | num2x2 | needleval | needleval4 | ticks | angles | angleshalf | nan | decimals`).
  Number readouts at all eight sizes in every layout: never bigger than asked, the worst-case reading
  (`250.0`) lies fully inside the round glass and its cell, the biggest size that fits is the one used
  (the next one up is checked to really not fit), a 1-cell page honours every size, the caption is not cut
  off. Needle dials: the readout fits the dial's lower area; the dial numbers have their own size
  (hidden / 14 / 28 / 36, and 0 = follow the readout as old configs do). Dial angles: sixteen start/end pairs
  (including wrap-around, a 5 degree sliver, equal angles, negative and over-360 values) checked against a
  hand-written table of the LVGL rotation, sweep and vertical shift; every pair is also drawn with moving
  data (a bad angle must not crash LVGL). `nan`: a reading that is not a number shows `--`, never "nan"
  (the digit-only fonts cannot draw letters). `decimals`: 0 / 1 / 2 digits after the point (unknown = 1), and "250.00" still fits the glass.
- `test_editor_dial.js`: the display editor's script: the dial-geometry table (the preview must equal the
  display's rules), which controls appear for each gauge type, typing an angle redraws only the picture,
  Needle/Half-Moon switching, and every path of Save & Reboot (save first and only then reboot, the
  X-MSP header, waits for the board to go down AND come back, reports a failed save, a failed reboot request,
  a board that never restarts, and one that never returns). Uses a fake clock, so it runs instantly.
- `test_front_page.js` (section 12): the front page draws each number at 1.25 x its Value Text Size.
- `layout_check.mjs` also measures the front page's numbers in a real browser (size honoured, capped at 26%
  of the cell width, never wider than the cell) and the editors' colour box.
- `tools/gen_fonts.sh` (repo root) regenerates `font_digits_*.c`.

## Tick colour zones and needle length

- `test_zones_needle.c`: the real `gauge_app.c` against LVGL, one scenario per run
  (`./t zones | zoneshalf | zonepix | needlepix`; link the font objects like the other display tests).
  `zones` / `zoneshalf` read LVGL's own indicator list: which tick-colour ranges exist, from/to which value,
  in which colour, and that YELLOW is added BEFORE red (LVGL draws the later one on top, so red wins where
  they overlap) and that the needle is the newest indicator (drawn over everything). `zonepix` renders the
  dial and measures the REAL colour of every tick along its ray (classified by colour proportions, because a
  thin tick is an anti-aliased, darker blend): six zone configurations x 11 ticks, including a threshold that
  falls between two ticks, a threshold exactly on a tick, low-end zones, and yellow above red. `needlepix`
  renders a bright green needle and measures where it really starts and stops, for no trim, hub gap only, tip
  only, both, the maximum, the over-limit case, and a small 2x2 dial; it also checks the grey hub dot is hidden
  exactly when there is a gap. The needle's untrimmed length is measured from the picture itself, so the
  expected values scale with the dial's real radius.
- `test_editor_dial.js` (sections 9-13): the preview colours exactly the same ticks as the display (the same
  hand-written cases as `zonepix`), the controls appear only for needle / half-moon cells, wording for the
  high / low end, switching a zone on offers a value inside the scale, the preview needle's start and end, the
  85% trim limit, and that all seven new settings are posted when saving.

## ECU flags and indicator pages

- `test_can_flags.c` (run from the repo root: `./t [docs/rusefi.dbc]`): the decoder for the ECU's 9 on/off flags
  (`can_flags.h`). Hand-written frames (each flag alone, all on, all off, other bytes ignored, wrong id, short frame)
  AND a CONTRACT test that reads the official DBC and checks the flag order and bit positions we decode.
- `test_indicators.c`: the pure-C lamp logic (`gauge_indicators.h`): the on/off rule (above/below, hysteresis band,
  no data, NaN, ECU flags) and the grid layout for every lamp count 1-12 and 1-6 columns (every corner on the round
  glass, no overlaps, centred rows, as large as possible).
- `test_indicator_page.c`: the real `gauge_app.c` against LVGL, one scenario per run
  (`./t layout | states | pixels | nav`; link the font objects like the other display tests): lamps where the layout says,
  text fits, live states incl. hysteresis / no data / flags / flashing, rendered pixels, page cycle, lamps tracking
  while their page is hidden, and a full-screen alert still working over an indicator page.
- `test_editor_indicators.js`: the shared indicators editor (`web_indicator_editor.h`) in BOTH editors that include it:
  presets, adding/removing/reordering (12 max), flag vs analog controls, HTML escaping, copy/empty/delete, saving, and a
  contract test that the editor's signal numbers equal the `SignalId` order in `gauge_config.h`.
- `test_front_page.js` (sections 13-16): the dashboard's lamp rule runs the SAME cases as `test_indicators.c`; live
  states, escaping, flashing driven by the page timer, hidden-page tracking, columns, empty page.
- `test_integration.cpp` (E5-E7): the real simulator and real DBC-layout CAN frames (through `can_apply_status_flags`)
  drive an indicators page; "off" differs from "no data", and silence turns every lamp dark.
- `layout_check.mjs` also checks, in a real browser, that each lamp on the front page has the colour and text the live
  mock data calls for, and that the editors show every lamp card with its ON and OFF chips.

## Warning zones (TunerStudio style)

Four thresholds per needle / half-moon dial (low critical = red, low warning = yellow, high warning = yellow, high critical =
red), each switched on by itself, drawn as coloured arc BANDS behind the tick marks, as coloured TICK MARKS, or both.

- `test_zones.c`: the pure-C range logic in `gauge_style.h` (`gauge_zone_ranges`): each threshold alone, all four in DRAW
  order (yellow first, red last, so red covers yellow), thresholds outside the scale clamped to it, scales that do not start
  at zero, min = max, and the style setting (unset or invalid means bands).
- `test_zones_needle.c` (`./t zones | zoneshalf | zonepix | bandpix | needlepix`): the real `gauge_app.c` against LVGL.
  `zones`/`zoneshalf` read LVGL's indicator lists for eight cases (the arcs and the coloured-tick ranges, colours, order,
  band width, no zero-width bands, a case with the style left unset, the needle newest). `zonepix` measures the colour of every
  tick in the rendered image (coloured-tick style). `bandpix` measures the bands in the image: the colour at 14 values (just
  inside and outside each threshold, between ticks), the band's thickness, that red covers yellow only where they overlap, and
  that a tick inside a band is still drawn ON TOP of it.
- `test_editor_dial.js` (sections 9-11): the preview colours the same ticks as `zonepix` (the same hand-written cases),
  draws bands in the right order and at the right angles, and the four threshold rows are each wired to their own threshold.
- `test_gauge_config.cpp` (S12, S12b): the new fields, and conversion of build 108's one-sided yellow/red zones.

## Caption size, units, title on the dial face, gauge position, default scales

- `test_board_name.js`: every page shows the board's name next to MSP-DASH and at the front of the tab title.
- `test_style_defaults.c`: the pure-C helpers in `gauge_style.h`: the gauge template per signal (scale and warning; AFR 10-20, temperatures 40-300,
  pressures 0-100; MAP/boost and everything else have none), caption sizes (snap to the nearest of 14/20/28/36/48, ties go
  smaller, 0 = never set), and the vertical position (centre / top / bottom).
- `test_caption_layout.c` (`./t capnum | capdial | caphist | units | face | valign | valignnum`): the real `gauge_app.c` against
  LVGL. Caption sizes (no bigger than asked, the BIGGEST that fits, inside the cell and on the round glass, in 1-, 2- and 4-cell
  pages, for number cells, dials and trend graphs, including a trend graph leaving room for a bigger caption); units under the
  readout / under a number and not shown when the readout is off; the caption ON the dial face (centred over the hub, 30% of the
  radius above it, inside the ticks, the readout then at the bottom); the dial's Top / Bottom placement measured from its real
  position (Top: highest point at the top of the cell; Bottom: lowest point just above the caption/readout stack; an arc that
  sits above its hub; a half moon moves a lot, a 270 degree dial a little) and number cells' Top / Bottom.
- `test_editor_layout.js`: editor step 4: layout picture and cell choice, folding groups, pinned preview, copy a cell (this screen, another screen), copy a page, restart button.
- `test_editor_cells.js`: both editors. CONTRACT: the editor's default scales and caption sizes are read from `gauge_style.h` and
  must match. Choosing a signal sets its scale (and not for MAP/RPM/ethanol/throttle/battery), the controls per gauge type, the
  face checkbox only in the display editor, HTML escaping, new-cell defaults, saving.
- `test_front_page.js` (12b): the dashboard's caption size (unknown sizes ignored), units line order and escaping, Top / Bottom.
- `test_gauge_config.cpp` (S19): the four new fields, snapping and cutting of bad input, and the new default scales
  (the default coolant gauge is 60-250, not 65).
