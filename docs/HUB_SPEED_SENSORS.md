# Hub speed sensors (hub build 18)

## Hall sensor: NJK-5002C (NPN, normally open, 5-30 V, with magnet)
| Wire | Goes to |
|---|---|
| Brown (+) | 5 V (anything 5-30 V works) |
| Blue (-) | ground |
| Black (signal) | GPIO 27, plus a 10k resistor from GPIO 27 to 3.3 V |

The sensor pulls the signal to ground when the magnet passes and lets go otherwise, so GPIO 27 never sees more
than 3.3 V. Check first: power the sensor with the black wire connected to nothing and measure black to ground.
About 0 V or floating = open collector, wire it as above. If it reads the supply voltage, this one has a built-in
pull-up: then add a divider (10k from black to GPIO 27, 20k from GPIO 27 to ground) and leave out the 10k to 3.3 V.

Speed = pulses per second / pulses per mile x 3600. Pulses per mile is set on the hub page (default 4000).
"Calibrate from GPS": drive steady above 10 mph with a GPS fix, press it, and it sets pulses per mile so the hall
speed matches the GPS. Speed goes into the Speed signal and, in kph, to rusEFI in frame 0x7F0 (docs/RUSEFI_LUA.md).

## GPS: GT-U7 (NEO-6M compatible)
| GT-U7 pin | Goes to |
|---|---|
| VCC | 5 V |
| GND | ground |
| TX | GPIO 16 (hub receives) |
| RX | GPIO 17 (hub sends the 5 Hz setting) |

9600 baud NMEA. The hub asks for 5 fixes a second. Signals: GPS Speed (mph) and GPS Satellites. The antenna needs
a view of the sky; the first fix can take several minutes. The hub page shows hall speed, GPS speed and the
difference in percent.
