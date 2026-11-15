# Hub pins (ESP32 DevKitC on the Freenove terminal board)

**Every GPIO the hub uses.** The code defines them all in `esp32-input/hub_pins.h`; this table must match it.
`tests/host/test_hub_pins.js` checks that the two agree, that no pin is used twice, and that no hub file uses a pin
number outside `hub_pins.h`. When a pin is added, moved or freed: change `hub_pins.h` and this table together.

Terminals: L = left row, R = right row of the Freenove board.

## GPIO map

| Terminal | GPIO | Define (hub_pins.h) | Purpose | Notes |
|---|---|---|---|---|
| L8 | 36 | FUEL_PIN | Fuel level sender | analog, input-only |
| L9 | 39 | RF_MISO | CC1101 MISO | input-only |
| L10 | 34 | POT_PIN | Brightness knob | analog, input-only |
| L11 | 35 | LIGHTS_PIN | Lights-on wire | analog, 47k / 10k divider, input-only |
| L12 | 32 | DIMMER_PIN | Dash dimmer | analog, 47k / 10k divider |
| L13 | 33 | RF_GDO0 | CC1101 GDO0 (data) | |
| L14 | 25 | BTN_NEXT | Right (next) button | to ground, pull-up |
| L15 | 26 | BTN_PREV | Left (previous) button | to ground, pull-up |
| L16 | 27 | RF_CS | CC1101 CSN | |
| L17 | 14 | RF_SCK | CC1101 SCK | |
| L18 | 12 | LAMP_IN_B | Lamp input (spare) | must not be held HIGH at power-up |
| L20 | 13 | RF_MOSI | CC1101 MOSI | |
| R7 | 23 | EGT_CS | MAX6675 CS (EGT) | |
| R8 | 22 | CAN_RX_PIN | CAN RX (SN65HVD230 CRX) | |
| R11 | 21 | CAN_TX_PIN | CAN TX (SN65HVD230 CTX) | |
| R13 | 19 | EGT_SO | MAX6675 SO | |
| R14 | 18 | EGT_SCK | MAX6675 SCK | |
| R15 | 5 | I2C_SDA | I2C SDA: OLED, MPU6050, MCP23017 | shared bus |
| R16 | 17 | GPS_TX_PIN | GPS: hub TX to GPS RX | Serial2, 9600 |
| R17 | 16 | GPS_RX_PIN | GPS: GPS TX to hub RX | Serial2, 9600 |
| R18 | 4 | DHT_PIN | DHT22 (cabin temp / humidity) | |
| R19 | 0 | LAMP_IN_C | Lamp input (spare) | the BOOT button: must not be held LOW at power-up |
| R20 | 2 | LAMP_IN_A | Lamp input: seatbelt | to ground, pull-up |
| R21 | 15 | I2C_SCL | I2C SCL: OLED, MPU6050, MCP23017 | shared bus |

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

No GPIO is fully free. GPIO 0 and 12 are lamp inputs with power-up limits. More inputs: MCP23017 expanders (16 each, up to 2).

## Elsewhere

The hall speed sensor (NJK-5002C) goes to rusEFI, not the hub: [HUB_SPEED_SENSORS.md](HUB_SPEED_SENSORS.md).
CC1101 wiring by module pin: [HUB_433MHZ.md](HUB_433MHZ.md).
