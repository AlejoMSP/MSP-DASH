#pragma once
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdio>
using String = std::string;            // ArduinoJson reads/writes std::string natively
extern uint32_t g_fake_millis;
static inline uint32_t millis() { return g_fake_millis; }
