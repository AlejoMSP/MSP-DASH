// aircore_link.h - the bits of the hub's ESP-NOW data packet this board reads (RPM and speed). A copy, so this sketch
// builds on its own; tests/host/test_aircore.cpp checks every number here against esp32s3-amoled-bsp-base/.
#pragma once
#include <stdint.h>
#define LINK_PKT_MAGIC 0x4D534431u     // "MSD1", sent 20 times a second by the hub
#define LINK_PKT_V_OFFSET 16           // magic 4 + seq 2 + count 2 + has_mask 8: the float values start here
#define LINK_SIG_RPM 1
#define LINK_SIG_VSS 25                // mph
#define AC_MASK_SIMULATED 1ull         // bit 0: the hub's bench simulator is running
// Copy of the hub <-> aircore packets in esp32s3-amoled-bsp-base/link_packets.h (test_aircore.cpp checks they match).

// ---- stock tach / speedo board (aircore/) <-> hub (Stock gauges page), aircore build 7 / hub 64 ----
// The aircore board sends AcStatus for each of its gauges every half second (and right after a command); the hub's
// page sends AcCmd. Both broadcast; a command names the board's MAC (FF:FF:FF:FF:FF:FF = every board).
#define AC_STATUS_MAGIC 0x4D534153u   // "MSAS"
#define AC_CMD_MAGIC    0x4D534143u   // "MSAC"
#define AC_POINTS 12
enum { AC_MODE_LIVE = 0, AC_MODE_SWEEP = 1, AC_MODE_TARGET = 2, AC_MODE_ANGLE = 3 };
enum { AC_OP_LIVE = 1, AC_OP_SWEEP, AC_OP_TARGET, AC_OP_ANGLE, AC_OP_PWM, AC_OP_DECAY, AC_OP_STRENGTH, AC_OP_SLEW,
       AC_OP_RANGE, AC_OP_MARK, AC_OP_MARK_HERE, AC_OP_UNMARK, AC_OP_CLEARMARKS, AC_OP_DEFAULTS, AC_OP_ZERO, AC_OP_SPAN };
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t build, gauge, gauges, sig;         // this gauge's index on the board, how many it has, LINK_SIG_RPM / _VSS
  uint8_t mode, slow, hubHeard, has;          // AC_MODE_*, slow decay, the hub's data heard, the value is live
  uint32_t pwmHz;
  float needle, value, target, sweepS, strength, slew, minV, maxV, zero, span;
  uint8_t npts, ch, pad[2];
  float pv[AC_POINTS], pa[AC_POINTS];         // calibration marks: value, coil angle
} AcStatus;
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t mac[6];                             // the board it is for (all FF: every board)
  uint8_t gauge, op;                          // gauge index on that board, AC_OP_*
  float a, b;
} AcCmd;
