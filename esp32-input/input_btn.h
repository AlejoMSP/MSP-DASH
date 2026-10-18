// input_btn.h - a debounced button. In its own header so the Arduino build's auto-generated prototypes
// (placed at the top of the .ino) can see the type.
#pragma once
#include <Arduino.h>
struct Btn { uint8_t pin; bool last; uint32_t changed; };
