#!/bin/bash
# Compile check of the air-core gauge sketch (aircore/) against the PC stubs. Run from tests/host.
T=$(mktemp -d)
(echo '#include <Arduino.h>'; cat ../../aircore/aircore.ino) > $T/main.cpp
if g++ -std=c++17 -fsyntax-only -Istubs -Istubs_hub -I../../aircore $T/main.cpp; then echo "OK aircore sketch"; else echo "FAILED aircore sketch"; exit 1; fi
