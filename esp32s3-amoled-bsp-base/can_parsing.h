#ifndef CAN_PARSING_H
#define CAN_PARSING_H

/*
  can_parsing.h
  CAN frame decoding for both supported ECU protocols - extracted from
  the main .ino to keep setup()/loop() as the only thing living there,
  per project reorganization. Included only by the main .ino (never by
  any other .cpp/.c file), so defining full function bodies here
  (rather than just declarations) is safe - no risk of a "multiple
  definition" linker error, since this amounts to one translation unit.

  rusEFI: little-endian, base 0x200 (bench-confirmed, cross-checked
  against the actual rusEFI DBC file).
  MegaSquirt/LinkECU: big-endian, base 0x5F0 - shared parser, ported
  directly from the WROOM board's confirmed handleSecondaryEcuFrame.
  Both ID ranges are disjoint, so both parsers stay active
  simultaneously regardless of the ECU selection in ecu_config.h/.cpp
  (which is purely a display label, not a parsing gate).

  Frames are received by a dedicated FreeRTOS task (can_start_task),
  not from loop(). loop() also does SD writes (open/write/close) and
  web server work, which can block for tens of milliseconds - long
  enough, at this bus's frame rate, to overflow the driver's receive
  queue and drop frames. The task runs at a higher priority than both
  loop() and the LVGL task, so nothing else can starve it. It only
  writes plain global values (same rule as before: never call LVGL
  from here).
*/

#include <Arduino.h>
#include "driver/twai.h"
#include "gauge_app.h"
#include "gauge_config.h"
#include "web_monitor.h"
#include "simulator.h"
#include "can_flags.h"

// ---- rusEFI CAN IDs ----
#define RUSEFI_BASE1 0x201
#define RUSEFI_BASE2 0x202
#define RUSEFI_BASE3 0x203
#define RUSEFI_BASE4 0x204
#define RUSEFI_BASE7 0x207
// PLACEHOLDER: no real CAN ID yet. Change this once the EGT source is known. Assumed: bytes 0-1, unsigned LE, deg C.
#define CAN_ID_EGT_PLACEHOLDER 0x7F0

// ---- MegaSquirt / LinkECU shared CAN IDs (base 1520 decimal / 0x5F0) ----
#define SEC_BASE      1520        // 0x5F0
#define SEC_BLOCK0    (SEC_BASE+0)  // Seconds, PW1, PW2, RPM
#define SEC_BLOCK2    (SEC_BASE+2)  // BARO, MAP, MAT, CLT
#define SEC_BLOCK3    (SEC_BASE+3)  // TPS, BATT, EGO1
#define SEC_BLOCK13   (SEC_BASE+13) // FUELP, OILP, OILT
#define SEC_BLOCK47   (SEC_BASE+47) // Ethanol %

static float cToF(float c) {
  return c * 9.0f / 5.0f + 32.0f;
}

static uint16_t readU16LE(uint8_t *d, int offset) {
  return d[offset] | (d[offset + 1] << 8);
}

static int16_t readS16LE(uint8_t *d, int offset) {
  return (int16_t)(d[offset] | (d[offset + 1] << 8));
}

static uint16_t readU16BE(uint8_t *d, int offset) {
  return (d[offset] << 8) | d[offset + 1];
}

