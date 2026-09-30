#ifndef ECU_CONFIG_H
#define ECU_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

// Matches the WROOM board's enum exactly (esp32-main) - rusEFI is
// handled by a completely separate parser/ID range on both boards;
// MegaSquirt and LinkECU share one CAN ID range and one parser (same
// format-overlap finding from the WROOM board), so this selection is
// purely a label for the web UI - both parsers can safely stay active
// simultaneously since the ID ranges never overlap.
typedef enum { ECU_RUSEFI = 0, ECU_MEGASQUIRT = 1, ECU_LINKECU = 2 } EcuSelection;

// Loads the persisted selection from NVS (defaults to rusEFI if never
// set before) - call once from setup().
void ecu_config_init(void);

// Returns the current selection.
EcuSelection ecu_config_get(void);

// Updates the selection and persists it to NVS.
void ecu_config_set(EcuSelection sel);

// Human-readable name, for display on the web page.
const char *ecu_config_name(EcuSelection sel);

#ifdef __cplusplus
}
#endif

#endif
