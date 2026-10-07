/*
  Tests the ECU on/off status flag decoder (can_flags.h) two ways:
    1. hand-written frames: every flag alone, all on, all off, other bytes ignored, wrong id / short frame ignored
    2. a CONTRACT test against the official rusEFI DBC (docs/rusefi.dbc): the flag order in gauge_config.h and the
       bit positions the decoder assumes must be exactly what the DBC says.
  Run from the repo root:  ./t [path/to/rusefi.dbc]
*/
#include "gauge_config.h"
#include "can_flags.h"
#include <stdio.h>
#include <string.h>

static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)

static void frame(uint32_t id, const uint8_t *d, uint8_t dlc, uint16_t wantValues, uint16_t wantPresent, const char *what) {
  uint16_t v = 0xFFFF, p = 0xFFFF;
  can_decode_status_flags(id, d, dlc, &v, &p);
  char msg[160];
  snprintf(msg, sizeof(msg), "%s: values 0x%03X present 0x%03X (got 0x%03X / 0x%03X)", what, wantValues, wantPresent, v, p);
  CHECK(v == wantValues && p == wantPresent, msg);
}

int main(int argc, char **argv) {
  const char *dbc = argc > 1 ? argv[1] : "docs/rusefi.dbc";

  printf("1. status byte of BASE0 (0x200), byte 4\n");
  static const char *NAMES[9] = { "rev limiter", "main relay", "fuel pump", "check engine", "O2 heater", "lambda protect", "fan 1", "fan 2", "brake" };
  for (int i = 0; i < 8; i++) {
    uint8_t d[8] = { 0, 0, 0, 0, (uint8_t)(1u << i), 0, 0, 0 };
    char w[80]; snprintf(w, sizeof(w), "only %s on", NAMES[i]);
    frame(0x200, d, 8, (uint16_t)(1u << i), 0x00FF, w);
  }
  { uint8_t d[8] = { 0, 0, 0, 0, 0xFF, 0, 0, 0 }; frame(0x200, d, 8, 0x00FF, 0x00FF, "all eight on"); }
  { uint8_t d[8] = { 0, 0, 0, 0, 0x00, 0, 0, 0 }; frame(0x200, d, 8, 0x0000, 0x00FF, "all eight OFF is real data: present, value 0"); }
  { uint8_t d[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF }; frame(0x200, d, 8, 0x0000, 0x00FF, "every OTHER byte set, byte 4 clear: no flag on (warning counter, gear, distance are not flags)"); }
  { uint8_t d[8] = { 0, 0, 0, 0, 0x28, 0, 0, 0 }; frame(0x200, d, 8, 0x0028, 0x00FF, "check engine + O2 heater"); }
  { uint8_t d[5] = { 0, 0, 0, 0, 0x40 }; frame(0x200, d, 5, 0x0040, 0x00FF, "a 5-byte frame is enough (the flags are in byte 4)"); }
  { uint8_t d[4] = { 0, 0, 0, 0 }; frame(0x200, d, 4, 0, 0, "a 4-byte frame cannot hold byte 4: ignored"); }

  printf("2. BrakePedal in BASE11 (0x20B), byte 0 bit 0\n");
  { uint8_t d[1] = { 0x01 }; frame(0x20B, d, 1, 0x0100, 0x0100, "brake pressed"); }
  { uint8_t d[1] = { 0x00 }; frame(0x20B, d, 1, 0x0000, 0x0100, "brake released is real data"); }
  { uint8_t d[8] = { 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }; frame(0x20B, d, 8, 0x0000, 0x0100, "only bit 0 counts"); }
  { uint8_t d[8] = { 0 }; frame(0x20B, d, 0, 0, 0, "an empty frame is ignored"); }

  printf("3. other frames are none of its business\n");
  { uint8_t d[8] = { 0, 0, 0, 0, 0xFF, 0, 0, 0 }; frame(0x201, d, 8, 0, 0, "BASE1"); frame(0x204, d, 8, 0, 0, "BASE4"); frame(0x5F0, d, 8, 0, 0, "a MegaSquirt id"); frame(0x201 + 0x100, d, 8, 0, 0, "0x301"); }

  printf("4. CONTRACT: our flag order and bit positions vs the official rusEFI DBC (%s)\n", dbc);
  // flag i must be signal SIG_FIRST_FLAG + i, and the DBC must put it at bit 32 + i of BASE0 (BrakePedal: bit 0 of BASE11)
  static const struct { int sig; const char *dbcName; int msg; int startBit; } EXPECT[9] = {
    { SIG_REVLIM, "RevLimAct", 512, 32 }, { SIG_MAINRELAY, "MainRelayAct", 512, 33 }, { SIG_FUELPUMP, "FuelPumpAct", 512, 34 },
    { SIG_CEL, "CELAct", 512, 35 }, { SIG_EGOHEAT, "EGOHeatAct", 512, 36 }, { SIG_LAMBDAPROT, "LambdaProtectAct", 512, 37 },
    { SIG_FAN, "Fan", 512, 38 }, { SIG_FAN2, "Fan2", 512, 39 }, { SIG_BRAKE, "BrakePedal", 523, 0 } };
  CHECK(SIG_FLAG_COUNT == 9, "nine flag signals");
  for (int i = 0; i < 9; i++) CHECK(EXPECT[i].sig == SIG_FIRST_FLAG + i, "signal order matches the table");
  CHECK(CAN_ID_BASE0 == 512 && CAN_ID_BASE11 == 523, "ids 0x200 / 0x20B are DBC messages 512 / 523");
  FILE *f = fopen(dbc, "r");
  if (!f) { printf("  FAIL: cannot open %s (run from the repo root, or pass the path)\n", dbc); fail++; }
  else {
    char line[256]; int curMsg = -1; int found[9] = { 0 };
    while (fgets(line, sizeof(line), f)) {
      int id; char name[64];
      if (sscanf(line, "BO_ %d %63[^:]", &id, name) == 2) { curMsg = id; continue; }
      int start, len; char order, sign;
      if (sscanf(line, " SG_ %63s : %d|%d@%c%c", name, &start, &len, &order, &sign) == 5) {
        for (int i = 0; i < 9; i++) {
          if (strcmp(name, EXPECT[i].dbcName) != 0 || curMsg != EXPECT[i].msg) continue;
          found[i] = 1;
          char m[160];
          snprintf(m, sizeof(m), "DBC: %s is in message %d at bit %d (we decode bit %d of that message)", name, curMsg, start, EXPECT[i].startBit);
          CHECK(start == EXPECT[i].startBit, m);
          snprintf(m, sizeof(m), "DBC: %s is a single bit, little-endian, unsigned", name);
          CHECK(len == 1 && order == '1' && sign == '+', m);
        }
      }
    }
    fclose(f);
    for (int i = 0; i < 9; i++) { char m[100]; snprintf(m, sizeof(m), "the DBC defines %s", EXPECT[i].dbcName); CHECK(found[i], m); }
    // ... and the decoder agrees with that table end to end
    for (int i = 0; i < 9; i++) {
      uint8_t d[8] = { 0 }; uint16_t v, p;
      if (EXPECT[i].msg == 512) { d[EXPECT[i].startBit / 8] = (uint8_t)(1u << (EXPECT[i].startBit % 8)); can_decode_status_flags(0x200, d, 8, &v, &p); }
      else { d[0] = (uint8_t)(1u << EXPECT[i].startBit); can_decode_status_flags(0x20B, d, 8, &v, &p); }
      char m[100]; snprintf(m, sizeof(m), "a frame with only %s's DBC bit set turns on exactly flag %d", EXPECT[i].dbcName, i);
      CHECK(v == (uint16_t)(1u << i), m);
    }
  }

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
