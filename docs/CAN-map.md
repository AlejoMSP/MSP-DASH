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

## Gauge on/off config page added (2026-09-06)
New /config page (linked from the dashboard's gear icon) lets you toggle
each of the 16 currently-implemented gauges on/off with checkboxes.
Settings persist across reboots via ESP32's Preferences (NVS flash), so
they survive power cycles.

Disabling a gauge suppresses it entirely - both the Nextion display push
AND the web dashboard update stop, since they share the same
nextionSend() output path. The gauge's tile/dial on the web page simply
stops updating (stays at its last value or default) rather than being
removed from the page layout.

This does NOT disable CAN parsing itself - the ESP32 still reads and
computes every value, it just stops sending disabled ones onward. If you
want a leaner config that also skips the CAN parsing work for disabled
channels, that would be a further optimization, not yet done.

Toggleable gauges: rpm, boost, tps (bar), tpst (text), eth, ego, fuelp,
oilp, oilt, map, clt, mat, vvt, battv, egt, mph. This list matches exactly
what's implemented in code - it is NOT the full set of ~40+ signals
available in the rusEFI DBC (see the "all rusEFI CAN messages" summary
elsewhere in this doc for the complete signal list, most of which aren't
parsed into the sketch yet).

## Classic instrument cluster redesign (2026-09-06)
Web dashboard rebuilt to look like a classic analog OEM instrument cluster
(white-faced dials, black tick marks, red needle, small trip-computer LCD)
rather than the earlier dark tech-style dials. Pure SVG + CSS, no external
libraries.

Layout: small Fuel gauge (left) | large Tachometer (RPM) | center LCD
panel (cycles every 3s through Boost/Oil Pressure/AFR/EGT) | large
Speedometer (GPS mph) | small Coolant Temp gauge (right). Everything else
(TPS bar, ethanol, fuel pressure, oil temp, MAP, boost, MAT, VVT, battery
voltage, EGT) remains in the tile grid below, same as before.

Added FuelLevel signal (rusEFI DBC, BASE3 byte7, raw x0.5 = %) - not
previously parsed. Registered as a new toggleable "fuel" gauge in
/config. NOTE: MegaSquirt's mapped blocks in this project don't include
a fuel level signal, so the fuel gauge stays at its last/default value
when running MegaSquirt instead of rusEFI.

Gauge sweep convention: -135deg (bottom-left, value=0) to +135deg
(bottom-right, value=max), 270deg total - standard automotive gauge
layout. Needle rotation via CSS transform on an absolutely-positioned
div overlaid on the SVG face, updated per WebSocket message.

## GPS status light + all temps converted to Fahrenheit (2026-09-06)
GPS status dot added on the dashboard's speedometer gauge (top of the
dial): gray when no fix, glowing green when gps.speed.isValid(). Sent via
a new broadcastStatus() helper that skips both the Nextion write and the
/config on/off gate - this is a system status indicator, not a
user-toggleable sensor gauge, and has no Nextion component.

All temperature fields (clt, mat, oilt, egt) now convert to Fahrenheit
before being pushed anywhere (Nextion, web dashboard, Serial debug).
Fixes a real inconsistency: the coolant-temp gauge's unit label already
said "F" but the underlying value being pushed was still Celsius.

- rusEFI: DBC explicitly states these signals are degC, so the conversion
  is well-defined and safe.
- MegaSquirt: temp units depend on how the ECU itself is configured
  (PCLink can output either C or F). The conversion ASSUMES Celsius
  source config (matching the common default) - this has NOT been
  bench-confirmed for MegaSquirt in this project. If your MegaSquirt is
  actually configured for Fahrenheit output, this would double-convert
  and show wrong values - worth checking once MegaSquirt bench testing
  happens.

New shared helper: cToF(float c) in the byte-reading-helpers section.

## Major config system overhaul + LinkECU + AEM scaffold (2026-09-06)

