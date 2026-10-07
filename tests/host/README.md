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
- `test_gauge_config.cpp`: signal staleness timeout and persistence of the
  per-cell settings, using the real ArduinoJson (v7) and the fake
  `Arduino.h` / `Preferences.h` here.

Build lines used (AddressSanitizer + UBSan on):

    gcc -O1 -g -fsanitize=address,undefined -I<lvgl> -I<sketch> \
        <sketch>/gauge_app.c test_gauge_app.c <lvgl objects> -o t -lm
    g++ -std=c++17 -O1 -g -fsanitize=address,undefined -I. -I<ArduinoJson>/src \
        -I<sketch> <sketch>/gauge_config.cpp test_gauge_config.cpp -o t2

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
