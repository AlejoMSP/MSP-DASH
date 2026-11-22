#!/bin/bash
# Compile check of the air-core gauge sketch (aircore/) against the PC stubs. Run from tests/host.
T=$(mktemp -d)
(echo '#include <Arduino.h>'; cat ../../aircore/aircore.ino) > $T/main.cpp
if g++ -std=c++17 -fsyntax-only -Istubs -Istubs_hub -I../../aircore $T/main.cpp; then echo "OK aircore sketch"; else echo "FAILED aircore sketch"; exit 1; fi
if g++ -std=c++17 -fsyntax-only -DESP8266 -Istubs_8266 -Istubs -I../../aircore $T/main.cpp; then echo "OK aircore sketch (ESP8266)"; else echo "FAILED aircore sketch (ESP8266)"; exit 1; fi
if g++ -std=c++17 -fsyntax-only -DCONFIG_IDF_TARGET_ESP32 -Istubs -Istubs_hub -I../../aircore $T/main.cpp; then echo "OK aircore sketch (ESP32 DevKitC)"; else echo "FAILED aircore sketch (ESP32 DevKitC)"; exit 1; fi
