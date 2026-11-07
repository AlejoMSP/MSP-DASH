#!/bin/bash
# Compile check of the hub sketch (esp32-input) against host stubs: catches C++ errors before Daniel compiles it.
# Copies the shared headers in the same way the flash command does. Run from tests/host.
set -e
H=../../esp32-input; S=../../esp32s3-amoled-bsp-base; T=$(mktemp -d)
cp $H/*.ino $H/*.h $T/
for f in can_parsing.h can_flags.h gauge_config.h link_packets.h simulator.h simulator.cpp custom_slots.h color_presets.h button_actions.h gps_time.h; do cp $S/$f $T/; done
(echo '#include "hub_extra.h"'; cat $T/esp32-input.ino) > $T/main.cpp
if g++ -std=c++17 -fsyntax-only -w -Istubs_hub -Istubs -I$T $T/main.cpp 2> $T/err.txt; then echo "OK hub sketch"; else head -40 $T/err.txt; echo "FAILED hub sketch"; exit 1; fi
