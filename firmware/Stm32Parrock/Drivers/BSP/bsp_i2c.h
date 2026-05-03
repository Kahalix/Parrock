/**
 * @file    bsp_i2c.h
 * @brief   Board Support Package for I2C communication.
 * @details This module isolates the application and device drivers from the vendor-specific
 * HAL (Hardware Abstraction Layer). If the MCU is changed in the future, only
 * this BSP file needs to be updated.
 */

#ifndef BSP_I2C_H
#define BSP_I2C_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief   Initializes the I2C BSP module using Dependency Injection.
 * @param   i2c_handle: Pointer to the hardware-specific I2C instance (e.g., &hi2c1).
 * Passed as void* to avoid exposing HAL types in the header.
 */
void BSP_I2C_Init(void *i2c_handle);

/**
 * @brief   Transmits data over the I2C bus in blocking mode.
 * @param   dev_addr: 8-bit I2C device address.
 * @param   data: Pointer to the data buffer to be transmitted.
 * @param   size: Number of bytes to transmit.
 * @retval  true if transmission was successful, false if a hardware error occurred.
 */
bool BSP_I2C_Write(uint8_t dev_addr, uint8_t *data, uint16_t size);

#endif /* BSP_I2C_H */
