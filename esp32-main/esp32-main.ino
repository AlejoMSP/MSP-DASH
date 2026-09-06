/*
  esp32-main
  Production sketch: reads rusEFI CAN frames (little-endian, per official
  rusEFI DBC), scales to real units, and pushes both needle angle and
  numeric text to the Nextion display.

  STATUS: RPM, MAP, TPS1, BattVolt, MCUTemp formulas are BENCH-CONFIRMED
  (see docs/CAN-map.md). CoolantTemp/OilPress/OilTemp/Lambda formulas are
  taken directly from the official DBC (high confidence) but the physical
  sensors are not yet wired on the bench harness - values will read
  0 degC / 0 kPa / 0.0000 lambda until sensors are connected, that is
  a hardware state, not a parsing bug.

  Byte order: LITTLE-ENDIAN throughout (opposite of MegaSquirt/big-endian).

  Wiring:
    ESP32 GPIO21 -> CAN transceiver TX
    ESP32 GPIO22 -> CAN transceiver RX
    ESP32 UART2 (TX2/RX2) -> Nextion RX/TX
*/

#include "driver/twai.h"

#define CAN_TX_PIN GPIO_NUM_21
#define CAN_RX_PIN GPIO_NUM_22
#define CAN_BITRATE TWAI_TIMING_CONFIG_500KBITS()

// Confirmed rusEFI CAN IDs (base 0x200)
#define ID_BASE0 0x200 // WarningCounter, LastError, flags, CurrentGear, DistanceTraveled
#define ID_BASE1 0x201 // RPM, IgnitionTiming, InjDuty, IgnDuty, VehicleSpeed, FlexPct
#define ID_BASE2 0x202 // PPS, TPS1, TPS2, Wastegate
#define ID_BASE3 0x203 // MAP, CoolantTemp, IntakeTemp, AUX1Temp, AUX2Temp, MCUTemp, FuelLevel
#define ID_BASE4 0x204 // OilPress, OilTemperature, FuelTemperature, BattVolt
#define ID_BASE7 0x207 // Lam1, Lam2, FpLow, FpHigh

// Little-endian 16-bit unsigned read
uint16_t readU16LE(uint8_t* data, int offset) {
  return data[offset] | (data[offset + 1] << 8);
}
// Little-endian 16-bit signed read
int16_t readS16LE(uint8_t* data, int offset) {
  return (int16_t)(data[offset] | (data[offset + 1] << 8));
}

unsigned long lastSeenRpm = 0, lastSeenMap = 0, lastSeenOil = 0, lastSeenLambda = 0;
const unsigned long STALE_MS = 2000; // some blocks round-robin slower than others

// Boost calc: no baro signal exists in the rusEFI DBC. Using a fixed
// atmospheric assumption for now - revisit once you decide how to handle
// altitude/weather variation (e.g. capture MAP at known-engine-off as a
// live baro estimate instead of a hardcoded constant).
#define ASSUMED_BARO_KPA 101.3f

void nextionSend(const String &cmd) {
  Serial2.print(cmd);
  Serial2.write(0xFF); Serial2.write(0xFF); Serial2.write(0xFF);
}

void updateGauge(const char* gaugeName, const char* textName, float realValue, float rangeMin, float rangeMax, int angleMax = 270) {
  int angle = (int)((realValue - rangeMin) / (rangeMax - rangeMin) * angleMax);
  angle = constrain(angle, 0, angleMax);
  nextionSend(String(gaugeName) + ".val=" + String(angle));
  nextionSend(String(textName) + ".txt=\"" + String(realValue, 1) + "\"");

  // Debug output so this is verifiable on Serial Monitor without the
  // Nextion display connected yet. Remove or comment out once the
  // display is wired up and confirmed working, to reduce serial noise.
  Serial.printf("%s = %.2f (angle %d)\n", textName, realValue, angle);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17); // adjust RX/TX pins to your wiring

  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = CAN_BITRATE;
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  twai_driver_install(&g_config, &t_config, &f_config);
  twai_start();

  Serial.println("esp32-main running - decoding rusEFI CAN + pushing to Nextion");
}

void loop() {
  twai_message_t message;
  if (twai_receive(&message, pdMS_TO_TICKS(20)) == ESP_OK) {
    unsigned long now = millis();
    uint8_t* d = message.data;

    switch (message.identifier) {
      case ID_BASE1: {
        uint16_t rpm = readU16LE(d, 0);
        updateGauge("g_rpm", "t_rpm", rpm, 0, 8000);
        lastSeenRpm = now;
        break;
      }
      case ID_BASE3: {
        uint16_t mapRaw = readU16LE(d, 0);
        float mapKpa = mapRaw * 0.0333333f;
        float boost = mapKpa - ASSUMED_BARO_KPA;
        int8_t cltRaw = (int8_t)d[2]; // byte-aligned per DBC
        float clt = cltRaw - 40;

        updateGauge("g_boost", "t_boost", boost, -15, 25);
        updateGauge("g_clnttemp", "t_clnttemp", clt, 100, 260); // will read far outside range until CLT sensor is wired
        lastSeenMap = now;
        break;
      }
      case ID_BASE4: {
        uint16_t oilPressRaw = readU16LE(d, 2); // OilPress starts at byte2 (bit16), not byte0
        float oilPressKpa = oilPressRaw * 0.0333333f;
        uint16_t battRaw = readU16LE(d, 6); // BattVolt starts at byte6 (bit48)
        float battV = battRaw * 0.001f;

        updateGauge("g_oilpress", "t_oilpress", oilPressKpa, 0, 700); // kPa range placeholder, adjust to your sensor's real range
        // BattVolt available as t_battv.txt if you add a battery voltage field to the HMI
        nextionSend("t_battv.txt=\"" + String(battV, 2) + "\"");
        Serial.printf("BattVolt = %.2fV\n", battV);
        lastSeenOil = now;
        break;
      }
      case ID_BASE7: {
        uint16_t lam1Raw = readU16LE(d, 0);
        float lambda = lam1Raw * 0.0001f;
        float afr = lambda * 14.7f; // gasoline stoich - adjust if running E85/flex fuel
        updateGauge("g_afr", "t_afr", afr, 10, 20);
        lastSeenLambda = now;
        break;
      }
    }
  }

  // TODO: check millis() - lastSeen* > STALE_MS per channel and show a
  // "no data" indicator on the Nextion side if a channel goes stale.
  // Especially relevant here since blocks round-robin at different rates.
}
