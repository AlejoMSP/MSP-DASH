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
        <sketch>/gauge_app.c test_gauge_app.c <lvgl objects> -o t -lm
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
`sd_logging.cpp` and `ecu_config.cpp` against small stand-ins for the Arduino /
WiFi / AsyncWebServer / TWAI APIs (`stubs/`). It is not a real compile, but it
catches typos, wrong argument counts and misspelled fields in code that is
otherwise only compiled on the board. Deliberate typos in the build macro, a
TWAI field name, a helper's arguments and the CAN task were each caught.

    ARDUINOJSON=/path/to/ArduinoJson/src ./tests/host/typecheck.sh

`test_front_page.js` (run with `node`) pulls the front page's script straight out
of `web_monitor.cpp` and drives it with a fake WebSocket and a controllable clock:
status messages, reconnect backoff, the silence watchdog, recovery.

`test_editor_save.js` checks the /gauges and /web-config Save buttons: success wording
for each page, and that a failed save shows the board's own explanation.

## Bench simulator tests

- `test_simulator.cpp`: the real `simulator.cpp` with a fake clock. Every safety rule has a
  test that fails when the rule is removed: off at boot, 10-minute timeout (restarted by
  touching a value), switches itself off on any real CAN frame (and a stray frame while off
  cannot kill the next run), manual values clamped, stopping clears the "has data" flags,
  and the demo never crosses the DEFAULT warning thresholds.
- `test_sim_logging.cpp`: the real `sd_logging.cpp` writing to a temp folder: rows are written
  for real data above 800 RPM and NEVER while the simulator runs. Build with
  `-DSD_MOUNT_POINT='"/tmp/sdtest"'`.
- `test_sim_page.js`: the /simulator page's script - modes, countdown, one slider request at
  a time (newest value wins), the board's refusal text shown. Set `WEB_MONITOR_CPP` to test a
  modified copy of web_monitor.cpp.
- `test_integration.cpp`: end to end. The real simulator feeds the real config code (defaults,
  2-second staleness) which feeds the real display code, for simulated minutes, with the
  default page layout: demo shows a rev cycle and never alarms, manual oil pressure 20 raises
  the full-screen alert under the SIMULATED banner, and a real CAN frame returns everything to
  "no data". Link `gauge_app.o`, `gauge_config.o`, `simulator.o`, the test and the LVGL objects.
