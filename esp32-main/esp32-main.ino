/*
  esp32-main
  Production sketch: reads CAN frames from EITHER rusEFI OR MegaSquirt
  (no mode switch needed - their CAN ID ranges don't overlap, so whichever
  ECU is actually connected is simply the one whose frames show up), plus
  GPS speed, and pushes all values to a digital 7-segment style Nextion
  dashboard (12 numeric readouts: rpm, mph, tps/tpst, eth, ego, fuelp,
  oilp, oilt, map, clt, mat, vvt).

  STATUS:
    rusEFI  - RPM/MAP/TPS1/BattVolt/MCUTemp are BENCH-CONFIRMED (real
              hardware, real values matched). CoolantTemp/OilPress/OilTemp/
              Lambda/IntakeTemp/FpLow/VVT come straight from the official
              DBC (high confidence, not yet bench-verified with live
              sensors - those fields read 0/default until wired).
    MegaSquirt - scale factors (/10, big-endian) and block layout are taken
              from a previously working reference sketch, not independently
              bench-verified in THIS project. Reasonably high confidence,
              but if you switch to MegaSquirt, running the CAN sniffer
              against it once (same sniff-and-compare method used for
              rusEFI) is worth doing before fully trusting these values.

  See docs/CAN-map.md for full signal tables and confirmation history.
*/


// =====================================================================
// includes
// =====================================================================

#include "driver/twai.h"
#include <TinyGPSPlus.h>


// =====================================================================
// pin / wiring configuration
// =====================================================================

// CAN transceiver (SN65HVD230 or MCP2551 + level shifting on RX)
#define CAN_TX_PIN GPIO_NUM_21
#define CAN_RX_PIN GPIO_NUM_22
#define CAN_BITRATE TWAI_TIMING_CONFIG_500KBITS()

// Nextion display, UART2
#define NEXTION_RX_PIN 16
#define NEXTION_TX_PIN 17
#define NEXTION_BAUD 115200 // must match Nextion project's baud= startup setting

// GPS module (GT-U7 / NEO-6M chipset), UART1
#define GPS_RX_PIN 4  // ESP32 pin receiving GPS module's TX
#define GPS_TX_PIN 5  // ESP32 pin driving GPS module's RX
#define GPS_BAUD 9600


// =====================================================================
// rusEFI CAN IDs (base 0x200 / 512 decimal, confirmed via TunerStudio)
// Byte order: LITTLE-ENDIAN
// =====================================================================

#define RUSEFI_BASE1 0x201 // RPM, IgnitionTiming, InjDuty, IgnDuty, VehicleSpeed, FlexPct
#define RUSEFI_BASE2 0x202 // PPS, TPS1, TPS2, Wastegate
#define RUSEFI_BASE3 0x203 // MAP, CoolantTemp, IntakeTemp, AUX1Temp, AUX2Temp, MCUTemp, FuelLevel
#define RUSEFI_BASE4 0x204 // OilPress, OilTemperature, FuelTemperature, BattVolt
#define RUSEFI_BASE7 0x207 // Lam1, Lam2, FpLow, FpHigh
#define RUSEFI_BASE8 0x208 // Cam1I, Cam1Itar, Cam1E, Cam1Etar, Cam2I, Cam2Itar, Cam2E, Cam2Etar

#define ASSUMED_BARO_KPA 101.3f // rusEFI's DBC has no baro signal - fixed assumption per project decision


// =====================================================================
// MegaSquirt CAN IDs (base 1520 decimal / 0x5F0, from working reference
// sketch - see docs/CAN-map.md for the source and confidence caveat)
// Byte order: BIG-ENDIAN
// =====================================================================

#define MS_BASE      1520        // 0x5F0
#define MS_BLOCK0    (MS_BASE+0) // Seconds, PW1, PW2, RPM
#define MS_BLOCK2    (MS_BASE+2) // BARO, MAP, MAT, CLT
#define MS_BLOCK3    (MS_BASE+3) // TPS, BATT, EGO1
#define MS_BLOCK13   (MS_BASE+13)// FUELP, OILP, OILT
#define MS_BLOCK42   (MS_BASE+42)// VSS1 (not used - mph is GPS-sourced)
#define MS_BLOCK44   (MS_BASE+44)// VVT angle
#define MS_BLOCK47   (MS_BASE+47)// Ethanol %


