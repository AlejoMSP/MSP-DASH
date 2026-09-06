# rusEFI CAN Map (CONFIRMED via official DBC + bench capture)

Source: official rusEFI DBC file (saved as `docs/rusefi.dbc`), cross-checked
against a live capture from the CANBed sniffer on 2026-09-05.

## Configuration (confirmed via TunerStudio)
- Base address: 512 decimal (0x200), 11-bit standard frames
- Broadcast period: 50ms (20Hz)
- Bitrate: 500kbps
- CAN dash type: None (RX8 dash emulation disabled - clean single stream)

## CRITICAL: byte order is LITTLE-ENDIAN
This is the opposite of MegaSquirt (which was big-endian). Any code adapted
from the old MegaSquirt reference sketch MUST have this flipped, or every
multi-byte value will decode wrong.

## Confirmed blocks and signals

| CAN ID (dec) | CAN ID (hex) | Block name | Signals (bit offset, length, scale, unit) |
|---|---|---|---|
| 512 | 0x200 | BASE0 | WarningCounter(0,16,x1), LastError(16,16,x1), RevLimAct(32,1), MainRelayAct(33,1), FuelPumpAct(34,1), CELAct(35,1), EGOHeatAct(36,1), LambdaProtectAct(37,1), Fan(38,1), Fan2(39,1), CurrentGear(40,8), DistanceTraveled(48,16,x0.1,km) |
| 513 | 0x201 | BASE1 | **RPM(0,16,x1,RPM)**, IgnitionTiming(16,16,x0.02,deg,signed), InjDuty(32,8,x0.5,%), IgnDuty(40,8,x0.5,%), VehicleSpeed(48,8,x1,kph), FlexPct(56,8,x1,%) |
| 514 | 0x202 | BASE2 | PPS(0,16,x0.01,%,signed), TPS1(16,16,x0.01,%,signed), TPS2(32,16,x0.01,%,signed), Wastegate(48,16,x0.01,%,signed) |
| 515 | 0x203 | BASE3 | MAP(0,16,x0.0333333,kPa), **CoolantTemp(16,8,x1,-40 offset,degC)**, IntakeTemp(24,8,x1,-40,degC), AUX1Temp(32,8,x1,-40,degC), AUX2Temp(40,8,x1,-40,degC), MCUTemp(48,8,x1,-40,degC), FuelLevel(56,8,x0.5,%) |
| 516 | 0x204 | BASE4 | **OilPress(16,16,x0.0333333,kPa)**, OilTemperature(32,8,x1,-40,degC), FuelTemperature(40,8,x1,-40,degC), **BattVolt(48,16,x0.001,mV)** |
| 517 | 0x205 | BASE5 | CylAM(0,16,x1,mg), EstMAF(16,16,x0.01,kg/h), InjPW(32,16,x0.00333333,ms), KnockCt(48,16,x1,count) |
| 518 | 0x206 | BASE6 | FuelUsed(0,16,x1,g), FuelFlow(16,16,x0.005,g/s), FuelTrim1(32,16,x0.01,%,signed), FuelTrim2(48,16,x0.01,%,signed) |
| 519 | 0x207 | BASE7 | **Lam1(0,16,x0.0001,lambda)**, Lam2(16,16,x0.0001,lambda), FpLow(32,16,x0.0333333,kPa), FpHigh(48,16,x0.1,bar) |
| 520 | 0x208 | BASE8 | Cam1I/Itar/E/Etar, Cam2I/Itar/E/Etar (all 8-bit signed, deg) |
| 521 | 0x209 | BASE9 | Egt1-8 (8-bit, x5, degC) |
| 522 | 0x20A | BASE10 | knock0-7 (8-bit signed, dB) |
| 523 | 0x20B | BASE11 | BrakePedal (1 bit) |

## Gauge -> confirmed source

| Gauge | Signal | CAN ID | Formula | Status |
|---|---|---|---|---|
| RPM | RPM | 0x201 | raw x 1 | **CONFIRMED - seen in capture, RPM=0 matches engine-off bench state** |
| Coolant Temp | CoolantTemp | 0x203 | raw x 1 - 40 | **NOT YET SEEN in capture - needs longer capture to confirm broadcasting** |
| Oil Pressure | OilPress | 0x204 | raw x 0.0333333 (kPa) | **NOT YET SEEN in capture - needs longer capture to confirm broadcasting** |
| Oil Temp | OilTemperature | 0x204 | raw x 1 - 40 | Same block as OilPress - not yet seen |
| AFR | Lam1 (lambda, not AFR directly) | 0x207 | raw x 0.0001 = lambda; AFR = lambda x stoich ratio (14.7 for gasoline) | Not yet seen. Also note: this is LAMBDA, needs conversion to AFR |
| Boost | MAP minus baro | 0x203 | MAP = raw x 0.0333333 (kPa). **No baro/ambient pressure signal exists in this DBC** - boost calc needs a fixed atmospheric assumption or another source | MAP not yet seen in capture either |

## Confirmed from bench capture (2026-09-05)
- **0x200 (BASE0):** WarningCounter climbing steadily (~38,925+) - expected on a bench harness with no cranking/trigger signal. MainRelayAct=1, CurrentGear=0, DistanceTraveled=0 - all correct for stationary bench.
- **0x201 (BASE1):** RPM=0, IgnitionTiming=19.0deg, InjDuty=0%, IgnDuty=0%, VehicleSpeed=0 - all correct for engine-off bench state.
- **0x202 (BASE2):** PPS/TPS2/Wastegate all read exactly -1.00% - this is rusEFI's "sensor not connected" sentinel value. TPS1 reads a real ~0.10% (closed throttle) - confirms TPS1 IS wired and functional on this bench harness.
- **0x206 (BASE6):** all zero - correct, no fuel flow with engine off.
- **0x20A (BASE10):** all zero - correct, no knock with engine off.

## Open item: blocks 0x203, 0x204, 0x207 not seen yet
These carry exactly the sensors we need for gauges (CLT, MAP, Oil Pressure,
Battery Voltage, Lambda/AFR) but didn't appear in the initial short capture.
Two possibilities:
1. Capture window was just too short to catch their broadcast cycle - re-run
   the sniffer for 20-30+ seconds and check again.
2. These blocks may only transmit if the underlying sensor is configured in
   rusEFI's firmware settings (e.g. no CLT sensor wired/configured = no CLT
   broadcast). If still missing after a longer capture, check TunerStudio's
   sensor configuration for CLT, oil pressure, and AFR/lambda inputs.

## Oil pressure / oil temp - now HAS a real onboard signal
Unlike MegaSquirt, rusEFI's DBC has dedicated OilPress and OilTemperature
signals in BASE4 (0x204) - IF the physical sensor is wired to the ECU and
configured in TunerStudio. This replaces the earlier open question about
using spare analog inputs - confirm the sensor is wired directly and
configured under the ECU's oil pressure/temp sensor settings.
