# PDM (power distribution module)

Parked: not started. Notes so far, to pick up later.

A CAN-controlled power distribution module for the 2002 NB2 Miata turbo, replacing relays and fuses with high-side
FET switching on one PCB (KiCad).

## Decided

- MCU: STM32 F405 / F407 class.
- Switching: high-side FETs (smart high-side switches with current sense), no mechanical relays on the board.
- Control: CAN, on the same bus as rusEFI and the MSP-DASH hub (the hub's buttons and outputs can command channels).
- Not on the PDM: ECU power (stays on its own isolated feed), power windows, door locks (the push-to-start system does them).

## Channels

| Channel | Notes |
|---|---|
| Headlights high / low | |
| Parking / DRL | |
| Tail / brake | |
| Turn signals | flasher logic in firmware |
| Reverse | |
| Fog | |
| Interior lighting | |
| Radio, switched + memory | memory feed always on |
| USB charging | |
| Fuel pump | about 20-30 A |
| Radiator fan | about 20-30 A |
| A/C compressor clutch | |
| A/C blower | |
| Horn | |
| Wipers | |

## Open

- Board approach: a carrier board first (modules on a base board), or one fully integrated board.
- Channel current ratings, wire gauges and fuse-equivalent trip limits per channel.
- CAN message layout (commands in, status and currents out).
- Enclosure, connectors, mounting spot.