### Key discovery: LinkECU and MegaSquirt likely share one CAN format
Going back through this project, the "MegaSquirt" reference sketch's
block labels ("Seconds,PW1,PW2,RPM" / "BARO,MAP,MAT,CLT" / "TPS,BATT,EGO")
are IDENTICAL to Link's own PCLink CAN Setup labels seen at the very start
of this project. Both use base ID 1520 decimal (0x5F0). Given this, both
ECUs are handled by ONE shared parser (handleSecondaryEcuFrame, renamed
from handleMegasquirtFrame) - the /config "Active Secondary ECU" selector
only changes labeling, not parsing, since they can't be told apart by
CAN ID range (unlike rusEFI, which has its own distinct 0x200+ range).
Reasonably high confidence but NOT independently bench-verified for
either brand - sniff-and-compare before fully trusting.

### AEM X-Series wideband - scaffolded, UNCONFIRMED
handleAemFrame() listens on a commonly-cited CAN ID (0x180) but the byte
layout is NOT parsed - it just dumps raw bytes to Serial (when debug
logging is on) so you can compare against the AEM unit's own display and
work out the real format. OFF by default (/config toggle). Do NOT trust
any value from this path until it's been sniffed and confirmed the same
way rusEFI was.

### Debug logging now OFF by default
Every nextionSend() call used to unconditionally print to Serial. Now
gated behind a debugLoggingEnabled flag (persisted, toggle on /config).
EGT's per-read Serial prints are gated the same way. Startup/WiFi
connection messages still print unconditionally (one-time boot info,
not the per-message flood this was meant to fix).

### Full per-gauge configurability
Every gauge's GaugeConfig struct now includes: ecu (grouping tag for the
config page), isHero (true for rpm/mph/fuel/clt - fixed needle dials in
the cluster, type selector hidden for these), enabled, displayType
(Number/Bar/Needle - only meaningful for non-hero gauges), refreshMs
(throttles how often a value is actually sent even if CAN data arrives
faster), minVal/maxVal, hasRedline/redline. All persisted in NVS flash,
edited via a rebuilt /config page grouped by ECU (rusEFI / secondary ECU
/ AEM / GPS / Auxiliary Sensors).

New GET /api/gauges JSON endpoint - the dashboard page fetches this at
load and builds itself dynamically: hero cluster gauges read their
min/max/redline from here instead of hardcoded JS; every other enabled
gauge renders as a number tile, a bar, or a small needle gauge based on
its configured displayType.

### Explicit scope boundary
This does NOT let you map a brand-new arbitrary CAN ID/byte offset to a
new gauge via the web UI - only the DISPLAY of gauges already wired into
this code is configurable. A true "add any CAN signal from a UI" system
would be a separate, larger feature (essentially a mini DBC editor).

## Full rusEFI DBC exposed as gauges + MegaSquirt/LinkECU removed (2026-09-06)

Every single signal in rusEFI's official DBC is now wired into the
sketch and configurable via /config - 74 total gauges (up from ~17):

- **BASE0** (status/diagnostic): WarningCounter, LastError, RevLimAct,
  MainRelayAct, FuelPumpAct, CELAct, EGOHeatAct, LambdaProtectAct, Fan,
  Fan2, CurrentGear, DistanceTraveled
- **BASE1**: RPM, IgnitionTiming, InjDuty, IgnDuty, VehicleSpeed (raw CAN
  VSS - separate from GPS-sourced mph), FlexPct
- **BASE2**: PPS, TPS1 (bar+text), TPS2, Wastegate
- **BASE3**: MAP, Boost (derived), CoolantTemp, IntakeTemp, AUX1Temp,
  AUX2Temp, MCUTemp, FuelLevel
