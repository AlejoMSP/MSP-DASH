# Speed: hall sensor on rusEFI, GPS on the hub (hub build 19)

Pins are GPIO numbers with the Freenove breakout terminal (L = left row, R = right row). Ground goes to the
breadboard ground bar.

## Hall sensor: NJK-5002C (NPN, normally open, 5-30 V) -> rusEFI
Wired to the ECU, not the hub. rusEFI turns its pulses into vehicle speed and broadcasts it in BASE1 (0x201) byte 6
(kph); the hub reads it from CAN into the Speed signal (mph), like every other ECU value.

| Wire | Goes to |
|---|---|
| Brown (+) | the ECU's 5 V sensor supply (or switched 12 V) |
| Blue (-) | ECU sensor ground |
| Black (signal) | a digital / VSS input on the ECU |

Measured: the black wire idles at 3.58 V on a 5 V supply (the sensor pulls its own output up). Check the uaEFI
pinout that the input you pick accepts that (and 12 V, if the sensor is powered from 12 V).

In TunerStudio: set the vehicle speed sensor input to that pin, then the speed calibration (gear ratio, teeth per
revolution, tire size / wheel revolutions per km). Burn and power cycle. The Lua script does NOT handle speed.

## GPS: GT-U7 (NEO-6M compatible) -> hub
| GT-U7 pin | Goes to |
|---|---|
| VCC | 5 V, L24 |
| GND | ground bar |
| TX | GPIO 16 = R17 (hub receives) |
| RX | GPIO 17 = R16 (hub sends the 5 Hz setting) |

9600 baud NMEA, the hub asks for 5 fixes a second. Signals: GPS Speed (mph) and GPS Satellites. The antenna needs
a view of the sky; the first fix can take several minutes.

## Checking the calibration
The hub page's Speed section shows rusEFI speed, GPS speed, the difference in percent and a factor. At a steady
speed above 10 mph, multiply rusEFI's speed calibration by that factor (for example, if rusEFI reads 5 % high the
factor is 0.952).
