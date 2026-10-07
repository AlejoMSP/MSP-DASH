#ifndef CAN_FLAGS_H
#define CAN_FLAGS_H

/*
  can_flags.h
  Decodes the rusEFI on/off status flags. Pure C, so the PC tests can check it against the official DBC
  (docs/rusefi.dbc), which is where these positions come from:

    BASE0  (id 512 = 0x200), byte 4, one bit each, bit 0 first (Intel/little-endian, "@1+"):
        32 RevLimAct   33 MainRelayAct   34 FuelPumpAct   35 CELAct
        36 EGOHeatAct  37 LambdaProtectAct   38 Fan   39 Fan2
    BASE11 (id 523 = 0x20B), byte 0, bit 0:  BrakePedal

  The flag number (0..8) is the order of SIG_REVLIM .. SIG_BRAKE in gauge_config.h, so flag i is signal
  SIG_FIRST_FLAG + i. A frame that carries a flag as 0 is real data ("the fan is off"), which is different
  from a frame that never arrived - hence the separate "present" mask.
*/

#include <stdint.h>
#include "gauge_config.h"
#include "gauge_app.h"

#define CAN_ID_BASE0  0x200
#define CAN_ID_BASE11 0x20B

// values: bit i = flag i is on.  present: bit i = this frame carried flag i. Both 0 for any other frame.
static inline void can_decode_status_flags(uint32_t id, const uint8_t *d, uint8_t dlc, uint16_t *values, uint16_t *present) {
  *values = 0;
  *present = 0;
  if (id == CAN_ID_BASE0 && dlc >= 5) {
    *values = d[4];           // bits 0..7 = flags 0..7
    *present = 0x00FF;
  } else if (id == CAN_ID_BASE11 && dlc >= 1) {
    *values = (uint16_t)((d[0] & 1u) << 8);   // flag 8 = BrakePedal
    *present = 0x0100;
  }
}


// What the CAN task does with every received frame: update the flag globals and mark each flag the frame
// carried as "seen just now" (that is what makes a flag go stale after 2 seconds of silence, like any signal).
static inline void can_apply_status_flags(uint32_t id, const uint8_t *d, uint8_t dlc) {
  uint16_t values, present;
  can_decode_status_flags(id, d, dlc, &values, &present);
  if (!present) return;
  g_flag_bits = (uint16_t)((g_flag_bits & ~present) | (values & present));
  g_flag_seen = (uint16_t)(g_flag_seen | present);
  for (int i = 0; i < SIG_FLAG_COUNT; i++) {
    if (present & (1u << i)) gauge_config_mark_signal_active((SignalId)(SIG_FIRST_FLAG + i));
  }
}

#endif
