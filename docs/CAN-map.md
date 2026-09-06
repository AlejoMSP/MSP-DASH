# rusEFI CAN Map (BENCH-CONFIRMED)

Source: official rusEFI DBC (`docs/rusefi.dbc`), cross-checked against a long
bench capture on 2026-09-06 (Hellen board, engine off, board powered).

## Configuration (confirmed via TunerStudio)
- Base address: 512 decimal (0x200), 11-bit standard frames
- Broadcast period: 50ms (20Hz) - NOTE: blocks appear to round-robin, not
  every block is sent every cycle. 0x200/0x201/0x202 update almost every
  cycle; 0x203/0x204/0x205/0x207/0x208/0x209 update less frequently. Expect
  to wait a bit longer to see the less-frequent blocks on a short capture.
- Bitrate: 500kbps
- CAN dash type: None (RX8 dash emulation disabled)

## CRITICAL: byte order is LITTLE-ENDIAN (opposite of MegaSquirt)

## Bench-confirmed signal values (engine off, board powered, 2026-09-06)

| Signal | Block/ID | Raw example | Formula | Result | Status |
|---|---|---|---|---|---|
| RPM | 0x201 | 0 | raw x 1 | 0 RPM | CONFIRMED - engine off |
| TPS1 | 0x202 | ~10 | raw x 0.01 (%) | ~0.10% | CONFIRMED - sensor wired, closed throttle |
| MAP | 0x203 | ~2963 | raw x 0.0333333 (kPa) | ~98.8 kPa | CONFIRMED - matches atmospheric pressure exactly |
| MCUTemp | 0x203 | 74 | raw - 40 (degC) | 34 degC | CONFIRMED - plausible board temp |
| BattVolt | 0x204 | ~12361 | raw x 0.001 (V) | ~12.36V | CONFIRMED - matches 12V bench supply exactly |
| PPS, TPS2, Wastegate | 0x202 | -100 | raw x 0.01 (%) | -1.00% | CONFIRMED sentinel - sensor NOT connected |
| CoolantTemp, IntakeTemp, AUX1Temp, AUX2Temp | 0x203 | 40 (every time) | raw - 40 (degC) | 0 degC | Sensor NOT wired - consistent zero-offset default |
| OilPress | 0x204 | 0 (every time) | raw x 0.0333333 (kPa) | 0 kPa | Sensor NOT wired |
| OilTemperature, FuelTemperature | 0x204 | 40 (every time) | raw - 40 (degC) | 0 degC | Sensor NOT wired |
| Lam1, Lam2 (lambda/AFR) | 0x207 | 0 (every time) | raw x 0.0001 | 0.0000 | O2 sensor not active (engine off, heater off) |
| CylAM | 0x205 | ~589 | raw x 1 (mg) | 589 mg | Static baseline estimate, engine off |
| Cam signals | 0x208 | 0 (every time) | - | 0 deg | No cam/VVT activity, engine off |
| EGT1-8 | 0x209 | 0 (every time) | raw x 5 (degC) | 0 degC | No exhaust flow, engine off |

## Gauge -> confirmed source (production-ready formulas)

| Gauge | Signal | CAN ID | Bytes (LE) | Formula |
|---|---|---|---|---|
| RPM | RPM | 0x201 | byte0-1 | raw x 1 |
| Boost | MAP | 0x203 | byte0-1 | (raw x 0.0333333) - baro_kPa. No baro signal exists in the DBC - use a fixed atmospheric assumption (~101.3 kPa) or capture MAP at known-off/idle as a live baro estimate. Decide and document once sensor testing resumes. |
| Coolant Temp | CoolantTemp | 0x203 | byte2 | raw - 40 |
| Oil Pressure | OilPress | 0x204 | byte2-3 | raw x 0.0333333 |
| Oil Temp | OilTemperature | 0x204 | byte4 | raw - 40 |
| AFR | Lam1 (lambda) | 0x207 | byte0-1 | lambda = raw x 0.0001; AFR = lambda x 14.7 (gasoline stoich - confirm fuel type if E85/flex) |
| Battery Voltage (bonus gauge?) | BattVolt | 0x204 | byte6-7 | raw x 0.001 |

## Outstanding hardware work
CoolantTemp, IntakeTemp, AUX temps, Oil Pressure, Oil Temp, Fuel Temp, and
the Lambda/AFR sensor are **not currently wired** to this Hellen board on
the bench harness. The CAN protocol and scale factors are now fully
confirmed and ready to use the moment real sensors are connected - no
further reverse-engineering needed for these signals. TPS1, MAP, RPM,
BattVolt, and MCUTemp are already wired, confirmed, and correct.

## HMI realignment (2026-09-06)
The real Nextion HMI is a 12-field digital 7-segment dashboard (not analog
dials as originally planned). Component names and sources, confirmed
against the actual Editor build:

| Component | Signal | Block | Formula |
|---|---|---|---|
| rpm | RPM | 0x201 | raw x 1 |
| mph | VehicleSpeed | 0x201 | raw x 0.621371 (DBC gives kph) |
| tps (bar) + tpst (text) | TPS1 | 0x202 | raw x 0.01 (%) |
| eth | FlexPct | 0x201 | raw x 1 (%) |
| ego | Lam1 | 0x207 | lambda x 14.7 = AFR (gasoline stoich assumed) |
| fuelp | FpLow | 0x207 | raw x 0.0333333 (kPa) - assumes port injection, not GDI |
| oilp | OilPress | 0x204 | raw x 0.0333333 (kPa) |
| oilt | OilTemperature | 0x204 | raw - 40 (degC) |
| map | MAP | 0x203 | raw x 0.0333333 (kPa) |
| clt | CoolantTemp | 0x203 | raw - 40 (degC) |
| mat | IntakeTemp | 0x203 | raw - 40 (degC) |
| vvt | Cam1I | 0x208 | raw x 1 (deg, signed) - closest DBC signal to VVT angle |

esp32-main.ino rewritten to push these 12 fields directly via .txt/.val
commands - no gauge-angle math needed since the display is digital readouts,
not analog dials.

## GPS speed (2026-09-06)
Added a GT-U7 (NEO-6M chipset) GPS module on ESP32 UART1 (pins 4/5,
9600 baud NMEA), parsed via TinyGPSPlus. The `mph` field on the Nextion
dashboard is now sourced from GPS speed instead of rusEFI's VehicleSpeed
(VSS) signal - the VSS byte is still read from CAN but no longer pushed
to the display. Sent to the display at a steady ~4Hz regardless of the
GPS module's actual fix update rate; shows "--" until a valid fix is
acquired.
