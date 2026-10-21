# rusEFI Lua script (hub frame 0x7F0)

The hub sends CAN frame 0x7F0 4 times a second, only while it hears the ECU:

| Bytes | Meaning |
|---|---|
| 0-1 | EGT, deg C, unsigned, little endian |
| 2 | 1 = thermocouple open (not connected) |

Speed does not go through Lua: the hall sensor is wired to rusEFI directly (docs/HUB_SPEED_SENSORS.md).

Script to paste in the rusEFI console Lua tab and burn. Keep it short: the F40x Lua heap is about 13 KB and
comments count toward it.

```lua
e=Sensor.new("EGT1")
e:setTimeout(1000)
canRxAdd(0x7F0,function(b,i,d,x) if x[3]==0 then e:set(x[1]+x[2]*256) end end)
function onTick() end
```

Note: `x` is 1-based in rusEFI Lua: `x[1]` is byte 0.
