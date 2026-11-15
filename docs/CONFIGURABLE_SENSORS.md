# Configurable sensors (design)

**Status (build 171 / hub 22):** stage 1 done, with CAN as the first source. 16 custom slots (SIG_C1..SIG_C16,
`custom_slots.h`) set up on the hub's `/custom` page: name, unit, decimals, gauge min/max, and a CAN field (ID,
start byte, 8/16/32 bit or 1 bit, byte order, signed, scale, offset) with a live preview. Names reach the screens
in MSN1 packets (kept in each screen's NVS); values ride in the normal data packet (has_mask is now 64 bits).
"Make a sensor" on the hub's Sensors page opens a slot for a frame nothing decodes. Next: GPIO sources (stage 2),
CAN presets from the DBC (rest of stage 3), then moving the fixed hub sensors onto slots (stage 4).

Goal: add or change a sensor from a web page on the hub, with no reflash of the hub or the screens.

## 1. Sensor slots
- The fixed signal list becomes 32 slots. Each slot has: name, unit, decimals, source, and its settings.
- The hub stores the slots in NVS and sends their names, units and decimals to the screens over ESP-NOW (a new "MSN1" names packet, sent on change and every 10 s).
- Screens show slot names from that packet. The gauge editor lists slots, not a hard-coded list.
- The current fixed signals stay working until the slots replace them (migration step).

## 2. Hub inputs (GPIO)
Pick a GPIO from a list of usable pins, then a type:
- Analog linear: volts to value (two points: V1 = value1, V2 = value2).
- Analog thermistor: pull-up value + curve (preset tables, or 3 points for Steinhart-Hart).
- Analog table: up to 8 points volts to value (fuel senders, pressure senders).
- Digital on/off: pull-up or pull-down, active high or low.
- Frequency: pulses to value (speed, Hall, RPM), pulses per unit setting.
- Chip drivers: DHT22 (temp + humidity), MAX6675 (EGT, 3 pins).
- Input range: 0-3.3 V, or 0-5 V / 0-12 V with a divider; the page corrects for the divider.
- Templates fill in the settings: GM coolant temp, Bosch 0-5 V pressure, Hall speed, fuel sender, brake switch.

Rules the page enforces:
- Analog only on ADC1 pins (GPIO 32-39); ADC2 does not work with WiFi/ESP-NOW on.
- GPIO 34-39 are input only, no internal pull-up.
- A pin can be used once. Pins taken by CAN, buttons and brightness are reserved.

## 3. CAN inputs
- Pick a frame ID from the IDs seen on the bus (from the raw CAN tracker), or type one.
- Settings: start byte, length (8/16/32 bit or a bit for flags), byte order, signed, scale, offset.
- Live preview of the decoded value while editing.
- Presets: rusEFI dash frames (from docs/rusefi.dbc), MegaSquirt/Link 0x5F0 block.

## 4. Wiring help
- Each type shows a small wiring diagram and plain steps.
- 0-5 V sensor: divider shown with values (example 10k top / 20k bottom: 5 V in = 3.33 V at the pin).
- 12 V signal: 47k / 10k divider (as the lights and dimmer inputs).
- Thermistor: pull-up to 3.3 V, value shown (example 2.2k for a GM sensor).
- Hall: open collector, pull-up to 3.3 V, signal / 5-12 V supply / ground wires.

## 5. Used GPIOs pop-up
- "Show used GPIOs" button opens a list: every pin, what uses it, free or reserved, and what it can do (analog, digital, input only).

## Build order
1. Slots on the screens + names packet (current signals become the first slots).
2. Hub GPIO sensors page + used GPIOs pop-up + wiring help.
3. CAN sensors page with presets.
4. Remove the hard-coded hub sensors once the slots match them.


## Stage 2: hub inputs (hub build 48)

On the hub's **Inputs and pins** page, a switch or an analog sensor on any free GPIO (analog: 32-36, 39) or MCP23017
pin fills a custom slot: presets for common pressure senders (0.5-4.5 V), 12 V / 5 V dividers and NTC temperature
sensors, or a custom two-point line with a divider multiplier. Buttons on any input run the same actions as the page
buttons (press and 1 s hold). A slot filled by an input cannot be changed on /custom.
