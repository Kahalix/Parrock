/**
 * @file    hw_config.c
 * @brief   Implementation of the hardware configuration.
 */

#include "hw_config.h"

/**
 * @brief   Global hardware configuration instance.
 * @details Declared as 'const' to ensure it is placed in Flash memory (ROM)
 * rather than consuming valuable RAM.
 */
const hw_config_t g_hw_cfg = {
    .oled_i2c_addr = 0x78
};
