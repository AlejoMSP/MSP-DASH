# Tasks to be done

Open work, newest ideas at the bottom of each group. Tick items off (or delete them) when done, and note the build in
[TIMELINE.md](TIMELINE.md).

## Software

- [ ] **ADS1115 inputs as custom sensor sources** (configurable sensors stage 2). On `/custom`, pick "ADS1115 A0-A3"
      as a slot's source, with a divider multiplier (see "Voltage dividers" below: 1.667 for 0-5 V sensors, 5.70 for
      12 V signals), then scale/offset or a calibration table into the reading. Live volts already show on Setup > I2C bus.
- [ ] Configurable sensors stage 3: DBC presets. Stage 4: move the fixed hub sensors onto slots.
- [ ] Web UI step 4: gauge editor rework - layout picture to click a cell, folding groups (Signal / Look / Text /
      Warning), copy a cell or page to another screen, preview pinned beside the settings.
- [ ] Web UI: one save model everywhere (unsaved-changes bar with Save / Undo) and pop-up messages.
- [ ] Dashboard Logging tile: show whether the center screen is logging (needs a CORS header on the screen's status reply).
- [ ] Screens: put every screen GPIO in one header and a pin table, checked by a test (as done for the hub).
- [ ] fps numbers from the needle pages; decide if the needle redraw rework is worth doing.
- [ ] TPMS decode (U912 kit): capture each tire at two pressures next to the kit display, paste the hex, decode the fields.

## Car and wiring

- [ ] Hall speed sensor (NJK-5002C) to rusEFI: wire, set the VSS input in TunerStudio, compare with GPS, apply the factor.
- [ ] GPS: get a fix outdoors (antenna by a window or outside).
- [ ] Fuel level: calibrate the real Miata sender (ohms, not volts).
- [ ] Burn the EGT Lua script (without the debug print).
- [ ] Confirm the IAT decode matches the rusEFI DBC.
- [ ] Mount the MPU6050 solid to the chassis, set its axes on Setup, zero it on level ground.
- [ ] Re-pick gauges and lamps whose template changed (pressures now psi, MAP now boost psi); recheck log channels.
- [ ] SD card check on the center screen (Logging page shows the file name, or "No SD card").

## Voltage dividers (for any 3.3 V input: ADS1115 or ESP32 analog)

```
 Signal --[ R1 top ]--+-- input
                      |
                  [ R2 bottom ]   + 100 nF from input to GND
                      |
 GND -----------------+-- hub ground (shared with the sensor's ground)
```

V at input = V in x R2 / (R1 + R2). Aim for about 3.0 V at the highest voltage expected.

| Source | R1 (top) | R2 (bottom) | Max in -> at input | Multiply the reading by | Extra |
|---|---|---|---|---|---|
| 0-5 V sensor (pressure, boost) | 10 k | 15 k | 5.0 V -> 3.00 V (5.5 V -> 3.30 V) | 1.667 | 100 nF |
| 12 V car signal | 47 k | 10 k | 14.4 V -> 2.53 V (16 V -> 2.81 V) | 5.70 | 100 nF + clamp |

Clamp for 12 V lines (load-dump spikes up to about 40 V): BAT54S dual Schottky, middle pin to the input, ends to 3.3 V
and GND (or a 3.3 V zener from input to GND); the 47 k limits the current. Use 1 % resistors and check the multiplier
once against a multimeter.
