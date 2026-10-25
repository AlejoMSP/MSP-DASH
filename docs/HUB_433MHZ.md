# 433 MHz receiver (CC1101) on the hub - tire pressure sensors (hub build 23)

Pins are GPIO numbers with the Freenove breakout terminal. Ground goes to the breadboard ground bar.
The module header is two rows: 2 4 6 8 on top, 1 3 5 7 below; pin 1 is the square pad.

| Module pin | Signal | Hub |
|---|---|---|
| 1 | GND | ground bar |
| 2 | VCC | 3.3 V, L6 (never 5 V) |
| 3 | GDO0 (data) | GPIO 33, L13 |
| 4 | CSN | GPIO 27, L16 |
| 5 | SCK | GPIO 14, L17 |
| 6 | MOSI | GPIO 13, L20 |
| 7 | MISO | GPIO 39, L9 |
| 8 | GDO2 | not connected |

GDO0 is not on GPIO 39: the ESP32 puts false short pulses on GPIO 36/39 while the ADC or WiFi is busy
(errata 3.11). MISO tolerates that (short register reads only). Screw the antenna on before powering.

## Listen mode (/rf on the hub)
At boot the hub reads the CC1101's part number and version (00 / 14); the serial log says "CC1101: ... found"
or "NOT found (check wiring)". The page shows the live signal strength and every burst above the threshold
(default -85 dBm): pulse widths, the half-bit time, the Manchester bits and bytes. 2-FSK (most tire sensors) or
OOK is chosen on the page. Code: `rf_cc1101.h` (driver, capture), `rf_decode.h` (host-tested decoding).

Next step: capture the tire sensors' bursts, match them to a known format (rtl_433 has decoders for most
aftermarket brands), then decode pressure/temperature per wheel into custom slots.