static void handleRusefiFrame(uint32_t id, uint8_t *d) {
  switch (id) {
    case RUSEFI_BASE1: {
      g_rpm = readU16LE(d, 0);
      g_rpm_has_data = true;
      gauge_config_mark_signal_active(SIG_RPM);
      g_flex_pct = d[7];
      g_flex_has_data = true;
      gauge_config_mark_signal_active(SIG_FLEX);
      break;
    }
    case RUSEFI_BASE2: {
      int16_t tpsRaw = readS16LE(d, 2);
      g_tps = tpsRaw * 0.01f;
      g_tps_has_data = true;
      gauge_config_mark_signal_active(SIG_TPS);
      break;
    }
    case RUSEFI_BASE3: {
      uint16_t mapRaw = readU16LE(d, 0);
      float mapKpa = mapRaw * 0.0333333f;
      g_boost_kpa = mapKpa; // raw MAP, no baro subtraction - gauge displays 0-250 with 100 as the "ambient/zero" reference point
      g_boost_has_data = true;
      gauge_config_mark_signal_active(SIG_MAP);
      uint8_t cltRaw = d[2]; // CONFIRMED against rusEFI's actual DBC: unsigned (8@1+), offset -40
      g_clt_f = cToF(cltRaw - 40);
      g_clt_has_data = true;
      gauge_config_mark_signal_active(SIG_CLT);
      uint8_t iatRaw = d[3]; // rusEFI BASE3 byte 3: IAT, unsigned, offset -40 (same as CLT). Confirm against the DBC.
      g_iat_f = cToF(iatRaw - 40);
      g_iat_has_data = true;
      gauge_config_mark_signal_active(SIG_IAT);
      break;
    }
    case RUSEFI_BASE4: {
      uint16_t oilPressRaw = readU16LE(d, 2);
      g_oilp_kpa = oilPressRaw * 0.0333333f;
      g_oilp_has_data = true;
      gauge_config_mark_signal_active(SIG_OILP);
      uint8_t oilTempRaw = d[4]; // same unsigned-vs-signed fix as CLT above
      g_oilt_f = cToF(oilTempRaw - 40);
      g_oilt_has_data = true;
      gauge_config_mark_signal_active(SIG_OILT);
      uint16_t battRaw = readU16LE(d, 6);
      g_battv = battRaw * 0.001f;
      g_battv_has_data = true;
      gauge_config_mark_signal_active(SIG_BATTV);
      break;
    }
    case RUSEFI_BASE7: {
      uint16_t lam1Raw = readU16LE(d, 0);
      float lambda = lam1Raw * 0.0001f;
      g_afr = lambda * 14.7f;
      g_afr_has_data = true;
      gauge_config_mark_signal_active(SIG_AFR);
      uint16_t fpLowRaw = readU16LE(d, 4);
      g_fuelp_kpa = fpLowRaw * 0.0333333f;
      g_fuelp_has_data = true;
      gauge_config_mark_signal_active(SIG_FUELP);
      break;
    }
    case CAN_ID_EGT_PLACEHOLDER: {
      g_egt_f = cToF((float)readU16LE(d, 0));
      g_egt_has_data = true;
      gauge_config_mark_signal_active(SIG_EGT);
      break;
    }
  }
}

