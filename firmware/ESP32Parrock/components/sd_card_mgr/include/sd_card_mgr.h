#ifndef SD_CARD_MGR_H
#define SD_CARD_MGR_H

#include "esp_err.h"

// Mount point path for the SD Card
#define MOUNT_POINT "/sdcard"

/**
 * @brief Initialize the SPI bus and mount the SD card FAT filesystem.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t sd_card_init(void);

/**
 * @brief Unmount the filesystem and free the SPI bus.
 * Should be called before going to Deep Sleep.
 */
void sd_card_deinit(void);

#endif // SD_CARD_MGR_H