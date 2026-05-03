/**
 * @file    bsp_i2c.c
 * @brief   STM32 HAL implementation of the I2C BSP.
 */

#include "bsp_i2c.h"
#include "main.h" /* HAL libraries are only allowed in BSP and Adapter layers */

/* Pointer to the injected STM32 HAL I2C handle */
static I2C_HandleTypeDef *internal_i2c = NULL;

void BSP_I2C_Init(void *i2c_handle) {
    /* Cast the generic void pointer back to the STM32-specific handle */
    internal_i2c = (I2C_HandleTypeDef *)i2c_handle;
}

bool BSP_I2C_Write(uint8_t dev_addr, uint8_t *data, uint16_t size) {
    // Safety check: Ensure the module was initialized before use
    if (internal_i2c == NULL) {
        return false;
    }

    // Execute the HAL-specific transmission function with a 50ms timeout
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(internal_i2c, dev_addr, data, size, 50);

    return (status == HAL_OK);
}
