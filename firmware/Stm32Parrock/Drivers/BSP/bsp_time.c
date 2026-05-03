/**
 * @file    bsp_time.c
 * @brief   Implementation of microsecond delays using ARM Cortex-M DWT
 */
#include "bsp_time.h"
#include "main.h" /* For CoreDebug and DWT registers */

void BSP_Time_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void BSP_Delay_us(uint32_t us) {
    uint32_t startTick = DWT->CYCCNT;
    uint32_t delayTicks = us * (SystemCoreClock / 1000000);
    while (DWT->CYCCNT - startTick < delayTicks);
}
