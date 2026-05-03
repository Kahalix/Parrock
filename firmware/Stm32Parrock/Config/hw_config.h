/**
 * @file    hw_config.h
 * @brief   Hardware-specific configuration parameters.
 * @details This file defines the physical parameters of the board (e.g., I2C addresses,
 * pin mappings). Using a typed constant structure instead of preprocessor macros
 * (#define) provides type safety and allows variables to be inspected during
 * live debugging.
 */

#ifndef HW_CONFIG_H
#define HW_CONFIG_H

#include <stdint.h>

/**
 * @struct  hw_config_t
 * @brief   Container for all hardware-specific addresses and settings.
 */
typedef struct {
    uint8_t oled_i2c_addr;  /*!< I2C address of the OLED display (e.g., 0x78) */
} hw_config_t;

/* Global extern declaration so other modules can read the configuration */
extern const hw_config_t g_hw_cfg;

#endif /* HW_CONFIG_H */
