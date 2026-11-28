# Hub carrier board, rev B

The hub's ESP32 DevKitC plugs into two 19-pin sockets. On the board: the protected 12 V input, the 5 V buck module
header, CAN (SN65HVD230 module), EGT (MAX6675 module: only the thermocouple is wired outside, to its own terminal),
GPS (module on a socket, SMA bulkhead hole at the top edge for an outside antenna), an MCP23017 (16 digital inputs)
and an ADS1115 module (4 analog inputs). Everything else lives off the board and is wired to terminals: CC1101, OLED,
MPU6050, DHT22, fuel sender, knob, lights, dimmer, buttons. 148 x 112 mm, 2 layers, through-hole only.

Rev A (all modules on sockets, 5.08 mm screw terminals) is in git history.

Files: `hub-board.kicad_pro` (open in KiCad 7 or 8), `hub-board.kicad_sch`, `hub-board.kicad_pcb`,
`hub-board-gerbers.zip` (JLCPCB: 2 layers, 1.6 mm, 1 oz), `gerbers/`, `drc.rpt`.

## Connectors

| Ref | Connector | Pins |
|---|---|---|
| J3 | 12 V in: Molex Mini-Fit Jr 5569-02A2 (right angle, PCB pegs). Plug: housing 5557-02R, pins 5556 | 1 +12V, 2 GND |
| J5 | 5 V out (screens, gauge board): Mini-Fit Jr 5569-02A2 | 1 +5V, 2 GND |
| J20 | MCP23017 port A: 8 inputs | A0-A7 |
| J21 | MCP23017 port B: 8 inputs | B0-B7 |
| J22 | ESP32 inputs | FUEL (IO36), KNOB (IO34), LIGHTS (IO35), DIMMER (IO32), NEXT (IO25), PREV (IO26), IO2, GND |
| J23 | ADS1115 | A0, A1, A2, A3, 3V3, GND |
| J24 | Sensor power | 3V3, GND, 3V3, GND |
| J25 | CAN | CANH, CANL, GND |
| J26 | CC1101 (outside) | 3V3, GND, SCK (IO14), MOSI (IO13), MISO (IO39), CSN (IO27), GDO0 (IO33) |
| J27 | I2C (OLED, MPU6050) + DHT22 | 3V3, GND, SDA (IO5), SCL (IO15), DHT (IO4), GND |

Signal terminals: Phoenix MC 1,5/x-G-3.5 headers (3.5 mm, pluggable). Plugs: Phoenix MC 1,5/x-ST-3,5 (same pin count).
Not on any terminal: IO0 and IO12 (boot pins: a signal on them at power-up stops the ESP32 starting), IO16/IO17 (GPS on
the board), IO18/IO19/IO23 (MAX6675), IO21/IO22 (CAN).

## Inputs

- **MCP23017 (U1, address 0x27):** each input through 10k. Takes 12 V signals (the chip's clamp limits the current
  to under 1 mA) and switches to ground (use the internal pull-up on the Inputs page).
- **ESP32 inputs:** FUEL 1k + 3.3 V clamp (pull-up position not fitted); KNOB 1k + clamp (0-3.3 V pot: use J24 for
  3V3/GND); LIGHTS and DIMMER 47k/10k dividers + clamp + 100 nF (12 V signals); NEXT and PREV 1k (buttons to
  ground, internal pull-up); IO2 10k + clamp.
- **ADS1115:** each channel has a series resistor (1k fitted), a divider resistor to ground and a pull-up to 3.3 V
  (not fitted). 0-3.3 V sensor: as built. 0-5 V sensor: fit the divider (for example 1k series stays, 2k to ground).
  Resistive sender (fuel, temperature): fit the pull-up (for example 1k). Values per channel:

| Channel | Series | To ground | Pull-up |
|---|---|---|---|
| A0 | R26 (1k) | R27 (not fitted) | R28 (not fitted) |
| A1 | R29 (1k) | R30 (not fitted) | R31 (not fitted) |
| A2 | R32 (1k) | R33 (not fitted) | R34 (not fitted) |
| A3 | R35 (1k) | R36 (not fitted) | R37 (not fitted) |
- **Outside modules:** 100 ohm in series only (fast buses). 4k7 I2C pull-ups and a 10k DHT pull-up on the board.

## Check before ordering

1. **DevKitC row spacing:** 25.4 mm between the 19-pin rows. Measure yours.
2. **Module pin order** (square pad = pin 1): SN65HVD230 J7 3V3, GND, CTX, CRX + J8 CANH, CANL; MAX6675 J9 GND,
   VCC, SCK, CS, SO; ADS1115 J12 VDD, GND, SCL, SDA, ADDR, ALRT, A0-A3; GPS J15 VCC (5 V), RX, TX, GND; buck J4
   IN+, IN-, OUT+, OUT-.
3. **GPS antenna:** a u.FL-to-SMA bulkhead pigtail (about 10 cm) from the module to the 6.4 mm hole (H5).
4. **CAN termination:** fit JP1 only if the bus needs another 120 ohm.

## Regenerating

`tools/gen_hub_board.py` holds the parts list, placement and nets. With KiCad 7's Python:

```
python3 tools/gen_hub_board.py place                     # board with parts, and hub-board.dsn for the router
java -jar freerouting-2.0.1.jar --gui.enabled=false -de hub-board.dsn -do run.ses -mp 25 -mt 1
python3 tools/gen_hub_board.py finish hardware/hub-board/run.ses   # routes in, ground pours, DRC
```

Freerouting 2.0.1 single-threaded (`-mt 1`): multi-threaded and 2.1.0 runs left connections out of the .ses. If DRC
still lists unconnected pads, export a DSN from that board and route it once more (the second pass keeps the first
pass's tracks and adds the rest); that is how this board's `hub-board.ses` was made.