static void handleSecondaryEcuFrame(uint32_t id, uint8_t *d) {
  switch (id) {
    case SEC_BLOCK0: {
      g_rpm = readU16BE(d, 6); // word3: Seconds/PW1/PW2/RPM
      g_rpm_has_data = true;
      gauge_config_mark_signal_active(SIG_RPM);
      break;
    }
    case SEC_BLOCK2: {
      float map_ = readU16BE(d, 2) / 10.0f; // word1
      g_boost_kpa = map_;
      g_boost_has_data = true;
      gauge_config_mark_signal_active(SIG_MAP);
      float clt = cToF(readU16BE(d, 6) / 10.0f); // word3 - assumes Celsius source config, matching WROOM board's noted caveat
      g_clt_f = clt;
      g_clt_has_data = true;
      gauge_config_mark_signal_active(SIG_CLT);
      break;
    }
    case SEC_BLOCK3: {
      float tps = readU16BE(d, 0) / 10.0f; // word0
      g_tps = tps;
      g_tps_has_data = true;
      gauge_config_mark_signal_active(SIG_TPS);
      float battV = readU16BE(d, 2) / 10.0f; // word1
      g_battv = battV;
      g_battv_has_data = true;
      gauge_config_mark_signal_active(SIG_BATTV);
      float ego1 = readU16BE(d, 4) / 10.0f; // word2 - already AFR units per WROOM board's source comment, not lambda
      g_afr = ego1;
      g_afr_has_data = true;
      gauge_config_mark_signal_active(SIG_AFR);
      break;
    }
    case SEC_BLOCK13: {
      float fuelp = readU16BE(d, 0) / 10.0f; // word0
      g_fuelp_kpa = fuelp;
      g_fuelp_has_data = true;
      gauge_config_mark_signal_active(SIG_FUELP);
      float oilp = readU16BE(d, 2) / 10.0f; // word1
      g_oilp_kpa = oilp;
      g_oilp_has_data = true;
      gauge_config_mark_signal_active(SIG_OILP);
      float oilt = cToF(readU16BE(d, 4) / 10.0f); // word2 - assumes Celsius source config, matching WROOM board's noted caveat
      g_oilt_f = oilt;
      g_oilt_has_data = true;
      gauge_config_mark_signal_active(SIG_OILT);
      break;
    }
    case SEC_BLOCK47: {
      float eth = readU16BE(d, 0) / 10.0f; // word0
      g_flex_pct = eth;
      g_flex_has_data = true;
      gauge_config_mark_signal_active(SIG_FLEX);
      break;
    }
  }
}

// ---- Dedicated CAN receive task ----
#define CAN_TASK_STACK_BYTES 4096
#define CAN_TASK_PRIORITY    3   // above loop() (1) and the LVGL task (2)
#define CAN_TASK_CORE        1   // same core setup() runs on, where the TWAI interrupt was installed (WiFi lives on core 0)
#define CAN_STATUS_CHECK_MS  2000

static void canReceiveTask(void *arg) {
  uint32_t lastMissed = 0;
  unsigned long lastStatusCheck = 0;

  for (;;) {
    twai_message_t message;
    if (twai_receive(&message, pdMS_TO_TICKS(100)) == ESP_OK) {
      sim_notify_real_frame(); // real traffic always wins: it switches the bench simulator off
      web_monitor_track_can_frame(message.identifier, message.data, message.data_length_code);
      handleRusefiFrame(message.identifier, message.data);
      can_apply_status_flags(message.identifier, message.data, message.data_length_code);   // fan, check engine ... (see can_flags.h)
      handleSecondaryEcuFrame(message.identifier, message.data);
    }

    unsigned long now = millis();
    if (now - lastStatusCheck >= CAN_STATUS_CHECK_MS) {
      lastStatusCheck = now;
      twai_status_info_t st;
      if (twai_get_status_info(&st) == ESP_OK) {
        // Frames the driver had to drop because its queue was full.
        // Should stay at 0 now - if this ever prints, the queue or
        // task priority needs another look.
        if (st.rx_missed_count != lastMissed) {
          Serial.printf("CAN: %u frames missed (queue full), %u overrun\n",
                        (unsigned)(st.rx_missed_count - lastMissed), (unsigned)st.rx_overrun_count);
          lastMissed = st.rx_missed_count;
        }
        // Standard TWAI recovery - a wiring glitch or power dip can put
        // the controller in bus-off, where it stays dead until told to
        // recover. Without this, gauges would freeze until a reboot.
        if (st.state == TWAI_STATE_BUS_OFF) {
          Serial.println("CAN: bus-off, starting recovery");
          twai_initiate_recovery();
        } else if (st.state == TWAI_STATE_STOPPED) {
          Serial.println("CAN: stopped, restarting");
          twai_start();
        }
      }
    }
  }
}

// Call once from setup(), right after twai_start() succeeds.
static void can_start_task() {
  xTaskCreatePinnedToCore(canReceiveTask, "CAN", CAN_TASK_STACK_BYTES, NULL, CAN_TASK_PRIORITY, NULL, CAN_TASK_CORE);
}

#endif
