#pragma once
#include "Arduino.h"
#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
struct UpdateClass { bool begin(size_t) { return true; } size_t write(uint8_t *, size_t n) { return n; } bool end(bool = false) { return true; } bool hasError() { return false; } template <typename T> void printError(T &) {} } extern Update;
