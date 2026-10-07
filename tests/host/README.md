# Host tests (run on a PC, no board needed)

These compile the REAL `gauge_app.c` and `gauge_config.cpp` from the sketch,
unmodified, on a normal Linux/WSL machine and drive them with fake data. They
caught real mistakes while the full-screen alert was being written.

- `test_gauge_app.c`: full-screen alert, "--" for no data, BOOT/page navigation,
  trend graph holding. Needs LVGL v8.4.0 built for the host (an `lv_conf.h`
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
