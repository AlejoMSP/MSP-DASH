// obd_poll.h (hub) - asks an OBD-II ECU for its values (Speeduino on its OBD CAN port, factory ECUs), hub 59 on.
// Broadcasting ECUs (rusEFI, MegaSquirt, Link) send everything by themselves; Speeduino's OBD port only answers
// questions. The hub asks on 0x7DF, one PID at a time, and can_parsing.h reads the answers (0x7E8-0x7EF).
//   Auto (default): asks only while no broadcasting ECU is heard. Always. Off.
//   Not answering yet: one "which PIDs do you have" question a second (single-shot frames, so an empty bus is not
//   flooded with retries). Answering: first the supported lists (0x00, 0x20, 0x40), then RPM, MAP and TPS every
//   round, speed and lambda every other round, the temperatures, battery, fuel pressure and ethanol in turn.
// Pure C: host-tested (tests/host/test_obd.cpp).
#pragma once
#include <stdint.h>
#include <stdbool.h>

enum { OBD_AUTO = 0, OBD_ALWAYS = 1, OBD_OFF = 2, OBD_MODES };
static const char *const OBD_MODE_NAMES[OBD_MODES] = { "Auto: when no broadcasting ECU is heard", "Always", "Off" };
#define OBD_TIMEOUT_MS 60     // no answer by then: ask the next one
#define OBD_GAP_MS 4          // at least this between questions (about 100 a second at most)
#define OBD_PROBE_MS 1000     // not answering: one question a second
#define OBD_ALIVE_MS 2000     // an answer within this: the ECU is there

static const uint8_t OBD_FAST[] = { 0x0C, 0x0B, 0x11 };                 // RPM, MAP, TPS
static const uint8_t OBD_MID[] = { 0x0D, 0x34, 0x24 };                  // speed, lambda (two PIDs ECUs use)
static const uint8_t OBD_SLOW[] = { 0x05, 0x0F, 0x42, 0x5C, 0x0A, 0x52 };   // coolant, intake air, battery, oil temp, fuel pressure, ethanol
static const uint8_t OBD_SUP[] = { 0x00, 0x20, 0x40 };                  // which PIDs it has

typedef struct {
  uint8_t sup;          // supported lists asked so far
  uint8_t slot;         // place in the round: 0-2 fast, 3 mid, 4 slow
  uint8_t mid, slow;    // turn within the mid and slow lists
  bool waiting;         // a question is out
  bool wasAlive;
  uint32_t sentMs, answersAtSend;
} ObdPoll;
static inline void obd_init(ObdPoll *p) { p->sup = p->slot = p->mid = p->slow = 0; p->waiting = p->wasAlive = false; p->sentMs = 0; p->answersAtSend = 0; }
static inline bool obd_should_poll(int mode, bool broadcastEcuHeard) { return mode == OBD_ALWAYS || (mode == OBD_AUTO && !broadcastEcuHeard); }

// The next PID to ask now, or -1 to wait. alive: an answer heard within OBD_ALIVE_MS. answers: answers heard so far.
// supported(pid): from the ECU's lists (true while unknown). Call obd_sent() after sending.
static inline int obd_next(ObdPoll *p, uint32_t now, bool alive, uint32_t answers, bool (*supported)(uint8_t)) {
  if (!alive) {   // not answering (yet): probe once a second, start over when it answers
    p->wasAlive = false;
    if (p->sentMs && now - p->sentMs < OBD_PROBE_MS) return -1;
    p->sup = 0; return 0x00;
  }
  if (!p->wasAlive) { p->wasAlive = true; p->sup = 0; p->slot = 0; p->waiting = false; }
  if (p->waiting && answers == p->answersAtSend && now - p->sentMs < OBD_TIMEOUT_MS) return -1;
  if (now - p->sentMs < OBD_GAP_MS) return -1;
  while (p->sup < sizeof OBD_SUP) {   // the supported lists first (0x20 / 0x40 only if the one before says it has them)
    uint8_t pid = OBD_SUP[p->sup++];
    if (pid == 0x00 || supported(pid)) return pid;
  }
  for (int tries = 0; tries < 16; tries++) {   // the round: fast x3, one mid, one slow; skip what it does not have
    uint8_t pid;
    if (p->slot < 3) pid = OBD_FAST[p->slot];
    else if (p->slot == 3) { pid = OBD_MID[p->mid]; p->mid = (p->mid + 1) % sizeof OBD_MID; }
    else { pid = OBD_SLOW[p->slow]; p->slow = (p->slow + 1) % sizeof OBD_SLOW; }
    p->slot = (p->slot + 1) % 5;
    if (supported(pid)) return pid;
  }
  return -1;
}
static inline void obd_sent(ObdPoll *p, uint32_t now, uint32_t answers) { p->waiting = true; p->sentMs = now ? now : 1; p->answersAtSend = answers; }
// The request frame's 8 bytes: 2 bytes follow, mode 01, the PID, padding.
static inline void obd_request(uint8_t pid, uint8_t out[8]) { out[0] = 0x02; out[1] = 0x01; out[2] = pid; for (int i = 3; i < 8; i++) out[i] = 0x55; }
