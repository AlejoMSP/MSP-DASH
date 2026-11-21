// aircore_link.h - the bits of the hub's ESP-NOW data packet this board reads (RPM and speed). A copy, so this sketch
// builds on its own; tests/host/test_aircore.cpp checks every number here against esp32s3-amoled-bsp-base/.
#pragma once
#include <stdint.h>
#define LINK_PKT_MAGIC 0x4D534431u     // "MSD1", sent 20 times a second by the hub
#define LINK_PKT_V_OFFSET 16           // magic 4 + seq 2 + count 2 + has_mask 8: the float values start here
#define LINK_SIG_RPM 1
#define LINK_SIG_VSS 25                // mph
#define AC_MASK_SIMULATED 1ull         // bit 0: the hub's bench simulator is running
