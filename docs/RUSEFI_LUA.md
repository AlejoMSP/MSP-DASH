# rusEFI Lua script (hub frame 0x7F0)

The hub sends one CAN frame, ID 0x7F0, 10 times a second, only while it hears the ECU:

| Bytes | Meaning |
|---|---|
| 0-1 | EGT, deg C, unsigned, little endian |
| 2 | 1 = thermocouple open (not connected) |
| 3-4 | Vehicle speed from the hall sensor, kph x 10, unsigned, little endian (hub build 18 on) |

Script to paste in the rusEFI console Lua tab and burn. Keep it short: the F40x Lua heap is about 13 KB and
comments count toward it.

```lua
e=Sensor.new("EGT1")
e:setTimeout(1000)
v=Sensor.new("VehicleSpeed")
v:setTimeout(1000)
canRxAdd(0x7F0,function(b,i,d,x) if x[3]==0 then e:set(x[1]+x[2]*256) end v:set((x[4]+x[5]*256)/10) end)
function onTick() end
```

Notes
- `x` is 1-based in rusEFI Lua: `x[1]` is byte 0.
- Like EGT, rusEFI's own speed input must be off, or it reports the sensor as already registered:
  set the vehicle speed sensor input pin to NONE in TunerStudio, burn, power cycle.
- The hub no longer reads rusEFI's own speed byte (BASE1 byte 6): it would just echo the hub's number back.
