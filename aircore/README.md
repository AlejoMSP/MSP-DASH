# aircore: the stock NB tach and speedo

## Wemos D1 Mini (ESP8266): one board per gauge (build 2)

Each gauge gets its own D1 Mini and its own DRV8833. Tell each board once which gauge it is: `role tach` or
`role speedo` in the serial monitor (it starts as the tach).

| D1 Mini pin | GPIO | DRV8833 |
|---|---|---|
| D1 | 5 | IN1 |
| D2 | 4 | IN2 |
| D5 | 14 | IN3 |
| D6 | 12 | IN4 |
| 5V | | VCC (VM), from the 5 V buck converter; the D1 Mini can take its own 5 V from the same supply |
| G | | GND |

OUT1 / OUT2 to the gauge's sine coil, OUT3 / OUT4 to its cosine coil; EEP / nSLEEP (if the board has it) to 3.3 V.
None of D1, D2, D5, D6 is a boot pin, so the DRV8833 cannot stop the D1 Mini from starting.

First time only, add the ESP8266 boards to arduino-cli (PowerShell):

```powershell
cls
arduino-cli config add board_manager.additional_urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core update-index
arduino-cli core install esp8266:esp8266
```

Build and flash one board (its COM port in place of COM7):

```powershell
cls
cd C:\Users\alejo\OneDrive\GIT\MSP-DASH
git pull origin main
arduino-cli compile --clean --fqbn esp8266:esp8266:d1_mini --build-path C:\esp32build-aircore8266 C:\Users\alejo\OneDrive\GIT\MSP-DASH\aircore
arduino-cli upload -p COM7 --fqbn esp8266:esp8266:d1_mini --input-dir C:\esp32build-aircore8266 C:\Users\alejo\OneDrive\GIT\MSP-DASH\aircore
arduino-cli monitor -p COM7 -c baudrate=115200
```

Then calibrate as below (only its own gauge's commands work on it).

## ESP32-S2 Mini: both gauges on one board

Drives the stock air-core tach and speedometer from the hub's data. It only listens: it picks up the hub's ESP-NOW
data packet (RPM and speed) on whatever Wi-Fi channel the hub is on, so it needs no network and no setup on the hub.

## Parts

- Lolin / Wemos **ESP32-S2 Mini**
- Two **DRV8833** dual H-bridge boards (one per gauge)
- A 12 V to **5 V** buck converter (the DRV8833 takes 2.7 to 10.8 V: never the car's 12 V)

Each gauge has two coils, sine and cosine, about 155 ohm each. At 5 V that is about 32 mA a coil.

## Wiring

| S2 Mini | DRV8833 #1 (tach) | | S2 Mini | DRV8833 #2 (speedo) |
|---|---|---|---|---|
| GPIO 3 | IN1 | | GPIO 11 | IN1 |
| GPIO 5 | IN2 | | GPIO 12 | IN2 |
| GPIO 7 | IN3 | | GPIO 16 | IN3 |
| GPIO 9 | IN4 | | GPIO 18 | IN4 |

- OUT1 / OUT2: the gauge's sine coil. OUT3 / OUT4: its cosine coil.
- VCC (VM): 5 V from the buck converter. GND: common with the S2 Mini.
- EEP / nSLEEP, if the board has one: to 3.3 V, or the board stays asleep.
- The 8 pins use all 8 of the S2's PWM channels (20 kHz, so the coils do not whine).

Which coil is which, and which way round, only changes where zero is and which way the needle turns: both are set
over the serial port, no rewiring needed.

## Build and flash (PowerShell)

```powershell
cls
cd C:\Users\alejo\OneDrive\GIT\MSP-DASH
git pull origin main
arduino-cli compile --clean --fqbn esp32:esp32:lolin_s2_mini --build-path C:\esp32build-aircore C:\Users\alejo\OneDrive\GIT\MSP-DASH\aircore
arduino-cli upload -p COM7 --fqbn esp32:esp32:lolin_s2_mini --input-dir C:\esp32build-aircore C:\Users\alejo\OneDrive\GIT\MSP-DASH\aircore
arduino-cli monitor -p COM7 -c baudrate=115200
```

Use the S2 Mini's own COM port (COM7 here is an example). If it does not show up, hold its 0 button, tap RST, let go.

## Calibration (serial monitor, type `help`)

1. `tach angle 0`: the needle goes where coil angle 0 points. Step it (`tach angle 10`, `tach angle -20`, ...)
   until it sits on the dial's 0. That number is the zero: `tach zero <it>`.
2. `tach angle <zero + 240>` and adjust until it sits on 8000. The difference from zero is the span: `tach span <it>`
   (negative if the needle turns the wrong way).
3. Same for the speedo, with `speedo max` set to the dial's top (150 mph by default).
4. `tach release`, `speedo release`, then `sweep` to check. `show` lists everything; every change is saved.

Defaults: tach 0-8000 RPM, speedo 0-150 mph, both 240 degrees, strength 0.8. Needles move at most 720 (tach) and 360
(speedo) degrees a second, and go back to the start of the scale 1.5 s after the hub goes quiet.
