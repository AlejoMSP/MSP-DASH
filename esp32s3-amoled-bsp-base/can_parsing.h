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
  OBD-II (hub 59): answers on 0x7E8-0x7EF to the hub's mode 01 requests
  (Speeduino on its OBD CAN port, factory ECUs). The hub asks only when no
  broadcasting ECU is heard (esp32-input/obd_poll.h).
  Both ID ranges are disjoint, so both parsers always stay active.
  There is no ECU type setting (removed from the screens in build 170);
  the hub's home page shows which ECU it hears.

  Frames are received by a dedicated FreeRTOS task (can_start_task),
  not from loop(). loop() also does SD writes (open/write/close) and
  web server work, which can block for tens of milliseconds - long
  enough, at this bus's frame rate, to overflow the driver's receive
  queue and drop frames. The task runs at a higher priority than both
  loop() and the LVGL task, so nothing else can starve it. It only
  writes plain global values (same rule as before: never call LVGL
  from here).
*/

// Runtime messages are off unless SERIAL_STREAM is 1 (serial_log.h on the screens; this guard also covers the hub).
#ifndef RT_LOG
#if defined(SERIAL_STREAM) && SERIAL_STREAM
#define RT_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define RT_LOG(...) do { } while (0)
#endif
#endif
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
// EGT frame sent by the hub (esp32-input, MAX6675): bytes 0-1 unsigned LE deg C, byte 2 = 1 if thermocouple open.
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

// Pressures are shown in psi (build 161). The ECUs send kPa.
#define KPA_TO_PSI 0.1450377f
static float kpaToPsi(float kpa) { return kpa * KPA_TO_PSI; }
// MAP arrives as ABSOLUTE kPa (101.3 = the atmosphere). Shown as boost: 0 at the atmosphere, negative under vacuum
// (about -10 psi at idle), positive under boost.
static float mapKpaToBoostPsi(float kpa) { return (kpa - 101.325f) * KPA_TO_PSI; }

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
      g_vss_mph = d[6] * 0.621371f;   // DBC: VehicleSpeed 48|8@1+ (1,0) kph -> shown in mph. The hall sensor is wired to rusEFI.
      g_vss_has_data = true;
      gauge_config_mark_signal_active(SIG_VSS);
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
      g_boost_psi = mapKpaToBoostPsi(mapKpa); // boost psi: 0 = atmosphere (fixed 101.3 kPa, no baro sensor)
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
      g_oilp_psi = kpaToPsi(oilPressRaw * 0.0333333f);
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
      g_fuelp_psi = kpaToPsi(fpLowRaw * 0.0333333f);
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
      g_boost_psi = mapKpaToBoostPsi(map_);
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
      g_fuelp_psi = kpaToPsi(fuelp);
      g_fuelp_has_data = true;
      gauge_config_mark_signal_active(SIG_FUELP);
      float oilp = readU16BE(d, 2) / 10.0f; // word1
      g_oilp_psi = kpaToPsi(oilp);
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

