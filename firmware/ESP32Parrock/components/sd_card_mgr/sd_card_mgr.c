#include "sd_card_mgr.h"
#include <stdio.h>
#include <string.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_common.h"
#include "board_config.h"
#include "esp_log.h"

static const char *TAG = "SD_CARD";

static sdmmc_card_t *sd_card = NULL;

esp_err_t sd_card_init(void) {
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Initializing SD card...");

    // Options for mounting the FAT filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, // We only want to read existing files
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI(TAG, "Initializing SPI bus...");
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_SD_MOSI, 
        .miso_io_num = PIN_SD_MISO, 
        .sclk_io_num = PIN_SD_CLK,  
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    // Initialize the SPI bus using SPI2_HOST
    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus.");
        return ret;
    }

    // Configure the SPI device slot for the SD card
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_SD_CS; 
    slot_config.host_id = SPI2_HOST;

    ESP_LOGI(TAG, "Mounting filesystem...");
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT; // Try 20MHz first

    // Mount the filesystem
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &sd_card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. Card might be formatted incorrectly (Use FAT32).");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). Check wiring and pull-up resistors.", esp_err_to_name(ret));
        }
        // Cleanup SPI bus if mount fails. sd_card remains NULL.
        spi_bus_free(SPI2_HOST);
        return ret;
    }

    // Card has been initialized successfully, print its properties
    ESP_LOGI(TAG, "Filesystem mounted successfully at %s", MOUNT_POINT);
    sdmmc_card_print_info(stdout, sd_card);

    return ESP_OK;
}

void sd_card_deinit(void) {
    // Only attempt to unmount and free the bus if it was successfully initialized
    if (sd_card != NULL) {
        // Unmount the FAT filesystem
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, sd_card);
        ESP_LOGI(TAG, "Card unmounted.");
        
        // Free the SPI bus
        spi_bus_free(SPI2_HOST);
        ESP_LOGI(TAG, "SPI bus freed.");
        
        sd_card = NULL;
    } else {
        // Safe fallback if called without a successful initialization
        ESP_LOGI(TAG, "Deinit skipped: Card was not mounted.");
    }
}