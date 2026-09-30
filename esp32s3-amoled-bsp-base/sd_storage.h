#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mounts the SD card via ESP-IDF's native VFS/FAT + SPI driver - ported
// directly from Waveshare's confirmed 04_SD_Card example for this exact
// board (MISO=40, MOSI=39, CLK=41, CS=38, mounted at /sd_card). Call
// once from setup(). Safe to call even if no card is inserted - later
// operations just report failure rather than crashing.
void sd_storage_init(void);

// True if the card mounted successfully.
bool sd_storage_is_mounted(void);

#ifdef __cplusplus
}
#endif

#endif