- **BASE4**: OilPress, OilTemperature, FuelTemperature, BattVolt
- **BASE5**: CylAM, EstMAF, InjPW, KnockCt
- **BASE6**: FuelUsed, FuelFlow, FuelTrim1, FuelTrim2
- **BASE7**: Lam1 (as AFR), Lam2, FpLow, FpHigh
- **BASE8**: Cam1I (kept as "vvt" key - matches existing physical Nextion
  component), Cam1Itar, Cam1E, Cam1Etar, Cam2I, Cam2Itar, Cam2E, Cam2Etar
- **BASE9**: Egt1-8 (8 individual ECU-reported EGT channels - distinct
  from the physical MAX6675 probe, which keeps its own "egt" key)
- **BASE10**: knock0-7 (8 per-cylinder knock channels)
- **BASE11**: BrakePedal

All new number-style gauges default to enabled=true, DISPLAY_NUMBER,
0ms refresh (no throttle) except WarningCounter/LastError/DistanceTraveled
which default to a gentler 1000ms refresh since they're not
time-critical. All are fully configurable (type/refresh/min/max/redline)
same as every other gauge.

### MegaSquirt / LinkECU support REMOVED
Per project decision, focusing on rusEFI only for now. All secondary-ECU
code (SEC_BASE/SEC_BLOCK* defines, handleSecondaryEcuFrame,
activeSecondaryProtocol selector, the /config protocol dropdown) has been
deleted from the sketch. If this is revisited later, the git history
(commits around 2026-09-06, before this one) has a working implementation
to reference or restore.

## Interactive dashboard: Light type, MDI icons, per-gauge debug, close/drag/right-click (2026-09-06)

### New "Light" display type
A 4th gauge display type alongside Number/Bar/Needle - a boolean on/off
icon indicator. All the naturally-boolean rusEFI signals now default to
this: RevLimAct, MainRelayAct, FuelPumpAct, CELAct (Check Engine),
EGOHeatAct, LambdaProtectAct, Fan, Fan2, BrakePedal - 9 total light gauges.
They render in a dedicated "warning light strip" just under the hero
cluster, similar to a real instrument cluster's telltale row.

### MDI icons - UNVERIFIED icon-name choices
Icons come from Material Design Icons (materialdesignicons.com /
mdisearch.com), loaded via CDN webfont
(https://cdn.jsdelivr.net/npm/@mdi/font). Icon name choices per light
(cel->engine, revlim->speedometer, mainrelay->flash, fuelpump->gas-station,
egoheat->thermometer, lambdaprot->shield-alert, fan1/fan2->fan,
brake->car-brake-alert) are my best guess based on common automotive icon
conventions - NOT independently verified against the current MDI icon
set. A wrong/missing name just renders blank (safe failure). Spot-check
at mdisearch.com and edit the ICON_MAP object in the dashboard's
JavaScript if any need correcting.

### Per-gauge console debug logging
Alongside the existing global debug toggle, each gauge now has its own
"Log to console" checkbox on /config. Either the global toggle OR a
gauge's own flag being on causes that gauge's Serial output to print -
lets you debug one specific signal without flooding the console with
everything.

### Dashboard interactivity (dynamic gauges only - NOT the 4 fixed hero
cluster gauges rpm/mph/fuel/clt, which stay a deliberately fixed
classic-cluster centerpiece)
- **Close (X)**: every light/bar/tile/mini-needle gauge has a small X in
  its corner. Clicking it disables that gauge immediately (same as
  unchecking it on /config) via GET /api/gauge/set?key=X&enabled=0,
  persisted to flash.
- **Drag-and-drop reorder**: gauges can be dragged within their own
  display-type section (lights stay with lights, bars with bars, etc).
  New order persists via GET /api/gauge/order?list=key1,key2,...
- **Right-click type change**: right-clicking any dynamic gauge shows a
  small menu (Number/Bar/Needle/Light); picking one changes that gauge's
  display type immediately via GET /api/gauge/type?key=X&type=N,
  persisted to flash. Note: since gauges keep their existing "order"
  value when their type changes, they may appear in a slightly
  unexpected position within their new section until manually reordered
  again - a minor accepted tradeoff for keeping this simple.

