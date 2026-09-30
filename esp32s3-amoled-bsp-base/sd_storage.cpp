/*
  sd_storage.cpp
  SD card mount, ported directly from Waveshare's confirmed
  04_SD_Card/sd_card_bsp.cpp example for this exact board - the SPI
  branch specifically (their SDMMC_U alternate mode is commented out/
  unused in their own source). Uses ESP-IDF's native VFS/FAT + SD SPI
  driver, not Arduino's SD.h - mounted files are accessed via standard
  POSIX file I/O (fopen/fread/fwrite/fclose/opendir/readdir) under the
  /sd_card mount point, matching their own approach exactly.

  Confirmed pins (from their source): MISO=40, MOSI=39, CLK=41, CS=38.
*/

#include "sd_storage.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"

#define PIN_NUM_MISO  (gpio_num_t)40
#define PIN_NUM_MOSI  (gpio_num_t)39
#define PIN_NUM_CLK   (gpio_num_t)41
#define PIN_NUM_CS    (gpio_num_t)38
#define SD_SPI SPI3_HOST
#define SD_MOUNT_POINT "/sd_card"

static sdmmc_card_t *s_card = NULL;

void sd_storage_init(void) {
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = true,
    .max_files = 5,
    .allocation_unit_size = 512
  };

  spi_bus_config_t bus_cfg = {
    .mosi_io_num = PIN_NUM_MOSI,
    .miso_io_num = PIN_NUM_MISO,
    .sclk_io_num = PIN_NUM_CLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 4000
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_initialize(SD_SPI, &bus_cfg, SDSPI_DEFAULT_DMA));

  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = PIN_NUM_CS;
  slot_config.host_id = SD_SPI;

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SD_SPI;

  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card));

  if (s_card != NULL) {
    sdmmc_card_print_info(stdout, s_card);
    Serial.printf("SD card mounted, capacity: %.2f GB\n", (float)(s_card->csd.capacity) / 2048 / 1024);
  } else {
    Serial.println("SD card mount FAILED (no card, or mount error)");
  }
}

bool sd_storage_is_mounted(void) {
  return s_card != NULL;
}