// ---- OBD-II answers (Speeduino, factory ECUs; rusEFI answers too) ----
// The hub asks on 0x7DF (esp32-input/obd_poll.h); ECUs answer on 0x7E8-0x7EF with a single frame:
// d[0] = bytes that follow, d[1] = 0x41 (mode 01 + 0x40), d[2] = PID, d[3..] = A, B, ...
// PIDs 0x00 / 0x20 / 0x40 answer which PIDs the ECU has (a bit each, the high bit of A first): kept in g_obd_sup.
#define OBD_REQ_ID   0x7DF
#define OBD_RESP_LO  0x7E8
#define OBD_RESP_HI  0x7EF
static volatile uint32_t g_obd_sup[3] = { 0, 0, 0 };   // PIDs 0x01-0x20, 0x21-0x40, 0x41-0x60 the ECU has
static volatile uint8_t g_obd_sup_known = 0;            // bit k: g_obd_sup[k] has been answered
static volatile uint32_t g_obd_last_ms = 0;             // last answer heard
static volatile uint32_t g_obd_answers = 0;
// Is this PID in the supported list? (unknown list: assume yes)
static inline bool obd_pid_supported(uint8_t pid) {
  int k = (pid - 1) / 32;
  if (pid == 0 || k > 2) return true;
  if (!(g_obd_sup_known & (1 << k))) return true;
  return (g_obd_sup[k] >> (31 - (pid - 1) % 32)) & 1;
}
// One PID's value into the dash's signals. Returns false for a PID the dash does not use.
static bool obd_apply(uint8_t pid, const uint8_t *v, int n) {
  if (n < 1) return false;
  float A = v[0], B = n > 1 ? v[1] : 0;
  switch (pid) {
    case 0x0C: if (n < 2) return false; g_rpm = (256 * A + B) / 4; g_rpm_has_data = true; gauge_config_mark_signal_active(SIG_RPM); return true;
    case 0x05: g_clt_f = cToF(A - 40); g_clt_has_data = true; gauge_config_mark_signal_active(SIG_CLT); return true;
    case 0x0B: g_boost_psi = mapKpaToBoostPsi(A); g_boost_has_data = true; gauge_config_mark_signal_active(SIG_MAP); return true;   // absolute kPa, 255 max
    case 0x0F: g_iat_f = cToF(A - 40); g_iat_has_data = true; gauge_config_mark_signal_active(SIG_IAT); return true;
    case 0x11: g_tps = A * 100.0f / 255.0f; g_tps_has_data = true; gauge_config_mark_signal_active(SIG_TPS); return true;
    case 0x0D: g_vss_mph = A * 0.621371f; g_vss_has_data = true; gauge_config_mark_signal_active(SIG_VSS); return true;   // km/h
    case 0x42: if (n < 2) return false; g_battv = (256 * A + B) / 1000.0f; g_battv_has_data = true; gauge_config_mark_signal_active(SIG_BATTV); return true;
    case 0x24: case 0x34: if (n < 2) return false; g_afr = (256 * A + B) * 2.0f / 65536.0f * 14.7f; g_afr_has_data = true; gauge_config_mark_signal_active(SIG_AFR); return true;   // lambda x 14.7
    case 0x5C: g_oilt_f = cToF(A - 40); g_oilt_has_data = true; gauge_config_mark_signal_active(SIG_OILT); return true;
    case 0x0A: g_fuelp_psi = kpaToPsi(3 * A); g_fuelp_has_data = true; gauge_config_mark_signal_active(SIG_FUELP); return true;   // gauge kPa
    case 0x52: g_flex_pct = A * 100.0f / 255.0f; g_flex_has_data = true; gauge_config_mark_signal_active(SIG_FLEX); return true;
  }
  return false;
}
static void handleObdFrame(uint32_t id, const uint8_t *d, uint8_t len) {
  if (id < OBD_RESP_LO || id > OBD_RESP_HI || len < 4 || d[1] != 0x41) return;
  int n = d[0] - 2;                       // value bytes after the mode and PID
  if (n < 1 || n > (int)len - 3) n = len - 3;
  uint8_t pid = d[2];
  g_obd_last_ms = millis(); g_obd_answers++;
  if ((pid == 0x00 || pid == 0x20 || pid == 0x40) && n >= 4) {
    int k = pid / 32;
    g_obd_sup[k] = ((uint32_t)d[3] << 24) | ((uint32_t)d[4] << 16) | ((uint32_t)d[5] << 8) | d[6];
    g_obd_sup_known |= (1 << k);
    return;
  }
  obd_apply(pid, d + 3, n);
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
      handleObdFrame(message.identifier, message.data, message.data_length_code);   // OBD-II answers (Speeduino ...)
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
          RT_LOG("CAN: %u frames missed (queue full), %u overrun\n",
                        (unsigned)(st.rx_missed_count - lastMissed), (unsigned)st.rx_overrun_count);
          lastMissed = st.rx_missed_count;
        }
        // Standard TWAI recovery - a wiring glitch or power dip can put
        // the controller in bus-off, where it stays dead until told to
        // recover. Without this, gauges would freeze until a reboot.
        if (st.state == TWAI_STATE_BUS_OFF) {
          RT_LOG("CAN: bus-off, starting recovery\n");
          twai_initiate_recovery();
        } else if (st.state == TWAI_STATE_STOPPED) {
          RT_LOG("CAN: stopped, restarting\n");
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
