/*
  esp32-main
  Production sketch: reads rusEFI CAN frames (little-endian, per official
  rusEFI DBC), scales to real units, and pushes to a digital 7-segment style
  Nextion dashboard (12 numeric readouts: rpm, mph, tps/tpst, eth, ego,
  fuelp, oilp, oilt, map, clt, mat, vvt).

  STATUS: RPM, MAP, TPS1, BattVolt, MCUTemp formulas are BENCH-CONFIRMED
  (see docs/CAN-map.md). CoolantTemp/OilPress/OilTemp/Lambda/IntakeTemp/
  FpLow/VVT formulas are taken directly from the official DBC (high
  confidence) but several physical sensors are not yet wired on the bench
  harness - those will read 0/default until sensors are connected, which
  is a hardware state, not a parsing bug.

  Byte order: LITTLE-ENDIAN throughout (opposite of MegaSquirt/big-endian).

  Wiring:
    ESP32 GPIO21 -> CAN transceiver TX
    ESP32 GPIO22 -> CAN transceiver RX
    ESP32 UART2 (TX2/RX2, pins 17/16) -> Nextion RX/TX
    ESP32 UART1 (pins 4/5) -> GPS module (GT-U7, NEO-6M chipset, 9600 baud NMEA)
    GPS TX -> ESP32 GPIO4
    GPS RX -> ESP32 GPIO5

  mph is now sourced from GPS speed (TinyGPSPlus), not rusEFI's
  VehicleSpeed/VSS signal - the VSS byte is still parsed from CAN but no
  longer pushed to the mph field.
*/

#include "driver/twai.h"
#include <TinyGPSPlus.h>

#define CAN_TX_PIN GPIO_NUM_21
#define CAN_RX_PIN GPIO_NUM_22
#define CAN_BITRATE TWAI_TIMING_CONFIG_500KBITS()

#define GPS_RX_PIN 4 // ESP32 pin receiving GPS module's TX
#define GPS_TX_PIN 5 // ESP32 pin driving GPS module's RX
#define GPS_BAUD 9600

HardwareSerial gpsSerial(1);
TinyGPSPlus gps;
unsigned long lastGpsSend = 0;
const unsigned long GPS_SEND_INTERVAL_MS = 250; // ~4Hz display update, independent of GPS fix rate

// Confirmed rusEFI CAN IDs (base 0x200)
#define ID_BASE1 0x201 // RPM, IgnitionTiming, InjDuty, IgnDuty, VehicleSpeed, FlexPct
#define ID_BASE2 0x202 // PPS, TPS1, TPS2, Wastegate
#define ID_BASE3 0x203 // MAP, CoolantTemp, IntakeTemp, AUX1Temp, AUX2Temp, MCUTemp, FuelLevel
#define ID_BASE4 0x204 // OilPress, OilTemperature, FuelTemperature, BattVolt
#define ID_BASE7 0x207 // Lam1, Lam2, FpLow, FpHigh
#define ID_BASE8 0x208 // Cam1I, Cam1Itar, Cam1E, Cam1Etar, Cam2I, Cam2Itar, Cam2E, Cam2Etar

uint16_t readU16LE(uint8_t* data, int offset) {
  return data[offset] | (data[offset + 1] << 8);
}
int16_t readS16LE(uint8_t* data, int offset) {
  return (int16_t)(data[offset] | (data[offset + 1] << 8));
}

const unsigned long STALE_MS = 2000; // blocks round-robin at different rates

void nextionSend(const String &cmd) {
  Serial2.print(cmd);
  Serial2.write(0xFF); Serial2.write(0xFF); Serial2.write(0xFF);
  Serial.println(cmd); // debug mirror - remove once display confirmed working
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17); // matches Nextion project's baud=115200 startup setting

  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = CAN_BITRATE;
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  twai_driver_install(&g_config, &t_config, &f_config);
  twai_start();

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  Serial.println("esp32-main running - decoding rusEFI CAN + GPS -> Nextion digital dashboard");
}

void loop() {
  twai_message_t message;
  if (twai_receive(&message, pdMS_TO_TICKS(20)) == ESP_OK) {
    uint8_t* d = message.data;

    switch (message.identifier) {
      case ID_BASE1: {
        uint16_t rpm = readU16LE(d, 0);
        // VehicleSpeed (byte6, kph) from rusEFI - no longer used for mph,
        // GPS is now the speed source. Left unread since unused.
        uint8_t flexPct = d[7]; // FlexPct: byte7, %

        nextionSend("rpm.txt=\"" + String(rpm) + "\"");
        nextionSend("eth.txt=\"" + String(flexPct) + "\"");
        // mph no longer sent here - now sourced from GPS, see loop()
        break;
      }
      case ID_BASE2: {
        int16_t tpsRaw = readS16LE(d, 2); // TPS1: byte2-3, signed
        float tps = tpsRaw * 0.01f;

        nextionSend("tps.val=" + String((int)tps)); // progress bar, native 0-100 range
        nextionSend("tpst.txt=\"" + String(tps, 1) + "\"");
        break;
      }
      case ID_BASE3: {
        uint16_t mapRaw = readU16LE(d, 0);
        float mapKpa = mapRaw * 0.0333333f;
        int8_t cltRaw = (int8_t)d[2];
        float clt = cltRaw - 40;
        int8_t matRaw = (int8_t)d[3];
        float mat = matRaw - 40;

        nextionSend("map.txt=\"" + String(mapKpa, 1) + "\"");
        nextionSend("clt.txt=\"" + String(clt, 1) + "\"");
        nextionSend("mat.txt=\"" + String(mat, 1) + "\"");
        break;
      }
      case ID_BASE4: {
        uint16_t oilPressRaw = readU16LE(d, 2); // OilPress: byte2-3
        float oilPressKpa = oilPressRaw * 0.0333333f;
        int8_t oilTempRaw = (int8_t)d[4];
        float oilTemp = oilTempRaw - 40;

        nextionSend("oilp.txt=\"" + String(oilPressKpa, 1) + "\"");
        nextionSend("oilt.txt=\"" + String(oilTemp, 1) + "\"");
        break;
      }
      case ID_BASE7: {
        uint16_t lam1Raw = readU16LE(d, 0);
        float lambda = lam1Raw * 0.0001f;
        float afr = lambda * 14.7f; // gasoline stoich - adjust if E85/flex
        uint16_t fpLowRaw = readU16LE(d, 4); // FpLow: byte4-5
        float fpLowKpa = fpLowRaw * 0.0333333f;

        nextionSend("ego.txt=\"" + String(afr, 1) + "\"");
        nextionSend("fuelp.txt=\"" + String(fpLowKpa, 1) + "\"");
        break;
      }
      case ID_BASE8: {
        int8_t vvtRaw = (int8_t)d[0]; // Cam1I: byte0, signed
        nextionSend("vvt.txt=\"" + String(vvtRaw) + "\"");
        break;
      }
    }
  }

  // Feed GPS parser continuously
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // Push GPS speed to the display at a steady rate, independent of GPS fix update rate
  unsigned long now = millis();
  if (now - lastGpsSend >= GPS_SEND_INTERVAL_MS) {
    lastGpsSend = now;
    if (gps.speed.isValid()) {
      nextionSend("mph.txt=\"" + String(gps.speed.mph(), 0) + "\"");
    } else {
      nextionSend("mph.txt=\"--\""); // no GPS fix yet
    }
  }

  // TODO: check per-block millis() timestamps vs STALE_MS and show a
  // "no data" indicator on the Nextion side if a channel goes stale -
  // especially relevant since blocks round-robin at different rates.
}
