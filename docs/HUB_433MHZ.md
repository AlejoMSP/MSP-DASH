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
(automatic by default: 10 dB above the measured background; or a fixed dBm): pulse widths, the half-bit time, the Manchester bits and bytes. 2-FSK (most tire sensors) or
OOK is chosen on the page. Code: `rf_cc1101.h` (driver, capture), `rf_decode.h` (host-tested decoding).

Next step: capture the tire sensors' bursts, match them to a known format (rtl_433 has decoders for most
aftermarket brands), then decode pressure/temperature per wheel into custom slots.

## U912 kit findings (first capture, hub build 26)
- Kit: "U912 TPMS" with 4 internal valve-stem sensors (generic, no FCC ID). Heard at -56 to -69 dBm in 2-FSK.
- Manchester at 9600 bit/s (half-bit 52 us). The CC1101's data output stretches one level and shortens the other
  (about 20 us, drifting to 7 within a packet), so the decoder works on pulse PAIRS (2/3/4 half-bits), merges
  glitches under T/3, and corrects the stretch locally (`rf_decode_pairs`, hub build 27).
- First packet: `44 1D C5 A2 A2 B5 3B 51 0E CD`, then `44 1D C5 A2 ...` again: probably 80 bits sent twice, with
  `44 1D C5 A2` a likely sensor ID. Fixture: tests/host/fixtures/u912_burst1.txt.
- Next: more captures (same tire at two pressures, and the other tires) next to the kit display's readings, to map
  the ID, pressure, temperature, flags and checksum.