// =====================================================================
// globals
// =====================================================================

HardwareSerial gpsSerial(1);
TinyGPSPlus gps;
unsigned long lastGpsSend = 0;
const unsigned long GPS_SEND_INTERVAL_MS = 250; // ~4Hz display update, independent of GPS fix rate

const unsigned long STALE_MS = 2000; // blocks round-robin at different rates (rusEFI); reserved for future staleness check


// =====================================================================
// byte-reading helpers
// =====================================================================

// rusEFI: little-endian
uint16_t readU16LE(uint8_t* data, int offset) {
  return data[offset] | (data[offset + 1] << 8);
}
int16_t readS16LE(uint8_t* data, int offset) {
  return (int16_t)(data[offset] | (data[offset + 1] << 8));
}

// MegaSquirt: big-endian
uint16_t readU16BE(uint8_t* data, int offset) {
  return (data[offset] << 8) | data[offset + 1];
}
int16_t readS16BE(uint8_t* data, int offset) {
  return (int16_t)((data[offset] << 8) | data[offset + 1]);
}


// =====================================================================
// Nextion output
// =====================================================================

void nextionSend(const String &cmd) {
  Serial2.print(cmd);
  Serial2.write(0xFF); Serial2.write(0xFF); Serial2.write(0xFF);
  Serial.println(cmd); // debug mirror - remove once display confirmed working
}


// =====================================================================
// rusEFI frame handling
// =====================================================================

void handleRusefiFrame(uint32_t id, uint8_t* d) {
  switch (id) {
    case RUSEFI_BASE1: {
      uint16_t rpm = readU16LE(d, 0);
      // VehicleSpeed (byte6, kph) intentionally not read - mph is GPS-sourced
      uint8_t flexPct = d[7]; // FlexPct: byte7, %

      nextionSend("rpm.txt=\"" + String(rpm) + "\"");
      nextionSend("eth.txt=\"" + String(flexPct) + "\"");
      break;
    }
    case RUSEFI_BASE2: {
      int16_t tpsRaw = readS16LE(d, 2); // TPS1: byte2-3, signed
      float tps = tpsRaw * 0.01f;

      nextionSend("tps.val=" + String((int)tps)); // progress bar, native 0-100 range
      nextionSend("tpst.txt=\"" + String(tps, 1) + "\"");
      break;
    }
    case RUSEFI_BASE3: {
      uint16_t mapRaw = readU16LE(d, 0);
      float mapKpa = mapRaw * 0.0333333f;
      float boost = mapKpa - ASSUMED_BARO_KPA; // no baro signal in rusEFI DBC - fixed assumption
      int8_t cltRaw = (int8_t)d[2];
      float clt = cltRaw - 40;
      int8_t matRaw = (int8_t)d[3];
      float mat = matRaw - 40;

      nextionSend("map.txt=\"" + String(mapKpa, 1) + "\"");
      nextionSend("clt.txt=\"" + String(clt, 1) + "\"");
      nextionSend("mat.txt=\"" + String(mat, 1) + "\"");
      break;
    }
    case RUSEFI_BASE4: {
      uint16_t oilPressRaw = readU16LE(d, 2); // OilPress: byte2-3
      float oilPressKpa = oilPressRaw * 0.0333333f;
      int8_t oilTempRaw = (int8_t)d[4];
      float oilTemp = oilTempRaw - 40;

      nextionSend("oilp.txt=\"" + String(oilPressKpa, 1) + "\"");
      nextionSend("oilt.txt=\"" + String(oilTemp, 1) + "\"");
      break;
    }
    case RUSEFI_BASE7: {
      uint16_t lam1Raw = readU16LE(d, 0);
      float lambda = lam1Raw * 0.0001f;
      float afr = lambda * 14.7f; // gasoline stoich - adjust if E85/flex
      uint16_t fpLowRaw = readU16LE(d, 4); // FpLow: byte4-5
      float fpLowKpa = fpLowRaw * 0.0333333f;

      nextionSend("ego.txt=\"" + String(afr, 1) + "\"");
      nextionSend("fuelp.txt=\"" + String(fpLowKpa, 1) + "\"");
      break;
    }
    case RUSEFI_BASE8: {
      int8_t vvtRaw = (int8_t)d[0]; // Cam1I: byte0, signed
      nextionSend("vvt.txt=\"" + String(vvtRaw) + "\"");
      break;
    }
  }
}