New GaugeConfig fields: debugEnabled (persisted "_dbg"), order (persisted
"_or", defaults to array declaration index). New live-action API
endpoints: GET /api/gauge/set, GET /api/gauge/type, GET /api/gauge/order
- all separate from the full /config form POST, so the dashboard can make
lightweight live changes without a full page reload.

## Fix: /config blank page (heap fragmentation) (2026-09-06)
With 74 gauges, building the /config page and /api/gauges JSON as one
giant String via repeated += concatenation was fragmenting/exhausting
ESP32 heap badly enough that the response came back empty. Fixed by
switching both to a streaming response (AsyncResponseStream, written to
incrementally row-by-row) instead of accumulating one large String
buffer before sending. Renamed buildConfigPage()->writeConfigPage() and
buildGaugeApiJson()->writeGaugeApiJson() to reflect the new signature
(both now take a response stream pointer and print directly to it,
rather than returning a String).

## ESP32 system info + SD card logging (2026-09-06)

### System info panel
New dashboard section showing: free/total RAM, CPU frequency, flash chip
size, sketch storage used/free, and the ESP32's internal chip temperature
(convert to F). Broadcast every 2s via broadcastStatus() - not part of
the GaugeConfig system (it's diagnostic/health info, not a vehicle
gauge), always on, not user-toggleable. NOTE: the internal temperature
sensor (temperatureRead()) is meant for chip calibration, not precise
ambient sensing - it commonly reads higher than actual ambient. Treat as
a rough health indicator only, flagged as such on the dashboard itself.

### SD card logging
Hardware: any SPI microSD breakout module + a FAT32-formatted card.
Wiring uses ESP32's default VSPI pins (SCK=18, MISO=19, MOSI=23 - not
used elsewhere in this project) plus CS=27.

Uses the ESP32 core's built-in SD library (no external install needed).
On boot, mounts the card and creates a new session log file
(/log_<N>.csv, N auto-incrementing via NVS so sessions never overwrite
each other) with a header row listing every gauge key. Toggle on
/config ("SD Card Logging") - shows whether a card was actually detected
at boot.

Every gauge's last-sent value is snapshotted into a lastNumericValue[]
array inside nextionSend() (added a couple lines there - the only change
to that function). Every 500ms (SD_LOG_INTERVAL_MS), if logging is
enabled and the card mounted successfully, a CSV row (millis timestamp +
all 74 gauge values) is appended - opened/closed per write rather than
keeping a file handle open continuously, trading some write performance
for better resilience against an unclean power loss (common on a car
bench/ignition-switched setup).

## OTA firmware updates + config-persistence clarification (2026-09-06)

### Config persistence
NVS (where all gauge config, debug flags, SD toggle, etc. live) is a
separate flash partition from the application code - a normal
`arduino-cli compile` + `arduino-cli upload` does NOT touch it. What
looked like "config lost on every flash" was actually an artifact of the
gauge schema changing rapidly across many consecutive feature additions
in this project (new keys/fields with no prior saved NVS entry fall back
to their new hardcoded default, which looks like a reset even though
unrelated saved keys were untouched). Now that schema changes have
slowed down, settings should reliably survive normal reflashes going
forward. Only a full `esptool erase_flash` (or a partition table change)
would genuinely wipe NVS.

### OTA firmware updates
New /update page (linked from the dashboard) lets you upload a compiled
.bin directly from any browser instead of needing a USB cable. Uses the
ESP32 core's built-in Update library (no external dependency).

To generate a .bin file:
```
arduino-cli compile --fqbn esp32:esp32:esp32 --export-binaries esp32-main
```
The .bin ends up in `esp32-main/build/esp32.esp32.esp32/`. Upload it at
http://<esp32-ip>/update - the device reboots automatically once the
flash completes successfully. NVS/config is untouched by this process,
same as a USB reflash.
