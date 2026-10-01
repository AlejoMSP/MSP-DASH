/*
  ecu_config.cpp
  Persisted ECU selection, same NVS/Preferences pattern as the WROOM
  board (esp32-main uses a "sysconf"-style namespace with
  getUChar/putUChar for the same purpose there).
*/

#include "ecu_config.h"
#include <Preferences.h>

static Preferences prefs;
static EcuSelection s_current = ECU_RUSEFI;

void ecu_config_init(void) {
  prefs.begin("ecuconf", false);
  s_current = (EcuSelection)prefs.getUChar("ecu", ECU_RUSEFI);
}

EcuSelection ecu_config_get(void) {
  return s_current;
}

void ecu_config_set(EcuSelection sel) {
  s_current = sel;
  prefs.putUChar("ecu", (uint8_t)sel);
}