// =====================================================================
// MegaSquirt frame handling
// =====================================================================

void handleMegasquirtFrame(uint32_t id, uint8_t* d) {
  switch (id) {
    case MS_BLOCK0: {
      float rpm = readU16BE(d, 6); // word3: Seconds/PW1/PW2/RPM
      nextionSend("rpm.txt=\"" + String((int)rpm) + "\"");
      break;
    }
    case MS_BLOCK2: {
      float baro = readU16BE(d, 0) / 10.0f; // word0
      float map_ = readU16BE(d, 2) / 10.0f; // word1
      float mat  = readU16BE(d, 4) / 10.0f; // word2
      float clt  = readU16BE(d, 6) / 10.0f; // word3
      float boost = map_ - baro; // real baro available here, unlike rusEFI

      nextionSend("map.txt=\"" + String(map_, 1) + "\"");
      nextionSend("mat.txt=\"" + String(mat, 1) + "\"");
      nextionSend("clt.txt=\"" + String(clt, 1) + "\"");
      break;
    }
    case MS_BLOCK3: {
      float tps  = readU16BE(d, 0) / 10.0f; // word0
      float ego1 = readU16BE(d, 4) / 10.0f; // word2 - already AFR units per source sketch, not lambda

      nextionSend("tps.val=" + String((int)tps));
      nextionSend("tpst.txt=\"" + String(tps, 1) + "\"");
      nextionSend("ego.txt=\"" + String(ego1, 1) + "\"");
      break;
    }
    case MS_BLOCK13: {
      float fuelp = readU16BE(d, 0) / 10.0f; // word0
      float oilp  = readU16BE(d, 2) / 10.0f; // word1
      float oilt  = readU16BE(d, 4) / 10.0f; // word2

      nextionSend("fuelp.txt=\"" + String(fuelp, 1) + "\"");
      nextionSend("oilp.txt=\"" + String(oilp, 1) + "\"");
      nextionSend("oilt.txt=\"" + String(oilt, 1) + "\"");
      break;
    }
    case MS_BLOCK44: {
      float vvt = readU16BE(d, 0) / 10.0f; // word0
      nextionSend("vvt.txt=\"" + String(vvt, 1) + "\"");
      break;
    }
    case MS_BLOCK47: {
      float eth = readU16BE(d, 0) / 10.0f; // word0
      nextionSend("eth.txt=\"" + String(eth, 1) + "\"");
      break;
    }
  }
}


// =====================================================================
// GPS handling
// =====================================================================

void updateGps() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  unsigned long now = millis();
  if (now - lastGpsSend >= GPS_SEND_INTERVAL_MS) {
    lastGpsSend = now;
    if (gps.speed.isValid()) {
      nextionSend("mph.txt=\"" + String(gps.speed.mph(), 0) + "\"");
    } else {
      nextionSend("mph.txt=\"--\""); // no GPS fix yet
    }
  }
}


// =====================================================================
// setup / loop
// =====================================================================

void setup() {
  Serial.begin(115200);
  Serial2.begin(NEXTION_BAUD, SERIAL_8N1, NEXTION_RX_PIN, NEXTION_TX_PIN);

  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = CAN_BITRATE;
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  twai_driver_install(&g_config, &t_config, &f_config);
  twai_start();

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  Serial.println("esp32-main running - rusEFI + MegaSquirt + GPS -> Nextion digital dashboard");
}

void loop() {
  twai_message_t message;
  if (twai_receive(&message, pdMS_TO_TICKS(20)) == ESP_OK) {
    uint32_t id = message.identifier;
    uint8_t* d = message.data;

    if (id >= 0x200 && id <= 0x20B) {
      handleRusefiFrame(id, d);
    } else if (id >= 1520 && id <= 1520 + 52) {
      handleMegasquirtFrame(id, d);
    }
  }

  updateGps();

  // TODO: check per-block millis() timestamps vs STALE_MS and show a
  // "no data" indicator on the Nextion side if a channel goes stale.
}
