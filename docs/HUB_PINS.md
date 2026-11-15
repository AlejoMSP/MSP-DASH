# Hub pins (ESP32 DevKitC on the Freenove terminal board)

**Every GPIO the hub uses.** The code lists every role with its default GPIO in `esp32-input/hub_pins.h`; the table
below must match it. `tests/host/test_hub_pins.js` checks that the two agree, that no default GPIO is used twice, and
that no hub file uses a pin number outside `hub_pins.h`. When a role is added or its default changes: change both.

Terminals: L = left row, R = right row of the Freenove board.

## GPIO map (defaults)

Every function ("role") has a default GPIO, listed here with the Freenove terminal it is wired to. A role can be moved to
another GPIO on the hub's **Inputs and pins** page (saved, applied when the hub restarts); the page then shows the GPIO in
use. Every other GPIO is free for the configurable inputs (buttons, switches, analog sensors) and the OLED lamps.

| Terminal (default) | Default GPIO | Role | Function | Notes |
|---|---|---|---|---|
| L8 | 36 | fuel | Fuel level sender | analog, input-only |
| L9 | 39 | rf_miso | CC1101 MISO | input-only |
| L10 | 34 | pot | Brightness knob | analog, input-only |
| L11 | 35 | lights | Lights-on wire (divider) | analog, 47k / 10k divider, input-only |
| L12 | 32 | dimmer | Dash dimmer (divider) | analog, 47k / 10k divider |
| L13 | 33 | rf_gdo0 | CC1101 GDO0 (data) |  |
| L14 | 25 | btn_next | Right (next) button | to ground, pull-up |
| L15 | 26 | btn_prev | Left (previous) button | to ground, pull-up |
| L16 | 27 | rf_cs | CC1101 CSN |  |
| L17 | 14 | rf_sck | CC1101 SCK |  |
| L20 | 13 | rf_mosi | CC1101 MOSI |  |
| R7 | 23 | egt_cs | EGT MAX6675 CS |  |
| R8 | 22 | can_rx | CAN RX (SN65HVD230 CRX) |  |
| R11 | 21 | can_tx | CAN TX (SN65HVD230 CTX) |  |
| R13 | 19 | egt_so | EGT MAX6675 SO |  |
| R14 | 18 | egt_sck | EGT MAX6675 SCK |  |
| R15 | 5 | i2c_sda | I2C SDA (OLED, MPU6050, MCP23017) | shared bus |
| R16 | 17 | gps_tx | GPS: hub TX to GPS RX | Serial2, 9600 |
| R17 | 16 | gps_rx | GPS: GPS TX to hub RX | Serial2, 9600 |
| R18 | 4 | dht | DHT22 cabin temp/humidity |  |
| R21 | 15 | i2c_scl | I2C SCL (OLED, MPU6050, MCP23017) | shared bus |

Free GPIOs with the defaults: 0 (R19, the BOOT button: not LOW at power-up), 2 (R20), 12 (L18, not HIGH at power-up).
Analog-capable GPIOs (ADC1): 32-36 and 39 (all taken by default; move a role to free one).

## Not GPIO

| Terminal | What |
|---|---|
| L6 | 3.3 V: CC1101, OLED, MPU6050, MCP23017 (never 5 V to the CC1101 or on the I2C bus) |
| L24 | 5 V: GPS power |
| R9 / R10 | GPIO 1 / 3: USB serial (uploads, serial monitor) - do not use |
| (none) | GPIO 6-11: the flash chip - not usable |

## I2C bus (R15 SDA, R21 SCL)

| Device | Address | Notes |
|---|---|---|
| SSD1306 OLED 0.96" | 0x3C (0x3D on some) | warning panel and pages |
| MPU6050 accelerometer | 0x68 (AD0 to 3.3 V: 0x69) | mount solid to the chassis; zero on the hub home page |
| MCP23017 board 1 (SG-IO-E017) | 0x27 (DIP switch default) | 16 lamp inputs PA0-PA7, PB0-PB7; VCC 3.3 V, never 5 V; INTA/INTB not used |
| MCP23017 board 2 | 0x26 (set the DIP switch) | 16 more |

Wire the devices in parallel from one junction. Keep the bus short (about 30 cm), SDA and SCL twisted with a ground.
Every module has its own pull-ups; past about 5 modules remove all but one set.

## Free

GPIO 0, 2 and 12 (power-up limits above), plus 16 inputs on each MCP23017 expander.

## Elsewhere

The hall speed sensor (NJK-5002C) goes to rusEFI, not the hub: [HUB_SPEED_SENSORS.md](HUB_SPEED_SENSORS.md).
CC1101 wiring by module pin: [HUB_433MHZ.md](HUB_433MHZ.md).
