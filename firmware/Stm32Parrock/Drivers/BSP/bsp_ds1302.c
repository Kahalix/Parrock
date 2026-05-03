/**
 * @file    bsp_ds1302.c
 * @brief   STM32 Hardware implementation for the DS1302 using BSRR/CRH registers.
 */

#include "bsp_ds1302.h"
#include "main.h"
#include "bsp_time.h"
#include "FreeRTOS.h"
#include "task.h"

/* --- Simple HAL calls for dedicated output pins --- */
static void HW_CE_High(void) { HAL_GPIO_WritePin(DS1302_CE_GPIO_Port, DS1302_CE_Pin, GPIO_PIN_SET); }
static void HW_CE_Low(void)  { HAL_GPIO_WritePin(DS1302_CE_GPIO_Port, DS1302_CE_Pin, GPIO_PIN_RESET); }
static void HW_CLK_High(void){ HAL_GPIO_WritePin(DS1302_CLK_GPIO_Port, DS1302_CLK_Pin, GPIO_PIN_SET); }
static void HW_CLK_Low(void) { HAL_GPIO_WritePin(DS1302_CLK_GPIO_Port, DS1302_CLK_Pin, GPIO_PIN_RESET); }

/* --- RTOS Critical Section Wrappers --- */
static void HW_EnterCritical(void) {
    taskENTER_CRITICAL();
}

static void HW_ExitCritical(void) {
    taskEXIT_CRITICAL();
}

/* --- OPTIMIZATION: Direct Register Access for Bidirectional IO Pin --- */
static void HW_Set_IO_Input(void) {
    /* Clear configuration bits for Pin 13, then set to Input Floating (0b0100) */
    GPIOB->CRH = (GPIOB->CRH & ~(0xF << 20)) | (0x4 << 20);
}

static void HW_Set_IO_Output(void) {
    /* Clear configuration bits for Pin 13, then set to Output Push-Pull 50MHz (0b0011) */
    GPIOB->CRH = (GPIOB->CRH & ~(0xF << 20)) | (0x3 << 20);
}

static void HW_IO_Write(uint8_t bit) {
    /* Fast toggle using BSRR/BRR registers */
    if (bit) GPIOB->BSRR = DS1302_IO_Pin;
    else     GPIOB->BRR  = DS1302_IO_Pin;
}

static uint8_t HW_IO_Read(void) {
    /* Fast read directly from Input Data Register */
    return (GPIOB->IDR & DS1302_IO_Pin) ? 1 : 0;
}

/*
 * @brief  The core interface mapping structure.
 */
static const ds1302_io_if_t ds1302_hw_interface = {
    .ce_high        = HW_CE_High,
    .ce_low         = HW_CE_Low,
    .clk_high       = HW_CLK_High,
    .clk_low        = HW_CLK_Low,
    .io_write       = HW_IO_Write,
    .io_read        = HW_IO_Read,
    .set_io_input   = HW_Set_IO_Input,
    .set_io_output  = HW_Set_IO_Output,
    .delay_us       = BSP_Delay_us,
    .enter_critical = HW_EnterCritical,
    .exit_critical  = HW_ExitCritical
};

const ds1302_io_if_t* BSP_DS1302_GetInterface(void) {
    return &ds1302_hw_interface;
}
