#pragma once
#include <Arduino.h>
struct SPISettings { SPISettings(uint32_t, int, int) {} SPISettings() {} };
class SPIClass { public: SPIClass(int = 0) {} void begin(int = -1, int = -1, int = -1, int = -1) {} void end() {} uint8_t transfer(uint8_t) { return 0; }
  void beginTransaction(SPISettings) {} void endTransaction() {} void setFrequency(uint32_t) {} };
extern SPIClass SPI;
#define HSPI 2
#define VSPI 3
