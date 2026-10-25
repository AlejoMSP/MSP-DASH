# Hub pin map (ESP32 DevKitC on the Freenove breakout)

L = left terminal row, R = right row. L1-L5 / R1-R5 are the breakout's own extra terminals (not ESP32 pins).
Terminal numbers past those were worked out from the pins in use (L14 = GPIO 25, R18 = GPIO 4, ...), which match
the standard 38-pin DevKitC order. Ground goes to the breadboard ground bar.

## Left row
| Terminal | GPIO | Used for | Notes |
|---|---|---|---|
| L6 | 3V3 | 3.3 V supply: CC1101, sensors, pull-ups | |
| L7 | EN | reset | do not use |
| L8 | 36 (VP) | fuel level (pot wiper / sender divider) | input only, ADC1 |
| L9 | 39 (VN) | CC1101 MISO (module pin 7) | input only; false pulses while ADC/WiFi busy, so no timing signals |
| L10 | 34 | brightness knob (bench pot) | input only, ADC1 |
| L11 | 35 | lights-on wire (47k/10k divider) | input only, ADC1 |
| L12 | 32 | dash dimmer (47k/10k divider) | ADC1 |
| L13 | 33 | CC1101 GDO0 data (module pin 3) | |
| L14 | 25 | next page button | |
| L15 | 26 | previous page button | |
| L16 | 27 | CC1101 CSN (module pin 4) | |
| L17 | 14 | CC1101 SCK (module pin 5) | |
| L18 | 12 | FREE | strapping: must be low at boot (flash voltage); avoid |
| L19 | GND | ground | |
| L20 | 13 | CC1101 MOSI (module pin 6) | |
| L21-L23 | 9, 10, 11 | flash chip | never use |
| L24 | 5V | 5 V: GT-U7 GPS VCC | check the label says 5V |

## Right row
| Terminal | GPIO | Used for | Notes |
|---|---|---|---|
| R6 | GND | ground | |
| R7 | 23 | MAX6675 CS (EGT) | |
| R8 | 22 | CAN RX (SN65HVD230 CRX) | |
| R9 | 1 (TX0) | USB serial | do not use |
| R10 | 3 (RX0) | USB serial | do not use |
| R11 | 21 | CAN TX (SN65HVD230 CTX) | |
| R12 | GND | ground | |
| R13 | 19 | MAX6675 SO (EGT) | |
| R14 | 18 | MAX6675 SCK (EGT) | |
| R15 | 5 | FREE | strapping (high at boot); fine for most uses |
| R16 | 17 | GPS: to the GT-U7's RX | |
| R17 | 16 | GPS: from the GT-U7's TX | |
| R18 | 4 | DHT22 data (ambient temp / humidity) | |
| R19 | 0 | BOOT button | avoid |
| R20 | 2 | FREE | strapping (low/floating for flashing); onboard LED on some boards |
| R21 | 15 | FREE | strapping (boot log); fine for most uses |
| R22-R24 | 8, 7, 6 | flash chip | never use |

## Free pins
GPIO 5 (R15), 15 (R21) and 2 (R20) are free and usable with care (strapping pins: nothing should pull them the
wrong way at power-up). GPIO 12 (L18) is free but best left alone: high at boot selects the wrong flash voltage.
No free ADC1 inputs remain (32-39 are all used); ADC2 pins do not work while WiFi is on.

Not on the hub: hall speed sensor (wired to rusEFI).
