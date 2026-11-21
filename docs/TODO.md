# Tasks to be done

Open work, newest ideas at the bottom of each group. Tick items off (or delete them) when done, and note the build in
[TIMELINE.md](TIMELINE.md).

## Software

- [ ] Configurable sensors stage 3: DBC presets. Stage 4: move the fixed hub sensors onto slots.
- [ ] Web UI: one save model everywhere (unsaved-changes bar with Save / Undo) and pop-up messages.
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

## Voltage dividers (for any 3.3 V input)

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

## PDM (parked)

- Power distribution module: notes in [pdm/README.md](../pdm/README.md). Pick the board approach (carrier first or fully integrated), then the CAN layout.

## Ideas from M-Gauge (hub 56 on)

- Done: staged shift light across the screens.
- Peak markers on needles and bars (fading or held).
- Per-signal smoothing time (fuel level).
- Text for values (gear 0 = N, map names).
- 0-60 and 1/8, 1/4 mile timer from GPS + MPU6050.
- Screens accept ESP-NOW data from the paired hub only (MAC filter).
- Done (hub 61): screens join the hub's own network in the car.
- rusEFI warning / error code as text on the screens and OLED (idea from ESP32-OBD-Display).

## Factory setup (built in hub 62 / build 226, see docs/FACTORY_SETUP.md)

1. Automatic roles and the hub flag.
2. First-boot setup (position, home Wi-Fi; show only what exists).
3. Factory reset without flashing (BOOT at power-up, Board settings button, serial `factory`).
4. Settings backup (hub Backup page; per-screen export / restore).
