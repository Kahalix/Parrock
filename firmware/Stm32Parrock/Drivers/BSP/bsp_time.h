/**
 * @file    bsp_time.h
 * @brief   Board Support Package for time delays and measurement.
 */
#ifndef BSP_TIME_H
#define BSP_TIME_H

#include <stdint.h>

/**
 * @brief   Initializes the Data Watchpoint and Trace (DWT) cycle counter.
 */
void BSP_Time_Init(void);

/**
 * @brief   Blocking microsecond delay using the DWT cycle counter.
 * @param   us: Number of microseconds to block.
 */
void BSP_Delay_us(uint32_t us);

#endif /* BSP_TIME_H */
