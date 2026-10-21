# Hub speed sensors (hub build 18)

Pins are GPIO numbers with the Freenove breakout terminal (L = left row, R = right row). Ground goes to the breadboard ground bar.

## Hall sensor: NJK-5002C (NPN, normally open, 5-30 V, with magnet)
| Wire | Goes to |
|---|---|
| Brown (+) | 5 V, L24 (anything 5-30 V works) |
| Blue (-) | ground |
| Black (signal) | through a Schottky diode to GPIO 27 = L16: diode stripe (cathode) on the black wire, other end on L16 |
| 10k resistor | from L16 to 3.3 V (L6) |

Why the diode: with nothing connected, this sensor's black wire measured 3.58 V (powered at 5 V). It has a
built-in pull-up (or its indicator LED) to its supply, so a plain 10k to 3.3 V would let more than 3.3 V into the
pin, and a divider would bring the high level below what the ESP32 reads as HIGH. The diode blocks anything above
3.3 V: magnet away = the 10k holds L16 at 3.3 V (HIGH); magnet there = the sensor pulls black to ground and L16
drops to about 0.3 V (LOW). Use a Schottky (1N5817, BAT43, BAT85, 1N5819): its small drop keeps LOW well under the
ESP32's 0.8 V limit. A 1N4148 also works but lands near 0.8 V, so check it. This also stays safe if the sensor is
later powered from the car's 12 V.

Speed = pulses per second / pulses per mile x 3600. Pulses per mile is set on the hub page (default 4000).
"Calibrate from GPS": drive steady above 10 mph with a GPS fix, press it, and it sets pulses per mile so the hall
speed matches the GPS. Speed goes into the Speed signal and, in kph, to rusEFI in frame 0x7F0 (docs/RUSEFI_LUA.md).

## GPS: GT-U7 (NEO-6M compatible)
| GT-U7 pin | Goes to |
|---|---|
| VCC | 5 V, L24 |
| GND | ground |
| TX | GPIO 16 = R17 (hub receives) |
| RX | GPIO 17 = R16 (hub sends the 5 Hz setting) |

9600 baud NMEA. The hub asks for 5 fixes a second. Signals: GPS Speed (mph) and GPS Satellites. The antenna needs
a view of the sky; the first fix can take several minutes. The hub page shows hall speed, GPS speed and the
difference in percent.
