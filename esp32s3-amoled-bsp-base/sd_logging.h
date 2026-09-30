#ifndef SD_LOGGING_H
#define SD_LOGGING_H

#ifdef __cplusplus
extern "C" {
#endif

// Creates a new session-numbered CSV log file (same pattern as the
// WROOM board: a persisted session counter, incremented each boot) and
// writes its header row. Call once from setup(), after sd_storage_init().
// Safe to call even if the SD card failed to mount.
void sd_logging_init(void);

// Call every loop() iteration - appends a row every 500ms with the
// current value of every enabled gauge signal. No-op if the card isn't
// mounted or logging never successfully initialized.
void sd_logging_loop(void);

#ifdef __cplusplus
}
#endif

#endif
