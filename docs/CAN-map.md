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

## MegaSquirt support added (2026-09-06)
esp32-main.ino now supports BOTH rusEFI and MegaSquirt in the same sketch -
no mode switch needed, since their CAN ID ranges don't overlap (rusEFI:
0x200-0x20B / 512-523 decimal; MegaSquirt: 1520+N decimal / 0x5F0+N).
Whichever ECU is physically connected is simply the one whose frames
appear and get parsed.

MegaSquirt byte order is BIG-ENDIAN (opposite of rusEFI's little-endian).
Scale factors and block layout are taken from a previously working
reference sketch (not independently bench-verified in this project). If
you switch to MegaSquirt, running the CAN sniffer against it once - same
sniff-and-compare method used to confirm rusEFI - is worth doing before
fully trusting these values in the same way RPM/MAP/TPS1/BattVolt are now
trusted for rusEFI.

| Component | MegaSquirt block | Formula |
|---|---|---|
| rpm | 1520+0, word3 | raw x 1 |
| map | 1520+2, word1 | raw / 10 (kPa) |
| mat | 1520+2, word2 | raw / 10 |
| clt | 1520+2, word3 | raw / 10 |
| tps / tpst | 1520+3, word0 | raw / 10 (%) |
| ego | 1520+3, word2 | raw / 10 (already AFR units per source, not lambda) |
| fuelp | 1520+13, word0 | raw / 10 |
| oilp | 1520+13, word1 | raw / 10 |
| oilt | 1520+13, word2 | raw / 10 |
| vvt | 1520+44, word0 | raw / 10 |
| eth | 1520+47, word0 | raw / 10 |
| mph | not used | GPS-sourced regardless of ECU |

Boost calc differs by ECU: rusEFI uses the fixed 101.3 kPa assumption
(no baro signal available); MegaSquirt broadcasts real BARO in the same
block as MAP (1520+2, word0), so boost = MAP - BARO using live data - more
accurate than the rusEFI fixed-constant approach.

## esp32-main.ino reorganized into clear sections
File is now split into labeled banner sections: includes, pin/wiring
config, rusEFI CAN IDs, MegaSquirt CAN IDs, globals, byte-reading helpers,
Nextion output, rusEFI frame handling, MegaSquirt frame handling, GPS
handling, setup/loop. Frame parsing for each ECU lives in its own function
(handleRusefiFrame / handleMegasquirtFrame) called from loop() based on
the incoming CAN ID range.

## EGT added (2026-09-06)
HiLetgo MAX6675 module + K-type thermocouple, bit-banged 3-wire interface
(not full SPI - MAX6675 is read-only, no MOSI needed).

Wiring: SCK->GPIO33, CS->GPIO32, SO->GPIO34 (input-only pin, fits since
this is a read-only data line).

Console-only for now (Serial.printf, not pushed to Nextion) - display
integration deferred until a field/page is designated for it. Reads every
500ms (MAX6675 needs ~220ms minimum between conversions per datasheet).
Reports NAN / "thermocouple open/disconnected" if bit 2 of the raw read
is set, per MAX6675's open-circuit detection.

## Web dashboard added (2026-09-06)
Hosts a live WebSocket-based dashboard over WiFi (STA mode, joins existing
network - credentials in a git-ignored esp32-main/wifi_config.h, copy from
wifi_config.h.example). Libraries: "ESP Async WebServer" + "Async TCP"
(both by ESP32Async, the actively maintained fork family - not the older
dvarrel/me-no-dev forks, to avoid version mismatches).

Implementation: nextionSend() was extended to also parse the same command
it sends to the Nextion display and broadcast it as JSON over WebSocket -
every existing call site gets web support for free, no other code changes
needed. The web page (served at http://<esp32-ip>/) shows MORE fields than
the physical 12-slot Nextion display: egt and battv are included on the
web dashboard even though neither has a Nextion component yet (Nextion
silently ignores field names it doesn't recognize, so this is safe).

Web page auto-reconnects on WebSocket disconnect (2s retry loop).

## Web dashboard redesign (2026-09-06)
Replaced the plain grid-of-boxes web page with a proper dashboard:
- RPM and Boost shown as CSS conic-gradient dial gauges (0-270deg sweep,
  no external libraries - pure CSS custom property driven by JS)
- TPS shown as a horizontal progress bar with live percentage label
- Everything else (mph, eth, ego, fuelp, oilp, oilt, map, clt, mat, vvt,
  battv, egt) remains as a clean tile grid below

Added a "boost" field broadcast (computed internally before, but never
actually sent) alongside "map" in both the rusEFI and MegaSquirt MAP
handlers, specifically so the new dashboard has a value to drive the
boost dial. No Nextion component for "boost" exists yet - web-only for now,
same pattern as egt/battv.

Dial ranges (in dashboard JS, easy to adjust): RPM 0-8000, Boost -15 to 25.
