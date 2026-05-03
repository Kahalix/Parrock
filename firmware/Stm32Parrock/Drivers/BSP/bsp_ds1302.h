/**
 * @file    bsp_ds1302.h
 * @brief   Board Support Package specifically for the DS1302 hardware interface.
 */

#ifndef BSP_DS1302_H
#define BSP_DS1302_H

#include "ds1302_driver.h" /* To expose the interface structure type */

/**
 * @brief   Retrieves the hardware-specific interface mappings for the STM32.
 * @retval  Pointer to a statically allocated, constant interface structure.
 */
const ds1302_io_if_t* BSP_DS1302_GetInterface(void);

#endif /* BSP_DS1302_H */
