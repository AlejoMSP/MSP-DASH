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
// current value of every signal enabled in the Log Channels config.
// Only writes rows once RPM exceeds 800 (engine running) - no-op
// otherwise, or if the card isn't mounted / logging never initialized.
void sd_logging_loop(void);

// True only while rows are actually being written (RPM above the
// logging threshold) - drives the on-screen blinking logging
// indicator in gauge_app.c.
bool sd_logging_is_active(void);

#ifdef __cplusplus
}
#endif

#endif
