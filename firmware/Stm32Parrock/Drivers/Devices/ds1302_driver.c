/**
 * @file    ds1302_driver.c
 * @brief   Implementation of the DS1302 protocol with strict API validation.
 */

#include "ds1302_driver.h"
#include <stddef.h>

/* Force a safe 5us timing. 72MHz CPU is extremely fast and can violate DS1302 setup times. */
#define SAFE_T_CLK_US 5
#define SAFE_T_CE_SETUP_US 10

static inline bool DS1302_IsValid(const ds1302_handle_t *handle) {
    return (handle != NULL && handle->io != NULL);
}

ds1302_status_t DS1302_Init(ds1302_handle_t *handle, const ds1302_io_if_t *io_interface) {
    if (handle == NULL) return DS1302_ERR_NULL_HANDLE;
    if (io_interface == NULL) return DS1302_ERR_INVALID_IO;

    handle->io = io_interface;

    if (!DS1302_IsValid(handle)) return DS1302_ERR_INVALID_IO;

    handle->io->ce_low();
    handle->io->clk_low();
    return DS1302_OK;
}

/* --- Internal Bit-Banging Helpers --- */
static void DS1302_WriteByte(ds1302_handle_t *handle, uint8_t data) {
    handle->io->set_io_output();

    if (handle->io->enter_critical != NULL) handle->io->enter_critical();

    for (uint8_t i = 0; i < 8; i++) {
        handle->io->io_write(data & 0x01);
        data >>= 1;

        handle->io->clk_high();
        handle->io->delay_us(SAFE_T_CLK_US);
        handle->io->clk_low();
        handle->io->delay_us(SAFE_T_CLK_US);
    }

    if (handle->io->exit_critical != NULL) handle->io->exit_critical();
}

static uint8_t DS1302_ReadByte(ds1302_handle_t *handle) {
    uint8_t data = 0;

    handle->io->set_io_input();
    handle->io->delay_us(SAFE_T_CLK_US);

    if (handle->io->enter_critical != NULL) handle->io->enter_critical();

    for (uint8_t i = 0; i < 8; i++) {
        if (handle->io->io_read()) {
            data |= (1 << i);
        }

        handle->io->clk_high();
        handle->io->delay_us(SAFE_T_CLK_US);
        handle->io->clk_low();
        handle->io->delay_us(SAFE_T_CLK_US);
    }

    if (handle->io->exit_critical != NULL) handle->io->exit_critical();

    return data;
}

/* --- Public Driver API --- */

ds1302_status_t DS1302_SetWriteProtect(ds1302_handle_t *handle, bool enable) {
    if (!DS1302_IsValid(handle)) return DS1302_ERR_NULL_HANDLE;
    return DS1302_WriteReg(handle, DS1302_REG_WP, enable ? 0x80 : 0x00);
}

ds1302_status_t DS1302_WriteReg(ds1302_handle_t *handle, uint8_t reg_addr, uint8_t data) {
    if (!DS1302_IsValid(handle)) return DS1302_ERR_NULL_HANDLE;

    handle->io->clk_low();
    handle->io->ce_high();
    handle->io->delay_us(SAFE_T_CE_SETUP_US);

    DS1302_WriteByte(handle, reg_addr);
    DS1302_WriteByte(handle, data);

    handle->io->ce_low();
    return DS1302_OK;
}

ds1302_status_t DS1302_ReadReg(ds1302_handle_t *handle, uint8_t reg_addr, uint8_t *out_data) {
    if (!DS1302_IsValid(handle) || out_data == NULL) return DS1302_ERR_NULL_HANDLE;

    handle->io->clk_low();
    handle->io->ce_high();
    handle->io->delay_us(SAFE_T_CE_SETUP_US);

    DS1302_WriteByte(handle, reg_addr);
    *out_data = DS1302_ReadByte(handle);

    handle->io->ce_low();
    return DS1302_OK;
}

ds1302_status_t DS1302_ReadBurst(ds1302_handle_t *handle, uint8_t *buffer) {
    if (!DS1302_IsValid(handle) || buffer == NULL) return DS1302_ERR_NULL_HANDLE;

    handle->io->clk_low();
    handle->io->ce_high();
    handle->io->delay_us(SAFE_T_CE_SETUP_US);

    DS1302_WriteByte(handle, DS1302_CMD_BURST_READ);

    for (uint8_t i = 0; i < 7; i++) {
        buffer[i] = DS1302_ReadByte(handle);
    }

    handle->io->ce_low();
    return DS1302_OK;
}

bool DS1302_IsOscillatorStopped(const uint8_t *burst_buffer) {
    if (burst_buffer == NULL) return true;
    return (burst_buffer[DS1302_REG_SEC] & DS1302_CH_BIT_MASK) != 0;
}
