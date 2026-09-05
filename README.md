# MSP-DASH

Custom digital gauge cluster for a turbo Miata, built on:
- ESP32 (CAN parsing via TWAI driver + Nextion serial control)
- Nextion 5" HMI display (analog dial-style gauges)
- rusEFI ECU, CAN broadcast stream (configured in TunerStudio)

Starting fresh — structure and code to be built up from here.

## Status
- ECU: rusEFI, CAN base address 0x200, 11-bit standard frames, 500kbps, 50ms broadcast period (confirmed via TunerStudio)
- Per-parameter frame layout: not yet confirmed — pending bench sniff-and-compare against live TunerStudio readings
- Bench tooling: PSA3018C USB-CAN analyzer for direct PC-side sniffing
