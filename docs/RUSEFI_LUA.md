# rusEFI Lua script (hub frames 0x7F0 and the ECU map frame)

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

## ECU maps (hub 53, Buttons page)

A button set to **ECU map (next)** moves to the next map on each press. The hub sends the map frame (CAN id set on the
Buttons page, default 0x7F1) 4 times a second while it hears the ECU, and right after a press:

| Byte | Meaning |
|---|---|
| 0 | map number, 1 = the first |
| 1 | how many maps |

The hub remembers the map, so after a power cycle the ECU gets the same map back within a second. The names live on the
hub (the screens show "Map 2: Boost"); what each map DOES is in the script below.

Script with both frames (replaces the one above). Each row of `M` is one map: timing added (deg), fuel multiplier,
boost target added (kPa). Map 1 should be your normal tune (0, 1, 0). Change the numbers to suit, one row per map on
the hub. Lua gauge 1 shows the map in use (log it to check).

```lua
e=Sensor.new("EGT1")
e:setTimeout(1000)
canRxAdd(0x7F0,function(b,i,d,x) if x[3]==0 then e:set(x[1]+x[2]*256) end end)
M={{0,1,0},{0,1,20}}
m=1
canRxAdd(0x7F1,function(b,i,d,x) if M[x[1]] then m=x[1] end end)
function onTick()
local t=M[m]
setTimingAdd(t[1])
setFuelMult(t[2])
setBoostTargetAdd(t[3])
setLuaGauge(1,m)
end
```

Before burning: check that your rusEFI firmware has these Lua functions (console Lua tab: a call that does not exist
stops the script with an error). If the hub's frame stops (hub off), the ECU stays on the last map it got. A map number
with no row in `M` is ignored. Bench test first: change maps with the button and watch Lua gauge 1 and the timing in
TunerStudio before driving.
